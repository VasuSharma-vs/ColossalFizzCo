#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraActor.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/ResourcePumpComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/CableMovementConstraintComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Resources/HoseCableActor.h"
#include "Resources/ResourcePortLibrary.h"
#include "Shop/ShopPlayerInteractionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CableHoseTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
struct FFixture
{
	UWorld* World=nullptr;
	FFixture()
	{
		const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
		World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	}
	~FFixture()
	{
		if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	AActor* Actor(FVector Position=FVector::ZeroVector)
	{
		auto* Result=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Result);
		Result->SetRootComponent(Root); Result->AddInstanceComponent(Root); Root->RegisterComponent(); Result->SetActorLocation(Position);
		return Result;
	}
	UResourceTransferEndpointComponent* Endpoint(EResourceTransportCategory Category,int32 ResourceID,float Capacity,float Stored,AActor* Owner=nullptr)
	{
		if (!Owner) Owner=Actor();
		auto* Result=NewObject<UResourceTransferEndpointComponent>(Owner); Owner->AddInstanceComponent(Result);
		Result->SetupAttachment(Owner->GetRootComponent()); Result->Category=Category; Result->ResourceID=ResourceID;
		Result->bCanSupply=Result->bCanReceive=true; Result->CapacityUnits=Capacity; Result->RegisterComponent(); Result->SetStoredUnits(Stored);
		return Result;
	}
	UHoseTransferComponent* Hose(EResourceTransportCategory Category,int32 ResourceID)
	{
		auto* Owner=Actor(); auto* Result=NewObject<UHoseTransferComponent>(Owner); Owner->AddInstanceComponent(Result);
		Result->Category=Category; Result->ResourceID=ResourceID; Result->MaxUnitsPerSecond=10; Result->bTransferEnabled=true; Result->RegisterComponent();
		Result->BufferCapacityUnits=10; Result->BurstPressureThreshold=100; Result->BlockedPressureRisePerSecond=0;
		return Result;
	}
	UHoseSocketComponent* Socket(AActor* Owner, const FVector& Offset=FVector::ZeroVector)
	{
		auto* Result=NewObject<UHoseSocketComponent>(Owner);
		Owner->AddInstanceComponent(Result);
		Result->SetupAttachment(Owner->GetRootComponent());
		Result->SetRelativeLocation(Offset);
		Result->RegisterComponent();
		return Result;
	}
	AActor* PullBody(FVector Position,float Weight=10,float Resistance=0,bool Fixed=false)
	{
		auto* Result=World->SpawnActor<AActor>(); auto* Root=NewObject<UBoxComponent>(Result);
		Result->SetRootComponent(Root); Result->AddInstanceComponent(Root); Root->SetMobility(EComponentMobility::Movable);
		Root->SetBoxExtent(FVector(5)); Root->SetCollisionProfileName(TEXT("BlockAll")); Root->RegisterComponent(); Result->SetActorLocation(Position);
		auto* Response=NewObject<UCablePullResponseComponent>(Result); Result->AddInstanceComponent(Response);
		Response->WeightKg=Weight; Response->ResistanceNewtons=Resistance; Response->bFixed=Fixed; Response->RegisterComponent();
		return Result;
	}
	UCollisionCableComponent* Link(AActor* Start,AActor* End,float Length)
	{
		auto* Cable=NewObject<UCollisionCableComponent>(Start); Start->AddInstanceComponent(Cable); Cable->SetupAttachment(Start->GetRootComponent());
		Cable->NumSegments=4; Cable->CableLength=Length; Cable->EndLocation=FVector::ZeroVector;
		Cable->bAttachStart=Cable->bAttachEnd=true; Cable->bStartAttachmentIsWorldAnchor=Cable->bEndAttachmentIsWorldAnchor=false;
		Cable->bEnableCollision=Cable->bEnableSleeping=Cable->bEnablePointWeight=Cable->bEnableNetworkSync=false;
		Cable->bEnableMovementRestrictions=true; Cable->LengthSlackTolerance=0; Cable->CableGravityScale=0; Cable->RegisterComponent();
		if (!Cable->HasBegunPlay()) Cable->BeginPlay();
		Cable->SetAttachEndToComponent(End->GetRootComponent()); Cable->ResetCable();
		return Cable;
	}
	UCableMovementConstraintComponent* Adapter(AActor* Driver,UCollisionCableComponent* Seed)
	{
		auto* Result=Driver->FindComponentByClass<UCableMovementConstraintComponent>();
		if (!Result) { Result=NewObject<UCableMovementConstraintComponent>(Driver); Driver->AddInstanceComponent(Result); Result->RegisterComponent(); }
		Result->RegisterCable(Seed); return Result;
	}
	void AddWalkingFloor()
	{
		auto* Floor = PullBody(FVector(0, 0, -25), 10, 0, true);
		CastChecked<UBoxComponent>(Floor->GetRootComponent())->SetBoxExtent(FVector(4000, 4000, 25));
	}
	ACharacter* SpawnWalker(FVector Location)
	{
		auto* Player = World->SpawnActor<ACharacter>(Location, FRotator::ZeroRotator);
		if (!Player) return nullptr;
		Location.Z = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15;
		Player->SetActorLocation(Location);
		auto* Movement = Player->GetCharacterMovement();
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(MOVE_Walking);
		Movement->Velocity = FVector(Movement->GetMaxSpeed(), 0, 0);
		return Player;
	}
	void Walk(ACharacter& Player, FVector Direction, float Step = 1.f / 60.f)
	{
		Player.AddMovementInput(Direction, 1.f, true);
		auto* Movement = Player.GetCharacterMovement();
		Movement->TickComponent(Step, LEVELTICK_All, &Movement->PrimaryComponentTick);
	}
	bool ManagedStocks(APropManager*& Manager,int32& Source,int32& Target)
	{
		Manager=World->SpawnActor<APropManager>(); if (!Manager) return false;
		Manager->bEnableAutosave=false; Manager->InitializePropManager(); Manager->WorldElapsedSeconds=100;
		FPropContentDefinition Content; Content.ContentID=9301; Content.ExpiryProfile.bHasExpiry=false; Manager->RegisterContentDefinition(Content);
		FPropProductDefinition Product; Product.ProductID=9311; Product.bEnableInstanceUniqueData=true;
		Product.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")); Product.CustomActiveActorClass=AActivePropActor::StaticClass();
		Product.ConsumableProfile.NumOfPortions=100; Product.MaxOverfillLevel=1; Product.DefaultUniqueData.EventData.ContentID=9301;
		Product.DefaultUniqueData.EventData.ContentLevel=.04f; Product.DefaultUniqueData.EventData.Strength=1;
		Product.DefaultUniqueData.EventData.bClean=true; Product.DefaultUniqueData.EventData.CapSealed=0; Product.SupportedContentIDs={9301};
		Manager->RegisterProductDefinition(Product);
		Product.ProductID=9312; Product.ConsumableProfile.NumOfPortions=4; Product.DefaultUniqueData.EventData.ContentID=0;
		Product.DefaultUniqueData.EventData.ContentLevel=0; Manager->RegisterProductDefinition(Product);
		Source=Manager->GenerateProductInstance(9311,FTransform(FVector(0,0,1000)));
		Target=Manager->GenerateProductInstance(9312,FTransform(FVector(200,0,1000)));
		FPropEventSyncDelta Age; Age.ExtendedDirtyMask=EPropExtendedEventMask::ContentCreationTime; Age.ContentCreationTime=10;
		Manager->SubmitInstanceEventUpdate(Source,Age); Manager->SetTemperature(Source,35);
		return Source>0 && Target>0;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseTypedRulesTest,"ColossalFizzCo.Hose.TypedRules",CableHoseTests::Flags)
bool FHoseTypedRulesTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Content,1,10,8);
	auto* Target=F.Endpoint(EResourceTransportCategory::Content,1,10,0);
	auto* Hose=F.Hose(EResourceTransportCategory::Content,1);
	Target->Category=EResourceTransportCategory::Fuel;
	TestFalse(TEXT("Matching resource number cannot connect incompatible transport categories"),Hose->ConnectEndpoints(Source,Target));
	Target->Category=EResourceTransportCategory::Content; Target->ResourceID=2;
	TestFalse(TEXT("Content identity must match at both ends"),Hose->ConnectEndpoints(Source,Target));
	Target->ResourceID=1; Target->bCanReceive=false;
	TestFalse(TEXT("Supply-only endpoint cannot be a receiving connection"),Hose->ConnectEndpoints(Source,Target));
	Target->bCanReceive=true;
	TestFalse(TEXT("Endpoint cannot transfer to itself"),Hose->ConnectEndpoints(Source,Source));
	TestEqual(TEXT("Rejected connections preserve source inventory"),Source->GetStoredUnits(),8.f);
	if (!TestTrue(TEXT("Matching category, resource and direction connect"),Hose->ConnectEndpoints(Source,Target))) return false;
	Target->ResourceID=2;
	TestEqual(TEXT("Resource changes after connection cannot move incompatible content"),Hose->TransferForDuration(1),0.f);
	TestEqual(TEXT("Invalidated connection cannot debit the source"),Source->GetStoredUnits(),8.f);
	TestEqual(TEXT("Invalidated connection cannot credit the target"),Target->GetStoredUnits(),0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseSocketFlowTest,"ColossalFizzCo.Hose.ReusableSocket.FlowAndBreakLimits",CableHoseTests::Flags)
