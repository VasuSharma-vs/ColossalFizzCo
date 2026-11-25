#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "Resources/HoseCableActor.h"
#include "Shop/ColaFlowController.h"
#include "Shop/MachinePlacementGeometry.h"
#include "UI/ShopMachinePlacementPreview.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlowControllerRelocationTest, "ColossalFizzCo.Hose.FlowController.Relocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowControllerRelocationTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
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

	const auto AddBox = [World](const FVector& Location, const FVector& HalfSize)
	{
		auto* Actor = World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Actor->AddInstanceComponent(Box);
		Box->InitBoxExtent(HalfSize);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		return Actor;
	};
	AddBox(FVector(0, 0, -10), FVector(1600, 1600, 10));

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto SpawnPlayer = [World, &Spawn](const FVector& Location) -> ACharacter*
	{
		auto* Pawn = World->SpawnActor<ACharacter>(Location, FRotator::ZeroRotator, Spawn);
		auto* Controller = World->SpawnActor<APlayerController>();
		if (!Pawn || !Controller) return nullptr;
		Controller->Possess(Pawn);
		return Pawn;
	};
	auto* Player = SpawnPlayer(FVector(0, 0, 96));
	auto* Other = SpawnPlayer(FVector(-300, -250, 96));
	auto* Flow = World->SpawnActor<AColaFlowController>(FVector(0, -230, 51), FRotator::ZeroRotator, Spawn);
	auto* HousingMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Player || !Other || !Flow || !HousingMesh) return false;
	Flow->Housing->SetStaticMesh(HousingMesh);
	const FTransform Start = Flow->GetActorTransform();

	FFlowControllerUpgradeState Ranks;
	Ranks.InputPortsRank = 1;
	Ranks.OutputPortsRank = 2;
	Ranks.FlowRank = 1;
	Ranks.LeverRank = 1;
	Flow->RestoreUpgradeState(Ranks);
	if (!TestEqual(TEXT("The fixture has two unlocked inlets"), Flow->SelectorSockets.Num(), 2)
		|| !TestEqual(TEXT("The fixture has three unlocked outlets"), Flow->FillerSockets.Num(), 3)) return false;
	auto* Inlet = Flow->SelectorSockets.Last().Get();
	auto* Outlet = Flow->FillerSockets.Last().Get();

	const auto AddEndpoint = [World](const FVector& Location, bool bSupply)
	{
		auto* Owner = World->SpawnActor<AActor>();
		auto* Port = NewObject<UResourceTransferEndpointComponent>(Owner);
		Owner->SetRootComponent(Port);
		Owner->AddInstanceComponent(Port);
		Port->bCanSupply = bSupply;
		Port->bCanReceive = !bSupply;
		Port->bSupplyEnabled = bSupply;
		Port->bRequestsFlow = !bSupply;
		Port->CapacityUnits = 10.f;
		Port->RegisterComponent();
		Port->SetWorldLocation(Location);
		return Port;
	};
	auto* Source = AddEndpoint(Inlet->GetComponentLocation() - FVector(0, 110, 0), true);
	Source->SetStoredUnits(5.f);
	auto* Receiver = AddEndpoint(Outlet->GetComponentLocation() + FVector(0, 110, 0), false);
	auto* InletHose = World->SpawnActor<AHoseCableActor>(FVector(800, -400, 140), FRotator::ZeroRotator, Spawn);
	auto* OutletHose = World->SpawnActor<AHoseCableActor>(FVector(800, -250, 140), FRotator::ZeroRotator, Spawn);
	if (!InletHose || !OutletHose) return false;
	if (!TestTrue(TEXT("The source hose reaches the upgraded inlet"), InletHose->ConnectStart(Source) && InletHose->ConnectEnd(Inlet))
		|| !TestTrue(TEXT("The controller can select that inlet"), Flow->SelectInput(1))
		|| !TestTrue(TEXT("The upgraded outlet is connected to a receiver"), OutletHose->ConnectStart(Outlet) && OutletHose->ConnectEnd(Receiver))) return false;
	if (!TestTrue(TEXT("The operator can pull the lever before moving"), Flow->BeginLever(Player, 350.f))
		|| !TestTrue(TEXT("The operator can move the controller while the lever was pulled"), Flow->BeginMachinePlacement(*Player))) return false;
	TestFalse(TEXT("Moving stops the active lever"), Flow->bLeverHeld);
	TestTrue(TEXT("The original controller is hidden and has no collision during placement"),
		Flow->IsMachinePlacementActive() && Flow->GetPlacementOperator() == Player && Flow->IsHidden() && !Flow->GetActorEnableCollision());
	TestTrue(TEXT("The inlet and outlet disconnect at the controller, keeping the other ends mounted"),
		!Inlet->ConnectedHose && !Outlet->ConnectedHose && !InletHose->EndPort && !OutletHose->StartPort
		&& InletHose->StartPort == Source && OutletHose->EndPort == Receiver);
	TestFalse(TEXT("A hose cannot be reattached during placement"), InletHose->ConnectEnd(Inlet));
	TestFalse(TEXT("A second player cannot take ownership of a move"), Flow->BeginMachinePlacement(*Other));
	Flow->CancelMachinePlacement(*Other);
	TestTrue(TEXT("A second player cannot cancel the move"), Flow->GetPlacementOperator() == Player);

	FVector Eye;
	FRotator PlayerView;
	Player->GetActorEyesViewPoint(Eye, PlayerView);
	constexpr float Yaw = 55.f;
	const FVector Aim = (FVector(500, 250, 0) - Eye).GetSafeNormal();
	FTransform Destination;
	if (!TestTrue(TEXT("A rotated Flow Controller fits on the traced floor"),
		ShopMachinePlacement::TracePlacement(*Flow, *Player, Eye, Aim, Yaw, Destination))) return false;
	TestTrue(TEXT("The candidate uses the selected yaw"), FMath::IsNearlyEqual(Destination.Rotator().Yaw, double(Yaw), .01));
	TestTrue(TEXT("Server geometry accepts the same candidate"), ShopMachinePlacement::CanPlace(*Flow, *Player, Destination));

	const FVector ObstacleLocation = Destination.TransformPosition(FVector(30, -15, 5));
	auto* Blocker = AddBox(ObstacleLocation, FVector(12));
	TestFalse(TEXT("Overlap with a side of the controller rejects placement"), ShopMachinePlacement::CanPlace(*Flow, *Player, Destination));
	TestFalse(TEXT("The server rejects an overlapping commit and keeps the move active"), Flow->FinishMachinePlacement(*Player, Destination));
	TestTrue(TEXT("A rejected commit keeps the original controller pose"),
		Flow->IsMachinePlacementActive() && Flow->GetActorTransform().Equals(Start));

	auto* Preview = World->SpawnActor<AShopMachinePlacementPreview>(Start.GetLocation(), Start.Rotator(), Spawn);
	if (!TestTrue(TEXT("The local preview can copy the controller's visible meshes"), Preview && Preview->InitializeFromMachine(*Flow))) return false;
	UMaterialInstanceDynamic* Tint = nullptr;
	for (const UStaticMeshComponent* Mesh : TInlineComponentArray<UStaticMeshComponent*>(Preview))
	{
		TestEqual(TEXT("Every translucent preview part has collision disabled"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		if (!Tint) Tint = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	}
	if (!TestNotNull(TEXT("The preview uses a dynamic tint material"), Tint)) return false;
	Preview->SetPlacementPreview(Destination, false);
	const FLinearColor BlockedTint = Tint->K2_GetVectorParameterValue(TEXT("Tint"));
	TestTrue(TEXT("A blocked preview is red"), BlockedTint.R > BlockedTint.G);
	TestTrue(TEXT("The preview is half opaque"), FMath::IsNearlyEqual(Tint->K2_GetScalarParameterValue(TEXT("Opacity")), .5f));
	Blocker->SetActorEnableCollision(false);
	Preview->SetPlacementPreview(Destination, ShopMachinePlacement::CanPlace(*Flow, *Player, Destination));
	const FLinearColor ClearTint = Tint->K2_GetVectorParameterValue(TEXT("Tint"));
	TestTrue(TEXT("A clear preview is mint green"), ClearTint.G > ClearTint.R && ClearTint.B > ClearTint.R);
	TestTrue(TEXT("The ghost follows the destination pose and remains non-solid"),
		!Preview->IsHidden() && !Preview->GetActorEnableCollision() && Preview->GetActorTransform().Equals(Destination));
	TestFalse(TEXT("A second player cannot commit the preview"), Flow->FinishMachinePlacement(*Other, Destination));
	if (!TestTrue(TEXT("The operator can place a clear controller"), Flow->FinishMachinePlacement(*Player, Destination))) return false;
	Preview->Destroy();
	TestTrue(TEXT("Placement restores the original actor, collision, and upgraded ports"),
		!Flow->IsMachinePlacementActive() && !Flow->GetPlacementOperator() && !Flow->IsHidden() && Flow->GetActorEnableCollision()
		&& Flow->GetActorTransform().Equals(Destination) && Flow->UpgradeState == Ranks
		&& Flow->SelectorSockets.Last().Get() == Inlet && Flow->FillerSockets.Last().Get() == Outlet);
	TestTrue(TEXT("The committed move leaves the detached ends free"), !InletHose->EndPort && !OutletHose->StartPort);

	Player->SetActorLocation(FVector(400, 150, 96));
	if (!TestTrue(TEXT("The controller can be moved again"), Flow->BeginMachinePlacement(*Player))) return false;
	Flow->CancelMachinePlacement(*Player);
	TestTrue(TEXT("Cancel restores the last committed solid pose"), !Flow->IsMachinePlacementActive() && !Flow->IsHidden()
		&& Flow->GetActorEnableCollision() && Flow->GetActorTransform().Equals(Destination));
	return true;
}

#endif
