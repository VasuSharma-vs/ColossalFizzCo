#include "Misc/AutomationTest.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/ProgressBar.h"
#include "Components/RocketProjectileComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/TextBlock.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/FillingMachineUpgradeTypes.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Shop/ColaSupplyContainer.h"
#include "Shop/ShopSupplyDepot.h"
#include "Components/CollisionCableComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopShelfStockComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "UI/ShopTerminalWidget.h"
#include "UI/ManualFillingPressureGauge.h"
#include "Shop/ShopCustomerFlow.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PropGrabTests { bool InvokeCarryRPC(ACharacter* Player,const FString& Name,AActor* Target); }

namespace
{
	bool SendSocketInteraction(AActor& Machine, FName Event, ACharacter* Player, UPrimitiveComponent* HitComponent=nullptr)
	{
		UFunction* Handler = Machine.FindFunction(Event);
		if (!Handler) return false;
		auto* PlayerParam = FindFProperty<FObjectPropertyBase>(Handler, TEXT("Player Charator"));
		if (!PlayerParam || !Player->IsA(PlayerParam->PropertyClass)) return false;
		FStructOnScope Params(Handler);
		PlayerParam->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Player);
		if (HitComponent)
		{
			auto* HitParam = FindFProperty<FObjectPropertyBase>(Handler, TEXT("Hitting Component"));
			if (!HitParam) return false;
			HitParam->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), HitComponent);
		}
		Machine.ProcessEvent(Handler, Params.GetStructMemory());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FManualColaFillingLoopTest,"ColossalFizzCo.Shop.ManualSoloFilling",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FManualColaFillingLoopTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!Manager->ShopSupplies->InitializeCatalog()) return false;
	Manager->SharedWallet->Credit(FGuid::NewGuid(),20000,TEXT("Isolated test funding"));
	const auto* Catalog=Manager->ShopSupplies->Catalog.Get();
	const auto* MachineOffer=Catalog->FindOffer(TEXT("ManualFillingMachine"));
	if (!TestNotNull(TEXT("Manual machine has a real purchase offer"),MachineOffer)) return false;
	const auto* MachineProduct=MachineOffer->Product.LoadSynchronous();
	const FTransform PlacedTransform(FRotator(0,35,0),FVector(2200,0,150));
	auto* Placed=World->SpawnActorDeferred<AManualColaFillingMachine>(MachineProduct->CustomActiveActorClass,PlacedTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Placed) return false;
	Placed->PropDataAsset=const_cast<UInteractablePropDataAsset*>(MachineProduct); Placed->State=EPropState::Unsettled; Placed->bAutoConvertToISMIfAtRest=false;
	UGameplayStatics::FinishSpawningActor(Placed,PlacedTransform); Placed->Tick(.05f);
	int32 PlacedID=0;
	for (const auto& Pair:Manager->ISIDData) if (const auto* Def=Manager->FindProductDefinition(Pair.Value.ProductID))
		if (Def->SourceDataAsset==MachineProduct && Manager->GetTransform(Pair.Key).GetLocation().Equals(PlacedTransform.GetLocation(),1)) PlacedID=Pair.Key;
	auto* Starter=Manager->GetActiveActorFromISID(PlacedID);
	if (!TestNotNull(TEXT("Starter machine receives an active managed inventory identity"),Starter)) return false;
	auto* StarterMachine=Cast<AManualColaFillingMachine>(Starter);
	if (!TestNotNull(TEXT("The placed stand keeps its filling machine class"),StarterMachine)) return false;
	TestTrue(TEXT("The saved filling table defaults to a Flow Controller feed"),StarterMachine->bUseFlowController);
	TestNull(TEXT("The filling table Blueprint has no legacy supply box"),StarterMachine->GetDefaultSubobjectByName(TEXT("SupplyPort")));
	TestTrue(TEXT("The placed stand has a single hose socket"),StarterMachine->GetHoseSocket(0) && !StarterMachine->GetHoseSocket(1));
	TestEqual(TEXT("The feed attaches directly to the filling table"),StarterMachine->GetHoseSocket(0),StarterMachine->FlowControllerInlet.Get());
	TestEqual(TEXT("A new machine starts with one cap slot"),StarterMachine->Production->GetCapTrayCapacity(),1);
	TestEqual(TEXT("A new machine starts with one seal slot"),StarterMachine->Production->GetSealTrayCapacity(),1);
	TestEqual(TEXT("Level placement registers the stand at rest"),Starter->State,EPropState::AtRest);
	TestFalse(TEXT("Level placement starts with stand physics off"),Starter->PropMesh->IsSimulatingPhysics());
	for (int32 Step=0; Step<10; ++Step) World->Tick(LEVELTICK_All,.05f);
	TestFalse(TEXT("The old selector dial stays hidden on the active stand"),StarterMachine->ContentSelectorDial->IsVisible());
	const auto* StarterLayout=Cast<ASocketColaFillingMachine>(StarterMachine);
	TestTrue(TEXT("The old housing is hidden and has no collision on the active stand"),
		StarterLayout && !StarterLayout->FillingController->IsVisible()
		&& StarterLayout->FillingController->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
	TestTrue(TEXT("The running stand preserves its level location and rotation"),Starter->GetActorTransform().Equals(PlacedTransform,0.01));
	TArray<int32> MachineIDs;
	{
		auto* Defaults=MachineProduct->CustomActiveActorClass->GetDefaultObject<AManualColaFillingMachine>();
		TGuardValue<bool> LegacyLayout(Defaults->bUseFlowController,false);
		if (!TestEqual(TEXT("Multiple manual machines can be purchased in one transaction"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),TEXT("ManualFillingMachine"),2,FTransform(FVector(0,0,1000)),MachineIDs),EShopPurchaseResult::Success)) return false;
	}
	if (!TestEqual(TEXT("Two distinct managed machine identities delivered"),MachineIDs.Num(),2)) return false;
	TestEqual(TEXT("Only the catalog machine price is charged"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(20000)-2*MachineOffer->UnitCostCents);
	auto* Machine=Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[0]));
	auto* Second=Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[1]));
	if (!TestTrue(TEXT("Purchased machines use the native equipment actor"),Machine && Second)) return false;
	auto* Layout = Cast<ASocketColaFillingMachine>(Machine);
	if (!TestNotNull(TEXT("The catalog Blueprint uses the sample component hierarchy"), Layout)) return false;
	TestTrue(TEXT("The legacy layout provides four hose sockets"), Machine->HoseSockets.Num()==4
		&& !Machine->HoseSockets.ContainsByPredicate([&](const auto& Socket) { return !IsValid(Socket)
			|| Socket->GetAttachParent()!=Machine->HoseConnections; }));
	for (auto* M:{Machine,Second}) M->PropMesh->SetSimulatePhysics(false);
	Manager->InitializeInstanceTransform(MachineIDs[0],FTransform(FVector(0,0,150)));
	Manager->InitializeInstanceTransform(MachineIDs[1],FTransform(FVector(0,1000,150)));
	// Exercise the saved character RPC and generic prop interface inherited by
	// the new machine Blueprint, in addition to testing the manager services.
	UClass* PlayerClass=LoadClass<ACharacter>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Production carry character exists"),PlayerClass)) return false;
	World->SetBegunPlay(false);
	auto* Carrier=World->SpawnActor<ACharacter>(PlayerClass,FVector(0,-100,200),FRotator::ZeroRotator);
	World->SetBegunPlay(true);
	auto* CarrierPC=World->SpawnActor<APlayerController>(); CarrierPC->Possess(Carrier);
	auto* CarrierActions=Carrier->FindComponentByClass<UShopPlayerInteractionComponent>();
	if (!TestNotNull(TEXT("Saved first-person character can send shop input"),CarrierActions)) return false;
	Carrier->GetCharacterMovement()->DisableMovement();
	TestTrue(TEXT("Saved player pickup RPC receives the machine target"),PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoAttachActortoComponent"),Machine));
	TestFalse(TEXT("Fixed machine cannot be carried via the prop manager"),Manager->BeginGrab(Machine->ISID,Carrier));
	TestTrue(TEXT("Picking up the fixed machine leaves its actor and ownership in place"),
		!Machine->bIsHeld && !Manager->FindISIDData(Machine->ISID)->RuntimeData.InteractionOwner.IsValid()
		&& Machine->GetActorLocation().Equals(FVector(0,0,150)));
	auto* ComponentGrab=Carrier->FindFunction(TEXT("Grab Component On Server"));
	const auto* ComponentArg=ComponentGrab ? FindFProperty<FObjectPropertyBase>(ComponentGrab,TEXT("Holding Component")) : nullptr;
	const auto* HeldComponent=FindFProperty<FObjectPropertyBase>(PlayerClass,TEXT("Holding Component"));
	if (!TestTrue(TEXT("The saved component pickup RPC is available"),ComponentArg && HeldComponent)) return false;
	FStructOnScope ComponentParams(ComponentGrab);
	ComponentArg->SetObjectPropertyValue_InContainer(ComponentParams.GetStructMemory(),Layout->FillingController);
	Carrier->ProcessEvent(ComponentGrab,ComponentParams.GetStructMemory());
	TestNull(TEXT("A machine control cannot be carried through the component pickup RPC"),HeldComponent->GetObjectPropertyValue_InContainer(Carrier));
	auto* Station=Machine->Production.Get(); auto* Filler=Machine->BottleFiller.Get();
	TestTrue(TEXT("Manual machine permits solo closure and requires held flow"),Station->bAllowSoloCap && Station->bManualLeverMode && Filler->bRequireLever);
	auto Operator=[&](FVector Location)
	{
		auto* Pawn=World->SpawnActor<ACharacter>(); Pawn->SetActorLocation(Location); Pawn->SetActorEnableCollision(false); Pawn->GetCharacterMovement()->SetMovementMode(MOVE_None);
		auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
		auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent(); return Actions;
	};
	const FVector SupplyOrigin = Layout->GetSupplyConnectionOrigin();
	const FVector ControllerSide = SupplyOrigin + FVector(0,-220,50);
	auto* Solo=Operator(ControllerSide); auto* Partner=Operator(FVector(100,-100,200));
	int64 Sequence=0,PartnerSequence=0;
	auto Action=[&](EShopProductionAction A,int32 Bottle=0,int32 Other=0){return Solo->ExecuteProductionAction(Station,Station->View.SessionID,A,Bottle,Other,++Sequence);};
	const auto* SourceOffer=Catalog->FindOffer(TEXT("CylinderLemonade"));
	const auto* CapOffer=Catalog->FindOffer(TEXT("TwistCap")); const auto* SealOffer=Catalog->FindOffer(TEXT("TemporarySeal"));
	if (!SourceOffer || !CapOffer || !TestNotNull(TEXT("Physical seal offer exists"),SealOffer)) return false;
	TestFalse(TEXT("Manual machine offers no purchases"),Station->bOffersSupplies);
	TestEqual(TEXT("Machine rejects supply purchase RPCs"),Station->Purchase(Station->View.SessionID,FGuid::NewGuid(),TEXT("TwistCap"),1),EShopCheckoutResult::Unavailable);
	auto* Depot=World->SpawnActor<AShopSupplyDepot>();
	TestEqual(TEXT("Separate depot delivers physical seals"),Depot->Supplies->Purchase(Depot->Supplies->View.SessionID,FGuid::NewGuid(),TEXT("TemporarySeal"),1),EShopCheckoutResult::Success);
	TestEqual(TEXT("Purchased seal has a managed physical identity"),Depot->Supplies->LastDeliveredSupplies.Num(),1);
	for (int32 ID:Depot->Supplies->LastDeliveredSupplies) Manager->InitializeInstanceTransform(ID,FTransform(FVector(-4000,0,200)));
	auto Spawn=[&](int32 PID,FVector Location)
	{
		if (const auto* Def=Manager->FindProductDefinition(PID); Def && Def->StaticMesh && Def->ConsumableProfile.NumOfPortions<=0)
			Location-=Def->StaticMesh->GetBoundingBox().GetCenter();
		const int32 ID=Manager->GenerateProductInstance(PID,FTransform(Location)); AActivePropActor* Active=nullptr;
		Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active);
		if (Active) Active->PropMesh->SetSimulatePhysics(false); return ID;
	};
	const int32 EmptyPID=Manager->RegisterProductFromDataAsset(Catalog->EmptyBottleProduct.LoadSynchronous());
	const int32 CapPID=Manager->RegisterProductFromDataAsset(CapOffer->Product.LoadSynchronous());
	const auto* SealProduct=SealOffer->Product.LoadSynchronous();
	const int32 SealPID=Manager->RegisterProductFromDataAsset(SealProduct);
	const int32 Source=Spawn(Manager->RegisterProductFromDataAsset(SourceOffer->Product.LoadSynchronous()),SupplyOrigin + FVector(-110,-140,30));
	TestEqual(TEXT("Source ID alone cannot connect a hose"),Action(EShopProductionAction::ConnectSupply,0,Source),EShopCheckoutResult::Unavailable);
	auto* Container=Cast<AColaSupplyContainer>(Manager->GetActiveActorFromISID(Source));
	if (!TestNotNull(TEXT("Bulk supply uses a real grab-capable hose actor"),Container)) return false;
	const auto* SourceDefinition=Manager->FindProductDefinition(Manager->FindISIDData(Source)->ProductID);
	TestTrue(TEXT("Activated bulk supply exposes its canonical liquid through the native resource port"),Container->SupplyPort
		&& Container->SupplyPort->IsManagedContentEndpoint() && Container->SupplyPort->GetManagedContentISID()==Source
		&& FMath::IsNearlyEqual(Container->SupplyPort->GetStoredUnits(),SourceDefinition->ConsumableProfile.NumOfPortions*Manager->FindUniqueData(Source)->EventData.ContentLevel,.001f));
	TestFalse(TEXT("A source mesh cannot masquerade as a nozzle"),Machine->AttachSupplyNozzle(FHitResult(Machine,Machine->GetHoseSocket(0)->GetTraceVolume(),Machine->GetHosePortTransform(0).GetLocation(),FVector::UpVector),Container->PropMesh,Source,Solo->GetOwner()));
	TArray<FVector> CablePoints; Container->SupplyHose->GetCableParticleLocations(CablePoints);
	if (!TestTrue(TEXT("Hose contains an endpoint"),CablePoints.Num()>1)) return false;
	if (!TestTrue(TEXT("Owned player grabs the hose endpoint"),Solo->ExecuteCableGrab(Container->SupplyHose,CablePoints.Last(),CablePoints.Last(),1))) return false;
	if (!TestTrue(TEXT("Owned hose-connection RPC attaches the nozzle"),Solo->ExecuteConnectSupplyHose(Container->SupplyHose,Machine,0,2))) return false;
	TestTrue(TEXT("Connected nozzle matches socket position and orientation"),Container->SupplyNozzle->GetComponentLocation().Equals(Machine->GetHosePortTransform(0).GetLocation(),.01f) && Container->SupplyNozzle->GetComponentQuat().Equals(Machine->GetHosePortTransform(0).GetRotation(),.001f));
	TestEqual(TEXT("One physical nozzle cannot supply two machines"),Second->Production->ExecuteForPlayer(Second->Production->View.SessionID,EShopProductionAction::ConnectSupply,0,Source,Partner->GetOwner()),EShopCheckoutResult::Unavailable);
	TestTrue(TEXT("Receiving sockets are mounted on the separate filling controller"),
		Machine->HoseConnections->GetAttachParent() == Layout->FillingController);
	Solo->GetOwner()->SetActorLocation(FVector(0,-120,220));

	const FName BeginEvent(TEXT("On Intraction Begain")), EndEvent(TEXT("On Intraction  End"));
	auto TraceControl = [&](UPrimitiveComponent* Target, AActor* Carried = nullptr)
	{
		FVector Eye;
		FRotator View;
		Carrier->GetActorEyesViewPoint(Eye,View);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(SocketMachineTraceTest),true,Carrier);
		if (Carried) Query.AddIgnoredActor(Carried);
		FHitResult Hit;
		World->LineTraceSingleByChannel(Hit, Eye, Target->Bounds.Origin, ECC_Visibility, Query);
		return Hit.GetComponent();
	};
	auto* Lever = FindObject<UStaticMeshComponent>(Layout,TEXT("ManualFillingLever"));
	if (!TestNotNull(TEXT("Sample lever mesh is present"), Lever)) return false;
	Carrier->SetActorLocation(ControllerSide);
	auto* LeverHit = TraceControl(Lever);
	TestEqual(TEXT("The player line trace sees the lever itself"), LeverHit, static_cast<UPrimitiveComponent*>(Lever));
	TestTrue(TEXT("Saved World Interaction begins at the traced lever"), SendSocketInteraction(*Layout, BeginEvent, Carrier, LeverHit));
	Machine->Tick(Layout->LeverTravelSeconds*.5f);
	TestTrue(TEXT("Lever moves gradually on its local X axis"), FMath::IsNearlyEqual(Lever->GetRelativeRotation().Roll,45.,.1));
	Machine->Tick(Layout->LeverTravelSeconds*.5f);
	TestTrue(TEXT("Held lever reaches 100 degrees"), FMath::IsNearlyEqual(Lever->GetRelativeRotation().Roll,100.,.1));
	TestTrue(TEXT("World Interaction ends even without the original hit"), SendSocketInteraction(*Layout, EndEvent, Carrier));
	Machine->Tick(Layout->LeverTravelSeconds*.5f);
	TestTrue(TEXT("Lever returns gradually"), FMath::IsNearlyEqual(Lever->GetRelativeRotation().Roll,45.,.1));
	Machine->Tick(Layout->LeverTravelSeconds*.5f);
	TestTrue(TEXT("Released lever returns to minus ten degrees"), FMath::IsNearlyEqual(Lever->GetRelativeRotation().Roll,-10.,.1));

	auto* Dial = Layout->ContentSelectorDial.Get();
	auto* DialHit = TraceControl(Dial);
	const int32 PreviousPort = Station->View.SelectedContentPort;
	const FQuat PreviousDialRotation = Dial->GetRelativeRotation().Quaternion();
	TestEqual(TEXT("The dial has a separate visibility hit beside the lever"), DialHit, static_cast<UPrimitiveComponent*>(Dial));
	TestTrue(TEXT("The saved World Interaction targets the content dial"), SendSocketInteraction(*Layout, BeginEvent, Carrier, DialHit));
	Machine->Tick(.01f);
	TestEqual(TEXT("The dial selects the next intake"), Station->View.SelectedContentPort, (PreviousPort+1)%Station->GetSupportedContentCount());
	TestFalse(TEXT("The dial rotates to indicate the selected intake"), Dial->GetRelativeRotation().Quaternion().Equals(PreviousDialRotation, .001f));
	TestEqual(TEXT("Restore the connected input for the bottling test"), Action(EShopProductionAction::SelectContentPort, PreviousPort), EShopCheckoutResult::Success);

	Carrier->SetActorLocation(Station->CapInput->GetComponentLocation() + FVector(0,-165,30));
	const int32 HandCap = Spawn(CapPID, Carrier->GetActorLocation()+FVector(0,60,0));
	auto* HandCapActor = Manager->GetActiveActorFromISID(HandCap);
	PropGrabTests::InvokeCarryRPC(Carrier, TEXT("RequestServertoAttachActortoComponent"), HandCapActor);
	if (!TestTrue(TEXT("The player can carry a physical cap"), HandCapActor && HandCapActor->bIsHeld)) return false;
	const auto* HoldingActor = FindFProperty<FObjectPropertyBase>(PlayerClass, TEXT("Holding Actor"));
	const auto* HoldingState = FindFProperty<FByteProperty>(PlayerClass, TEXT("Holding State"));
	const auto* TraceIgnore = FindFProperty<FArrayProperty>(PlayerClass, TEXT("trace  ignore"));
	const auto* IgnoreElement = TraceIgnore ? CastField<FObjectPropertyBase>(TraceIgnore->Inner) : nullptr;
	if (!TestTrue(TEXT("The saved character exposes its hand state and trace ignore list"), HoldingActor && HoldingState && IgnoreElement)) return false;
	FScriptArrayHelper Ignored(TraceIgnore, TraceIgnore->ContainerPtrToValuePtr<void>(Carrier));
	IgnoreElement->SetObjectPropertyValue(Ignored.GetRawPtr(Ignored.AddValue()), HandCapActor);
	HoldingState->SetPropertyValue_InContainer(Carrier, 1);
	const auto SupportedCapTypes = Layout->SupportedCapTypes;
	auto FinishInputPlacement = [&]()
	{
		for (int32 Frame = 0; Frame < 90; ++Frame) Layout->Tick(1.f / 60.f);
	};
	Layout->SupportedCapTypes = {EBottleCapType::Cork};
	SendSocketInteraction(*Layout, BeginEvent, Carrier, Station->CapInput);
	TestTrue(TEXT("A rejected cap stays in the player's hand"), HandCapActor->bIsHeld && Station->View.CapTrayCount==0
		&& HoldingActor->GetObjectPropertyValue_InContainer(Carrier)==HandCapActor && HoldingState->GetPropertyValue_InContainer(Carrier)==1);
	Layout->SupportedCapTypes = SupportedCapTypes;
	auto* CapHit = TraceControl(Station->CapInput,HandCapActor);
	TestEqual(TEXT("Empty magazine can be targeted by a line trace"), CapHit, static_cast<UPrimitiveComponent*>(Station->CapInput.Get()));
	const FVector CapStart = HandCapActor->GetActorLocation();
	TestTrue(TEXT("Saved interaction targets the cap volume"), SendSocketInteraction(*Layout, BeginEvent, Carrier, CapHit));
	TestNull(TEXT("Cap handoff clears the first-person held actor"), HoldingActor->GetObjectPropertyValue_InContainer(Carrier));
	TestEqual(TEXT("Cap handoff resets the first-person hand to empty"), HoldingState->GetPropertyValue_InContainer(Carrier), uint8(0));
	for (int32 Index=0; Index<Ignored.Num(); ++Index)
		TestTrue(TEXT("The transferred cap is removed from the player's trace ignore list"), IgnoreElement->GetObjectPropertyValue(Ignored.GetRawPtr(Index))!=HandCapActor);
	Layout->Tick(.05f);
	TestTrue(TEXT("A cap leaves the hand and travels before it is counted"), !HandCapActor->GetAttachParentActor()
		&& !HandCapActor->GetActorLocation().Equals(CapStart, 1.) && Station->View.CapTrayCount == 0);
	FinishInputPlacement();
	TestTrue(TEXT("The cap mounts and registers on arrival"), !HandCapActor->bIsHeld && Station->View.CapTrayCount == 1
		&& HandCapActor->GetAttachParentActor() == Machine);

	Carrier->SetActorLocation(Station->SealInput->GetComponentLocation()+FVector(0,-150,30));
	const int32 HeldSealID=Spawn(SealPID,Carrier->GetActorLocation()+FVector(0,-100,0));
	auto* HeldSeal=Manager->GetActiveActorFromISID(HeldSealID);
	PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoAttachActortoComponent"),HeldSeal);
	if (!TestTrue(TEXT("Player can carry the physical seal to the tray"),HeldSeal && HeldSeal->bIsHeld)) return false;
	auto* SealHit=TraceControl(Station->SealInput,HeldSeal);
	TestEqual(TEXT("The empty seal tray can be targeted"),SealHit,static_cast<UPrimitiveComponent*>(Station->SealInput.Get()));
	const FVector SealStart = HeldSeal->GetActorLocation();
	SendSocketInteraction(*Layout,BeginEvent,Carrier,SealHit);
	TestNull(TEXT("Seal handoff clears the first-person hand"),HoldingActor->GetObjectPropertyValue_InContainer(Carrier));
	Layout->Tick(.05f);
	TestTrue(TEXT("A seal moves before it enters the tray inventory"), !HeldSeal->GetActorLocation().Equals(SealStart, 1.)
		&& Station->View.SealTrayCount == 0);
	FinishInputPlacement();
	TestTrue(TEXT("Interacting with the seal tray registers the held seal"),!HeldSeal->bIsHeld && Station->View.SealTrayCount==1
		&& HeldSeal->GetRootComponent()->GetAttachParent()==Station->SealInput);
	FPropEventSyncDelta TrayUpgrades;
	TrayUpgrades.ExtendedDirtyMask=EPropExtendedEventMask::FillingMachineUpgrades;
	TrayUpgrades.FillingMachineUpgrades.SetRank(EFillingMachineUpgradeType::CapTray,3);
	TrayUpgrades.FillingMachineUpgrades.SetRank(EFillingMachineUpgradeType::SealTray,3);
	if (!TestTrue(TEXT("The physical tray fixture has four upgraded slots per tray"),
		Manager->SubmitInstanceEventUpdate(Machine->ISID,TrayUpgrades)
		&& Station->GetCapTrayCapacity()==4 && Station->GetSealTrayCapacity()==4)) return false;
	TArray<int32> Caps,Seals;
	for (int32 I=0;I<4;++I)
	{
		Caps.Add(Spawn(CapPID,Station->CapInput->GetComponentLocation()));
		Seals.Add(Spawn(SealPID,Station->SealInput->GetComponentLocation()));
	}
	Station->RefreshInputs();
	FinishInputPlacement();
	TestEqual(TEXT("Cap tray accepts exactly four physical caps"),Station->View.CapTrayCount,4);
	TestEqual(TEXT("Seal tray accepts exactly four physical seals"),Station->View.SealTrayCount,4);
	TestEqual(TEXT("Full cap volume blocks incoming physics props"),Station->CapInput->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Block);
	TestEqual(TEXT("Full seal volume blocks incoming physics props"),Station->SealInput->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Block);
	for (int32 ID:Manager->FindUniqueData(Machine->ISID)->EventData.LoadedCapISIDs)
		TestEqual(TEXT("Registered caps have collision disabled"),Manager->GetActiveActorFromISID(ID)->PropMesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
	const auto& LoadedCaps = Manager->FindUniqueData(Machine->ISID)->EventData.LoadedCapISIDs;
	for (int32 Index=0; Index<LoadedCaps.Num(); ++Index)
	{
		const FTransform Pose = Manager->GetTransform(LoadedCaps[Index]);
		const FVector Slot = Layout->CapSocket->GetComponentTransform().InverseTransformPosition(Pose.GetLocation());
		TestTrue(TEXT("Cap uses the socket's local spacing and orientation"),
			Slot.Equals(FVector(0,0,Index*15),.01) && Pose.GetRotation().Equals(Layout->CapSocket->GetComponentQuat(),.001));
	}
	for (int32 ID:Manager->FindUniqueData(Machine->ISID)->EventData.LoadedSealISIDs)
	{
		const auto* Seal=Manager->GetActiveActorFromISID(ID);
		TestEqual(TEXT("Registered seals have collision disabled"),Seal->PropMesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
		const FBox InTray=Seal->PropMesh->GetStaticMesh()->GetBoundingBox().TransformBy(
			Seal->GetActorTransform().GetRelativeTransform(Station->SealInput->GetComponentTransform()));
		TestTrue(TEXT("Seal rests inside the tray near its floor"), InTray.Min.Z>=-Station->SealInput->GetUnscaledBoxExtent().Z-.05
			&& InTray.Min.Z<0.0 && InTray.Max.Z<=Station->SealInput->GetUnscaledBoxExtent().Z);
	}
	TestEqual(TEXT("Fifth cap remains unclaimed"),Manager->FindUniqueData(Caps.Last())->EventData.StoredInMachineISID,0);
	TestFalse(TEXT("Overflow cap is pushed out of the full tray"),ShopInstanceVolumeQuery::Query(Station->CapInput).Contains(Caps.Last()));
	TestFalse(TEXT("Overflow seal is pushed out of the full tray"),ShopInstanceVolumeQuery::Query(Station->SealInput).Contains(Seals.Last()));

	auto MakeBottle=[&]()
	{
		const int32 ID=Spawn(EmptyPID,Station->BottleInput->GetComponentLocation()); Station->RefreshInputs();
		FinishInputPlacement();
		if (!TestEqual(TEXT("Dropped bottle seats without an explicit align command"),Station->View.SeatedBottleISID,ID)) return int32(0);
		if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID))) Bottle->bUseOriginalRocketFlight=false;
		return ID;
	};
	auto Tick=[&](float Dt)
	{
		TArray<int32> IDs; Manager->ISIDData.GetKeys(IDs);
		for (int32 ID:IDs) if (auto* Active=Manager->GetActiveActorFromISID(ID)) if (Active->PropMesh) Active->PropMesh->SetSimulatePhysics(false);
		World->Tick(LEVELTICK_All,Dt);
		// Repeated isolated-world steps share one engine frame, so advance equipment
		// explicitly just like the filler and pressure simulation below.
		Machine->Tick(Dt); Station->RefreshInputs();
		if (Filler->bIsFilling) Filler->TickComponent(Dt,LEVELTICK_All,nullptr);
		Manager->ISIDData.GetKeys(IDs);
		for (int32 ID:IDs) if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID))) Bottle->UpdatePlasticPressure(Dt);
	};
	auto Fill=[&](int32 ID)
	{
		auto Report=[&](int32 Iteration,EShopCheckoutResult Result)
		{
			const auto* Record=Manager->FindISIDData(ID);
			const auto* Definition=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
			const auto* Data=Manager->FindUniqueData(ID);
			const auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
			AddError(FString::Printf(TEXT("Fill diagnostic: bottle=%d iteration=%d result=%d level=%.4f capacity=%d rate=%.3f alpha=%.3f aligned=%d ready=%d held=%d filling=%d stopped=%d seated=%d state=%d motion=%d machineTick=%d machineActive=%d worldBegun=%d"),
				ID,Iteration,int32(Result),Data ? Data->EventData.ContentLevel : -1.f,Definition ? Definition->ConsumableProfile.NumOfPortions : -1,
				Filler->PortionsPerSecond,Machine->CradleAlpha,Station->IsBottleAligned(ID),Station->IsBottleReadyForFilling(ID),
				Station->View.bLeverHeld,Filler->bIsFilling,Station->View.bFlowStopped,Station->View.SeatedBottleISID,
				Record ? int32(Record->State) : -1,Bottle ? Bottle->bIsInMotion : false,Machine->IsActorTickEnabled(),Machine->bIsActive,World->HasBegunPlay()));
			const int32 SelectedSource=Station->View.ConnectedSourceISID;
			const auto* SourceData=Manager->FindUniqueData(SelectedSource);
			const auto* SourceRecord=Manager->FindISIDData(SelectedSource);
			const auto* SourceProduct=SourceRecord ? Manager->FindProductDefinition(SourceRecord->ProductID) : nullptr;
			const auto* SourceActor=Manager->GetActiveActorFromISID(SelectedSource);
			const auto* Content=SourceData ? Manager->FindContentDefinition(SourceData->EventData.ContentID) : nullptr;
			const FVector SourcePosition=Manager->GetTransform(SelectedSource).GetLocation(),TargetPosition=Manager->GetTransform(ID).GetLocation();
			AddInfo(FString::Printf(TEXT("Fill source: selectedPort=%d selectedSource=%d fillerSource=%d fillerTarget=%d sourceLevel=%.4f sourceCapacity=%d content=%d temperature=%.3f creation=%.3f worldElapsed=%.3f expiryEnabled=%d expiryMinutes=%.3f admission=%d"),
				Station->View.SelectedContentPort,SelectedSource,Filler->SourceISID,Filler->TargetISID,SourceData ? SourceData->EventData.ContentLevel : -1.f,
				SourceProduct ? SourceProduct->ConsumableProfile.NumOfPortions : -1,SourceData ? SourceData->EventData.ContentID : -1,
				SourceData ? SourceData->ContinuousData.Temperature : -1.f,SourceData ? SourceData->EventData.ContentCreationTime : -1.0,Manager->WorldElapsedSeconds,
				Content ? Content->ExpiryProfile.bHasExpiry : false,Content ? double(Content->ExpiryProfile.ExpiryLifetimeMinutes) : -1.0,
				Bottle && SourceData && Bottle->CanAcceptManufacturingContent(SourceData->EventData.ContentID)));
			AddInfo(FString::Printf(TEXT("Fill range: machine=(%s) source=(%s) liveSource=(%s) target=(%s) sourceDistance=%.3f targetDistance=%.3f max=%.3f operatorDistance=%.3f leaseRemaining=%.3f sessions=%d targetCap=%.3f targetStrength=%.3f targetClean=%d"),
				*Machine->GetActorLocation().ToString(),*SourcePosition.ToString(),*(SourceActor ? SourceActor->GetActorLocation() : FVector::ZeroVector).ToString(),*TargetPosition.ToString(),
				FVector::Distance(Machine->GetActorLocation(),SourcePosition),FVector::Distance(Machine->GetActorLocation(),TargetPosition),Filler->MaxConnectionDistance,
				Filler->LeverOperator.IsValid() ? FVector::Distance(Filler->LeverOperator->GetActorLocation(),Machine->GetActorLocation()) : -1.0,
				Filler->LeverExpiresAt-World->GetTimeSeconds(),Manager->FillSessions.Num(),Data ? Data->EventData.CapSealed : -1.f,Data ? Data->EventData.Strength : -1.f,Data && Data->EventData.bClean));
			for (const auto& Session:Manager->FillSessions)
				AddInfo(FString::Printf(TEXT("Remaining transfer: source=%d target=%d content=%d"),Session.Value.SourceISID,Session.Value.TargetISID,Session.Value.ContentID));
		};
		for (int32 I=0;I<150 && Manager->FindUniqueData(ID)->EventData.ContentLevel<.99f;++I)
		{
			const auto Result=Action(EShopProductionAction::HoldFillingLever,ID);
			if (Result!=EShopCheckoutResult::Success) { Report(I,Result); return false; }
			Tick(.05f);
		}
		if (Manager->FindUniqueData(ID)->EventData.ContentLevel<.99f) Report(150,EShopCheckoutResult::Success);
		return Manager->FindUniqueData(ID)->EventData.ContentLevel>=.99f;
	};
	auto MoveAway=[&](int32 ID)
	{
		Manager->InitializeInstanceTransform(ID,FTransform(FVector(3000+ID*30,0,200))); Station->RefreshInputs();
	};
	auto Vent=[&](int32 ID)
	{
		for (int32 I=0;I<100 && Manager->FindUniqueData(ID)->ContinuousData.SealedPressure>Station->SafeResidualPressure;++I)
		{ Action(EShopProductionAction::HoldFillingLever,ID); Tick(.1f); }
		return Manager->FindUniqueData(ID)->ContinuousData.SealedPressure<=Station->SafeResidualPressure;
	};
	auto* Ports=World->SpawnActor<AManualColaFillingMachine>(FVector(5000,0,1000),FRotator::ZeroRotator);
	TestEqual(TEXT("Four content types create four hose sockets"),Ports->HoseSockets.Num(),4);
	Ports->SupportedContentCount=7; Ports->OnConstruction(Ports->GetActorTransform());
	TestEqual(TEXT("Seven content types create seven hose sockets"),Ports->HoseSockets.Num(),7);
	Ports->HosePortTransforms[6]=FTransform(FRotator(0,35,0),FVector(0,120,80)); Ports->OnConstruction(Ports->GetActorTransform());
	FHitResult PortHit; const auto Mount=Ports->GetHosePortTransform(6);
	World->LineTraceSingleByChannel(PortHit,Mount.GetLocation()+Mount.GetUnitAxis(EAxis::Z)*100,Mount.GetLocation(),ECC_Visibility);
	int32 PortIndex=-1; FTransform ReportedMount;
	TestTrue(TEXT("Visibility trace identifies the hose socket"),Ports->GetHosePortFromHit(PortHit,PortIndex,ReportedMount));
	TestTrue(TEXT("Trace supplies authored nozzle position and rotation"),PortIndex==6 && ReportedMount.Equals(Mount));
	TestEqual(TEXT("The hose socket volume uses trace-only collision"),Ports->GetHoseSocket(6)->GetTraceVolume()->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
	const FVector BeforeMovingVolume=Ports->GetHosePortTransform(6).GetLocation();
	Ports->HoseConnections->AddRelativeLocation(FVector(30,20,10));
	TestTrue(TEXT("Moving supply volume moves its nozzle sockets"),Ports->GetHosePortTransform(6).GetLocation().Equals(BeforeMovingVolume+FVector(30,20,10),.01f));
	TestTrue(TEXT("All receiving volumes are exposed native components"),Machine->BottleInputVolume==Station->BottleInput && Machine->CapInputVolume==Station->CapInput && Machine->SealInputVolume==Station->SealInput);
	Carrier->SetActorLocation(Station->BottleInput->GetComponentLocation()+FVector(0,-165,40));
	const int32 Safe=Spawn(EmptyPID,Carrier->GetActorLocation()+FVector(0,60,0));
	auto* HandBottle = Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Safe));
	PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoAttachActortoComponent"),HandBottle);
	if (!TestTrue(TEXT("The player can carry an empty bottle to the socket"),HandBottle && HandBottle->bIsHeld)) return false;
	CarrierActions->CompleteCarryHandoff(HandCapActor);
	TestTrue(TEXT("A repeated cap handoff preserves a subsequently held bottle"), HoldingActor->GetObjectPropertyValue_InContainer(Carrier)==HandBottle && HandBottle->bIsHeld);
	auto* BottleHit = TraceControl(Station->BottleInput,HandBottle);
	TestEqual(TEXT("Bottle volume can be targeted by a line trace"), BottleHit, static_cast<UPrimitiveComponent*>(Station->BottleInput.Get()));
	const FVector BottleStart = HandBottle->GetActorLocation();
	TestTrue(TEXT("Saved interaction targets the bottle volume"),SendSocketInteraction(*Layout,BeginEvent,Carrier,BottleHit));
	TestNull(TEXT("Bottle handoff also clears the first-person held actor"), HoldingActor->GetObjectPropertyValue_InContainer(Carrier));
	Layout->Tick(.05f);
	TestTrue(TEXT("Moving the bottle toward the cradle doesn't make it ready for filling"), !HandBottle->GetActorLocation().Equals(BottleStart, 1.)
		&& Station->View.SeatedBottleISID == 0 && !Station->IsBottleAligned(Safe));
	FinishInputPlacement();
	if (!TestTrue(TEXT("Bottle handoff seats at the authored socket"),!HandBottle->bIsHeld && Station->IsBottleAligned(Safe) && Station->View.SeatedBottleISID==Safe)) return false;
	TestEqual(TEXT("The occupied bottle volume lets pickup traces through"),Station->BottleInput->GetCollisionResponseToChannel(ECC_Visibility),ECR_Ignore);
	TestTrue(TEXT("Bottle overlap detection still sees the seated bottle"),ShopInstanceVolumeQuery::Query(Station->BottleInput).Contains(Safe));
	TestFalse(TEXT("A seated bottle keeps its active representation"),Manager->DemoteActorToISM(Safe));
	HandBottle->bUseOriginalRocketFlight=false;
	Carrier->SetActorLocation(ControllerSide);
	SendSocketInteraction(*Layout,BeginEvent,Carrier,TraceControl(Lever));
	Machine->Tick(Layout->CradleTravelSeconds+.02f);
	TestTrue(TEXT("Holding the physical lever starts the real hose flow"), Station->View.bLeverHeld && Filler->bIsFilling);
	CarrierActions->RequestProductionAction(Station, Station->View.SessionID, EShopProductionAction::StopFilling, Safe);
	TestTrue(TEXT("Stopping flow keeps the physical lever held"), Station->View.bLeverHeld && !Filler->bIsFilling && Station->View.bFlowStopped);
	SendSocketInteraction(*Layout,EndEvent,Carrier);
	TestTrue(TEXT("Releasing the physical lever closes the hose and clears the X stop"), !Station->View.bLeverHeld && !Filler->bIsFilling && !Station->View.bFlowStopped);
	Machine->Tick(Layout->CradleTravelSeconds+.02f);
	SendSocketInteraction(*Layout,BeginEvent,Carrier,TraceControl(Lever));
	Machine->Tick(Layout->CradleTravelSeconds+.02f);
	TestTrue(TEXT("The next physical pull starts flow without a V press"),Station->View.bLeverHeld && Filler->bIsFilling);
	SendSocketInteraction(*Layout,EndEvent,Carrier);
	Machine->Tick(Layout->CradleTravelSeconds+.02f);
	auto* UIActions=Operator(FVector(0,-120,200));
	auto* Widget=NewObject<UShopTerminalWidget>(UIActions->GetOwner()); Widget->SetOwningPlayer(Cast<APlayerController>(Cast<APawn>(UIActions->GetOwner())->GetController())); Widget->Configure(UIActions,Machine);
	const auto Slate=Widget->TakeWidget();
	const auto Geometry=FGeometry::MakeRoot(FVector2D(1920,1080),FSlateLayoutTransform());
	const FKeyEvent LeverKey(EKeys::F,FModifierKeysState(),0,false,0,0);
	TestEqual(TEXT("The filling display ignores pointer hits"), Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("The filling display leaves keyboard input in gameplay"), Widget->NativeOnKeyDown(Geometry, LeverKey).IsEventHandled());
	TestFalse(TEXT("A widget key event does not pull the physical lever"), Station->View.bLeverHeld);
	Action(EShopProductionAction::HoldFillingLever,Safe);
	if (!TestTrue(TEXT("Lever fills the physical bottle"),Fill(Safe))) return false;
	TestTrue(TEXT("Nominal full mark does not automatically close the valve"),Filler->bIsFilling);
	Action(EShopProductionAction::StopFilling,Safe);
	FPropProductDefinition FilledProduct=*Manager->FindProductDefinition(HandBottle->ProductID);
	FilledProduct.CatalogPID=Manager->CanonicalProductPID(HandBottle->ProductID);
	FilledProduct.ProductID=74199;
	FilledProduct.CustomActiveActorClass=AActivePropActor::StaticClass();
	if (!TestTrue(TEXT("A different-class finished product can be registered"),Manager->RegisterProductDefinition(FilledProduct))) return false;
	FPropEventSyncDelta ProductChange;
	ProductChange.SetFlag(EPropEventDirtyMask::ProductID);
	ProductChange.NewProductID=FilledProduct.ProductID;
	if (!TestTrue(TEXT("Product ID update succeeds while the filled bottle is in the cradle"),Manager->SubmitInstanceEventUpdate(Safe,ProductChange))) return false;
	Station->RefreshInputs();
	TestTrue(TEXT("New product ID preserves the bottle actor, cradle and trace state"),
		Manager->GetActiveActorFromISID(Safe)==HandBottle && HandBottle->ProductID==FilledProduct.ProductID
		&& HandBottle->IsSeatedInFiller() && Station->IsBottleAligned(Safe)
		&& Station->BottleInput->GetCollisionResponseToChannel(ECC_Visibility)==ECR_Ignore);
	const float StoppedLevel=Manager->FindUniqueData(Safe)->EventData.ContentLevel;
	Action(EShopProductionAction::HoldFillingLever,Safe); Tick(.05f);
	TestTrue(TEXT("Lever heartbeat does not restart stopped flow"),!Filler->bIsFilling && Station->View.bLeverHeld && Manager->FindUniqueData(Safe)->EventData.ContentLevel==StoppedLevel);
	TestTrue(TEXT("Holding after stop vents to normal"),Vent(Safe));
	Action(EShopProductionAction::ReleaseFillingLever,Safe);
	TestFalse(TEXT("Safe release uses no seal"),Manager->FindUniqueData(Safe)->EventData.bTemporarySealUsed);
	const int32 CapID=Station->View.Caps[0].ISID;
	TestEqual(TEXT("Cap key fits a cap from the physical tray"),Action(EShopProductionAction::FitOrTightenCap,Safe),EShopCheckoutResult::Success);
	TestEqual(TEXT("Next cap key press turns the fitted cap"),Action(EShopProductionAction::FitOrTightenCap,Safe),EShopCheckoutResult::Success);
	Station->RefreshInputs();
	Widget->RefreshProduction(Station);
	const float PartlyTight=Manager->FindUniqueData(Safe)->EventData.CapSealed;
	TestTrue(TEXT("Corner HUD shows partial cap tightness as a percentage and bar"),
		PartlyTight>0 && PartlyTight<1 && Widget->CapTightnessReadout && Widget->CapTightnessProgress
		&& Widget->CapTightnessReadout->GetText().ToString().Contains(FString::Printf(TEXT("%d%% TIGHT"),FMath::RoundToInt(PartlyTight*100)))
		&& FMath::IsNearlyEqual(Widget->CapTightnessProgress->GetPercent(),PartlyTight));
	Tick(.1f);
	for (int32 I=0;I<20 && Manager->FindUniqueData(Safe)->EventData.CapSealed<1;++I) { Action(EShopProductionAction::FitOrTightenCap,Safe); Tick(.1f); }
	TestTrue(TEXT("Cap identity is retained in finished sealed bottle"),Manager->FindUniqueData(Safe)->EventData.AttachedCapISID==CapID && Manager->FindUniqueData(Safe)->EventData.CapSealed>=1);
	Widget->RefreshProduction(Station);
	TestTrue(TEXT("Corner HUD shows the fully tightened cap"),
		Widget->CapTightnessReadout && Widget->CapTightnessProgress
		&& Widget->CapTightnessReadout->GetText().ToString().Contains(TEXT("100% TIGHT"))
		&& Widget->CapTightnessProgress->GetPercent()==1.f);
	TestEqual(TEXT("Using cap removes one tray item"),Station->View.CapTrayCount,3);
	TestEqual(TEXT("Magazine returns to trace-only once a cap is used"),Station->CapInput->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
	{
		// Isolate the visibility path from the machine art while keeping the real
		// seated bottle, held cap, and server socket handoff.
		Carrier->SetActorLocation(HandBottle->GetActorLocation()+FVector(0,-180,280));
		const int32 RefillID=Spawn(CapPID,Carrier->GetActorLocation()+FVector(0,60,0));
		auto* Refill=Manager->GetActiveActorFromISID(RefillID);
		PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoAttachActortoComponent"),Refill);
		if (!TestTrue(TEXT("The held cap is compatible with the seated bottle"),Refill && Refill->bIsHeld
			&& Station->CanLoadCap(RefillID,Carrier))) return false;
		FVector Eye;
		FRotator View;
		Carrier->GetActorEyesViewPoint(Eye,View);
		const FVector TrayLocation=Station->CapInput->GetComponentLocation();
		Station->CapInput->SetWorldLocation(Eye+FVector(0,170,0));
		auto AddVisibilityBlocker=[](AActor& Owner,const FVector& Location)
		{
			auto* Box=NewObject<UBoxComponent>(&Owner);
			Box->SetBoxExtent(FVector(12));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
			Owner.AddInstanceComponent(Box);
			if (Owner.GetRootComponent()) Box->SetupAttachment(Owner.GetRootComponent());
			else Owner.SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(Location);
			return Box;
		};
		auto* BottleBlocker=AddVisibilityBlocker(*HandBottle,Eye+FVector(0,90,0));
		auto* UnrelatedActor=World->SpawnActor<AActor>();
		auto* UnrelatedBlocker=AddVisibilityBlocker(*UnrelatedActor,Eye+FVector(0,65,0));
		TestEqual(TEXT("The external obstruction is in front of the cap tray"),TraceControl(Station->CapInput,Refill),
			static_cast<UPrimitiveComponent*>(UnrelatedBlocker));
		Layout->BeginSocketInteraction(Carrier,Station->CapInput);
		TestTrue(TEXT("A separate obstruction prevents cap handoff"),HoldingActor->GetObjectPropertyValue_InContainer(Carrier)==Refill);
		UnrelatedBlocker->DestroyComponent();
		UnrelatedActor->Destroy();
		TestEqual(TEXT("The seated bottle itself is now in front of the cap tray"),TraceControl(Station->CapInput,Refill),
			static_cast<UPrimitiveComponent*>(BottleBlocker));
		Layout->BeginSocketInteraction(Carrier,Station->CapInput);
		TestNull(TEXT("The seated bottle cannot prevent cap handoff"),HoldingActor->GetObjectPropertyValue_InContainer(Carrier));
		BottleBlocker->DestroyComponent();
		Station->CapInput->SetWorldLocation(TrayLocation);
		FinishInputPlacement();
		TestEqual(TEXT("A cap loaded behind the seated bottle is registered"),Station->View.CapTrayCount,4);
		Station->ReleaseStoredSupply(RefillID,false);
		MoveAway(RefillID);
	}
	Carrier->SetActorLocation(Station->BottleInput->GetComponentLocation()+FVector(0,-165,40));
	PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoAttachActortoComponent"),HandBottle);
	TestTrue(TEXT("Finished bottle can be picked up and immediately makes the empty volume traceable"),
		HandBottle->bIsHeld && Station->View.SeatedBottleISID==0 && Station->BottleInput->GetCollisionResponseToChannel(ECC_Visibility)==ECR_Block);
	PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoDetachActortoComponent"),HandBottle);
	MoveAway(Safe);
	const int32 ReleaseWithAutoSealOff=MakeBottle();
	if (!ReleaseWithAutoSealOff || !Fill(ReleaseWithAutoSealOff)) return false;
	Station->bAutoApplySealOnRelease=false;
	Action(EShopProductionAction::ReleaseFillingLever,ReleaseWithAutoSealOff);
	TestEqual(TEXT("Disabling Auto Seal keeps all four stocked seals"),Station->View.SealTrayCount,4);
	TestFalse(TEXT("Disabling Auto Seal leaves the released bottle unprotected"),
		Manager->FindUniqueData(ReleaseWithAutoSealOff)->EventData.bTemporarySealUsed);
	MoveAway(ReleaseWithAutoSealOff);
	Station->bAutoApplySealOnRelease=true;
	const int32 FlowRelease=MakeBottle(); if (!FlowRelease || !Fill(FlowRelease)) return false;
	Action(EShopProductionAction::ReleaseFillingLever,FlowRelease);
	auto* FlowReleasedBottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(FlowRelease));
	TestFalse(TEXT("Releasing during flow starts seal grace instead of immediate launch"),FlowReleasedBottle->RocketComp->IsPressureThrustActive());
	TestEqual(TEXT("Unsafe flow release consumes one stocked temporary seal"),Station->View.SealTrayCount,3);
	Manager->WorldElapsedSeconds=Manager->FindUniqueData(FlowRelease)->EventData.TemporarySealUntil+.01;
	FlowReleasedBottle->UpdatePlasticPressure(.01f);
	TestTrue(TEXT("Unsafe flow-release bottle launches when its seal reaches zero"),FlowReleasedBottle->RocketComp->IsPressureThrustActive()); MoveAway(FlowRelease);
	const int32 Overfill=MakeBottle(); if (!Overfill) return false;
	for (int32 I=0;I<180 && !Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Overfill))->RocketComp->IsPressureThrustActive();++I)
	{ Action(EShopProductionAction::HoldFillingLever,Overfill); Tick(.05f); }
	TestTrue(TEXT("Overfill launches even while lever is held"),Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Overfill))->RocketComp->IsPressureThrustActive()); MoveAway(Overfill);
	const int32 Protected=MakeBottle(); if (!Protected || !Fill(Protected)) return false;
	Action(EShopProductionAction::StopFilling,Protected); Action(EShopProductionAction::ReleaseFillingLever,Protected);
	const double Deadline=Manager->FindUniqueData(Protected)->EventData.TemporarySealUntil;
	const double SealSeconds=Deadline-Manager->WorldElapsedSeconds;
	const double AuthoredDuration=SealProduct->NumOfPortions;
	TestTrue(TEXT("Early stopped release uses one physical seal with its authored duration and jitter"),
		SealSeconds>=FMath::Max(.1,AuthoredDuration-1.0) && SealSeconds<=AuthoredDuration+1.0 && Station->View.SealTrayCount==2);
	Station->RefreshInputs();
	Widget->RefreshProduction(Station);
	TestTrue(TEXT("Table HUD shows the active seal countdown"),
		Widget->SealReadout && Widget->SealReadout->GetText().ToString()==FString::Printf(TEXT("SEAL %.1f s LEFT"),SealSeconds));
	TestTrue(TEXT("Table gauge reads the seated bottle pressure"),
		Widget->PressureGauge && Widget->PressureGauge->Pressure>0
		&& FMath::IsNearlyEqual(Widget->PressureGauge->Pressure,Manager->FindUniqueData(Protected)->ContinuousData.SealedPressure));
	TestEqual(TEXT("Seal cannot be reapplied to extend its deadline"),Action(EShopProductionAction::ApplyTemporarySeal,Protected),EShopCheckoutResult::Unavailable);
	const auto* MintOffer=Catalog->FindOffer(TEXT("CylinderMint")); if (!MintOffer) return false;
	const int32 Mint=Spawn(Manager->RegisterProductFromDataAsset(MintOffer->Product.LoadSynchronous()),SupplyOrigin + FVector(-115,-110,30));
	auto* MintContainer=Cast<AColaSupplyContainer>(Manager->GetActiveActorFromISID(Mint)); if (!MintContainer) return false;
	Solo->GetOwner()->SetActorLocation(ControllerSide);
	TestTrue(TEXT("Second physical hose connects to another port"),Machine->AttachSupplyNozzle(FHitResult(Machine,Machine->GetHoseSocket(1)->GetTraceVolume(),Machine->GetHosePortTransform(1).GetLocation(),FVector::UpVector),MintContainer->SupplyNozzle,Mint,Solo->GetOwner()));
	TestEqual(TEXT("Content dial selects connected second input"),Action(EShopProductionAction::SelectContentPort,1),EShopCheckoutResult::Success);
	Solo->GetOwner()->SetActorLocation(FVector(0,-120,220));
	const FString Slot=TEXT("Automation_ManualFill_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!Manager->SavePropDatabase(Slot,0) || !Manager->LoadPropDatabase(Slot,0)) return false;
	UGameplayStatics::DeleteGameInSlot(Slot,0);
	Machine=Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[0])); if (!Machine) return false;
	Station=Machine->Production; Filler=Machine->BottleFiller; Station->RefreshInputs();
	TestTrue(TEXT("Reload restores physical hose attachment"),Machine->HasSupplyNozzle(0,Source));
	TestEqual(TEXT("Reload restores the bottle seated in the saved cradle pose"),Station->View.SeatedBottleISID,Protected);
	TestTrue(TEXT("Reload keeps restored bottle aligned with its cradle"),Station->IsBottleAligned(Protected));
	TestTrue(TEXT("Reload leaves flow closed with the next pull armed"),!Filler->bIsFilling && !Station->View.bFlowStopped);
	TestTrue(TEXT("Reload restores second hose and dial selection"),Machine->HasSupplyNozzle(1,Mint) && Station->View.SelectedContentPort==1 && Station->View.ConnectedSourceISID==Mint);
	TestTrue(TEXT("Reload restores physical stock without duplication"),Station->View.CapTrayCount==3 && Station->View.SealTrayCount==2);
	TestEqual(TEXT("Reload cannot reset the seal deadline"),Manager->FindUniqueData(Protected)->EventData.TemporarySealUntil,Deadline);
	auto* ProtectedActor=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Protected)); if (!ProtectedActor) return false;
	ProtectedActor->bUseOriginalRocketFlight=false; Manager->SetBottleAgitationPressure(Protected,.6f);
	Manager->WorldElapsedSeconds=Deadline+.01; ProtectedActor->UpdatePlasticPressure(.01f);
	TestTrue(TEXT("Unsafe pressure at seal expiry launches"),ProtectedActor->RocketComp->IsPressureThrustActive()); MoveAway(Protected);
	const int32 Recover=MakeBottle(); if (!Recover || !Fill(Recover)) return false;
	Action(EShopProductionAction::StopFilling,Recover); Action(EShopProductionAction::ReleaseFillingLever,Recover);
	const double RecoveryDeadline=Manager->FindUniqueData(Recover)->EventData.TemporarySealUntil;
	TestEqual(TEXT("Pulling lever again during grace is permitted"),Action(EShopProductionAction::HoldFillingLever,Recover),EShopCheckoutResult::Success);
	TestEqual(TEXT("Another player cannot steal the held lever"),Partner->ExecuteProductionAction(Station,Station->View.SessionID,EShopProductionAction::HoldFillingLever,Recover,0,++PartnerSequence),EShopCheckoutResult::OperationConflict);
	TestTrue(TEXT("Holding during grace vents to normal"),Vent(Recover)); Action(EShopProductionAction::ReleaseFillingLever,Recover);
	Manager->WorldElapsedSeconds=FMath::Max(Manager->WorldElapsedSeconds,RecoveryDeadline+.01);
	auto* Recovered=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Recover)); Recovered->UpdatePlasticPressure(.01f);
	TestTrue(TEXT("Normal pressure before seal expiry preserves contents"),!Recovered->RocketComp->IsPoweredFlight() && Manager->FindUniqueData(Recover)->EventData.ContentLevel>=.99f);
	TestEqual(TEXT("Reholding cannot spend a second seal"),Station->View.SealTrayCount,1);
	MoveAway(Recover);
	const int32 LeaseExpired=MakeBottle(); if (!LeaseExpired || !Fill(LeaseExpired)) return false;
	auto* LeaseBottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(LeaseExpired));
	Filler->LeverExpiresAt=World->GetTimeSeconds()-1;
	// Evaluate the bottle before the station's next refresh, reproducing either actor tick order.
	LeaseBottle->UpdatePlasticPressure(.01f);
	TestFalse(TEXT("Expired input lease installs stocked seal before bottle failure evaluation"),LeaseBottle->RocketComp->IsPoweredFlight());
	TestTrue(TEXT("Expired lease starts the same seal deadline as explicit release"),LeaseBottle->LocalUniqueData.EventData.bTemporarySealUsed && LeaseBottle->LocalUniqueData.EventData.TemporarySealUntil>Manager->WorldElapsedSeconds);
	TestEqual(TEXT("Lease expiry consumes exactly the final stocked seal"),Station->View.SealTrayCount,0);
	Manager->WorldElapsedSeconds=LeaseBottle->LocalUniqueData.EventData.TemporarySealUntil+.01;
	LeaseBottle->UpdatePlasticPressure(.01f);
	TestTrue(TEXT("Lease expiry grace still launches if pressure remains unsafe"),LeaseBottle->RocketComp->IsPressureThrustActive());
	MoveAway(LeaseExpired);
	const auto RemainingSeals=Manager->FindUniqueData(Machine->ISID)->EventData.LoadedSealISIDs;
	for (int32 ID:RemainingSeals) { Station->ReleaseStoredSupply(ID,false); MoveAway(ID); }
	const int32 NoSeal=MakeBottle(); if (!NoSeal || !Fill(NoSeal)) return false;
	Action(EShopProductionAction::StopFilling,NoSeal); Action(EShopProductionAction::ReleaseFillingLever,NoSeal);
	TestTrue(TEXT("Stopping then releasing high pressure without a seal launches immediately"),Cast<AColaPropActor>(Manager->GetActiveActorFromISID(NoSeal))->RocketComp->IsPressureThrustActive());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FManualPrototypeQueueTest,"ColossalFizzCo.Shop.PlanarCheckoutQueue",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FManualPrototypeQueueTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Flow=World->SpawnActor<AShopCustomerFlow>();
	auto Marker=[&](FVector Position)
	{
		auto* A=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(A); A->SetRootComponent(Root); Root->RegisterComponent(); A->SetActorLocation(Position); return A;
	};
	Flow->QueuePoint=Marker(FVector(0,0,530)); Flow->QueuePoint->SetActorRotation(FRotator(90,0,0));
	Flow->CheckoutStation=Marker(FVector(0,1000,460));
	const FVector Front=Flow->QueueLocation(0), Next=Flow->QueueLocation(1), Third=Flow->QueueLocation(2);
	TestEqual(TEXT("Queue spacing remains on the shop floor"),Next.Z,Front.Z);
	TestTrue(TEXT("A vertical target marker cannot collapse all customers onto one position"),FVector::Dist2D(Front,Next)>=140 && FVector::Dist2D(Next,Third)>=140);
	TestTrue(TEXT("Fallback queue direction leads away from checkout"),Next.Y<Front.Y);
	return true;
}
#endif