bool FHoseSocketFlowTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F;
	if (!F.World) return false;
	auto* Outlet=F.Socket(F.Actor(FVector(0,0,150)));
	auto* Inlet=F.Socket(F.Actor(FVector(200,0,150)));
	Outlet->bCanSupply=true;
	Outlet->bCanReceive=false;
	Outlet->ResourceID=7;
	Outlet->PushPressure=1.f;
	Outlet->MaxSupplyUnitsPerSecond=2.f;
	Outlet->SetStoredUnits(20.f);
	Inlet->ResourceID=7;
	Inlet->CapacityUnits=10.f;
	Inlet->BreakAtStoredUnits=3.f;
	Inlet->PullPressure=1.f;
	Inlet->SetRelativeRotation(FRotator(27,64,18));
	auto* Hose=F.World->SpawnActor<AHoseCableActor>();
	if (!Hose) return false;
	Hose->Transfer->BurstPressureThreshold=100.f;
	if (!TestTrue(TEXT("An empty adaptable hose accepts the typed receiving socket first, using either end"),Inlet->AttachHose(Hose,0))) return false;
	TestTrue(TEXT("The socket tracks a physically attached hose before the other end is connected"),Inlet->ConnectedHose==Hose && !Inlet->IsHoseSupplying());
	TestEqual(TEXT("The receiver sets the subtype of the empty unconnected hose"),Hose->Transfer->ResourceID,7);
	Hose->Tick(0.f);
	TestTrue(TEXT("The hose connector aligns to the component's location and arrow axis"),Hose->StartEndpointMesh->GetComponentLocation().Equals(Inlet->GetComponentLocation(),.01)
		&& Hose->StartEndpointMesh->GetUpVector().Equals(Inlet->GetUpVector(),.001));
	if (!TestTrue(TEXT("The remaining connector joins the configured outlet"),Outlet->AttachHose(Hose,Hose->Cable->NumSegments))) return false;
	TestTrue(TEXT("Flow starts when both compatible sockets are connected"),FMath::IsNearlyEqual(Hose->Transfer->TransferForDuration(.5f),1.f,.002f));
	TestTrue(TEXT("Both ends expose the measured rate after the outlet cap"),Inlet->IsHoseSupplying() && Outlet->IsHoseSupplying()
		&& FMath::IsNearlyEqual(Inlet->GetCurrentFlowUnitsPerSecond(),2.f,.004f)
		&& FMath::IsNearlyEqual(Outlet->GetCurrentFlowUnitsPerSecond(),2.f,.004f));
	TestEqual(TEXT("Incoming pressure includes configured suction"),Inlet->LastIncomingPressure,2.f);
	Inlet->SetFlowRequested(false);
	Hose->Transfer->TransferForDuration(.5f);
	TestFalse(TEXT("Closing demand clears the live flow reading"),Inlet->IsHoseSupplying());
	Inlet->SetFlowRequested(true);
	Hose->Transfer->TransferForDuration(3.f);
	TestTrue(TEXT("The stored-volume limit stops flow and pops only the inlet"),Inlet->bIsBroken && !Inlet->ConnectedHose
		&& !Hose->EndPort && Hose->StartPort==Outlet && !Hose->Transfer->HasValidConnection());
	TestTrue(TEXT("Storage and received total stop at the configured amount"),FMath::IsNearlyEqual(Inlet->GetStoredUnits(),3.f,.002f)
		&& FMath::IsNearlyEqual(Inlet->TotalReceivedUnits,3.0,.002));
	TestFalse(TEXT("A broken inlet rejects another connection"),Inlet->AttachHose(Hose));
	TestFalse(TEXT("The storage limit must be cleared before repair"),Inlet->RepairSocket());
	Inlet->SetStoredUnits(0.f);
	if (!TestTrue(TEXT("A drained inlet can be repaired and reconnected"),Inlet->RepairSocket() && Inlet->AttachHose(Hose))) return false;

	Inlet->MaxIntakePressure=1.f;
	TestEqual(TEXT("Excess pressure breaks the inlet before any credit"),Hose->Transfer->TransferForDuration(.5f),0.f);
	TestTrue(TEXT("Pressure overload leaves the outlet connected"),Inlet->bIsBroken && !Hose->EndPort && Hose->StartPort==Outlet);
	Inlet->MaxIntakePressure=6.f;
	Inlet->MaxIntakeUnitsPerSecond=.5f;
	if (!TestTrue(TEXT("Repair restores the pressure-popped inlet"),Inlet->RepairSocket() && Inlet->AttachHose(Hose))) return false;
	TestEqual(TEXT("Excess intake rate also fails before any credit"),Hose->Transfer->TransferForDuration(.5f),0.f);
	TestTrue(TEXT("Rate overload breaks the socket before transferring stored resource"),Inlet->bIsBroken && !Hose->EndPort && Inlet->GetStoredUnits()==0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseSocketInteractionTest,"ColossalFizzCo.Hose.ReusableSocket.SelectsExactVolume",CableHoseTests::Flags)
bool FHoseSocketInteractionTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F;
	if (!F.World) return false;
	auto* Player=F.World->SpawnActor<ACharacter>();
	auto* PC=F.World->SpawnActor<APlayerController>();
	Player->SetActorEnableCollision(false);
	Player->GetCharacterMovement()->DisableMovement();
	PC->Possess(Player);
	auto* Local=NewObject<ULocalPlayer>(GEngine);
	Local->PlayerController=PC;
	PC->Player=Local;
	FVector Eye;
	FRotator Aim;
	Player->GetActorEyesViewPoint(Eye,Aim);
	auto* Camera=F.World->SpawnActor<ACameraActor>(Eye,Aim);
	if (!Camera) return false;
	PC->SetViewTarget(Camera);
	PC->UpdateCameraManager(0);
	auto* Actions=NewObject<UShopPlayerInteractionComponent>(Player);
	Player->AddInstanceComponent(Actions);
	Actions->RegisterComponent();
	PC->GetPlayerViewPoint(Eye,Aim);
	const FVector Direction=Aim.Vector();
	const FVector Side=FRotationMatrix(Aim).GetUnitAxis(EAxis::Y);
	auto* SocketActor=F.Actor(Eye+Direction*180);
	auto* Wrong=F.Socket(SocketActor,-Side*45);
	Wrong->Category=EResourceTransportCategory::Fuel;
	auto* Right=F.Socket(SocketActor,Side*45);
	if (!TestTrue(TEXT("Each addable socket registers its own trace-only box"),Right->GetTraceVolume()
		&& Right->GetTraceVolume()->GetCollisionEnabled()==ECollisionEnabled::QueryOnly
		&& Right->GetTraceVolume()->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Ignore
		&& UHoseSocketComponent::GetSocketFromHitComponent(Right->GetTraceVolume())==Right)) return false;
	TestNull(TEXT("The actor-only fallback cannot select a socket without its specific hit"),
		UResourcePortLibrary::ResolveResourcePort(SocketActor,EResourceTransportCategory::Content,1,false));

	auto* Hose=F.World->SpawnActor<AHoseCableActor>();
	Hose->bCarryConnectorOnPickup=true;
	Hose->Cable->bEnableGrabbing=true;
	Hose->Cable->bEnableCollision=false;
	const FVector Near=Eye+Direction*80;
	TArray<FVector> Points;
	for (int32 Index=0; Index<=Hose->Cable->NumSegments; ++Index) Points.Add(Near+Side*(5*Index));
	Hose->Cable->SetCableParticleLocations(Points);
	Hose->Tick(0);
	if (!TestTrue(TEXT("The player's grab takes the physical connector"),Actions->TryToggleCableGrabFromView() && Hose->FindHandHeldConnector(Player)==0)) return false;

	auto AimAtSocket = [&](const UHoseSocketComponent* Target)
	{
		const FRotator DirectionToSocket=(Target->GetTraceVolume()->Bounds.Origin-Eye).Rotation();
		Camera->SetActorRotation(DirectionToSocket);
		PC->SetControlRotation(DirectionToSocket);
		PC->UpdateCameraManager(0);
	};
	AimAtSocket(Wrong);
	TestTrue(TEXT("E on an incompatible socket leaves the hose in hand"),Actions->TryOpenTerminalFromView() && Actions->IsCableGrabActive() && !Wrong->ConnectedHose);
	AimAtSocket(Right);
	TestTrue(TEXT("E attaches to the exact compatible component and consumes the hold"),Actions->TryOpenTerminalFromView()
		&& !Actions->IsCableGrabActive() && Right->ConnectedHose==Hose && Hose->EndPort==Right && !Wrong->ConnectedHose);

	auto* Source=F.Endpoint(EResourceTransportCategory::Content,Hose->Transfer->ResourceID,10,5,F.Actor(Eye+Direction*220-Side*90));
	if (!TestTrue(TEXT("The supply side connects before rupture"), Hose->ConnectStart(Source))) return false;
	Source->PushPressure=2;
	Source->MaxSupplyUnitsPerSecond=1;
	Right->SetFlowRequested(false);
	Hose->Transfer->BurstPressureThreshold=1;
	Hose->Transfer->BurstDelay=.1f;
	Hose->Transfer->TransferForDuration(.2f);
	if (!TestTrue(TEXT("A ruptured content hose stays mounted until unplugged"),
		Hose->Transfer->bBurst && Right->ConnectedHose==Hose)) return false;
	TestTrue(TEXT("Grab takes the burst connector out of the socket while preserving the supply end"),
		Actions->TryGrabSocketHoseFromView(true) && !Right->ConnectedHose && Hose->StartPort==Source
		&& Hose->FindHandHeldConnector(Player)==Hose->GetReceivingConnectorIndex());
	Actions->RequestCableRelease();
	TestTrue(TEXT("A released burst connector can be grabbed again"), Hose->CanGrabConnector(Hose->GetReceivingConnectorIndex()));
	TestFalse(TEXT("Burst connectors cannot be plugged back in"), Right->AttachHose(Hose));
	Hose->DisconnectStart();
	Hose->Cable->SetCableParticleLocations(Points);
	Hose->Tick(0);
	Camera->SetActorRotation(Aim);
	PC->SetControlRotation(Aim);
	PC->UpdateCameraManager(0);
	if (!TestTrue(TEXT("A fully loose burst hose can be picked up from the floor"), Actions->TryGrabLooseHoseFromView())) return false;
	const int32 HeldPoint=Hose->FindHandHeldConnector(Player);
	Hose->Cable->GetCableParticleLocations(Points);
	if (!TestTrue(TEXT("Picking up the burst hose still takes a connector into the hand"), Points.IsValidIndex(HeldPoint))) return false;
	const FVector ThrowOrigin=Points[HeldPoint];
	for (int32 I=0; I<Points.Num(); ++I) Points[I]=ThrowOrigin+Side*5*(I-HeldPoint);
	Hose->Cable->CableLength=5*Hose->Cable->NumSegments;
	Hose->Cable->CableGravityScale=0;
	Hose->Cable->SetCableParticleLocations(Points);
	Actions->RequestCableDrop();
	Hose->Cable->TickComponent(1.f/30.f, LEVELTICK_All, nullptr);
	Hose->Cable->GetCableParticleLocations(Points);
	TestTrue(TEXT("Dropping a loose blown hose tosses it along the player's aim"), !Actions->IsCableGrabActive()
		&& Hose->FindHandHeldConnector(Player)==INDEX_NONE && FVector::DotProduct(Points[HeldPoint]-ThrowOrigin,Direction)>.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLooseHoseCarryWeightTest, "ColossalFizzCo.Hose.Interaction.LooseCarryWeight", CableHoseTests::Flags)
bool FLooseHoseCarryWeightTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F;
	if (!F.World) return false;
	F.AddWalkingFloor();
	auto* HoseClass = LoadClass<AHoseCableActor>(nullptr, TEXT("/Game/Resources/Hoses/BP_ContentHose.BP_ContentHose_C"));
	if (!TestNotNull(TEXT("The saved content hose preset loads"), HoseClass)) return false;
	const FVector Forward(1, 0, 0);
	double Speeds[2]{};
	for (int32 Trial = 0; Trial < 2; ++Trial)
	{
		const double Lane = Trial * 500.;
		auto* Player = F.SpawnWalker(FVector(0, Lane, 100));
		auto* Hose = F.World->SpawnActor<AHoseCableActor>(HoseClass, FVector(150, Lane, 100), FRotator::ZeroRotator);
		if (!Player || !Hose) return false;
		Hose->Cable->PointMassKg = Trial == 0 ? .05f : 5.f;
		const int32 Connector = Trial == 0 ? 0 : Hose->Cable->NumSegments;
		if (!TestTrue(TEXT("A loose connector enters the normal hand-grip path"),
			Hose->RequestConnectorHandGrip(Connector, Player->GetMesh(), NAME_None))) return false;
		auto* Movement = Player->GetCharacterMovement();
		const float BaseSpeed = Movement->MaxWalkSpeed;
		F.Walk(*Player, Forward);
		TestTrue(TEXT("Picking up the hose lets walking speed ease down"),
			Movement->Velocity.X > BaseSpeed - 90. && Movement->Velocity.X < BaseSpeed);
		for (int32 Frame = 0; Frame < 54; ++Frame) F.Walk(*Player, Forward);
		Speeds[Trial] = Movement->Velocity.X;
		TestTrue(TEXT("The loose hose still lets the holder move freely across the floor"),
			Movement->IsMovingOnGround() && Speeds[Trial] > BaseSpeed * .2 && Player->GetActorLocation().X > 100.);
		TestEqual(TEXT("Holding the hose leaves the character's configured walk speed intact"), Movement->MaxWalkSpeed, BaseSpeed);

		if (!TestTrue(TEXT("Release the loose connector"), Hose->ReleaseHandHeldConnector(Player))) return false;
		for (int32 Frame = 0; Frame < 30; ++Frame) F.Walk(*Player, Forward);
		TestTrue(TEXT("Releasing the hose restores normal walking speed"), FMath::IsNearlyEqual(Movement->Velocity.X, double(BaseSpeed), 1.));

		if (Trial == 1)
		{
			if (!TestTrue(TEXT("Regrab the loose connector before falling"),
				Hose->RequestConnectorHandGrip(Connector, Player->GetMesh(), NAME_None))) return false;
			Player->SetActorLocation(FVector(0, Lane, 900));
			Movement->SetMovementMode(MOVE_Falling);
			Movement->Velocity = FVector(BaseSpeed, 0, -100);
			constexpr float Step = 1.f / 60.f;
			F.Walk(*Player, FVector::ZeroVector, Step);
			TestTrue(TEXT("Carrying the loose hose preserves falling speed"),
				FMath::IsNearlyEqual(Movement->Velocity.Z, double(-100.f + Movement->GetGravityZ() * Step), .1));
		}
	}
	TestTrue(TEXT("A heavier loose hose slows walking more than a light hose"), Speeds[0] > Speeds[1] * 1.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectedHoseBrakingTest, "ColossalFizzCo.Hose.Interaction.ConnectedWalkingBraking", CableHoseTests::Flags)
bool FConnectedHoseBrakingTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F;
	if (!F.World) return false;
	F.AddWalkingFloor();
	const FVector Forward(1, 0, 0);
	for (int32 Trial = 0; Trial < 2; ++Trial)
	{
		const FVector Start(0, Trial * 500., 100);
		auto* Player = F.SpawnWalker(Start);
		auto* Hose = F.World->SpawnActor<AHoseCableActor>();
		if (!Player || !Hose) return false;
		Hose->bCarryConnectorOnPickup = true;
		Hose->Cable->CableLength = 450.f;
		Hose->Cable->LengthSlackTolerance = 0.f;
		Hose->Cable->bEnablePointWeight = false;
		Hose->Cable->bEnableCollision = false;
		if (!TestTrue(TEXT("Grab the connector from a normal hand grip"),
			Hose->RequestConnectorHandGrip(Hose->Cable->NumSegments, Player->GetMesh(), NAME_None))) return false;
		const FVector MountLocation = Player->GetMesh()->GetComponentLocation() - Forward * 275.;
		auto* Mount = Trial == 0 ? F.Actor(MountLocation) : F.PullBody(MountLocation, 200.f, 100.f);
		Mount->SetActorEnableCollision(false);
		auto* Supply = F.Endpoint(EResourceTransportCategory::Content, 1, 100, 30, Mount);
		if (!TestTrue(TEXT("Attach the other end to the test load"), Hose->ConnectStart(Supply))) return false;

		auto* Movement = Player->GetCharacterMovement();
		const double NormalSpeed = Movement->GetMaxSpeed();
		double BiggestSpeedDrop = 0.;
		bool bBrakedWithSlack = false;
		double GreatestSpan = 0.;
		for (int32 Frame = 0; Frame < 135; ++Frame)
		{
			const double PreviousSpeed = Movement->Velocity.X;
			F.Walk(*Player, Forward);
			const double Span = FVector::Distance(Player->GetMesh()->GetComponentLocation(), Supply->GetComponentLocation());
			GreatestSpan = FMath::Max(GreatestSpan, Span);
			BiggestSpeedDrop = FMath::Max(BiggestSpeedDrop, PreviousSpeed - Movement->Velocity.X);
			bBrakedWithSlack |= Span < Hose->Cable->CableLength - 15. && Movement->Velocity.X < NormalSpeed - 30.;
		}
		TestTrue(TEXT("Walking speed eases down before the connected hose is taut"), bBrakedWithSlack);
		TestTrue(TEXT("Tension avoids a one-frame speed drop"), BiggestSpeedDrop < 90.);
		TestTrue(TEXT("The eased movement stays within the cable length"), GreatestSpan <= Hose->Cable->CableLength + .25);
		TestTrue(TEXT("The player keeps the physical hand grip"), Hose->FindHandHeldConnector(Player) == Hose->Cable->NumSegments);
		if (Trial == 0)
		{
			TestTrue(TEXT("The fixed mount holds the player at full reach"),
				Movement->Velocity.X < 1. && GreatestSpan > Hose->Cable->CableLength - 2.);
			TestTrue(TEXT("The fixed supply stays in place"), Mount->GetActorLocation().Equals(MountLocation, .01));
		}
		else
		{
			TestTrue(TEXT("The heavy movable body keeps progressing under an affordable pull"),
				Mount->GetActorLocation().X > MountLocation.X + 5. && Movement->Velocity.X > 2. && Movement->Velocity.X < 40.);
		}
		const double TurnPoint = Player->GetActorLocation().X;
		for (int32 Frame = 0; Frame < 12; ++Frame) F.Walk(*Player, -Forward);
		TestTrue(TEXT("Moving back toward the mount immediately frees the player to accelerate"),
			Player->GetActorLocation().X < TurnPoint - 12. && Movement->Velocity.X < -150.);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldHoseFallTest, "ColossalFizzCo.Hose.Interaction.FallingWithMountedHose", CableHoseTests::Flags)
