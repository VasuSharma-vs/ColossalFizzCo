#include "Misc/AutomationTest.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopShelfStockComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "UI/ShopTerminalWidget.h"
#include "Shop/ShopCustomerFlow.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PropGrabTests { bool InvokeCarryRPC(ACharacter* Player,const FString& Name,AActor* Target); }
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
	const FTransform PlacedTransform(FVector(2200,0,150));
	auto* Placed=World->SpawnActorDeferred<AManualColaFillingMachine>(MachineProduct->CustomActiveActorClass,PlacedTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Placed) return false;
	Placed->PropDataAsset=const_cast<UInteractablePropDataAsset*>(MachineProduct); Placed->State=EPropState::Unsettled; Placed->bAutoConvertToISMIfAtRest=false;
	UGameplayStatics::FinishSpawningActor(Placed,PlacedTransform); Placed->Tick(.05f);
	int32 PlacedID=0;
	for (const auto& Pair:Manager->ISIDData) if (const auto* Def=Manager->FindProductDefinition(Pair.Value.ProductID))
		if (Def->SourceDataAsset==MachineProduct && Manager->GetTransform(Pair.Key).GetLocation().Equals(PlacedTransform.GetLocation(),1)) PlacedID=Pair.Key;
	if (!TestTrue(TEXT("Starter machine receives one active managed inventory identity"),PlacedID>0 && Manager->GetActiveActorFromISID(PlacedID))) return false;
	if (auto* Starter=Manager->GetActiveActorFromISID(PlacedID)) Starter->PropMesh->SetSimulatePhysics(false);
	TArray<int32> MachineIDs;
	if (!TestEqual(TEXT("Multiple manual machines can be purchased in one transaction"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),TEXT("ManualFillingMachine"),2,FTransform(FVector(0,0,1000)),MachineIDs),EShopPurchaseResult::Success)) return false;
	if (!TestEqual(TEXT("Two distinct managed machine identities delivered"),MachineIDs.Num(),2)) return false;
	TestEqual(TEXT("Only the catalog machine price is charged"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(20000)-2*MachineOffer->UnitCostCents);
	auto* Machine=Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[0]));
	auto* Second=Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[1]));
	if (!TestTrue(TEXT("Purchased machines use the native equipment actor"),Machine && Second)) return false;
	for (auto* M:{Machine,Second}) M->PropMesh->SetSimulatePhysics(false);
	Manager->InitializeInstanceTransform(MachineIDs[0],FTransform(FVector(0,0,150)));
	Manager->InitializeInstanceTransform(MachineIDs[1],FTransform(FVector(250,0,150)));
	// Exercise the saved character RPC and generic prop interface inherited by
	// the new machine Blueprint, in addition to testing the manager services.
	UClass* PlayerClass=LoadClass<ACharacter>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Production carry character exists"),PlayerClass)) return false;
	World->SetBegunPlay(false);
	auto* Carrier=World->SpawnActor<ACharacter>(PlayerClass,FVector(0,-100,200),FRotator::ZeroRotator);
	World->SetBegunPlay(true);
	auto* CarrierPC=World->SpawnActor<APlayerController>(); CarrierPC->Possess(Carrier);
	Carrier->GetCharacterMovement()->DisableMovement();
	TestTrue(TEXT("Saved player pickup RPC accepts purchased machine"),PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoAttachActortoComponent"),Machine));
	if (!TestTrue(TEXT("Purchased machine enters actual owned carry lifecycle"),Machine->bIsHeld && Manager->FindISIDData(MachineIDs[0])->RuntimeData.InteractionOwner.Get()==Carrier)) return false;
	TestTrue(TEXT("Saved player placement RPC accepts purchased machine"),PropGrabTests::InvokeCarryRPC(Carrier,TEXT("RequestServertoDetachActortoComponent"),Machine));
	TestTrue(TEXT("Purchased machine releases with physics and retains identity"),!Machine->bIsHeld && Machine->PropMesh->IsSimulatingPhysics() && Machine->ISID==MachineIDs[0]);
	Machine->PropMesh->SetSimulatePhysics(false);
	Manager->InitializeInstanceTransform(MachineIDs[0],FTransform(FVector(0,0,150)));
	auto* Station=Machine->Production.Get(); auto* Filler=Machine->BottleFiller.Get();
	TestTrue(TEXT("Manual machine permits solo closure and requires held flow"),Station->bAllowSoloCap && Station->bManualLeverMode && Filler->bRequireLever);
	auto Operator=[&](FVector Location)
	{
		auto* Pawn=World->SpawnActor<ACharacter>(); Pawn->SetActorLocation(Location); Pawn->SetActorEnableCollision(false); Pawn->GetCharacterMovement()->SetMovementMode(MOVE_None);
		auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
		auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent(); return Actions;
	};
	auto* Solo=Operator(FVector(0,-100,200)); auto* Partner=Operator(FVector(100,-100,200));
	int64 Sequence=0,PartnerSequence=0;
	auto Action=[&](EShopProductionAction A,int32 Bottle=0,int32 Other=0){return Solo->ExecuteProductionAction(Station,Station->View.SessionID,A,Bottle,Other,++Sequence);};
	const auto* SourceOffer=Catalog->FindOffer(TEXT("CylinderLemonade")); const auto* CapOffer=Catalog->FindOffer(TEXT("TwistCap"));
	if (!SourceOffer || !CapOffer) return false;
	const int32 Source=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(SourceOffer->Product.LoadSynchronous()),FTransform(FVector(120,0,180)));
	if (!TestEqual(TEXT("First pipe connects to real Normal bulk cola"),Action(EShopProductionAction::ConnectSupply,0,Source),EShopCheckoutResult::Success)) return false;
	TestEqual(TEXT("Second machine can share the same source through its own single pipe"),Second->Production->ExecuteForPlayer(Second->Production->View.SessionID,EShopProductionAction::ConnectSupply,0,Source,Partner->GetOwner()),EShopCheckoutResult::Success);
	const int32 EmptyPID=Manager->RegisterProductFromDataAsset(Catalog->EmptyBottleProduct.LoadSynchronous());
	const int32 CapPID=Manager->RegisterProductFromDataAsset(CapOffer->Product.LoadSynchronous());
	auto MakeBottle=[&]()
	{
		const int32 ID=Manager->GenerateProductInstance(EmptyPID,FTransform(Station->BottleInput->GetComponentLocation()));
		Station->RefreshInputs();
		if (Action(EShopProductionAction::AlignBottle,ID)!=EShopCheckoutResult::Success) return int32(0);
		return ID;
	};
	auto Tick=[&](float Dt)
	{
		for (const auto& Pair:Manager->ISIDData) if (auto* Active=Manager->GetActiveActorFromISID(Pair.Key)) if (Active->PropMesh) Active->PropMesh->SetSimulatePhysics(false);
		World->Tick(LEVELTICK_All,Dt);
		// The synthetic world dispatches equipment services explicitly; the real
		// listen-server test separately verifies normal registered frame ticking.
		Station->RefreshInputs(); Second->Production->RefreshInputs();
		if (Second->BottleFiller->bIsFilling) Second->BottleFiller->TickComponent(Dt,LEVELTICK_All,nullptr);
		if (Filler->bIsFilling) Filler->TickComponent(Dt,LEVELTICK_All,nullptr);
		for (const auto& Pair:Manager->ISIDData) if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Pair.Key))) Bottle->UpdatePlasticPressure(Dt);
	};
	auto Fill=[&](int32 ID)
	{
		for (int32 I=0;I<100 && Manager->FindUniqueData(ID)->EventData.ContentLevel<1;++I)
		{ if (const auto Result=Action(EShopProductionAction::HoldFillingLever,ID); Result!=EShopCheckoutResult::Success) { AddInfo(FString::Printf(TEXT("Fill rejected result=%d now=%.3f lease=%.3f seated=%d target=%d aligned=%d"),int32(Result),World->GetTimeSeconds(),Filler->LeverExpiresAt,Station->View.SeatedBottleISID,ID,Station->IsBottleAligned(ID))); return false; } Tick(.1f); }
		Action(EShopProductionAction::ReleaseFillingLever,ID);
		return Manager->FindUniqueData(ID)->EventData.ContentLevel==1;
	};
	auto Attach=[&](int32 ID)
	{
		const auto* CapDef=Manager->FindProductDefinition(CapPID);
		const FVector Center=CapDef->StaticMesh->GetBoundingBox().GetCenter();
		const int32 Cap=Manager->GenerateProductInstance(CapPID,FTransform(Station->CapInput->GetComponentLocation()-Center));
		const auto Result=Action(EShopProductionAction::AttachCap,ID,Cap);
		if (Result!=EShopCheckoutResult::Success)
		{
			const auto* B=Manager->FindUniqueData(ID);
			const auto* C=Manager->FindUniqueData(Cap);
			const auto* R=Manager->FindISIDData(ID);
			AddInfo(FString::Printf(TEXT("Attach diagnosis bottleTray=%d capTray=%d distance=%.4f compatible=%d bottleSeal=%.4f bottleCap=%d capBottle=%d capStrength=%.3f capState=%d filling=%d"),
				ShopInstanceVolumeQuery::Query(Station->BottleInput).Contains(ID),ShopInstanceVolumeQuery::Query(Station->CapInput).Contains(Cap),FVector::Dist(Manager->GetTransform(ID).GetLocation(),Manager->GetTransform(Cap).GetLocation()),R && B && Manager->IsAssemblyCompatible(R->ProductID,B->EventData.ContentID,CapPID),B?B->EventData.CapSealed:-1,B?B->EventData.AttachedCapISID:0,C?C->EventData.AttachedBottleISID:0,C?C->EventData.Strength:-1,int32(Manager->GetPropState(Cap)),Filler->bIsFilling));
		}
		return Result==EShopCheckoutResult::Success ? Cap : 0;
	};
	auto Twist=[&](int32 ID)
	{
		for (int32 I=0;I<12 && Manager->FindUniqueData(ID)->EventData.CapSealed<1;++I)
		{ if (Action(EShopProductionAction::TurnCap,ID)!=EShopCheckoutResult::Success) return false; Tick(.1f); }
		return Manager->FindUniqueData(ID)->EventData.CapSealed==1;
	};
	const int32 Fast=MakeBottle(); if (!Fast) return false;
	auto* Seated = Manager->GetActiveActorFromISID(Fast);
	if (!Seated || !Seated->PhysicsSimComponent) return false;
	Seated->PhysicsSimComponent->SetStateSettling();
	TestTrue(TEXT("An ordinary settling transition cannot wake a bottle in the filler"),Seated->IsSeatedInFiller() && !Seated->PropMesh->IsSimulatingPhysics() && !Seated->CanDemoteToISM());
	TestEqual(TEXT("Old click-to-fill cannot bypass a held lever"),Action(EShopProductionAction::Fill,Fast,Source),EShopCheckoutResult::Unavailable);
	// Exercise the real widget key/button callbacks, including release and loss of focus.
	auto* UIPlayer=Operator(FVector(0,-120,200));
	auto* Widget=NewObject<UShopTerminalWidget>(UIPlayer->GetOwner()); Widget->SetOwningPlayer(Cast<APlayerController>(Cast<APawn>(UIPlayer->GetOwner())->GetController())); Widget->Configure(UIPlayer,Machine);
	const auto Geometry=FGeometry::MakeRoot(FVector2D(800,650),FSlateLayoutTransform());
	const FKeyEvent LeverKey(EKeys::F,FModifierKeysState(),0,false,0,0);
	Widget->NativeOnKeyDown(Geometry,LeverKey); TestTrue(TEXT("Actual F-down callback opens the held valve"),Filler->bIsFilling);
	Widget->NativeOnKeyUp(Geometry,LeverKey); TestFalse(TEXT("Actual F-up callback closes the valve immediately"),Filler->bIsFilling);
	Widget->NativeOnKeyDown(Geometry,LeverKey); Widget->NativeOnFocusLost(FFocusEvent(EFocusCause::Cleared,0));
	TestFalse(TEXT("Losing terminal focus releases a held lever"),Filler->bIsFilling);
	auto* LeverButton=NewObject<UShopTerminalLeverButton>(Widget); LeverButton->Configure(Widget,TEXT("Test held lever")); LeverButton->ISID=Fast;
	LeverButton->OnPressed.Broadcast(); TestTrue(TEXT("Mouse press opens the same valve"),Filler->bIsFilling);
	LeverButton->OnReleased.Broadcast(); TestFalse(TEXT("Mouse release closes the same valve"),Filler->bIsFilling);
	TestEqual(TEXT("Aligned bottle begins flow while lever held"),Action(EShopProductionAction::HoldFillingLever,Fast),EShopCheckoutResult::Success);
	Tick(.1f); Action(EShopProductionAction::ReleaseFillingLever,Fast);
	const float Partial=Manager->FindUniqueData(Fast)->EventData.ContentLevel;
	Tick(.2f);
	TestEqual(TEXT("Release immediately stops further flow"),Manager->FindUniqueData(Fast)->EventData.ContentLevel,Partial);
	Manager->InitializeInstanceTransform(MachineIDs[0],FTransform(FVector(0,1000,150))); Station->RefreshInputs();
	TestTrue(TEXT("Moving a machine releases the previous seated bottle to physics"),Manager->GetActiveActorFromISID(Fast)->PropMesh->IsSimulatingPhysics() && Station->View.SeatedBottleISID==0 && !Filler->bIsFilling);
	TestEqual(TEXT("Moving equipment preserves partial bottle contents"),Manager->FindUniqueData(Fast)->EventData.ContentLevel,Partial);
	Manager->GetActiveActorFromISID(Fast)->PropMesh->SetSimulatePhysics(false);
	Manager->InitializeInstanceTransform(MachineIDs[0],FTransform(FVector(0,0,150))); Station->RefreshInputs();
	TestEqual(TEXT("Another operator can resume a released lever"),Partner->ExecuteProductionAction(Station,Station->View.SessionID,EShopProductionAction::HoldFillingLever,Fast,0,++PartnerSequence),EShopCheckoutResult::Success);
	TestEqual(TEXT("Two players cannot own one lever simultaneously"),Action(EShopProductionAction::HoldFillingLever,Fast),EShopCheckoutResult::OperationConflict);
	AddInfo(FString::Printf(TEXT("Lease before wait now=%.3f expires=%.3f ticking=%d registered=%d"),World->GetTimeSeconds(),Filler->LeverExpiresAt,Filler->IsComponentTickEnabled(),Filler->IsRegistered()));
	for (int32 I=0;I<8;++I) Tick(.1f);
	AddInfo(FString::Printf(TEXT("Lease after wait now=%.3f expires=%.3f filling=%d"),World->GetTimeSeconds(),Filler->LeverExpiresAt,Filler->bIsFilling));
	TestFalse(TEXT("Missing operator heartbeat releases the valve"),Filler->bIsFilling);
	if (!TestTrue(TEXT("One player resumes and fills the same bottle"),Fill(Fast))) return false;
	TestTrue(TEXT("Filling creates real residual pressure"),Manager->FindUniqueData(Fast)->ContinuousData.SealedPressure>Station->SafeResidualPressure);
	if (!TestTrue(TEXT("Normal physical cap attaches"),Attach(Fast)>0)) return false;
	TestEqual(TEXT("Temporary seal can protect a full bottle"),Action(EShopProductionAction::ApplyTemporarySeal,Fast),EShopCheckoutResult::Success);
	TestFalse(TEXT("Temporary seal blocks liquid-transfer sessions"),Manager->BeginLiquidTransfer(Source,Fast).IsValid());
	TestEqual(TEXT("Temporary seal cannot be extended repeatedly"),Action(EShopProductionAction::ApplyTemporarySeal,Fast),EShopCheckoutResult::Unavailable);
	if (!TestTrue(TEXT("Solo player twists a protected bottle shut without a holder"),Twist(Fast))) return false;
	TestEqual(TEXT("Proper closure cancels the expiry hazard"),Manager->FindUniqueData(Fast)->EventData.TemporarySealUntil,0.);
	Manager->InitializeInstanceTransform(Fast,FTransform(FVector(1500,0,200))); Station->RefreshInputs();
	const int32 Slow=MakeBottle(); if (!Slow) return false;
	const int32 Concurrent=Manager->GenerateProductInstance(EmptyPID,FTransform(Second->Production->BottleInput->GetComponentLocation()));
	Second->Production->RefreshInputs();
	auto OtherAction=[&](EShopProductionAction A){return Partner->ExecuteProductionAction(Second->Production,Second->Production->View.SessionID,A,Concurrent,0,++PartnerSequence);};
	if (!TestEqual(TEXT("Second operator aligns an independent bottle"),OtherAction(EShopProductionAction::AlignBottle),EShopCheckoutResult::Success)) return false;
	const float SharedBefore=Manager->FindUniqueData(Source)->EventData.ContentLevel;
	for (int32 I=0;I<100 && (Manager->FindUniqueData(Slow)->EventData.ContentLevel<1 || Manager->FindUniqueData(Concurrent)->EventData.ContentLevel<1);++I)
	{
		if (Manager->FindUniqueData(Slow)->EventData.ContentLevel<1 && Action(EShopProductionAction::HoldFillingLever,Slow)!=EShopCheckoutResult::Success) return false;
		if (Manager->FindUniqueData(Concurrent)->EventData.ContentLevel<1 && OtherAction(EShopProductionAction::HoldFillingLever)!=EShopCheckoutResult::Success) return false;
		Tick(.1f);
	}
	Action(EShopProductionAction::ReleaseFillingLever,Slow); OtherAction(EShopProductionAction::ReleaseFillingLever);
	TestEqual(TEXT("Both machines can fill at the same time"),Manager->FindUniqueData(Concurrent)->EventData.ContentLevel,1.f);
	TestTrue(TEXT("Concurrent machines debit exactly two bottles from one source"),FMath::IsNearlyEqual(SharedBefore-Manager->FindUniqueData(Source)->EventData.ContentLevel,.2f,.0001f));
	if (!Attach(Slow)) return false;
	for (int32 I=0;I<80 && Manager->FindUniqueData(Slow)->ContinuousData.SealedPressure>Station->SafeResidualPressure;++I) Tick(.1f);
	TestTrue(TEXT("Unsealed residual pressure vents naturally"),Manager->FindUniqueData(Slow)->ContinuousData.SealedPressure<=Station->SafeResidualPressure);
	if (!TestTrue(TEXT("Solo player safely closes naturally vented bottle"),Twist(Slow))) return false;
	TestFalse(TEXT("Safe method needs no temporary seal"),Manager->FindUniqueData(Slow)->EventData.bTemporarySealUsed);
	Manager->InitializeInstanceTransform(Slow,FTransform(FVector(1700,0,200))); Station->RefreshInputs();
	const int32 Risk=MakeBottle(); if (!Risk || !Fill(Risk) || !Attach(Risk)) return false;
	if (!TestEqual(TEXT("Risk bottle receives its limited seal"),Action(EShopProductionAction::ApplyTemporarySeal,Risk),EShopCheckoutResult::Success)) return false;
	TestEqual(TEXT("Risk bottle may have an incomplete manual twist"),Action(EShopProductionAction::TurnCap,Risk),EShopCheckoutResult::Success);
	const double Deadline=Manager->FindUniqueData(Risk)->EventData.TemporarySealUntil;
	const FString Slot=TEXT("Automation_ManualFill_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!TestTrue(TEXT("Save includes equipment links and pending seal deadline"),Manager->SavePropDatabase(Slot,0))) return false;
	if (!TestTrue(TEXT("Reload restores manufacturing progress"),Manager->LoadPropDatabase(Slot,0))) return false;
	TestEqual(TEXT("Loading cannot restart a temporary seal timer"),Manager->FindUniqueData(Risk)->EventData.TemporarySealUntil,Deadline);
	TestTrue(TEXT("Loading retains partial twist progress"),Manager->FindUniqueData(Risk)->EventData.CapSealed>0 && Manager->FindUniqueData(Risk)->EventData.CapSealed<1);
	TestEqual(TEXT("Supply pipe connection persists"),Manager->FindUniqueData(MachineIDs[0])->EventData.LinkedSupplyISID,Source);
	TestNotNull(TEXT("Purchased machine is active after reload"),Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[1])));
	auto* RiskBottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Risk));
	if (!RiskBottle) return false;
	RiskBottle->bUseOriginalRocketFlight=false;
	Manager->WorldElapsedSeconds=Deadline+.01;
	RiskBottle->UpdatePlasticPressure(.01f);
	TestEqual(TEXT("Expired temporary protection clears once"),Manager->FindUniqueData(Risk)->EventData.TemporarySealUntil,0.);
	TestEqual(TEXT("Residual pressure ejects improperly secured physical cap"),Manager->FindUniqueData(Risk)->EventData.AttachedCapISID,0);
	TestTrue(TEXT("Expiry reuses the existing pressure rocket physics"),RiskBottle->RocketComp && RiskBottle->RocketComp->IsPressureThrustActive());
	if (auto* LoadedMachine=Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[0]))) LoadedMachine->Production->RefreshInputs();
	TestTrue(TEXT("Holder releases the launching bottle instead of disabling rocket physics"),RiskBottle->PropMesh->IsSimulatingPhysics());
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
