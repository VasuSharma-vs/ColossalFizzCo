#include "Misc/AutomationTest.h"
#include "Components/CollisionCableComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CollisionCableTests
{
    struct FFixture
    {
        UWorld* World;
        explicit FFixture(EWorldType::Type WorldType = EWorldType::Game)
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(WorldType, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(WorldType).SetCurrentWorld(World);
            if (WorldType == EWorldType::Game)
            {
                World->InitializeActorsForPlay(FURL());
                World->GetWorldSettings()->NotifyBeginPlay();
            }
        }
        ~FFixture()
        {
            if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
            World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
        }
        UBoxComponent* Box(FVector Center, FVector Extent)
        {
            AActor* Actor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
            Box->SetBoxExtent(Extent); Box->SetCollisionProfileName(TEXT("BlockAll"));
            Box->SetWorldLocation(Center); Box->RegisterComponent();
            return Box;
        }
        UCollisionCableComponent* Cable(FVector Start, FVector End, int32 Segments, bool Pinned = false)
        {
            AActor* Actor = World->SpawnActor<AActor>();
            auto* Cable = NewObject<UCollisionCableComponent>(Actor);
            Actor->SetRootComponent(Cable); Actor->AddInstanceComponent(Cable);
            Cable->SetWorldLocation(Start); Cable->EndLocation = End - Start;
            Cable->NumSegments = Segments; Cable->CableLength = FVector::Distance(Start, End);
            Cable->bAttachStart = Cable->bAttachEnd = Pinned;
            // Existing strict no-penetration regressions exercise the immediate
            // option. Smooth recovery is checked separately over time below.
            Cable->bSmoothCollisionRecovery = false;
            Cable->bEnableSleeping = false;
            Cable->bEnablePointWeight = false;
            Cable->bEnableGrabbing = false;
            Cable->bEnableMovementRestrictions = false;
            Cable->bEnableVisualSubdivision = false;
            Cable->bEnableNetworkSync = false;
            Cable->CollisionMethod = ECableCollisionMethod::Sweeps;
            Cable->CollisionPointsPerFrame = Segments + 1;
            Cable->RegisterComponent();
            return Cable;
        }
        bool Step(UCollisionCableComponent* Cable, int32 Frames, float Dt = 1.f / 60.f)
        {
            for (int32 Frame = 0; Frame < Frames; ++Frame)
            {
                Cable->TickComponent(Dt, LEVELTICK_All, nullptr);
                if (Cable->bHasUnresolvedCollision || !Cable->IsCableCollisionFree())
                {
                    UE_LOG(LogTemp, Display, TEXT("Cable contact failure at frame %d, queries %d, unresolved %d"), Frame, Cable->CollisionQueryCount, Cable->bHasUnresolvedCollision);
                    TArray<FVector> FailedPoints; Cable->GetCableParticleLocations(FailedPoints);
                    for (int32 I = 0; I < FailedPoints.Num(); ++I) UE_LOG(LogTemp, Display, TEXT("Point %d: %s"), I, *FailedPoints[I].ToString());
                    return false;
                }
                TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
                for (const FVector& Point : Points) if (Point.ContainsNaN()) return false;
            }
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableFloorTest, "ColossalFizzCo.Cable.FloorAndRestingContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableFloorTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    F.Box(FVector(0,0,-10), FVector(5000,5000,10));
    auto* Cable = F.Cable(FVector(-800,0,200), FVector(800,0,200), 32);
    TestTrue(TEXT("Every complete span remains clear throughout falling and resting"), F.Step(Cable, 120));
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    double MinZ = UE_BIG_NUMBER, MaxZ = -UE_BIG_NUMBER;
    for (const FVector& P : Points) { MinZ = FMath::Min(MinZ, P.Z); MaxZ = FMath::Max(MaxZ, P.Z); }
    TestTrue(TEXT("Rope actually falls and rests above its visible radius"), MinZ >= 5 && MaxZ < 30);
    TestTrue(TEXT("Hitch uses bounded substeps without losing floor collision"), F.Step(Cable, 1, .5f));
    TestTrue(TEXT("Hitch time is dropped instead of using an unsafe huge step"), Cable->DroppedSimulationTime > .4f);
    Cable->SetComponentTickEnabled(false);
    TestTrue(TEXT("Legacy BP sleep request cannot disable collision by default"), Cable->IsComponentTickEnabled());
    Cable->bKeepCollisionAwake = false; Cable->SetComponentTickEnabled(false);
    TestFalse(TEXT("Explicit opt out still allows disabling ticking"), Cable->IsComponentTickEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableWallTest, "ColossalFizzCo.Cable.ThinWallHighSpeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableWallTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    F.Box(FVector(0,0,0), FVector(.5,2000,2000));
    auto* Cable = F.Cable(FVector(-200,0,-200), FVector(-200,0,200), 8);
    Cable->CableGravityScale = 0; Cable->CableForce = FVector(5000000,0,0);
    TestTrue(TEXT("Fast rope stays outside a one-centimetre wall"), F.Step(Cable, 12, 1.f/30.f));
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    for (const FVector& P : Points) TestTrue(TEXT("No particle tunneled to the far side"), P.X < -5.49);
    AddInfo(FString::Printf(TEXT("Last-frame motion/recovery queries: %d"), Cable->CollisionQueryCount));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableSpanTest, "ColossalFizzCo.Cable.ObstacleBetweenParticles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableSpanTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    F.Box(FVector(0,0,0), FVector(1,1000,5));
    auto* Cable = F.Cable(FVector(-50,0,50), FVector(50,0,50), 1);
    Cable->CableGravityScale = 0; Cable->CableForce = FVector(0,0,-20000);
    TestTrue(TEXT("A two-centimetre obstacle between widely separated particles blocks the span"), F.Step(Cable, 12));
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    const FVector Midpoint = (Points[0] + Points[1]) * .5;
    TestTrue(TEXT("The rope did not pass down through the narrow obstacle"), Midpoint.Z >= 10.49);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableAnchorTest, "ColossalFizzCo.Cable.AttachmentMotionAndMountFiltering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableAnchorTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    UBoxComponent* Wall = F.Box(FVector(0,0,0), FVector(1,2000,2000));
    // The end target and wall deliberately share one actor. Ignoring its entire
    // actor would incorrectly remove the wall from the cable's collision queries.
    auto* Target = NewObject<USceneComponent>(Wall->GetOwner());
    Target->SetWorldLocation(FVector(-200,0,200)); Target->RegisterComponent();
    auto* Cable = F.Cable(FVector(-200,0,0), FVector(-200,0,200), 4, true);
    Cable->CableGravityScale = 0; Cable->EndLocation = FVector::ZeroVector;
    Cable->SetAttachEndToComponent(Target);
    Cable->SetWorldLocation(FVector(200,0,0)); Target->SetWorldLocation(FVector(200,0,200));
    TestTrue(TEXT("Swept anchors cannot drag the cable across the wall"), F.Step(Cable, 3, 1.f/30.f));
    TestTrue(TEXT("Blocked target is reported"), Cable->bAttachmentBlocked);
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    for (const FVector& P : Points) TestTrue(TEXT("Wall is still considered despite sharing target owner"), P.X < -5.99);
    TestTrue(TEXT("Socket reports the actual blocked endpoint, not the unreachable target"), Cable->GetSocketTransform(TEXT("CableEnd")).GetLocation().Equals(Points.Last(), .001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableRecoveryTest, "ColossalFizzCo.Cable.PenetrationRecoveryAndImpossibleAnchors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableRecoveryTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    F.Box(FVector(0,0,-10), FVector(2000,2000,10));
    auto* Cable = F.Cable(FVector(-200,0,2), FVector(200,0,2), 8);
    TestFalse(TEXT("Initially overlapping free cable is recovered"), Cable->bHasUnresolvedCollision);
    TestTrue(TEXT("Recovered cable remains clear under gravity"), F.Step(Cable, 30));
    auto* Impossible = F.Cable(FVector(-20,0,-5), FVector(20,0,-5), 1, true);
    TestTrue(TEXT("Fixed endpoints embedded in solid geometry are explicitly reported"), Impossible->bHasUnresolvedCollision);
    TestFalse(TEXT("Impossible geometry is not advertised as collision free"), Impossible->IsCableCollisionFree());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableMovingObstacleTest, "ColossalFizzCo.Cable.MovingObstacleRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableMovingObstacleTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    auto* Box = F.Box(FVector(0,0,20), FVector(10,100,10));
    auto* Cable = F.Cable(FVector(-200,0,100), FVector(200,0,100), 16, true);
    Cable->CableGravityScale = 0;
    TestTrue(TEXT("Starting configuration is clear"), F.Step(Cable, 2));
    Box->SetWorldLocation(FVector(0,0,100));
    TestFalse(TEXT("Moving collider really overlaps the old rope position"), Cable->IsCableCollisionFree());
    TestTrue(TEXT("Cable recovers from a moving collider instead of using an invalid old fallback"), F.Step(Cable, 30));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableTriangleSurfaceTest, "ColossalFizzCo.Cable.TriangleSurface", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableTriangleSurfaceTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    auto* Actor = F.World->SpawnActor<AActor>();
    auto* Surface = NewObject<UStaticMeshComponent>(Actor);
    Actor->SetRootComponent(Surface); Actor->AddInstanceComponent(Surface);
    Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
    Surface->SetWorldScale3D(FVector(30)); Surface->SetCollisionProfileName(TEXT("BlockAll")); Surface->RegisterComponent();
    auto* Cable = F.Cable(FVector(-400,0,200), FVector(400,0,200), 16);
    Cable->bTraceComplex = true;
    TestTrue(TEXT("Swept rope remains above a triangle-mesh surface"), F.Step(Cable, 120));
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    for (const FVector& P : Points) TestTrue(TEXT("Triangle surface did not allow downward tunnelling"), P.Z >= 5 && P.Z < 30);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCablePlacementTest, "ColossalFizzCo.Cable.EditorPlacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCablePlacementTest::RunTest(const FString& Parameters)
{
    for (EWorldType::Type WorldType : {EWorldType::Editor, EWorldType::EditorPreview})
    {
        CollisionCableTests::FFixture F(WorldType);
        AActor* Actor = F.World->SpawnActor<AActor>();
        auto* Root = NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root); Actor->AddInstanceComponent(Root); Root->RegisterComponent();
        auto* Cable = NewObject<UCollisionCableComponent>(Actor);
        Actor->AddInstanceComponent(Cable); Cable->SetupAttachment(Root);
        Cable->SetRelativeLocation(FVector(-5,0,65));
        Cable->CableLength = 500; Cable->EndLocation = FVector(250,0,0);
        Cable->bEnableStiffness = true; Cable->RegisterComponent();
        Actor->SetActorTransform(FTransform(FRotator(0,90,0), FVector(12000,-8000,-3000), FVector(2,1,1)));
        TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
        const FVector Start = Cable->GetComponentLocation();
        const FVector End = Cable->GetComponentTransform().TransformPosition(Cable->EndLocation);
        TestTrue(TEXT("Placement immediately updates the start without waiting for a simulation tick"), Points[0].Equals(Start, .001));
        TestTrue(TEXT("Placement immediately updates the end without sweeping from the old world origin"), Points.Last().Equals(End, .001));
        for (const FVector& Point : Points)
            if (!TestTrue(TEXT("Placed rope has no stale points at the old world origin"), FVector::Distance(Point, Start) <= 501)) break;

        Cable->SetRelativeLocation(FVector(383,0,216));
        Cable->GetCableParticleLocations(Points);
        TestTrue(TEXT("Moving a nested component refreshes its start"), Points[0].Equals(Cable->GetComponentLocation(), .001));
        Cable->UnregisterComponent();
        Cable->EndLocation = FVector(0,200,-50);
        Cable->RegisterComponent();
        Cable->GetCableParticleLocations(Points);
        TestTrue(TEXT("Re-registration with the same segment count reinitializes changed endpoints"),
            Points.Last().Equals(Cable->GetComponentTransform().TransformPosition(Cable->EndLocation), .001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableCoincidentEndsTest, "ColossalFizzCo.Cable.CoincidentEndpointInitialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableCoincidentEndsTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    const FVector Mount(4000,-3000,800);
    auto* Cable = F.Cable(Mount, Mount, 32, true);
    Cable->CableLength = 500; Cable->bEnableStiffness = true; Cable->ResetCable();
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    double Length = 0, MinZ = Mount.Z;
    for (int32 I = 0; I < Points.Num(); ++I)
    {
        MinZ = FMath::Min(MinZ, Points[I].Z);
        if (I > 0) Length += FVector::Distance(Points[I - 1], Points[I]);
    }
    TestTrue(TEXT("Zero-offset attached ends seed a noncollapsed hanging loop"), Length > 490 && Length < 510 && MinZ < Mount.Z - 100);
    TestTrue(TEXT("Initialized hanging loop remains collision free"), F.Step(Cable, 60));
    Cable->GetCableParticleLocations(Points);
    for (const FVector& Point : Points)
    {
        if (!TestTrue(TEXT("Loop remains near its mount instead of shooting upward"),
            Point.Z <= Mount.Z + 10 && FVector::Distance(Point, Mount) < 510)) break;
    }
    F.Box(FVector(0,0,-10), FVector(2000,2000,10));
    auto* NearFloor = F.Cable(FVector(0,0,65), FVector(0,0,65), 32, true);
    NearFloor->CableLength = 500; NearFloor->bEnableStiffness = true; NearFloor->ResetCable();
    TestTrue(TEXT("A loop mounted near the floor can settle without crossing the surface"), F.Step(NearFloor, 60));
    NearFloor->GetCableParticleLocations(Points);
    for (const FVector& Point : Points)
        if (!TestTrue(TEXT("The near-floor loop stays above the surface and close to its mount"),
            Point.Z >= 5 && FVector::Distance(Point, FVector(0,0,65)) < 510)) break;
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableBudgetTest, "ColossalFizzCo.Cable.SharedPerFrameBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableBudgetTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    auto* Cable = F.Cable(FVector(0,0,100), FVector(800,0,100), 8, true);
    Cable->CableGravityScale = 0;
    for (ECableCollisionMethod Method : {ECableCollisionMethod::LineTraces, ECableCollisionMethod::Sweeps})
    {
        Cable->CollisionMethod = Method;
        for (int32 Budget : {1, 3, 9})
        {
            Cable->CollisionPointsPerFrame = Budget;
            Cable->ResetCable();
            TSet<int32> Visited;
            int32 ExpectedIndex = 0, FirstQueryCount = -1;
            for (int32 Frame = 0; Frame < 9; ++Frame)
            {
                // These deliberately vary the simulation work. Collision must
                // still visit exactly Budget distinct points per FRAME.
                Cable->SubstepTime = Frame % 2 ? .001f : 1.f / 30.f;
                Cable->SolverIterations = Frame % 2 ? 32 : 1;
                Cable->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);
                TestEqual(TEXT("Shared budget is enforced once per frame for either method"), Cable->LastCollisionPointIndices.Num(), Budget);
                TSet<int32> ThisFrame, Edges;
                for (int32 Index : Cable->LastCollisionPointIndices)
                {
                    TestEqual(TEXT("Points rotate continuously including both endpoints"), Index, ExpectedIndex);
                    ExpectedIndex = (ExpectedIndex + 1) % 9;
                    ThisFrame.Add(Index); Visited.Add(Index); Edges.Add(FMath::Min(Index, 7));
                }
                TestEqual(TEXT("A point is never processed twice in a frame"), ThisFrame.Num(), Budget);
                if (FirstQueryCount < 0) FirstQueryCount = Cable->CollisionQueryCount;
                const int32 ExpectedQueries = Budget + 2 * Edges.Num();
                TestEqual(TEXT("Substeps and solver iterations do not multiply world queries"), Cable->CollisionQueryCount, ExpectedQueries);
            }
            TestEqual(TEXT("Round-robin eventually visits every cable point"), Visited.Num(), 9);
            AddInfo(FString::Printf(TEXT("Method=%s, budget=%d/9, empty-scene queries/frame=%d"),
                Method == ECableCollisionMethod::LineTraces ? TEXT("LineTraces") : TEXT("Sweeps"), Budget, FirstQueryCount));
        }
        Cable->CollisionPointsPerFrame = 999;
        Cable->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Budget maximum is the actual number of points"), Cable->CollisionPointsPerFrame, 9);
        TestEqual(TEXT("Maximum budget checks the complete cable once"), Cable->LastCollisionPointIndices.Num(), 9);
        Cable->CollisionPointsPerFrame = 0;
        Cable->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Budget minimum is one point"), Cable->CollisionPointsPerFrame, 1);
        Cable->bEnableCollision = false;
        Cable->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Disabled collision runs no world queries"), Cable->CollisionQueryCount, 0);
        TestEqual(TEXT("Disabled collision checks no points"), Cable->LastCollisionPointIndices.Num(), 0);
        Cable->bEnableCollision = true;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableLineTraceTest, "ColossalFizzCo.Cable.LineTraceFloorAndWall", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableLineTraceTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    F.Box(FVector(0,0,-10), FVector(2000,2000,10));
    F.Box(FVector(0,0,300), FVector(.5,2000,300));
    auto* Falling = F.Cable(FVector(-800,0,200), FVector(-200,0,200), 16);
    Falling->CollisionMethod = ECableCollisionMethod::LineTraces;
    TestTrue(TEXT("Line-trace mode falls and rests above the floor"), F.Step(Falling, 120));
    TArray<FVector> Points; Falling->GetCableParticleLocations(Points);
    for (const FVector& P : Points) TestTrue(TEXT("Line-trace contact offsets the visible cable above the floor"), P.Z >= 5 && P.Z < 30);
    auto* Fast = F.Cable(FVector(-200,100,100), FVector(-200,100,500), 8);
    Fast->CollisionMethod = ECableCollisionMethod::LineTraces;
    Fast->CableGravityScale = 0; Fast->CableForce = FVector(5000000,0,0);
    TestTrue(TEXT("Line-trace motion catches the thin wall"), F.Step(Fast, 12, 1.f/30.f));
    Fast->GetCableParticleLocations(Points);
    for (const FVector& P : Points) TestTrue(TEXT("Line-trace particles stay on the starting side of the wall"), P.X < -5.49);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableCachedContactTest, "ColossalFizzCo.Cable.LowBudgetHangingLoop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableCachedContactTest::RunTest(const FString& Parameters)
{
    for (ECableCollisionMethod Method : {ECableCollisionMethod::LineTraces, ECableCollisionMethod::Sweeps})
    {
        CollisionCableTests::FFixture F;
        F.Box(FVector(0,0,-10), FVector(2000,2000,10));
        auto* Cable = F.Cable(FVector(0,0,300), FVector(0,0,300), 32, true);
        Cable->CollisionMethod = Method; Cable->CollisionPointsPerFrame = 1;
        Cable->CableLength = 500; Cable->bEnableStiffness = true; Cable->ResetCable();
        TestTrue(TEXT("One-point budget supports a hanging loop in clear space"), F.Step(Cable, 120));
        TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
        double MinZ = 300, MaxZ = 300;
        for (const FVector& P : Points) { MinZ = FMath::Min(MinZ, P.Z); MaxZ = FMath::Max(MaxZ, P.Z); }
        TestTrue(TEXT("Low-budget loop hangs below its mount and above the floor"), MinZ >= 5 && MinZ < 200 && MaxZ <= 310);
        TestEqual(TEXT("Supporting cached contacts does not spend additional point checks"), Cable->LastCollisionPointIndices.Num(), 1);

        auto* Resting = F.Cable(FVector(-800,400,100), FVector(-200,400,100), 32);
        Resting->CollisionMethod = Method;
        TestTrue(TEXT("Full budget establishes floor contacts before reducing the budget"), F.Step(Resting, 120));
        Resting->CollisionPointsPerFrame = 1;
        Resting->CableForce = FVector(0,0,3000);
        TestTrue(TEXT("Rope can lift away from a cached floor at a one-point budget"), F.Step(Resting, 30));
        Resting->CableForce = FVector::ZeroVector;
        TestTrue(TEXT("Known floor contacts remain valid when the rope falls back between checks"), F.Step(Resting, 180));
        Resting->GetCableParticleLocations(Points);
        for (const FVector& P : Points) TestTrue(TEXT("Cached contacts keep the resting visible radius above the floor"), P.Z >= 5 && P.Z < 30);
        TestEqual(TEXT("Only one point receives new collision queries per frame"), Resting->LastCollisionPointIndices.Num(), 1);
        const int32 PreviousIndex = Resting->LastCollisionPointIndices[0];
        Resting->CollisionMethod = Method == ECableCollisionMethod::LineTraces ? ECableCollisionMethod::Sweeps : ECableCollisionMethod::LineTraces;
        TestTrue(TEXT("Runtime method switching preserves cached support at a one-point budget"), F.Step(Resting, 8));
        TestEqual(TEXT("Runtime method switching preserves round-robin progress"), Resting->LastCollisionPointIndices[0], (PreviousIndex + 8) % 33);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableSmoothRecoveryTest, "ColossalFizzCo.Cable.SmoothBoundsRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableSmoothRecoveryTest::RunTest(const FString& Parameters)
{
    for (ECableCollisionMethod Method : {ECableCollisionMethod::LineTraces, ECableCollisionMethod::Sweeps})
    {
        CollisionCableTests::FFixture F;
        F.Box(FVector::ZeroVector, FVector(50));
        auto MakeEmbedded = [&](double X)
        {
            auto* Cable = F.Cable(FVector(200,0,0), FVector(201,0,0), 1);
            Cable->bSmoothCollisionRecovery = true; Cable->CollisionMethod = Method;
            Cable->CableGravityScale = 0; Cable->CollisionRecoveryRate = 2;
            Cable->MinCollisionRecoverySpeed = 0; Cable->MaxCollisionRecoverySpeed = 120;
            Cable->SetCableParticleLocations({FVector(X,0,0), FVector(X+1,0,0)});
            return Cable;
        };
        auto* Deep = MakeEmbedded(20);
        auto* Shallow = MakeEmbedded(40);
        TArray<FVector> Before, After, ShallowAfter;
        Deep->GetCableParticleLocations(Before);
        TestTrue(TEXT("Smooth reset preserves embedded points instead of instantly snapping them outside"), Before[0].Equals(FVector(20,0,0), .001));
        Deep->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
        Shallow->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
        Deep->GetCableParticleLocations(After); Shallow->GetCableParticleLocations(ShallowAfter);
        TestTrue(TEXT("Embedded point moves away from the mesh bounds center"), After[0].X > 20 && FMath::Abs(After[0].Y) < .001 && FMath::Abs(After[0].Z) < .001);
        TestTrue(TEXT("A deeper overlap recovers faster"), After[0].X - 20 > (ShallowAfter[0].X - 40) * 1.2);
        for (int32 I = 0; I < After.Num(); ++I)
            TestTrue(TEXT("First-frame recovery is gradual and respects the speed cap"), FVector::Distance(Before[I], After[I]) <= 2.001);
        TestTrue(TEXT("Pending smooth recovery is reported while still embedded"), Deep->bHasUnresolvedCollision);
        Deep->CollisionPointsPerFrame = 1;
        Deep->CollisionRecoveryRate = 8; Deep->MinCollisionRecoverySpeed = 5; Deep->MaxCollisionRecoverySpeed = 100;
        for (int32 Frame = 0; Frame < 240; ++Frame)
        {
            Deep->SolverIterations = Frame % 2 ? 32 : 1;
            Deep->SubstepTime = Frame % 2 ? 1.f/120.f : 1.f/60.f;
            Deep->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("All contacts/substeps share one per-point push-out speed limit"), Deep->MaxRecoveryDistanceThisFrame <= 100.f/60.f + .001f);
            TestEqual(TEXT("Smooth recovery retains the one-point query budget"), Deep->LastCollisionPointIndices.Num(), 1);
        }
        Deep->GetCableParticleLocations(After);
        TestTrue(TEXT("An embedded cable eventually clears the mesh"), Deep->IsCableCollisionFree());
        for (const FVector& P : After)
            TestTrue(FString::Printf(TEXT("Recovery remains finite and does not launch the rope: %s"), *P.ToString()), !P.ContainsNaN() && P.Size() < 120);
        Deep->SetCableParticleLocations({FVector::ZeroVector, FVector(0,0,1)});
        for (int32 Frame = 0; Frame < 10; ++Frame) Deep->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
        Deep->GetCableParticleLocations(After);
        for (const FVector& P : After) TestFalse(TEXT("A point exactly at bounds center has a finite fallback direction"), P.ContainsNaN());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCableSmoothFloorTest, "ColossalFizzCo.Cable.SmoothFloorRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCableSmoothFloorTest::RunTest(const FString& Parameters)
{
    for (ECableCollisionMethod Method : {ECableCollisionMethod::LineTraces, ECableCollisionMethod::Sweeps})
    {
        CollisionCableTests::FFixture F;
        F.Box(FVector(0,0,-10), FVector(5000,5000,10));
        auto* Cable = F.Cable(FVector(100,0,100), FVector(200,0,100), 8);
        Cable->bSmoothCollisionRecovery = true; Cable->CollisionMethod = Method;
        for (int32 Frame = 0; Frame < 180; ++Frame) Cable->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
        TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
        TestTrue(TEXT("Smooth recovery can settle on a wide floor"), Cable->IsCableCollisionFree());
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            TestTrue(TEXT("Known floor contacts prevent gravity from deepening penetration"), Points[I].Z >= 5.49 && Points[I].Z < 10);
            TestTrue(TEXT("Bounds direction uses the floor normal instead of pulling sideways"), FMath::Abs(Points[I].X - (100 + 100.*I/8)) < .01);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionCablePlayerCollisionTest, "ColossalFizzCo.Cable.IgnorePlayerCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCollisionCablePlayerCollisionTest::RunTest(const FString& Parameters)
{
    CollisionCableTests::FFixture F;
    auto* Pawn = F.World->SpawnActor<APawn>();
    auto* Capsule = NewObject<UCapsuleComponent>(Pawn);
    Pawn->SetRootComponent(Capsule); Pawn->AddInstanceComponent(Capsule);
    Capsule->SetCapsuleSize(80,100); Capsule->SetCollisionProfileName(TEXT("Pawn"));
    Capsule->SetWorldLocation(FVector(0,0,100)); Capsule->RegisterComponent();
    auto* Cable = F.Cable(FVector(-20,0,100), FVector(20,0,100), 4);
    Cable->CableGravityScale = 0;
    const TArray<FVector> Inside = {FVector(-20,0,100), FVector(-10,0,100), FVector(0,0,100), FVector(10,0,100), FVector(20,0,100)};
    TestTrue(TEXT("Player collision filtering is enabled by default"), Cable->bIgnorePlayerCollision);
    TestTrue(TEXT("The Pawn collision channel does not push the cable while grabbing"), Cable->IsCableCollisionFree());
    Cable->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    for (int32 I = 0; I < Points.Num(); ++I) TestTrue(TEXT("Ignored pawn does not displace cable points"), Points[I].Equals(Inside[I], .001));
    Cable->bIgnorePlayerCollision = false;
    Cable->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
    Cable->GetCableParticleLocations(Points);
    TestFalse(TEXT("Opting out restores pawn contact"), Points[2].Equals(Inside[2], .1));
    auto* Controller = F.World->SpawnActor<APlayerController>();
    Controller->SetPlayerState(F.World->SpawnActor<APlayerState>());
    Controller->Possess(Pawn);
    AddInfo(FString::Printf(TEXT("Player fixture: controller=%d, possessed=%d, pawn-controller=%d"), Controller->IsPlayerController(), Controller->GetPawn() == Pawn, Pawn->GetController() == Controller));
    TestTrue(TEXT("Custom-channel fixture is player controlled"), Pawn->IsPlayerControlled());
    Capsule->SetCollisionObjectType(ECC_WorldDynamic);
    Cable->bIgnorePlayerCollision = true;
    Cable->SetCableParticleLocations(Inside);
    TestTrue(TEXT("Player pawn is ignored even with a custom object channel"), Cable->IsCableCollisionFree());
    Cable->TickComponent(1.f/60.f, LEVELTICK_All, nullptr);
    Cable->GetCableParticleLocations(Points);
    for (int32 I = 0; I < Points.Num(); ++I) TestTrue(TEXT("Enabling player-ignore clears previous cached player contacts"), Points[I].Equals(Inside[I], .001));
    F.Box(FVector(300,0,100), FVector(1,1000,1000));
    Cable->CableForce = FVector(5000000,0,0);
    TestTrue(TEXT("Ignoring players still retains blocking wall collision"), F.Step(Cable, 12, 1.f/30.f));
    Cable->GetCableParticleLocations(Points);
    for (const FVector& P : Points) TestTrue(TEXT("The world wall still blocks the cable"), P.X < 293.51);
    return true;
}
#endif