bool FHeldHoseFallTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F;
	if (!F.World) return false;
	const FVector Start(0, 0, 600);
	auto* Player = F.World->SpawnActor<ACharacter>(Start, FRotator::ZeroRotator);
	auto* Reference = F.World->SpawnActor<ACharacter>(Start + FVector(0, 1000, 0), FRotator::ZeroRotator);
	auto* Controller = F.World->SpawnActor<APlayerController>();
	auto* Hose = F.World->SpawnActor<AHoseCableActor>();
	if (!Player || !Reference || !Controller || !Hose) return false;
	Controller->Possess(Player);
	auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	LocalPlayer->PlayerController = Controller;
	Controller->Player = LocalPlayer;
	Controller->SetViewTarget(Player);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Controller->UpdateCameraManager(0);
	auto* Actions = NewObject<UShopPlayerInteractionComponent>(Player);
	Player->AddInstanceComponent(Actions);
	Actions->RegisterComponent();

	FVector Eye;
	FRotator Aim;
	Controller->GetPlayerViewPoint(Eye, Aim);
	const FVector LooseEnd = Eye + Aim.Vector() * 100;
	auto* Supply = F.Endpoint(EResourceTransportCategory::Content, 1, 100, 30, F.Actor(LooseEnd + FVector(0, 150, 0)));
	Hose->bCarryConnectorOnPickup = true;
	Hose->Cable->CableLength = 450;
	Hose->Cable->bEnablePointWeight = true;
	Hose->Cable->PointMassKg = 500;
	if (!TestTrue(TEXT("Mount the weighted hose to a fixed supply"), Hose->ConnectStart(Supply))) return false;
	TArray<FVector> Points;
	for (int32 Index = 0; Index <= Hose->Cable->NumSegments; ++Index)
		Points.Add(FMath::Lerp(Supply->GetComponentLocation(), LooseEnd, double(Index) / Hose->Cable->NumSegments));
	Hose->Cable->SetCableParticleLocations(Points);
	Hose->Tick(0);
	if (!TestTrue(TEXT("Grab the other connector through the player action"), Actions->TryToggleCableGrabFromView()
		&& Hose->FindHandHeldConnector(Player) == Hose->Cable->NumSegments)) return false;

	auto* Movement = Player->GetCharacterMovement();
	auto* ReferenceMovement = Reference->GetCharacterMovement();
	ReferenceMovement->bRunPhysicsWithNoController = true;
	for (auto* Component : {Movement, ReferenceMovement})
	{
		Component->SetMovementMode(MOVE_Falling);
		Component->Velocity = FVector(180, 0, 0);
	}
	constexpr float Step = 1.f / 60.f;
	auto Advance = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Movement->TickComponent(Step, LEVELTICK_All, &Movement->PrimaryComponentTick);
			ReferenceMovement->TickComponent(Step, LEVELTICK_All, &ReferenceMovement->PrimaryComponentTick);
		}
	};
	Advance(16);
	TestTrue(TEXT("The player keeps the weighted hose while it still has slack"), Actions->IsCableGrabActive());
	TestTrue(TEXT("Weight still limits horizontal movement while carrying the hose"),
		Player->GetActorLocation().X < Reference->GetActorLocation().X - 1);
	TestTrue(TEXT("Slack hose preserves falling speed and distance"), Movement->Velocity.Z < -200
		&& FMath::IsNearlyEqual(Movement->Velocity.Z, ReferenceMovement->Velocity.Z, .01)
		&& FMath::IsNearlyEqual(Player->GetActorLocation().Z, Reference->GetActorLocation().Z, .01));
	const FVector BeforeSwing = Player->GetActorLocation();
	const FVector Anchor = Supply->GetComponentLocation();
	Advance(60);
	const auto* Hand = Hose->Cable->GetAttachedComponent();
	const double Reach = Hose->Cable->CableLength + Hose->Cable->LengthSlackTolerance;
	if (!TestTrue(TEXT("The player swings at the hose's full reach with the connector in hand"),
		Actions->IsCableGrabActive() && Hose->FindHandHeldConnector(Player) == Hose->Cable->NumSegments
		&& Hand && Hand->GetOwner() == Player
		&& FMath::IsNearlyEqual(FVector::Distance(Hand->GetComponentLocation(), Anchor), Reach, .1))) return false;
	const FVector Radial = (Hand->GetComponentLocation() - Anchor).GetSafeNormal();
	TestTrue(TEXT("The fall builds swing speed along the arc with no outward velocity"),
		FMath::Abs(FVector::DotProduct(Movement->Velocity, Radial)) < .1
		&& Movement->Velocity.Size2D() > 60 && FVector::Dist2D(Player->GetActorLocation(), BeforeSwing) > 30);
	Advance(20);
	TestTrue(TEXT("The hose stays mounted and supports the player throughout the swing"), Actions->IsCableGrabActive()
		&& Movement->IsFalling() && Supply->GetComponentLocation().Equals(Anchor, .01)
		&& FVector::Distance(Hand->GetComponentLocation(), Anchor) <= Reach + .1
		&& Player->GetActorLocation().Z > Reference->GetActorLocation().Z + 100);

	const FVector ReleaseVelocity = Movement->Velocity;
	Actions->RequestCableRelease();
	Advance(1);
	TestTrue(TEXT("Manual release retains swing momentum and resumes free flight"), !Actions->IsCableGrabActive()
		&& Hose->FindHandHeldConnector(Player) == INDEX_NONE
		&& ReleaseVelocity.Size2D() > 60
		&& Movement->Velocity.Equals(ReleaseVelocity + FVector(0, 0, Movement->GetGravityZ() * Step), .1));
	TestTrue(TEXT("The supply connector remains mounted after manual release"), Hose->StartPort == Supply
		&& Hose->Cable->GetStartAttachmentComponent() == Supply);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseLocalConservationTest,"ColossalFizzCo.Hose.LocalConservation",CableHoseTests::Flags)
bool FHoseLocalConservationTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	for (const auto Category:{EResourceTransportCategory::Content,EResourceTransportCategory::Fuel,EResourceTransportCategory::Electricity})
	{
		auto* Source=F.Endpoint(Category,7,10,3); auto* Target=F.Endpoint(Category,7,2,0); auto* Hose=F.Hose(Category,7);
		if (!TestTrue(TEXT("Typed local stores connect"),Hose->ConnectEndpoints(Source,Target))) return false;
		TestEqual(TEXT("Partial acceptance reports actual remaining target capacity"),Hose->TransferForDuration(1),2.f);
		TestEqual(TEXT("Source debits units actually received by the hose"),Source->GetStoredUnits(),0.f);
		TestEqual(TEXT("Target receives exactly accepted units"),Target->GetStoredUnits(),2.f);
		TestEqual(TEXT("Unaccepted contents remain in finite hose inventory"),Hose->Buffer.Units,1.f);
		TestEqual(TEXT("Source, target and hose conserve the original amount"),Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,3.f);
		TestEqual(TEXT("Full target rejects further movement"),Hose->TransferForDuration(1),0.f);
		Hose->DisconnectEndpoints();
		TestEqual(TEXT("Disconnected hose cannot move resources"),Hose->TransferForDuration(1),0.f);
		TestEqual(TEXT("Disconnect retains trapped contents"),Hose->Buffer.Units,1.f);
		TestEqual(TEXT("Explicit purge releases exactly trapped liquid or energy"),Hose->PurgeContents(),1.f);
		TestEqual(TEXT("Repeating purge cannot duplicate a release"),Hose->PurgeContents(),0.f);
	}
	// Different stores on one equipment actor are distinct endpoints, not a self-transfer.
	auto* Equipment=F.Actor(); auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,8,10,5,Equipment);
	auto* Left=F.Endpoint(EResourceTransportCategory::Fuel,8,10,0,Equipment);
	auto* Right=F.Endpoint(EResourceTransportCategory::Fuel,8,10,0);
	auto* First=F.Hose(EResourceTransportCategory::Fuel,8); auto* Second=F.Hose(EResourceTransportCategory::Fuel,8);
	TestTrue(TEXT("Distinct stores on the same equipment actor may connect"),First->ConnectEndpoints(Source,Left));
	TestTrue(TEXT("A second hose may share the source store"),Second->ConnectEndpoints(Source,Right));
	TestEqual(TEXT("First branch consumes its requested rate"),First->TransferForDuration(.3f),3.f);
	TestEqual(TEXT("Second branch sees actual remaining source units"),Second->TransferForDuration(.3f),2.f);
	TestEqual(TEXT("Branching conserves total inventory without cached double debit"),Source->GetStoredUnits()+Left->GetStoredUnits()+Right->GetStoredUnits(),5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseAuthorityAndDisconnectTest,"ColossalFizzCo.Hose.AuthorityAndDisconnect",CableHoseTests::Flags)
bool FHoseAuthorityAndDisconnectTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Electricity,3,10,7);
	auto* Target=F.Endpoint(EResourceTransportCategory::Electricity,3,10,0);
	auto* Hose=F.Hose(EResourceTransportCategory::Electricity,3);
	Hose->GetOwner()->SwapRoles();
	TestFalse(TEXT("Client cannot establish authoritative resource connections"),Hose->ConnectEndpoints(Source,Target));
	Hose->GetOwner()->SwapRoles(); if (!Hose->ConnectEndpoints(Source,Target)) return false;
	Hose->GetOwner()->SwapRoles();
	TestEqual(TEXT("Client cannot move stored resources"),Hose->TransferForDuration(1),0.f);
	Hose->GetOwner()->SwapRoles();
	Hose->bTransferEnabled=false;
	TestEqual(TEXT("Closed transfer valve prevents flow"),Hose->TransferForDuration(1),0.f);
	Hose->bTransferEnabled=true; Hose->MaxUnitsPerSecond=-1;
	TestEqual(TEXT("Negative transfer rate cannot reverse or mint resources"),Hose->TransferForDuration(1),0.f);
	Hose->MaxUnitsPerSecond=10;
	TestEqual(TEXT("Negative elapsed time cannot move resources"),Hose->TransferForDuration(-1),0.f);
	Target->DestroyComponent();
	TestEqual(TEXT("Destroyed endpoint stops transfer before debit"),Hose->TransferForDuration(1),0.f);
	TestEqual(TEXT("All rejected transfers preserve source inventory"),Source->GetStoredUnits(),7.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseMotorAndBurstTest,"ColossalFizzCo.Hose.MotorStorageAndBurst",CableHoseTests::Flags)
bool FHoseMotorAndBurstTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,7,30,20);
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,7,10,10);
	auto* Motor=NewObject<UResourcePumpComponent>(Source->GetOwner()); Source->GetOwner()->AddInstanceComponent(Motor);
	Motor->PushPressure=1; Motor->RegisterComponent(); Source->ConnectedMotor=Motor;
	auto* Hose=F.Hose(EResourceTransportCategory::Fuel,7); Hose->BufferCapacityUnits=2; Hose->MaxUnitsPerSecond=2;
	Hose->bRequiresMotorDrive=true; Hose->BlockedPressureRisePerSecond=1;
	if (!TestTrue(TEXT("Full receiver remains a valid physical connection"),Hose->ConnectEndpoints(Source,Target))) return false;
	Hose->TransferForDuration(1);
	TestEqual(TEXT("Stopped required motor prevents intake"),Source->GetStoredUnits(),20.f);
	TestEqual(TEXT("Stopped required motor leaves hose empty"),Hose->Buffer.Units,0.f);
	Motor->bRunning=true; Hose->TransferForDuration(1);
	TestEqual(TEXT("Blocked receiver fills only the finite hose capacity"),Hose->Buffer.Units,2.f);
	TestEqual(TEXT("Only stored hose contents are debited from source"),Source->GetStoredUnits(),18.f);
	const float Pressure=Hose->CurrentPressure; Hose->TransferForDuration(1);
	TestTrue(TEXT("Running motor builds pressure against a full blocked hose"),Hose->CurrentPressure>Pressure);
	TestEqual(TEXT("Full hose cannot keep draining source inventory"),Source->GetStoredUnits(),18.f);
	Hose->bIntakeEnabled=false; Target->SetStoredUnits(0); Hose->TransferForDuration(1);
	TestEqual(TEXT("Drain-only mode empties trapped contents into receiver"),Hose->Buffer.Units,0.f);
	TestEqual(TEXT("Drain-only mode receives the actual trapped amount"),Target->GetStoredUnits(),2.f);
	TestEqual(TEXT("Drain-only mode does not draw fresh supply"),Source->GetStoredUnits(),18.f);
	Hose->bIntakeEnabled=true; Target->SetStoredUnits(10); Hose->TransferForDuration(1);
	const float SourceBeforeBurst=Source->GetStoredUnits(); const float Trapped=Hose->Buffer.Units;
	Hose->DisconnectEndpoints(); Hose->ResourceID=8; Source->ResourceID=Target->ResourceID=8;
	TestFalse(TEXT("Reconnection cannot relabel trapped fuel as a different resource"),Hose->ConnectEndpoints(Source,Target));
	Hose->ResourceID=7; Source->ResourceID=Target->ResourceID=7;
	if (!Hose->ConnectEndpoints(Source,Target)) return false;
	Motor->PushPressure=10; Hose->BurstPressureThreshold=4; Hose->BurstDelay=.1f;
	Hose->TransferForDuration(.2f);
	TestTrue(TEXT("Sustained motor overdrive bursts the hose"),Hose->bBurst);
	TestEqual(TEXT("Burst empties all trapped contents"),Hose->Buffer.Units,0.f);
	TestEqual(TEXT("Burst reports exactly the contents that were trapped"),Hose->TotalReleasedUnits,Trapped);
	TestEqual(TEXT("Burst of a full hose does not debit additional supply"),Source->GetStoredUnits(),SourceBeforeBurst);
	TestEqual(TEXT("Burst cannot be purged again"),Hose->PurgeContents(),0.f);
	Hose->TransferForDuration(1);
	TestEqual(TEXT("Burst hose cannot continue pumping"),Source->GetStoredUnits(),SourceBeforeBurst);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseManagedEscrowTest,"ColossalFizzCo.Hose.ManagedContentEscrow",CableHoseTests::Flags)
