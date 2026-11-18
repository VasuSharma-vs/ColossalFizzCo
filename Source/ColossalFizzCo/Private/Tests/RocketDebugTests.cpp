#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/LineBatchComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Props/ColaPropActor.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketDebugSettingsTest, "ColossalFizzCo.Props.RocketDebugSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRocketDebugSettingsTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	const bool Settings[][3] = {{true, true, true}, {false, false, false}, {false, false, true}};
	for (const auto& Flags : Settings)
	{
		auto* Bottle = World->SpawnActorDeferred<AColaPropActor>(AColaPropActor::StaticClass(), FTransform::Identity);
		if (!TestNotNull(TEXT("Deferred bottle"), Bottle)) return false;
		Bottle->bAutoConvertToISMIfAtRest = false;
		auto* Rocket = NewObject<URocketProjectileComponent>(Bottle, TEXT("AuthoredRocket"));
		Bottle->AddInstanceComponent(Rocket);
		Rocket->bLaunchOnInitialized = false;
		Rocket->bEnableDebugLogs = Flags[0];
		Rocket->bShowOnScreenDebug = Flags[1];
		Rocket->bDrawDebugTrajectory = Flags[2];
		Rocket->RegisterComponent();
		Bottle->FinishSpawning(FTransform::Identity);
		TestTrue(TEXT("Native initialization uses authored rocket"), Bottle->RocketComp == Rocket);
		TestTrue(TEXT("Fixture reaches BeginPlay"), Bottle->HasActorBegunPlay());
		auto CheckSettings = [&]()
		{
			TestEqual(TEXT("Authored logging preference survives bottle lifecycle"), Rocket->bEnableDebugLogs, Flags[0]);
			TestEqual(TEXT("Authored HUD preference survives bottle lifecycle"), Rocket->bShowOnScreenDebug, Flags[1]);
			TestEqual(TEXT("Authored trajectory preference survives bottle lifecycle"), Rocket->bDrawDebugTrajectory, Flags[2]);
		};
		CheckSettings();
		Bottle->ResetTransientPoolState();
		Bottle->HandleOnBecomeActive(Bottle, 0, 0, FTransform::Identity);
		CheckSettings();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketPhysicsDebugDrawingTest, "ColossalFizzCo.Props.RocketPhysicsDebugDrawing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRocketPhysicsDebugDrawingTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Actor = World->SpawnActor<AActor>();
	auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
	Actor->SetRootComponent(Mesh);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->RegisterComponent();
	Actor->SetActorLocation(FVector(100, 0, 1000));
	auto* Rocket = NewObject<URocketProjectileComponent>(Actor);
	Actor->AddInstanceComponent(Rocket);
	Rocket->bLaunchOnInitialized = false;
	Rocket->SetAllDebugEnabled(false);
	Rocket->bDrawDebugTrajectory = true;
	Rocket->RegisterComponent();
	if (!TestTrue(TEXT("Pressure physics flight starts"), Rocket->StartPressurePhysicsFlight())) return false;
	Mesh->SetPhysicsLinearVelocity(FVector(100, 0, 0));
	auto LineCount = [&]()
	{
		int32 Count = 0;
		for (const auto Type : {UWorld::ELineBatcherType::World, UWorld::ELineBatcherType::WorldPersistent, UWorld::ELineBatcherType::Foreground})
			if (const auto* Batcher = World->GetLineBatcher(Type)) Count += Batcher->BatchedLines.Num();
		return Count;
	};
	auto TickRocket = [&]() { static_cast<UActorComponent*>(Rocket)->TickComponent(.016f, LEVELTICK_All, nullptr); };
	int32 Before = LineCount();
	TickRocket();
	TestTrue(TEXT("Powered Chaos flight submits debug trajectory lines"), LineCount() > Before);
	Rocket->FinishPressureThrust();
	Before = LineCount();
	TickRocket();
	TestTrue(TEXT("Physics falling also submits debug trajectory lines"), LineCount() > Before);
	Rocket->bDrawDebugTrajectory = false;
	Before = LineCount();
	TickRocket();
	TestEqual(TEXT("Disabling trajectory drawing submits no debug lines"), LineCount(), Before);
	return true;
}
#endif
