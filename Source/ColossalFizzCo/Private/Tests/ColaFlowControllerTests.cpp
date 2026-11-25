#include "Misc/AutomationTest.h"

#include "Components/CollisionCableComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Resources/HoseCableActor.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ColaFlowController.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaFlowControllerOutletSplitTest,
	"ColossalFizzCo.Hose.FlowController.ConnectedOutletSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FColaFlowControllerOutletSplitTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();

	constexpr float InletRate=8.f;
	constexpr float InletPressure=2.f;
	constexpr float Step=.05f;
	constexpr int32 ContentID=9491;
	constexpr int32 InletCount=AColaFlowController::MaxInletPorts;
	constexpr int32 OutletCount=AColaFlowController::MaxOutletPorts;
	auto* Flow=World->SpawnActor<AColaFlowController>(FVector(0,0,1000),FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Flow Controller"),Flow)
		|| !TestEqual(TEXT("The controller starts with one inlet"),Flow->SelectorSockets.Num(),1)
		|| !TestEqual(TEXT("The controller starts with one outlet"),Flow->FillerSockets.Num(),1)) return false;
	TestEqual(TEXT("The controller starts with a manual lever"),Flow->LeverLevel,1);
	TestEqual(TEXT("Default intake is one portion per second"),Flow->GetIntakeRate(),1.f);
	TestEqual(TEXT("Default output is one portion per second"),Flow->GetOutputRate(),1.f);
	TestEqual(TEXT("Default suction is zero"),Flow->GetIntakeSuctionStrength(),0.f);
	const float BaseTolerance=Flow->GetInletPressureTolerance();
	FFlowControllerUpgradeState State;

	const auto MakePort=[World](const FVector& Location, bool bSupply)
	{
		auto* Owner=World->SpawnActor<AActor>();
		auto* Port=NewObject<UResourceTransferEndpointComponent>(Owner);
		Owner->SetRootComponent(Port);
		Owner->AddInstanceComponent(Port);
		Port->ResourceID=ContentID;
		Port->bCanSupply=bSupply;
		Port->bCanReceive=!bSupply;
		Port->bSupplyEnabled=bSupply;
		Port->bRequestsFlow=!bSupply;
		Port->CapacityUnits=200.f;
		Port->RegisterComponent();
		Port->SetWorldLocation(Location);
		return Port;
	};
	const auto MakeHose=[World]()
	{
		auto* Hose=World->SpawnActor<AHoseCableActor>(FVector(1000,0,1000),FRotator::ZeroRotator);
		Hose->Transfer->SetComponentTickEnabled(false);
		Hose->Cable->SetComponentTickEnabled(false);
		return Hose;
	};

	auto* Inlet=Flow->SelectorSockets[0].Get();
	auto* Source=MakePort(Inlet->GetComponentLocation()+FVector(0,-140,0),true);
	Source->SetStoredUnits(100.f);
	Source->MaxSupplyUnitsPerSecond=InletRate;
	Source->SetOperatingPressure(InletPressure);
	auto* InletHose=MakeHose();
	if (!TestTrue(TEXT("The pressurized supply is physically connected"),
		InletHose->ConnectStart(Source) && InletHose->ConnectEnd(Inlet))) return false;
	{
		TGuardValue<float> HoseRate(InletHose->Transfer->MaxUnitsPerSecond,4.f);
		TGuardValue<float> HoseBurst(InletHose->Transfer->BurstPressureThreshold,1.75f);
		TGuardValue<float> HoseBurstDelay(InletHose->Transfer->BurstDelay,.1f);
		Source->SetOperatingPressure(.1f);
		Flow->Tick(Step);
		const float DefaultDelivery=InletHose->Transfer->TransferForDuration(Step)/Step;
		TestTrue(TEXT("Low source pressure limits unassisted intake"),DefaultDelivery>0.f && DefaultDelivery<1.f);
		State.SuctionRank=1;
		Flow->RestoreUpgradeState(State);
		Flow->Tick(Step);
		TestEqual(TEXT("One suction rank adds a quarter strength"),Flow->GetIntakeSuctionStrength(),.25f);
		const float AssistedDelivery=InletHose->Transfer->TransferForDuration(Step)/Step;
		TestTrue(TEXT("Suction helps a weak supply reach the manifold's intake cap"),FMath::IsNearlyEqual(AssistedDelivery,1.f,.005f));
		Source->SetOperatingPressure(0.f);
		Flow->Tick(Step);
		TestTrue(TEXT("Suction can draw cola from an open supply at zero gauge pressure"),InletHose->Transfer->TransferForDuration(Step)>0.f);
		Source->SetSupplyEnabled(false);
		Flow->Tick(Step);
		InletHose->Transfer->TransferForDuration(Step);
		TestEqual(TEXT("Suction never draws new cola through a closed supply valve"),InletHose->Transfer->LastIntakeUnits,0.f);
		Source->SetSupplyEnabled(true);
		Source->SetOperatingPressure(InletPressure);
		Inlet->SetStoredUnits(Inlet->CapacityUnits);
		InletHose->Transfer->TransferForDuration(.3f);
		TestFalse(TEXT("Intake assist reduces pressure load before the connected hose can burst"),InletHose->Transfer->bBurst);
		TestTrue(TEXT("Connected hose load is source load divided by one plus suction"),
			FMath::IsNearlyEqual(InletHose->Transfer->CurrentPressure,InletPressure/1.25f,.005f));
		Inlet->SetStoredUnits(0.f);
	}
	auto* FirstOutlet=Flow->FillerSockets[0].Get();
	auto* FirstOutletHose=MakeHose();
	auto* LockedInlet=FindObject<UHoseSocketComponent>(Flow,TEXT("SelectorSocket_02"));
	auto* LockedOutlet=FindObject<UHoseSocketComponent>(Flow,TEXT("FillerSocket_02"));
	if (!TestNotNull(TEXT("The future inlet has a stable component"),LockedInlet)
		|| !TestNotNull(TEXT("The future outlet has a stable component"),LockedOutlet)) return false;
	TestFalse(TEXT("A locked inlet cannot take a hose"),FirstOutletHose->ConnectEnd(LockedInlet));
	TestFalse(TEXT("A locked outlet cannot take a hose"),FirstOutletHose->ConnectStart(LockedOutlet));
	if (!TestTrue(TEXT("A branch attaches before upgrading"),FirstOutletHose->ConnectStart(FirstOutlet))) return false;
	for (int32 Count=2;Count<=InletCount;++Count)
	{
		State.InputPortsRank=Count-1;
		Flow->RestoreUpgradeState(State);
		if (!TestEqual(TEXT("An inlet rank adds one socket"),Flow->SelectorSockets.Num(),Count)) return false;
	}
	for (int32 Count=2;Count<=OutletCount;++Count)
	{
		State.OutputPortsRank=Count-1;
		Flow->RestoreUpgradeState(State);
		if (!TestEqual(TEXT("An outlet rank adds one socket"),Flow->FillerSockets.Num(),Count)) return false;
	}
	State.FlowRank=7;
	State.SuctionRank=0;
	Flow->RestoreUpgradeState(State);
	Flow->Tick(Step);
	TestEqual(TEXT("Top flow rank caps total intake at eight portions per second"),Inlet->MaxReceiveUnitsPerSecond,8.f);
	TestEqual(TEXT("Top flow rank caps total output at eight portions per second"),Flow->GetOutputRate(),8.f);
	TestTrue(TEXT("Top flow rank adds 70 percent of base inlet pressure tolerance"),
		FMath::IsNearlyEqual(Inlet->MaxIntakePressure,BaseTolerance*1.7f,.005f));
	TestTrue(TEXT("Port upgrades preserve the existing inlet and hose"),Flow->SelectorSockets[0].Get()==Inlet
		&& Inlet->ConnectedHose.Get()==InletHose && InletHose->EndPort==Inlet);
	TestTrue(TEXT("Port upgrades preserve the existing outlet and hose"),Flow->FillerSockets[0].Get()==FirstOutlet
		&& FirstOutlet->ConnectedHose.Get()==FirstOutletHose && FirstOutletHose->StartPort==FirstOutlet);
	TestTrue(TEXT("The new fourth inlet can be selected"),Flow->SelectInput(InletCount-1));
	TestFalse(TEXT("An inlet beyond the cap cannot be selected"),Flow->SelectInput(InletCount));
	TestTrue(TEXT("The operator can return to the connected inlet"),Flow->SelectInput(0));
	if (!TestTrue(TEXT("The original branch can be unplugged"),FirstOutlet->DetachHose())) return false;

	FActorSpawnParameters PlayerSpawn;
	PlayerSpawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Player=World->SpawnActor<ACharacter>(Flow->Lever->Bounds.Origin+FVector(0,130,45),FRotator::ZeroRotator,PlayerSpawn);
	auto* PlayerController=World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("The lever operator is present"),Player)
		|| !TestNotNull(TEXT("The operator's controller is present"),PlayerController)) return false;
	Player->SetActorEnableCollision(false);
	PlayerController->Possess(Player);
	if (!TestTrue(TEXT("The operator can pull the lever with all outlets empty"),Flow->BeginLever(Player,350.f))) return false;

	TArray<AHoseCableActor*> Hoses;
	TArray<UResourceTransferEndpointComponent*> Sinks;
	const auto CheckSplit=[&](int32 Connected)
	{
		Flow->Tick(Step);
		const float Rate=Connected>0 ? InletRate/Connected : 0.f;
		const float Pressure=Connected>0 ? InletPressure/Connected : 0.f;
		TestEqual(FString::Printf(TEXT("%d plugged hoses are counted"),Connected),Flow->ConnectedOutletCount,Connected);
		TestEqual(TEXT("Inlet pressure is independent of the outlet count"),Flow->InletSupplyPressure,InletPressure);
		TestEqual(TEXT("Flow is split across the plugged hoses"),Flow->PortionsPerSecondPerFiller,Rate);
		TestEqual(TEXT("Pressure is split across the plugged hoses"),Flow->PressurePerOutlet,Pressure);
		for (int32 Index=0;Index<OutletCount;++Index)
		{
			const auto* Port=Flow->FillerSockets[Index].Get();
			const bool bPlugged=Index<Connected;
			TestEqual(TEXT("Only plugged outlets can supply"),Port->bSupplyEnabled,bPlugged);
			TestEqual(TEXT("The outlet has its assigned pressure or zero"),Port->GetOperatingPressure(),bPlugged ? Pressure : 0.f);
			TestEqual(TEXT("The outlet has its assigned rate or zero"),Port->MaxSupplyUnitsPerSecond,bPlugged ? Rate : 0.f);
		}
	};
	const auto CheckDelivery=[&](int32 Connected)
	{
		const float Rate=InletRate/Connected;
		const float Pressure=InletPressure/Connected;
		InletHose->Transfer->TransferForDuration(Step);
		for (int32 Index=0;Index<Connected;++Index)
		{
			auto* Sink=Sinks[Index];
			const float Before=Sink->GetStoredUnits();
			Hoses[Index]->Transfer->TransferForDuration(Step);
			TestTrue(TEXT("The receiving end accepts its split rate"),
				FMath::IsNearlyEqual((Sink->GetStoredUnits()-Before)/Step,Rate,.005f));
			TestTrue(TEXT("The receiving end reads its split pressure"),
				FMath::IsNearlyEqual(Sink->LastIncomingPressure,Pressure,.005f));
		}
	};
	const auto PlugThrough=[&](int32 Connected)
	{
		while (Hoses.Num()<Connected)
		{
			auto* Outlet=Flow->FillerSockets[Hoses.Num()].Get();
			auto* Hose=MakeHose();
			if (!TestTrue(TEXT("An open-ended hose plugs into the outlet"),Hose->ConnectStart(Outlet))) return false;
			Hoses.Add(Hose);
			if (Hoses.Num()==1) CheckSplit(1);
			auto* Sink=MakePort(Outlet->GetComponentLocation()+FVector(0,160,0),false);
			if (!TestTrue(TEXT("The far end plugs into a receiver"),Hose->ConnectEnd(Sink))) return false;
			Sinks.Add(Sink);
		}
		return true;
	};

	CheckSplit(0);
	for (int32 Connected : {1,2,4,OutletCount})
	{
		if (!PlugThrough(Connected)) return false;
		CheckSplit(Connected);
		CheckDelivery(Connected);
	}

	for (int32 Index=OutletCount-1;Index>0;--Index)
		TestTrue(TEXT("A branch can be unplugged while the lever is held"),Flow->FillerSockets[Index]->DetachHose());
	CheckSplit(1);
	CheckDelivery(1);
	TestTrue(TEXT("The last branch can be unplugged while the lever is held"),Flow->FillerSockets[0]->DetachHose());
	CheckSplit(0);
	Flow->ReleaseLeverInput(Player);
	TestFalse(TEXT("Releasing interact returns the level-one lever"),Flow->bLeverHeld);
	Flow->Tick(Flow->LeverTravelSeconds);
	TestTrue(TEXT("Level one returns to its resting angle"),FMath::IsNearlyEqual(Flow->Lever->GetRelativeRotation().Roll,-10.,.1));

	State.LeverRank=1;
	Flow->RestoreUpgradeState(State);
	if (!TestEqual(TEXT("A lever rank advances one level"),Flow->LeverLevel,2)
		|| !TestTrue(TEXT("Pressing interact pulls the level-two lever"),Flow->BeginLever(Player,350.f))) return false;
	Flow->ReleaseLeverInput(Player);
	Flow->Tick(Flow->LeverTravelSeconds);
	TestTrue(TEXT("Level two stays pulled after interact is released"),Flow->bLeverHeld);
	TestTrue(TEXT("The latched lever rotates to its pulled angle"),FMath::IsNearlyEqual(Flow->Lever->GetRelativeRotation().Roll,100.,.1));
	const FVector InteractLocation=Player->GetActorLocation();
	Player->SetActorLocation(InteractLocation+FVector(0,500,0));
	Flow->Tick(Step);
	TestTrue(TEXT("The latched lever stays pulled while the operator checks a distant bottle"),Flow->bLeverHeld);
	Player->SetActorLocation(InteractLocation);
	TestTrue(TEXT("Pressing interact again releases level two"),Flow->BeginLever(Player,350.f));
	TestFalse(TEXT("The second press clears the latch"),Flow->bLeverHeld);

	auto* Manager=World->SpawnActor<APropManager>();
	if (!TestNotNull(TEXT("The bottle manager is present"),Manager)) return false;
	Manager->bEnableAutosave=false;
	Manager->InitializePropManager();
	FPropContentDefinition Content;
	Content.ContentID=ContentID;
	if (!TestTrue(TEXT("The manager registers the test cola"),Manager->RegisterContentDefinition(Content))) return false;

	FPropProductDefinition Equipment;
	Equipment.ProductID=9492;
	Equipment.CustomActiveActorClass=AManualColaFillingMachine::StaticClass();
	Equipment.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
	Equipment.bEnableInstanceUniqueData=true;
	FPropProductDefinition BottleProduct;
	BottleProduct.ProductID=9493;
	BottleProduct.CustomActiveActorClass=AColaPropActor::StaticClass();
	BottleProduct.StaticMesh=Equipment.StaticMesh;
	BottleProduct.bEnableInstanceUniqueData=true;
	BottleProduct.ConsumableProfile.NumOfPortions=4;
	BottleProduct.BottleMaterialType=EBottleMaterialType::Plastic;
	BottleProduct.MaxOverfillLevel=1.5f;
	BottleProduct.SupportedContentIDs={Content.ContentID};
	BottleProduct.DefaultUniqueData.EventData.ContentLevel=0.f;
	BottleProduct.DefaultUniqueData.EventData.CapSealed=0.f;
	BottleProduct.DefaultUniqueData.EventData.bClean=true;
	BottleProduct.DefaultUniqueData.EventData.Strength=1.f;
	if (!TestTrue(TEXT("The manager registers filling tables and bottles"),
		Manager->RegisterProductDefinition(Equipment) && Manager->RegisterProductDefinition(BottleProduct))) return false;

	AManualColaFillingMachine* Tables[2]={};
	int32 BottleIDs[2]={};
	const auto SeedBottle=[&](int32 Index, float Level)
	{
		FPropEventSyncDelta Delta;
		Delta.SetFlag(EPropEventDirtyMask::ContentID);
		Delta.SetFlag(EPropEventDirtyMask::ContentLevel);
		Delta.ContentID=Content.ContentID;
		Delta.ContentLevel=Level;
		return Manager->SubmitInstanceEventUpdate(BottleIDs[Index],Delta);
	};
	for (int32 Index=0;Index<UE_ARRAY_COUNT(Tables);++Index)
	{
		const int32 MachineID=Manager->GenerateProductInstance(Equipment.ProductID,FTransform(FVector(Index==0 ? -180 : 100,250,1000)));
		AActivePropActor* Active=nullptr;
		if (!TestTrue(TEXT("The table promotes to an active machine"),Manager->PromoteInstanceToASM(
			MachineID,EPropActivationReason::Interaction,false,Active))) return false;
		auto* Table=Cast<AManualColaFillingMachine>(Active);
		if (!TestNotNull(TEXT("The active machine is a manual filler"),Table)) return false;
		Tables[Index]=Table;
		Table->PropMesh->SetSimulatePhysics(false);
		Table->bUseFlowController=true;
		Table->OnConstruction(Table->GetActorTransform());

		auto* Hose=MakeHose();
		if (!TestTrue(TEXT("Each table connects to its own controller outlet"),
			Table->PrepareReceivingHosePort(0,Content.ContentID)
			&& Hose->ConnectStart(Flow->FillerSockets[Index].Get()) && Hose->ConnectEnd(Table->GetHoseSocket(0)))) return false;
		BottleIDs[Index]=Manager->GenerateProductInstance(BottleProduct.ProductID,Table->BottleHolderPoint->GetComponentTransform());
		if (!TestTrue(TEXT("Each filling table seats a managed bottle"),Table->Production->SeatBottle(BottleIDs[Index]))) return false;
		auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleIDs[Index]));
		if (!TestNotNull(TEXT("The seated bottle is active"),Bottle)) return false;
		Bottle->SafeResidualPressure=InletPressure;
		if (!TestTrue(TEXT("Each bottle starts with an assigned fill level"),SeedBottle(Index,Index==0 ? .98f : .1f))) return false;
	}

	const auto ReadFill=[&](int32 Index) { return Manager->FindUniqueData(BottleIDs[Index])->EventData.ContentLevel; };
	const auto StepBatch=[&]()
	{
		InletHose->Transfer->TransferForDuration(Step);
		for (auto* Table : Tables) Table->BottleFiller->TickComponent(Step,LEVELTICK_All,nullptr);
		Flow->Tick(Step);
	};
	const auto StartBatch=[&]()
	{
		if (!Flow->BeginLever(Player,350.f)) return false;
		Flow->ReleaseLeverInput(Player);
		for (auto* Table : Tables) Table->UpdateCradleMotion(Table->CradleTravelSeconds);
		Flow->Tick(Step);
		return Flow->bLeverHeld && Tables[0]->BottleFiller->bIsFilling && Tables[1]->BottleFiller->bIsFilling;
	};

	State.LeverRank=2;
	Flow->RestoreUpgradeState(State);
	if (!TestTrue(TEXT("Level three starts both bottles"),StartBatch())) return false;
	for (int32 Index=0;Index<10 && Flow->bLeverHeld;++Index) StepBatch();
	TestEqual(TEXT("Level three fills to nominal capacity"),ReadFill(0),1.f);
	TestTrue(TEXT("The other bottle is still below capacity"),ReadFill(1)<1.f);
	TestFalse(TEXT("One full bottle releases the level-three lever"),Flow->bLeverHeld);
	TestFalse(TEXT("Level three closes both outlets"),Flow->FillerSockets[0]->bSupplyEnabled || Flow->FillerSockets[1]->bSupplyEnabled);
	if (!TestTrue(TEXT("A second run starts with the first bottle nearly full"),SeedBottle(0,.98f))) return false;

	State.LeverRank=3;
	Flow->RestoreUpgradeState(State);
	TestEqual(TEXT("The final lever level is four"),Flow->LeverLevel,4);
	auto* FullBottle=CastChecked<AColaPropActor>(Manager->GetActiveActorFromISID(BottleIDs[0]));
	FullBottle->SafeResidualPressure=0.f;
	if (!TestTrue(TEXT("Level four starts both bottles"),StartBatch())) return false;
	for (int32 Index=0;Index<10 && Flow->FillerSockets[0]->bSupplyEnabled;++Index) StepBatch();
	TestEqual(TEXT("Level four fills to nominal capacity"),ReadFill(0),1.f);
	TestTrue(TEXT("Level four stays pulled after the first bottle fills"),Flow->bLeverHeld);
	TestFalse(TEXT("The full bottle's outlet closes"),Flow->FillerSockets[0]->bSupplyEnabled);
	TestTrue(TEXT("The unfilled bottle's outlet remains open"),Flow->FillerSockets[1]->bSupplyEnabled);
	TestTrue(TEXT("The full bottle stays held while its pressure vents"),Tables[0]->Production->View.bLeverHeld);
	TestTrue(TEXT("The operator can pause and resume the batch"),Flow->SetFlowPaused(Player,true) && Flow->SetFlowPaused(Player,false));
	Flow->Tick(Step);
	TestFalse(TEXT("Resuming the batch keeps the full bottle's outlet closed"),Flow->FillerSockets[0]->bSupplyEnabled);
	const float OtherBefore=ReadFill(1);
	StepBatch();
	TestEqual(TEXT("The closed outlet cannot add cola to a full bottle"),ReadFill(0),1.f);
	TestTrue(TEXT("The other bottle continues to fill"),ReadFill(1)>OtherBefore);
	Manager->SetBottleAgitationPressure(BottleIDs[0],0.f,true);
	Flow->Tick(Step);
	TestTrue(TEXT("Automatic mode releases the vented bottle without returning the main lever"),
		!Tables[0]->Production->View.bLeverHeld && Flow->bLeverHeld);
	Tables[0]->Production->ReleaseSeatedBottle();
	Manager->InitializeInstanceTransform(BottleIDs[0],FTransform(FVector(-700,700,1000)));
	Flow->Tick(Step);
	TestFalse(TEXT("Removing the full bottle leaves the empty station's outlet closed"),Flow->FillerSockets[0]->bSupplyEnabled);
	BottleIDs[0]=Manager->GenerateProductInstance(BottleProduct.ProductID,Tables[0]->BottleHolderPoint->GetComponentTransform());
	if (!TestTrue(TEXT("The first station can seat another full bottle"),
		Tables[0]->Production->SeatBottle(BottleIDs[0]) && SeedBottle(0,1.f))) return false;
	Flow->Tick(Step);
	TestFalse(TEXT("An already full replacement leaves the outlet closed"),Flow->FillerSockets[0]->bSupplyEnabled);
	Tables[0]->Production->ReleaseSeatedBottle();
	Manager->InitializeInstanceTransform(BottleIDs[0],FTransform(FVector(-700,780,1000)));
	BottleIDs[0]=Manager->GenerateProductInstance(BottleProduct.ProductID,Tables[0]->BottleHolderPoint->GetComponentTransform());
	if (!TestTrue(TEXT("The first station seats a replacement bottle"),Tables[0]->Production->SeatBottle(BottleIDs[0]))) return false;
	Flow->Tick(Step);
	Tables[0]->UpdateCradleMotion(Tables[0]->CradleTravelSeconds);
	Flow->Tick(Step);
	TestTrue(TEXT("A valid replacement opens its outlet without restarting the controller"),Flow->FillerSockets[0]->bSupplyEnabled);
	StepBatch();
	TestTrue(TEXT("The replacement bottle receives cola"),ReadFill(0)>0.f);
	TestTrue(TEXT("A second press releases level four"),Flow->BeginLever(Player,350.f));
	return true;
}
#endif