bool FHoseManagedEscrowTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	APropManager* Manager=nullptr; int32 Source=0,Target=0; if (!F.ManagedStocks(Manager,Source,Target)) return false;
	FHoseResourceBuffer Buffer; Buffer.Category=EResourceTransportCategory::Content; Buffer.ResourceID=9301;
	TestTrue(TEXT("Intake obeys finite buffer room"),FMath::IsNearlyEqual(Manager->IntakeHoseContent(Source,Buffer,2,3),2.f));
	TestEqual(TEXT("Buffered liquid preserves its creation time"),Buffer.ContentCreationTime,10.0);
	TestEqual(TEXT("Buffered liquid preserves its source temperature"),Buffer.ContentTemperature,35.f);
	TestEqual(TEXT("Partial delivery reports actual units"),Manager->DeliverHoseContent(Buffer,Target,1),1.f);
	TestEqual(TEXT("First delivery preserves original liquid age"),Manager->FindUniqueData(Target)->EventData.ContentCreationTime,10.0);
	Manager->SetTemperature(Source,15);
	FPropEventSyncDelta NewAge; NewAge.ExtendedDirtyMask=EPropExtendedEventMask::ContentCreationTime; NewAge.ContentCreationTime=20;
	Manager->SubmitInstanceEventUpdate(Source,NewAge);
	TestEqual(TEXT("Top-up accepts only remaining buffer space"),Manager->IntakeHoseContent(Source,Buffer,2,2),1.f);
	TestTrue(TEXT("Buffer mixing conserves temperature by actual units"),FMath::IsNearlyEqual(Buffer.ContentTemperature,25.f));
	TestEqual(TEXT("Buffer top-up cannot freshen older contents"),Buffer.ContentCreationTime,10.0);
	TestEqual(TEXT("Mixed contents deliver without loss"),Manager->DeliverHoseContent(Buffer,Target,2),2.f);
	TestTrue(TEXT("Receiving container conserves mixed heat"),FMath::IsNearlyEqual(Manager->FindUniqueData(Target)->ContinuousData.Temperature,85.f/3.f,.001f));
	TestTrue(TEXT("Last source portion enters the hose"),FMath::IsNearlyEqual(Manager->IntakeHoseContent(Source,Buffer,2,2),1.f,.001f));
	const FGuid Episode=FGuid::NewGuid();
	TestTrue(TEXT("Managed purge releases exact remaining buffer amount"),FMath::IsNearlyEqual(Manager->ReleaseHoseContent(Buffer,Episode,FVector(0,0,100)),1.f,.001f));
	TestEqual(TEXT("Managed purge is idempotent"),Manager->ReleaseHoseContent(Buffer,Episode,FVector(0,0,100)),0.f);
	double Released=0; for (const auto& Item:Manager->ReleasedLiquids) Released+=Item.Portions;
	const double Total=Manager->FindUniqueData(Source)->EventData.ContentLevel*100+Manager->FindUniqueData(Target)->EventData.ContentLevel*4+Buffer.Units+Released;
	TestTrue(TEXT("Canonical source, target, hose and release ledger conserve original contents"),FMath::IsNearlyEqual(Total,4.0,.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseDrivenRateTest,"ColossalFizzCo.Hose.SocketDriveRateLimit",CableHoseTests::Flags)
bool FHoseDrivenRateTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Electricity,9,20,20);
	auto* Target=F.Endpoint(EResourceTransportCategory::Electricity,9,20,0);
	Source->PushPressure=1; Target->PullPressure=1;
	auto* Motor=NewObject<UResourcePumpComponent>(Source->GetOwner()); Source->GetOwner()->AddInstanceComponent(Motor);
	Motor->PushPressure=1; Motor->bRunning=true; Motor->RegisterComponent(); Source->ConnectedMotor=Motor;
	auto* Hose=F.Hose(EResourceTransportCategory::Electricity,9); Hose->MaxUnitsPerSecond=2; Hose->MaxDrivenUnitsPerSecond=5; Hose->bRequiresMotorDrive=true;
	if (!Hose->ConnectEndpoints(Source,Target)) return false;
	const float Delivered=Hose->TransferForDuration(.2f);
	TestEqual(TEXT("Socket push, receiver pull and connected motor combine within driven rate limit"),Hose->CurrentFlowRate,5.f);
	TestTrue(TEXT("Driven transfer exceeds normal rate while respecting maximum throughput"),Delivered>.4f && Delivered<=1.0001f);
	TestTrue(TEXT("Electrical drive conserves source, target and buffered energy"),FMath::IsNearlyEqual(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,20.f,.001f));
	Motor->bRunning=false; const float Before=Source->GetStoredUnits(); Hose->TransferForDuration(1);
	TestEqual(TEXT("Required motor stop prevents intake despite passive socket pressure"),Source->GetStoredUnits(),Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseManagedBurstTest,"ColossalFizzCo.Hose.ManagedBurstConservation",CableHoseTests::Flags)
bool FHoseManagedBurstTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	APropManager* Manager=nullptr; int32 SourceID=0,TargetID=0; if (!F.ManagedStocks(Manager,SourceID,TargetID)) return false;
	FPropEventSyncDelta Full; Full.SetFlag(EPropEventDirtyMask::ContentLevel); Full.SetFlag(EPropEventDirtyMask::ContentID);
	Full.ContentID=9301; Full.ContentLevel=1; Manager->SubmitInstanceEventUpdate(TargetID,Full);
	auto* Source=F.Endpoint(EResourceTransportCategory::Content,9301,10,0);
	auto* Target=F.Endpoint(EResourceTransportCategory::Content,9301,10,0);
	if (!Source->ConfigureManagedContent(Manager,SourceID,9301) || !Target->ConfigureManagedContent(Manager,TargetID,9301)) return false;
	auto* Hose=F.Hose(EResourceTransportCategory::Content,9301); Hose->BufferCapacityUnits=2;
	if (!TestTrue(TEXT("Managed full container connects while retaining finite hose storage"),Hose->ConnectEndpoints(Source,Target))) return false;
	Hose->TransferForDuration(1);
	TestTrue(TEXT("Canonical source debits the units trapped in hose"),FMath::IsNearlyEqual(Source->GetStoredUnits(),2.f,.001f));
	TestEqual(TEXT("Blocked managed delivery leaves contents in the hose"),Hose->Buffer.Units,2.f);
	Source->PushPressure=10; Hose->BurstPressureThreshold=4; Hose->BurstDelay=.1f; Hose->TransferForDuration(.2f);
	TestTrue(TEXT("Overpressure bursts managed content hose"),Hose->bBurst);
	double Released=0; for (const auto& Item:Manager->ReleasedLiquids) Released+=Item.Portions;
	TestTrue(TEXT("Burst enters actual trapped liquid in release ledger"),FMath::IsNearlyEqual(Released,2.0,.001));
	TestTrue(TEXT("Managed burst conserves source, receiver, buffer and spill"),FMath::IsNearlyEqual(double(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units)+Released,8.0,.001));
	Hose->PurgeContents(); Hose->TransferForDuration(1);
	double Again=0; for (const auto& Item:Manager->ReleasedLiquids) Again+=Item.Portions;
	TestEqual(TEXT("Repeated burst tick cannot duplicate ledger release"),Again,Released);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableConnectedPullTest,"ColossalFizzCo.Cable.ConnectedPull.TautAndSlack",CableHoseTests::Flags)
