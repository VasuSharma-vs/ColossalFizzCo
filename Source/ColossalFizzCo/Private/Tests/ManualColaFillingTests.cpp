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
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Shop/ManualColaFillingMachine.h"
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
	const int32 SealPID=Manager->RegisterProductFromDataAsset(SealOffer->Product.LoadSynchronous());
	const int32 Source=Spawn(Manager->RegisterProductFromDataAsset(SourceOffer->Product.LoadSynchronous()),FVector(120,0,180));
	TestEqual(TEXT("Source ID alone cannot connect a hose"),Action(EShopProductionAction::ConnectSupply,0,Source),EShopCheckoutResult::Unavailable);
	auto* Container=Cast<AColaSupplyContainer>(Manager->GetActiveActorFromISID(Source));
	if (!TestNotNull(TEXT("Bulk supply uses a real grab-capable hose actor"),Container)) return false;
	const auto* SourceDefinition=Manager->FindProductDefinition(Manager->FindISIDData(Source)->ProductID);
	TestTrue(TEXT("Activated bulk supply exposes its canonical liquid through the native resource port"),Container->SupplyPort
		&& Container->SupplyPort->IsManagedContentEndpoint() && Container->SupplyPort->GetManagedContentISID()==Source
		&& FMath::IsNearlyEqual(Container->SupplyPort->GetStoredUnits(),SourceDefinition->ConsumableProfile.NumOfPortions*Manager->FindUniqueData(Source)->EventData.ContentLevel,.001f));
	TestFalse(TEXT("A source mesh cannot masquerade as a nozzle"),Machine->AttachSupplyNozzle(FHitResult(Machine,Machine->HosePorts[0],Machine->GetHosePortTransform(0).GetLocation(),FVector::UpVector),Container->PropMesh,Source,Solo->GetOwner()));
	TArray<FVector> CablePoints; Container->SupplyHose->GetCableParticleLocations(CablePoints);
	if (!TestTrue(TEXT("Hose contains an endpoint"),CablePoints.Num()>1)) return false;
	if (!TestTrue(TEXT("Owned player grabs the hose endpoint"),Solo->ExecuteCableGrab(Container->SupplyHose,CablePoints.Last(),CablePoints.Last(),1))) return false;
	if (!TestTrue(TEXT("Owned hose-connection RPC attaches the nozzle"),Solo->ExecuteConnectSupplyHose(Container->SupplyHose,Machine,0,2))) return false;
	TestTrue(TEXT("Connected nozzle matches socket position and orientation"),Container->SupplyNozzle->GetComponentLocation().Equals(Machine->GetHosePortTransform(0).GetLocation(),.01f) && Container->SupplyNozzle->GetComponentQuat().Equals(Machine->GetHosePortTransform(0).GetRotation(),.001f));
	TestEqual(TEXT("One physical nozzle cannot supply two machines"),Second->Production->ExecuteForPlayer(Second->Production->View.SessionID,EShopProductionAction::ConnectSupply,0,Source,Partner->GetOwner()),EShopCheckoutResult::Unavailable);
	TArray<int32> Caps,Seals;
	for (int32 I=0;I<5;++I)
	{
		Caps.Add(Spawn(CapPID,Station->CapInput->GetComponentLocation()));
		Seals.Add(Spawn(SealPID,Station->SealInput->GetComponentLocation()));
	}
	Station->RefreshInputs();
	TestEqual(TEXT("Cap tray accepts exactly four physical caps"),Station->View.CapTrayCount,4);
	TestEqual(TEXT("Seal tray accepts exactly four physical seals"),Station->View.SealTrayCount,4);
	TestEqual(TEXT("Full cap volume blocks incoming physics props"),Station->CapInput->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Block);
	TestEqual(TEXT("Full seal volume blocks incoming physics props"),Station->SealInput->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Block);
	for (int32 ID:Manager->FindUniqueData(Machine->ISID)->EventData.LoadedCapISIDs)
		TestEqual(TEXT("Registered caps have collision disabled"),Manager->GetActiveActorFromISID(ID)->PropMesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
	for (int32 ID:Manager->FindUniqueData(Machine->ISID)->EventData.LoadedSealISIDs)
		TestEqual(TEXT("Registered seals have collision disabled"),Manager->GetActiveActorFromISID(ID)->PropMesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Fifth cap remains unclaimed"),Manager->FindUniqueData(Caps.Last())->EventData.StoredInMachineISID,0);
	TestFalse(TEXT("Overflow cap is pushed out of the full tray"),ShopInstanceVolumeQuery::Query(Station->CapInput).Contains(Caps.Last()));
	TestFalse(TEXT("Overflow seal is pushed out of the full tray"),ShopInstanceVolumeQuery::Query(Station->SealInput).Contains(Seals.Last()));

	auto MakeBottle=[&]()
	{
		const int32 ID=Spawn(EmptyPID,Station->BottleInput->GetComponentLocation()); Station->RefreshInputs();
		if (Action(EShopProductionAction::AlignBottle,ID)!=EShopCheckoutResult::Success) return int32(0);
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
	TestEqual(TEXT("Four content types create four trace spheres"),Ports->HosePorts.Num(),4);
	Ports->SupportedContentCount=7; Ports->OnConstruction(Ports->GetActorTransform());
	TestEqual(TEXT("Seven content types create seven trace spheres"),Ports->HosePorts.Num(),7);
	Ports->HosePortTransforms[6]=FTransform(FRotator(0,35,0),FVector(0,120,80)); Ports->OnConstruction(Ports->GetActorTransform());
	FHitResult PortHit; const auto Mount=Ports->GetHosePortTransform(6);
	World->LineTraceSingleByChannel(PortHit,Mount.GetLocation()+Mount.GetUnitAxis(EAxis::X)*100,Mount.GetLocation(),ECC_Visibility);
	int32 PortIndex=-1; FTransform ReportedMount;
	TestTrue(TEXT("Visibility trace identifies the connection sphere"),Ports->GetHosePortFromHit(PortHit,PortIndex,ReportedMount));
	TestTrue(TEXT("Trace supplies authored nozzle position and rotation"),PortIndex==6 && ReportedMount.Equals(Mount));
	TestEqual(TEXT("Connection spheres use trace-only collision"),Ports->HosePorts[6]->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
	const FVector BeforeMovingVolume=Ports->GetHosePortTransform(6).GetLocation();
	Ports->SupplyInputVolume->AddRelativeLocation(FVector(30,20,10));
	TestTrue(TEXT("Moving supply volume moves its nozzle sockets"),Ports->GetHosePortTransform(6).GetLocation().Equals(BeforeMovingVolume+FVector(30,20,10),.01f));
	TestTrue(TEXT("All receiving volumes are exposed native components"),Machine->BottleInputVolume==Station->BottleInput && Machine->CapInputVolume==Station->CapInput && Machine->SealInputVolume==Station->SealInput);
	const int32 Safe=MakeBottle(); if (!Safe) return false;
	auto* UIActions=Operator(FVector(0,-120,200));
	auto* Widget=NewObject<UShopTerminalWidget>(UIActions->GetOwner()); Widget->SetOwningPlayer(Cast<APlayerController>(Cast<APawn>(UIActions->GetOwner())->GetController())); Widget->Configure(UIActions,Machine);
	const auto Geometry=FGeometry::MakeRoot(FVector2D(1920,1080),FSlateLayoutTransform());
	const FKeyEvent LeverKey(EKeys::F,FModifierKeysState(),0,false,0,0), StopKey(EKeys::X,FModifierKeysState(),0,false,0,0);
	Widget->NativeOnKeyDown(Geometry,LeverKey); TestTrue(TEXT("F-down holds the lever"),Station->View.bLeverHeld);
	Widget->NativeOnKeyDown(Geometry,StopKey); TestTrue(TEXT("X stops flow while F remains held"),Station->View.bLeverHeld && !Filler->bIsFilling && Station->View.bFlowStopped);
	Widget->NativeOnKeyUp(Geometry,LeverKey); TestFalse(TEXT("F-up releases the lever"),Station->View.bLeverHeld);
	Action(EShopProductionAction::HoldFillingLever,Safe); Action(EShopProductionAction::ResumeFilling,Safe);
	if (!TestTrue(TEXT("Lever fills the physical bottle"),Fill(Safe))) return false;
	TestTrue(TEXT("Nominal full mark does not automatically close the valve"),Filler->bIsFilling);
	Action(EShopProductionAction::StopFilling,Safe);
	const float StoppedLevel=Manager->FindUniqueData(Safe)->EventData.ContentLevel;
	Action(EShopProductionAction::HoldFillingLever,Safe); Tick(.05f);
	TestTrue(TEXT("Lever heartbeat does not restart stopped flow"),!Filler->bIsFilling && Station->View.bLeverHeld && Manager->FindUniqueData(Safe)->EventData.ContentLevel==StoppedLevel);
	TestTrue(TEXT("Holding after stop vents to normal"),Vent(Safe));
	Action(EShopProductionAction::ReleaseFillingLever,Safe);
	TestFalse(TEXT("Safe release uses no seal"),Manager->FindUniqueData(Safe)->EventData.bTemporarySealUsed);
	const int32 CapID=Station->View.Caps[0].ISID;
	TestEqual(TEXT("Cap comes from physical tray"),Action(EShopProductionAction::AttachCap,Safe,CapID),EShopCheckoutResult::Success);
	for (int32 I=0;I<20 && Manager->FindUniqueData(Safe)->EventData.CapSealed<1;++I) { Action(EShopProductionAction::TurnCap,Safe); Tick(.1f); }
	TestTrue(TEXT("Cap identity is retained in finished sealed bottle"),Manager->FindUniqueData(Safe)->EventData.AttachedCapISID==CapID && Manager->FindUniqueData(Safe)->EventData.CapSealed>=1);
	TestEqual(TEXT("Using cap removes one tray item"),Station->View.CapTrayCount,3);
	TestEqual(TEXT("Tray unblocks when a cap is used"),Station->CapInput->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
	MoveAway(Safe);
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
	TestTrue(TEXT("Early stopped release uses one physical seal for three to five seconds"),Deadline-Manager->WorldElapsedSeconds>=3 && Deadline-Manager->WorldElapsedSeconds<=5 && Station->View.SealTrayCount==2);
	TestEqual(TEXT("Seal cannot be reapplied to extend its deadline"),Action(EShopProductionAction::ApplyTemporarySeal,Protected),EShopCheckoutResult::Unavailable);
	const auto* MintOffer=Catalog->FindOffer(TEXT("CylinderMint")); if (!MintOffer) return false;
	const int32 Mint=Spawn(Manager->RegisterProductFromDataAsset(MintOffer->Product.LoadSynchronous()),FVector(120,100,180));
	auto* MintContainer=Cast<AColaSupplyContainer>(Manager->GetActiveActorFromISID(Mint)); if (!MintContainer) return false;
	TestTrue(TEXT("Second physical hose connects to another port"),Machine->AttachSupplyNozzle(FHitResult(Machine,Machine->HosePorts[1],Machine->GetHosePortTransform(1).GetLocation(),FVector::UpVector),MintContainer->SupplyNozzle,Mint,Solo->GetOwner()));
	TestEqual(TEXT("Content dial selects connected second input"),Action(EShopProductionAction::SelectContentPort,1),EShopCheckoutResult::Success);
	const FString Slot=TEXT("Automation_ManualFill_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!Manager->SavePropDatabase(Slot,0) || !Manager->LoadPropDatabase(Slot,0)) return false;
	UGameplayStatics::DeleteGameInSlot(Slot,0);
	Machine=Cast<AManualColaFillingMachine>(Manager->GetActiveActorFromISID(MachineIDs[0])); if (!Machine) return false;
	Station=Machine->Production; Filler=Machine->BottleFiller; Station->RefreshInputs();
	TestTrue(TEXT("Reload restores physical hose attachment"),Machine->HasSupplyNozzle(0,Source));
	TestEqual(TEXT("Reload restores the bottle seated in the saved cradle pose"),Station->View.SeatedBottleISID,Protected);
	TestTrue(TEXT("Reload keeps restored bottle aligned with its cradle"),Station->IsBottleAligned(Protected));
	TestTrue(TEXT("Reload keeps the liquid valve stopped"),Station->View.bFlowStopped);
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