bool FCableConnectedPullTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* A=F.PullBody(FVector(0,0,100)); auto* B=F.PullBody(FVector(100,0,100),10,20); auto* C=F.PullBody(FVector(200,0,100),20,30);
	auto* AB=F.Link(A,B,100); auto* BC=F.Link(B,C,100);
	AB->SetAttachEndToComponent(BC,TEXT("CableStart")); AB->ResetCable();
	auto* Adapter=F.Adapter(A,AB);
	const FVector Old=A->GetActorLocation(),Requested=Old-FVector(30,0,0);
	const auto Preview=Adapter->QueryConnectedPull(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Read-only force query leaves remote bodies in place"),B->GetActorLocation().Equals(FVector(100,0,100)) && C->GetActorLocation().Equals(FVector(200,0,100)));
	TestTrue(TEXT("Taut chain reports remote load"),Preview.TotalMassKg>=30 && Preview.RequiredForceNewtons>0);
	const auto Result=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Enough force permits requested driver movement"),Result.AllowedLocation.Equals(Requested,.1));
	TestTrue(TEXT("Tension moves every body along the attached chain"),B->GetActorLocation().Equals(FVector(70,0,100),.1) && C->GetActorLocation().Equals(FVector(170,0,100),.1));
	TestEqual(TEXT("Moved body count excludes the driver"),Result.MovedActorCount,2);
	TestTrue(TEXT("Chain moves farthest body before its upstream neighbor"),Result.MovedActors.Num()==2 && Result.MovedActors[0]==C && Result.MovedActors[1]==B);
	TestTrue(TEXT("Driver location is returned to the movement caller without teleporting it"),A->GetActorLocation().Equals(Old));
	auto* SlackA=F.PullBody(FVector(0,500,100)); auto* SlackB=F.PullBody(FVector(100,500,100)); auto* SlackC=F.PullBody(FVector(200,500,100));
	auto* SlackLink=F.Link(SlackA,SlackB,150); F.Link(SlackB,SlackC,100);
	SlackLink->bEnablePointWeight=true; SlackLink->PointMassKg=2;
	auto* SlackAdapter=F.Adapter(SlackA,SlackLink);
	const auto SlackQuery=SlackAdapter->QueryConnectedPull(SlackA->GetActorLocation(),SlackA->GetActorLocation()-FVector(30,0,0),.1f,1.e8f);
	TestEqual(TEXT("Slack carries its own cable point mass without downstream body mass"),SlackQuery.TotalMassKg,10.f);
	const auto Slack=SlackAdapter->TryPullConnectedChain(SlackA->GetActorLocation(),SlackA->GetActorLocation()-FVector(30,0,0),.1f,1.e8f);
	TestEqual(TEXT("Available slack does not move remote bodies"),Slack.MovedActorCount,0);
	TestTrue(TEXT("Slack leaves both downstream body positions unchanged"),SlackB->GetActorLocation().Equals(FVector(100,500,100)) && SlackC->GetActorLocation().Equals(FVector(200,500,100)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHosePhysicalBreakTest,"ColossalFizzCo.Hose.PhysicalDisconnectAndBurst",CableHoseTests::Flags)
bool FHosePhysicalBreakTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,4,10,5,F.Actor(FVector(0,0,100)));
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,4,5,5,F.Actor(FVector(200,0,100)));
	auto* Hose=F.World->SpawnActor<AHoseCableActor>();
	Hose->Cable->bEnableCollision=false; Hose->Transfer->Category=EResourceTransportCategory::Fuel; Hose->Transfer->ResourceID=4;
	Hose->Transfer->BufferCapacityUnits=2; Hose->Transfer->BurstPressureThreshold=100;
	if (!TestTrue(TEXT("Reusable hose connects both typed physical endpoints"),Hose->ConnectStart(Source) && Hose->ConnectEnd(Target))) return false;
    TestTrue(TEXT("Both anchors follow their connected sockets and the render component stays on the hose"),
        Hose->Cable->GetStartAttachmentComponent()==Source && Hose->Cable->GetAttachedComponent()==Target && Hose->Cable->GetAttachParent()==Hose->SceneRoot);
	Hose->Transfer->TransferForDuration(1);
	TestEqual(TEXT("Blocked receiver leaves two units trapped in the physical hose"),Hose->Transfer->Buffer.Units,2.f);
	Hose->DisconnectEnd();
	TestFalse(TEXT("Unplugging an end disables its physical anchor"),Hose->Cable->bAttachEnd);
	TestFalse(TEXT("Unplugging an end invalidates resource transfer"),Hose->Transfer->HasValidConnection());
	TestEqual(TEXT("Unplugging retains trapped contents"),Hose->Transfer->Buffer.Units,2.f);
	if (!Hose->ConnectEnd(Target)) return false;
	Source->PushPressure=10; Hose->Transfer->BurstPressureThreshold=4; Hose->Transfer->BurstDelay=.1f;
	Hose->Transfer->TransferForDuration(.2f);
	TestTrue(TEXT("Burst disconnects both gameplay socket references"),!Hose->StartPort && !Hose->EndPort);
	TestFalse(TEXT("Burst releases the cable start anchor"),Hose->Cable->bAttachStart);
	TestFalse(TEXT("Burst releases the cable end anchor"),Hose->Cable->bAttachEnd);
	TestEqual(TEXT("Physical burst empties exactly its trapped contents"),Hose->Transfer->TotalReleasedUnits,2.f);
	TestEqual(TEXT("Physical burst leaves no inventory inside hose"),Hose->Transfer->Buffer.Units,0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseSocketHousingTest,"ColossalFizzCo.Hose.SocketHousingKeepsConnectorSeated",CableHoseTests::Flags)
bool FHoseSocketHousingTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F;
	if (!F.World) return false;
	for (const bool bStart : {true, false})
	{
		const FVector Mount(0,bStart ? 0 : 500,150);
		auto* Housing = F.Actor(Mount);
		auto* Body = NewObject<UBoxComponent>(Housing);
		Housing->AddInstanceComponent(Body);
		Body->SetupAttachment(Housing->GetRootComponent());
		Body->SetBoxExtent(FVector(10,40,40));
		Body->SetCollisionProfileName(TEXT("BlockAll"));
		Body->RegisterComponent();
		auto* Socket = F.Endpoint(EResourceTransportCategory::Content,1,10,5,Housing);
		auto* Hose = F.World->SpawnActor<AHoseCableActor>();
		auto* Cable = Hose->Cable.Get();
		Cable->bEnableCollision = true;
		Cable->bUseFullCollisionQueries = !bStart;
		Cable->CableGravityScale = 0;
		Cable->CableLength = 400;
		Cable->CollisionPointsPerFrame = Cable->NumSegments+1;
		TArray<FVector> Points;
		for (int32 Index=0; Index<=Cable->NumSegments; ++Index)
		{
			const int32 FromMount = bStart ? Index : Cable->NumSegments-Index;
			Points.Add(Mount+FVector(16+FromMount*400.0/Cable->NumSegments,0,0));
		}
		Cable->SetCableParticleLocations(Points);
		const int32 Index = bStart ? 0 : Cable->NumSegments;
		if (!TestTrue(TEXT("The plug can seat inside the authored housing"),Hose->ConnectSupplyAtConnector(Socket,Index))) return false;
		for (int32 Frame=0; Frame<12; ++Frame) Cable->TickComponent(1.f/60,LEVELTICK_All,nullptr);
		Cable->GetCableRenderLocations(Points);
		TestTrue(TEXT("The housing cannot push the rendered cable away from the seated connector"),
			(bStart ? Points[0] : Points.Last()).Equals(Mount,.01) && !Cable->bAttachmentBlocked);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseSubstepFlowTest,"ColossalFizzCo.Hose.FrameIndependentFlowAndBurstDeadline",CableHoseTests::Flags)
bool FHoseSubstepFlowTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto Run=[&](int32 Frames,float Dt)
	{
		auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,12,100,100);
		auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,12,100,0);
		auto* Hose=F.Hose(EResourceTransportCategory::Fuel,12); Hose->BufferCapacityUnits=1; Hose->MaxUnitsPerSecond=50;
		if (!Hose->ConnectEndpoints(Source,Target)) return -1.f;
		float Total=0; for (int32 I=0;I<Frames;++I) Total+=Hose->TransferForDuration(Dt);
		TestTrue(TEXT("Continuous flow conserves source, target and finite buffer"),FMath::IsNearlyEqual(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,100.f,.002f));
		TestTrue(TEXT("Supported frame duration does not exhaust transfer substep budget"),Hose->DroppedTransferTime<=UE_SMALL_NUMBER);
		return Total;
	};
	const float LargeFrame=Run(1,1),SmallFrames=Run(20,.05f);
	TestTrue(TEXT("Small hose buffer does not impose a per-frame throughput cap"),FMath::IsNearlyEqual(LargeFrame,50.f,.002f));
	TestTrue(TEXT("One large transfer step matches twenty smaller frames"),FMath::IsNearlyEqual(LargeFrame,SmallFrames,.002f));
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,13,100,100);
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,13,100,0);
	Source->bMotorRunning=true; Source->MotorPressureBoost=10;
	auto* Hose=F.Hose(EResourceTransportCategory::Fuel,13); Hose->BufferCapacityUnits=20; Hose->MaxUnitsPerSecond=50;
	Hose->bRequiresMotorDrive=true; Hose->BurstPressureThreshold=4; Hose->BurstDelay=.1f;
	if (!Hose->ConnectEndpoints(Source,Target)) return false;
	const float Delivered=Hose->TransferForDuration(1);
	TestTrue(TEXT("Overdrive ruptures at the configured deadline within a long frame"),Hose->bBurst);
	TestTrue(TEXT("Only pre-rupture elapsed time may pump resources"),FMath::IsNearlyEqual(Delivered,5.f,.002f) && FMath::IsNearlyEqual(Hose->LastIntakeUnits,5.f,.002f));
	TestTrue(TEXT("No source debit occurs after the burst instant"),FMath::IsNearlyEqual(Source->GetStoredUnits(),95.f,.002f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseManagedPortResolverTest,"ColossalFizzCo.Hose.ManagedPortResolver",CableHoseTests::Flags)
bool FHoseManagedPortResolverTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	APropManager* Manager=nullptr; int32 SourceID=0,TargetID=0; if (!F.ManagedStocks(Manager,SourceID,TargetID)) return false;
	AActivePropActor* SourceActor=nullptr; AActivePropActor* TargetActor=nullptr;
	if (!Manager->PromoteInstanceToASM(SourceID,EPropActivationReason::Interaction,false,SourceActor)
		|| !Manager->PromoteInstanceToASM(TargetID,EPropActivationReason::Interaction,false,TargetActor)) return false;
	auto* Source=UResourcePortLibrary::ResolveResourcePort(SourceActor,EResourceTransportCategory::Content,9301,true);
	auto* Target=UResourcePortLibrary::ResolveResourcePort(TargetActor,EResourceTransportCategory::Content,9301,false);
	if (!TestTrue(TEXT("Managed props expose source and receiving content ports without Blueprint components"),Source && Target)) return false;
	TestEqual(TEXT("Source port binds canonical instance identity"),Source->GetManagedContentISID(),SourceID);
	TestEqual(TEXT("Receiving port binds canonical instance identity"),Target->GetManagedContentISID(),TargetID);
	TestTrue(TEXT("Repeated resolution reuses the same managed adapter"),UResourcePortLibrary::ResolveResourcePort(SourceActor,EResourceTransportCategory::Content,9301,true)==Source);
	TestNull(TEXT("Content props do not acquire invented fuel storage"),UResourcePortLibrary::ResolveResourcePort(SourceActor,EResourceTransportCategory::Fuel,9301,true));
	TargetActor->SwapRoles();
	TestNull(TEXT("Client cannot create or rebind a managed resource port"),UResourcePortLibrary::ResolveResourcePort(TargetActor,EResourceTransportCategory::Content,9301,false));
	TargetActor->SwapRoles();
	auto* Hose=F.Hose(EResourceTransportCategory::Content,9301);
	if (!TestTrue(TEXT("Resolved managed ports connect to normal hose transfer"),Hose->ConnectEndpoints(Source,Target))) return false;
	TestTrue(TEXT("Resolved ports transfer actual canonical content"),Hose->TransferForDuration(.1f)>0);
	TestTrue(TEXT("Managed adapter flow conserves canonical stocks and hose buffer"),FMath::IsNearlyEqual(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,4.f,.002f));
	const float BeforePool=Manager->FindUniqueData(SourceID)->EventData.ContentLevel;
	if (!TestTrue(TEXT("Managed source can demote while its adapter disconnects transport"),Manager->DemoteActorToISM(SourceID))) return false;
	TestFalse(TEXT("Pooling invalidates the old logical hose connection"),Hose->HasValidConnection());
	TestEqual(TEXT("Pooled adapter exposes no stale instance inventory"),Source->GetStoredUnits(),0.f);
	TestEqual(TEXT("Disconnected pooled source cannot continue transferring"),Hose->TransferForDuration(1),0.f);
	TestEqual(TEXT("Pooling cleanup preserves canonical source inventory"),Manager->FindUniqueData(SourceID)->EventData.ContentLevel,BeforePool);
	AActivePropActor* ReboundActor=nullptr;
	if (!Manager->PromoteInstanceToASM(SourceID,EPropActivationReason::Interaction,false,ReboundActor)) return false;
	auto* Rebound=UResourcePortLibrary::ResolveResourcePort(ReboundActor,EResourceTransportCategory::Content,9301,true);
	TestTrue(TEXT("Reactivated prop adapter resolves its current canonical identity"),Rebound && Rebound->GetManagedContentISID()==SourceID);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseAuthoredPortResolverTest,"ColossalFizzCo.Hose.AuthoredPortSelection",CableHoseTests::Flags)
bool FHoseAuthoredPortResolverTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Equipment=F.Actor();
	F.Endpoint(EResourceTransportCategory::Content,1,10,5,Equipment);
	auto* FuelSource=F.Endpoint(EResourceTransportCategory::Fuel,2,10,5,Equipment); FuelSource->bCanReceive=false;
	auto* FuelReceiver=F.Endpoint(EResourceTransportCategory::Fuel,2,10,0,Equipment); FuelReceiver->bCanSupply=false;
	TestTrue(TEXT("Resolver searches beyond the first incompatible authored component"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Fuel,2,true)==FuelSource);
	TestTrue(TEXT("Resolver selects a compatible authored port in the requested direction"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Fuel,2,false)==FuelReceiver);
	TestNull(TEXT("Resolver does not invent a missing electrical port"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Electricity,2,false));
	TestNull(TEXT("Resolver cannot reinterpret another resource ID"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Fuel,3,true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseOwnedConnectionTest,"ColossalFizzCo.Hose.OwnedConnectionGuards",CableHoseTests::Flags)
bool FHoseOwnedConnectionTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Player=F.World->SpawnActor<ACharacter>(); Player->SetActorLocation(FVector(0,-50,100));
	Player->SetActorEnableCollision(false); Player->GetCharacterMovement()->DisableMovement();
	auto* Controller=F.World->SpawnActor<APlayerController>(); Controller->Possess(Player);
	auto* Interaction=NewObject<UShopPlayerInteractionComponent>(Player); Player->AddInstanceComponent(Interaction); Interaction->RegisterComponent();
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,7,10,5,F.Actor(FVector(50,0,100)));
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,7,10,0,F.Actor(FVector(200,0,100)));
	auto* Wrong=F.Endpoint(EResourceTransportCategory::Electricity,7,10,0,F.Actor(FVector(200,50,100)));
	auto* Hose=F.World->SpawnActor<AHoseCableActor>(FVector(0,0,100),FRotator::ZeroRotator);
	Hose->Transfer->Category=EResourceTransportCategory::Fuel; Hose->Cable->bEnableCollision=false;
	TArray<FVector> Points; Hose->Cable->GetCableParticleLocations(Points);
	if (Points.Num()<2 || !TestTrue(TEXT("Owned interaction grabs the source end"),Interaction->ExecuteCableGrab(Hose->Cable,Points[0],Points[0],1))) return false;
	if (!TestTrue(TEXT("Owned source connection adopts the actual resource identity"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Source->GetOwner(),2))) return false;
	TestEqual(TEXT("Empty hose adopts source resource ID"),Hose->Transfer->ResourceID,7);
	Hose->Cable->GetCableParticleLocations(Points); Player->SetActorLocation(Points.Last()+FVector(0,-50,0));
	if (!TestTrue(TEXT("Owned interaction grabs the receiving end"),Interaction->ExecuteCableGrab(Hose->Cable,Points.Last(),Points.Last(),3))) return false;
	TestFalse(TEXT("Owned connection cannot bypass the socket category"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Wrong->GetOwner(),4));
	TestTrue(TEXT("Valid receiving socket connects after rejected mismatch"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Target->GetOwner(),5));
	TestFalse(TEXT("Replayed connection sequence is rejected"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Target->GetOwner(),5));
	TestTrue(TEXT("Only accepted owned requests establish physical endpoints"),Hose->StartPort==Source && Hose->EndPort==Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCablePullCycleTest,"ColossalFizzCo.Cable.ConnectedPull.CycleDeduplication",CableHoseTests::Flags)
bool FCablePullCycleTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* A=F.PullBody(FVector(0,0,100)); auto* B=F.PullBody(FVector(100,0,100),10,20); auto* C=F.PullBody(FVector(200,0,100),20,30);
	auto* AB=F.Link(A,B,100); F.Link(B,C,100); auto* Adapter=F.Adapter(A,AB);
	const FVector Old=A->GetActorLocation(),Requested=Old-FVector(20,0,0);
	const auto Baseline=Adapter->QueryConnectedPull(Old,Requested,.1f,1.e8f);
	F.Link(A,C,200);
	const auto Loop=Adapter->QueryConnectedPull(Old,Requested,.1f,1.e8f);
	TestFalse(TEXT("Finite cable cycle terminates without exhausting graph traversal"),Loop.bTraversalLimitReached);
	TestTrue(TEXT("Shared bodies are charged once across branching cycle paths"),FMath::IsNearlyEqual(Loop.TotalMassKg,Baseline.TotalMassKg,.01f));
	const auto Moved=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestEqual(TEXT("Cycle moves each remote body only once"),Moved.MovedActorCount,2);
	TestTrue(TEXT("Cycle does not multiply remote displacement"),B->GetActorLocation().Equals(FVector(80,0,100),.1) && C->GetActorLocation().Equals(FVector(180,0,100),.1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCablePullResetTest,"ColossalFizzCo.Cable.ConnectedPull.ResetAfterTeleport",CableHoseTests::Flags)
bool FCablePullResetTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.PullBody(FVector(0,0,100)); auto* Target=F.PullBody(FVector(100,0,100));
	auto* Cable=F.Link(Source,Target,100); Cable->bEnablePointWeight=true; Cable->PointMassKg=2;
	Source->FindComponentByClass<UCablePullResponseComponent>()->PullForceNewtons=0;
	auto* Adapter=F.Adapter(Source,Cable);
	const FVector RestoredSource(-2000,0,100),RestoredTarget(-1900,0,100);
	Source->SetActorLocation(RestoredSource,false,nullptr,ETeleportType::TeleportPhysics);
	Target->SetActorLocation(RestoredTarget,false,nullptr,ETeleportType::TeleportPhysics);
	Cable->ResetCable();
	Adapter->TickComponent(.1f,LEVELTICK_All,nullptr);
	TestTrue(TEXT("Explicit cable reset preserves a restored source transform on the next movement tick"),Source->GetActorLocation().Equals(RestoredSource,.01));
	TestTrue(TEXT("Reset does not interpret restored endpoint placement as a pull on its neighbor"),Target->GetActorLocation().Equals(RestoredTarget,.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCablePullBlockingTest,"ColossalFizzCo.Cable.ConnectedPull.BlockingAndAuthority",CableHoseTests::Flags)
bool FCablePullBlockingTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* A=F.PullBody(FVector(0,0,100)); auto* B=F.PullBody(FVector(100,0,100),10,100);
	auto* AB=F.Link(A,B,100); auto* Adapter=F.Adapter(A,AB);
	const FVector Old=A->GetActorLocation(),Requested=Old-FVector(30,0,0);
	const auto Weak=Adapter->TryPullConnectedChain(Old,Requested,.1f,0);
	TestTrue(TEXT("Insufficient force blocks taut-chain progress"),Weak.bBlocked && Weak.AllowedLocation.Equals(Old,.1));
	TestTrue(TEXT("Insufficient force leaves downstream body unmoved"),B->GetActorLocation().Equals(FVector(100,0,100)));
	B->FindComponentByClass<UCablePullResponseComponent>()->bFixed=true;
	const auto Fixed=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Pinned equipment remains an anchor regardless of available force"),Fixed.bBlocked && Fixed.AllowedLocation.Equals(Old,.1));
	B->FindComponentByClass<UCablePullResponseComponent>()->bFixed=false;
	AB->SetPointFixed(2,true);
	const auto InteriorFixed=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Pinned interior cable point anchors its upstream span"),InteriorFixed.bBlocked && InteriorFixed.AllowedLocation.Equals(Old,.1));
	AB->SetPointFixed(2,false);
	A->SwapRoles(); const auto Client=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f); A->SwapRoles();
	TestEqual(TEXT("Client never mutates remote chain actors"),Client.MovedActorCount,0);
	TestTrue(TEXT("Client attempt preserves downstream location"),B->GetActorLocation().Equals(FVector(100,0,100)));
	auto* Wall=F.PullBody(FVector(80,0,100),10,0,true);
	Cast<UBoxComponent>(Wall->GetRootComponent())->SetBoxExtent(FVector(2,40,40));
	const auto Blocked=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Swept remote body cannot pass through blocking world geometry"),B->GetActorLocation().X>=86-.2);
	TestTrue(TEXT("Blocked downstream movement also restricts the driver"),Blocked.AllowedLocation.X>Requested.X+.5);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseConnectorTraceTest,"ColossalFizzCo.Hose.Interaction.SavedConnectorTraces",CableHoseTests::Flags)
bool FHoseConnectorTraceTest::RunTest(const FString& Parameters)
{
    CableHoseTests::FFixture F;
    UClass* Class=LoadClass<AHoseCableActor>(nullptr,TEXT("/Game/Assest/Blueprints/Cola_Container/BP_ColaHose.BP_ColaHose_C"));
    if(!TestNotNull(TEXT("Requested saved hose Blueprint"),Class))return false;
    auto* Hose=F.World->SpawnActor<AHoseCableActor>(Class,FVector(0,0,200),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("Actual saved hose instance"),Hose))return false;
    for(auto* Mesh:{Hose->StartEndpointMesh.Get(),Hose->EndEndpointMesh.Get()})
    {
        TestEqual(TEXT("Saved connector is query-only at runtime"),Mesh->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
        TestEqual(TEXT("Player Visibility traces can see the mesh"),Mesh->GetCollisionResponseToChannel(ECC_Visibility),ECR_Block);
        TestEqual(TEXT("Connectors still ignore Pawn movement"),Mesh->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
        TestEqual(TEXT("Connectors do not participate in rope/world simulation collision"),Mesh->GetCollisionResponseToChannel(ECC_WorldStatic),ECR_Ignore);
        const FVector Center=Mesh->Bounds.Origin;FHitResult Hit;
        TestTrue(TEXT("An ordinary complex world line trace hits the actual connector mesh"),F.World->LineTraceSingleByChannel(Hit,Center-FVector(0,100,0),Center+FVector(0,100,0),ECC_Visibility,FCollisionQueryParams(NAME_None,true)));
        TestTrue(TEXT("The hit resolves the correct endpoint component"),Hit.GetComponent()==Mesh);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseCableRayTest,"ColossalFizzCo.Hose.Interaction.VisibleTubeRay",CableHoseTests::Flags)
bool FHoseCableRayTest::RunTest(const FString& Parameters)
{
    CableHoseTests::FFixture F;auto* Hose=F.World->SpawnActor<AHoseCableActor>();auto* Cable=Hose->Cable.Get();
    Cable->bEnableCollision=false;Cable->NumSegments=1;Cable->CableWidth=4;Cable->CableLength=200;
    Cable->SetCableParticleLocations({FVector(100,-100,100),FVector(100,100,100)});
    FHitResult Hit;
    TestTrue(TEXT("The middle of a long visible span is selectable"),Cable->TraceCableInteraction(FVector(0,0,100),FVector(200,0,100),0,Hit));
    TestTrue(TEXT("Capsule entry includes the actual tube radius"),FMath::IsNearlyEqual(Hit.Distance,98.f,.001f));
    TestTrue(TEXT("Hit identifies the cable component and owning hose"),Hit.GetComponent()==Cable && Hit.GetActor()==Hose);
    TestTrue(TEXT("Server resolves a span hit instead of requiring a nearby raw knot"),Cable->FindCableGrabPoint(Hit.ImpactPoint,5)!=INDEX_NONE);
    TestEqual(TEXT("The old knot-only proximity would miss this long span"),Cable->FindNearestCablePoint(Hit.ImpactPoint,19),INDEX_NONE);
    TestFalse(TEXT("A ray outside the visible tube misses"),Cable->TraceCableInteraction(FVector(0,0,103),FVector(200,0,103),0,Hit));
    TestTrue(TEXT("Aiming padding is optional"),Cable->TraceCableInteraction(FVector(0,0,103),FVector(200,0,103),2,Hit));
    TestTrue(TEXT("Parallel rays can hit the rounded endpoint cap"),Cable->TraceCableInteraction(FVector(100,-200,100),FVector(100,0,100),0,Hit));
    TestTrue(TEXT("Starting inside the tube is a zero-distance hit"),Cable->TraceCableInteraction(FVector(100,0,100),FVector(200,0,100),0,Hit)&&Hit.Distance==0);
    Cable->NumSegments=3;Cable->CableWidth=.2f;Cable->VisualSubdivisions=8;Cable->bEnableVisualSubdivision=true;
    Cable->VisualSmoothing=1;Cable->bLimitVisualDeviation=false;
    Cable->SetCableParticleLocations({FVector(-100,0,100),FVector(-50,0,100),FVector(0,50,100),FVector(50,50,100)});
    TArray<FVector> Curve;Cable->GetCableRenderLocations(Curve);const FVector OnCurve=Curve[10];
    const int32 Queries=Cable->CollisionQueryCount,RenderQueries=Cable->RenderCollisionQueryCount;
    TestTrue(TEXT("Pick trace follows the rounded visible curve"),Cable->TraceCableInteraction(OnCurve+FVector(0,0,20),OnCurve-FVector(0,0,20),0,Hit));
    TestTrue(TEXT("A curved-span hit maps to a real simulation point"),Hit.Item>=0 && Hit.Item<4);
    TestTrue(TEXT("Server accepts the same visible rounded span"),Cable->FindCableGrabPoint(Hit.ImpactPoint,.2f)!=INDEX_NONE);
    TestEqual(TEXT("Picking issues no additional simulation collision queries"),Cable->CollisionQueryCount,Queries);
    TestEqual(TEXT("Picking reuses render locations without shape queries"),Cable->RenderCollisionQueryCount,RenderQueries);
    Cable->bEnableGrabbing=false;
    TestFalse(TEXT("Disabled grabbing prevents selection"),Cable->TraceCableInteraction(OnCurve+FVector(0,0,20),OnCurve-FVector(0,0,20),0,Hit));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseViewGrabTest,"ColossalFizzCo.Hose.Interaction.ViewSelectionAndOcclusion",CableHoseTests::Flags)
bool FHoseViewGrabTest::RunTest(const FString& Parameters)
{
    CableHoseTests::FFixture F;auto* Player=F.World->SpawnActor<ACharacter>();Player->SetActorEnableCollision(false);Player->GetCharacterMovement()->DisableMovement();
    auto* PC=F.World->SpawnActor<APlayerController>();PC->Possess(Player);
    auto* Local=NewObject<ULocalPlayer>(GEngine);PC->Player=Local;Local->PlayerController=PC;
    PC->SetViewTarget(Player);PC->UpdateCameraManager(0);
    auto* Actions=NewObject<UShopPlayerInteractionComponent>(Player);Player->AddInstanceComponent(Actions);Actions->RegisterComponent();
    FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);const FVector Forward=Rotation.Vector();
    auto* Hose=F.World->SpawnActor<AHoseCableActor>();auto* Cable=Hose->Cable.Get();
    Cable->bEnableCollision=false;Cable->NumSegments=1;Cable->CableWidth=4;Cable->CableLength=200;
    const FVector Center=Eye+Forward*100;
    Cable->SetCableParticleLocations({Center-FVector(0,100,0),Center+FVector(0,100,0)});
    Hose->Tick(0);
    TestTrue(TEXT("Native player view grab finds and grabs the middle of a coarse span"),Actions->TryToggleCableGrabFromView());
    TestTrue(TEXT("The authoritative point grab belongs to this player"),Cable->GetActiveGrabs().Num()==1&&Cable->GetActiveGrabs()[0].Participant==Player);
    Actions->RequestCableRelease();
    // Aim at a connector deliberately offset outside the cable's padded radius.
    Hose->StartMeshOffset.SetTranslation(FVector(0,0,60));Hose->Tick(0);
    const FVector Connector=Hose->StartEndpointMesh->Bounds.Origin;
    PC->SetControlRotation((Connector-Eye).Rotation());PC->UpdateCameraManager(0);
    TestTrue(TEXT("A direct connector mesh hit selects its cable endpoint"),Actions->TryToggleCableGrabFromView());
    if(Actions->IsCableGrabActive())TestEqual(TEXT("Source connector selects point zero"),Cable->GetActiveGrabs()[0].PointIndex,0);
    Actions->RequestCableRelease();
    auto* Wall=F.World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(1,500,500));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Wall->SetActorLocation(Eye+Forward*50);
    PC->SetControlRotation(Rotation);PC->UpdateCameraManager(0);
    TestFalse(TEXT("A foreground wall prevents native view grabbing"),Actions->TryToggleCableGrabFromView());
    Wall->SetActorLocation(Eye+Forward*90);
    TestFalse(TEXT("Aiming padding cannot reach through a wall near the cable"),Actions->TryToggleCableGrabFromView());
    TestFalse(TEXT("Server rejects a forged visible hit for a point behind a wall"),Actions->ExecuteCableGrab(Cable,Eye+Forward*83,Center-FVector(0,100,0),100));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseHeldConnectorPortTest,"ColossalFizzCo.Hose.Interaction.HeldConnectorDoesNotHidePort",CableHoseTests::Flags)
bool FHoseHeldConnectorPortTest::RunTest(const FString& Parameters)
{
    CableHoseTests::FFixture F;auto* Player=F.World->SpawnActor<ACharacter>();Player->SetActorEnableCollision(false);Player->GetCharacterMovement()->DisableMovement();
    auto* PC=F.World->SpawnActor<APlayerController>();PC->Possess(Player);
    auto* Local=NewObject<ULocalPlayer>(GEngine);PC->Player=Local;Local->PlayerController=PC;
    PC->SetViewTarget(Player);PC->UpdateCameraManager(0);
    auto* Actions=NewObject<UShopPlayerInteractionComponent>(Player);Player->AddInstanceComponent(Actions);Actions->RegisterComponent();
    FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);const FVector Forward=View.Vector();
    auto* Hose=F.World->SpawnActor<AHoseCableActor>();Hose->Transfer->Category=EResourceTransportCategory::Fuel;Hose->Transfer->ResourceID=7;
    auto* Cable=Hose->Cable.Get();Cable->bEnableCollision=false;Cable->NumSegments=1;Cable->CableLength=100;
    const FVector Start=Eye+Forward*100;
    Cable->SetCableParticleLocations({Start,Start+FVector(0,100,0)});Hose->Tick(0);
    if(!TestTrue(TEXT("Player grabs the traceable source connector"),Actions->TryToggleCableGrabFromView()))return false;
    auto* Target=F.World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Target);Target->SetRootComponent(Box);Target->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(5));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Target->SetActorLocation(Eye+Forward*200);
    auto* Port=F.Endpoint(EResourceTransportCategory::Fuel,7,10,5,Target);Port->bCanReceive=false;
    TestTrue(TEXT("Receiving-port view trace ignores the currently held connector art"),Actions->TryConnectSupplyHoseFromView());
    TestTrue(TEXT("The source end connects to the visible resource socket"),Hose->StartPort==Port);
    TestFalse(TEXT("A successful socket connection releases the point grab"),Actions->IsCableGrabActive());
    return true;
}
#endif
