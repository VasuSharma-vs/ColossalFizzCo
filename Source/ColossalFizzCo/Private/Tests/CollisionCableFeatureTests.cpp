#include "Misc/AutomationTest.h"
#include "Components/CollisionCableComponent.h"
#include "Components/CableMovementConstraintComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "TimerManager.h"
#include "../Components/CollisionCableRendering.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CableFeatureTests
{
    struct FFixture
    {
        UWorld* World;
        FFixture()
        {
            const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();
        }
        ~FFixture(){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
        AActor* Actor(FVector Position)
        {
            auto* A=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(A);
            A->SetRootComponent(Root);A->AddInstanceComponent(Root);Root->SetWorldLocation(Position);Root->RegisterComponent();return A;
        }
        UBoxComponent* Box(FVector Position,FVector Extent)
        {
            auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);
            A->SetRootComponent(B);A->AddInstanceComponent(B);B->SetBoxExtent(Extent);B->SetCollisionProfileName(TEXT("BlockAll"));
            B->SetGenerateOverlapEvents(true);B->SetWorldLocation(Position);B->RegisterComponent();return B;
        }
        UCollisionCableComponent* Cable(FVector Start,FVector End,int32 Segments)
        {
            auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UCollisionCableComponent>(A);
            A->SetRootComponent(C);A->AddInstanceComponent(C);C->SetWorldLocation(Start);
            C->NumSegments=Segments;C->CableLength=FVector::Distance(Start,End);C->EndLocation=End-Start;
            C->bAttachStart=C->bAttachEnd=false;C->bEnableCollision=false;C->bSmoothCollisionRecovery=false;
            C->bEnableSleeping=false;C->bEnablePointWeight=false;C->bEnableGrabbing=false;C->bEnableMovementRestrictions=false;
            C->bEnableVisualSubdivision=false;C->bEnableNetworkSync=false;C->CableGravityScale=0;C->Damping=0;
            C->CollisionMethod=ECableCollisionMethod::Sweeps;C->CollisionPointsPerFrame=Segments+1;C->RegisterComponent();
            if(!C->HasBegunPlay())C->BeginPlay();return C;
        }
        void Step(UCollisionCableComponent* C,int32 Frames,float Dt=1.f/60.f){for(int32 I=0;I<Frames;++I)C->TickComponent(Dt,LEVELTICK_All,nullptr);}
        TArray<FVector> Points(UCollisionCableComponent* C){TArray<FVector>P;C->GetCableParticleLocations(P);return P;}
    };
    constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableFeatureSwitchTest,"ColossalFizzCo.Cable.Features.Switches",CableFeatureTests::Flags)
bool FCableFeatureSwitchTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(0,0,100),FVector(100,0,100),4);
    const ECableFeature Features[]={ECableFeature::WorldCollision,ECableFeature::SelfCollision,ECableFeature::Sleeping,ECableFeature::PointWeight,ECableFeature::GrabbingAndPulling,ECableFeature::MovementRestrictions,ECableFeature::VisualSubdivision};
    auto State=[&](int32 I){switch(I){case 0:return C->bEnableCollision;case 1:return C->bEnableSelfCollision;case 2:return C->bEnableSleeping;case 3:return C->bEnablePointWeight;case 4:return C->bEnableGrabbing;case 5:return C->bEnableMovementRestrictions;default:return C->bEnableVisualSubdivision;}};
    for(int32 I=0;I<UE_ARRAY_COUNT(Features);++I){C->SetCableFeatureEnabled(Features[I],true);TestTrue(TEXT("Feature switch enables real component state"),State(I));C->SetCableFeatureEnabled(Features[I],false);TestFalse(TEXT("Feature switch disables independently"),State(I));}
    auto* Participant=F.Actor(FVector(50,0,100));FCableGrabSettings Settings;
    TestFalse(TEXT("The single grabbing/pulling switch blocks grab requests when off"),C->BeginPointGrab(Participant,2,FVector(50,100,100),Settings));
    C->SetCableFeatureEnabled(ECableFeature::GrabbingAndPulling,true);
    TestTrue(TEXT("Enabling grabbing also enables its pull state"),C->BeginPointGrab(Participant,2,FVector(50,100,100),Settings));
    C->SetCableFeatureEnabled(ECableFeature::GrabbingAndPulling,false);
    TestEqual(TEXT("Disabling the combined feature releases all contributors"),C->GetActiveGrabs().Num(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableSelfCollisionTest,"ColossalFizzCo.Cable.Features.SelfCollision",CableFeatureTests::Flags)
bool FCableSelfCollisionTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(-100,0,0),FVector(0,-100,0),4);
    C->CableLength=600;C->SolverIterations=1;C->SelfCollisionThickness=10;C->SelfCollisionNeighborExclusion=2;C->SelfCollisionIterations=3;
    C->SetCableParticleLocations({FVector(-100,0,0),FVector(100,0,0),FVector(100,100,0),FVector(0,100,0),FVector(0,-100,0)});
    C->SetCableFeatureEnabled(ECableFeature::SelfCollision,true);F.Step(C,1,1.f/120.f);
    auto P=F.Points(C);FVector A,B;FMath::SegmentDistToSegmentSafe(P[0],P[1],P[3],P[4],A,B);
    TestTrue(TEXT("Non-neighboring crossing spans separate by the configured thickness"),FVector::Distance(A,B)>9.5);
    TestTrue(TEXT("Self collision performs its own geometric checks"),C->SelfCollisionPairChecks>0);
    TestEqual(TEXT("Self collision does not require world collision queries"),C->CollisionQueryCount,0);
    C->SetCableFeatureEnabled(ECableFeature::SelfCollision,false);F.Step(C,1);
    TestEqual(TEXT("Self collision has no pair checks while disabled"),C->SelfCollisionPairChecks,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableSleepWakeTest,"ColossalFizzCo.Cable.Features.SleepWake",CableFeatureTests::Flags)
bool FCableSleepWakeTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* Floor=F.Box(FVector(0,0,-10),FVector(500,500,10));
    auto* C=F.Cable(FVector(-50,0,5.51),FVector(50,0,5.51),8);
    C->bEnableCollision=true;C->bSmoothCollisionRecovery=true;C->Damping=2;C->CableGravityScale=1;C->SleepDelay=.05f;C->SleepConstraintTolerance=.5;
    C->SetCableFeatureEnabled(ECableFeature::Sleeping,true);F.Step(C,120);
    TestTrue(TEXT("A settled floor cable reaches full sleep"),C->bCableSleeping);
    TestFalse(TEXT("Full sleep disables the actual simulation tick"),C->IsComponentTickEnabled());
    TestEqual(TEXT("The asleep cable reports no collision queries"),C->CollisionQueryCount,0);
    const int32 Revision=C->RenderGeometryRevision;F.Step(C,30);
    TestTrue(TEXT("Persistent floor contact does not wake the cable"),C->bCableSleeping);
    TestEqual(TEXT("Sleeping geometry is not republished"),C->RenderGeometryRevision,Revision);
    Floor->SetWorldLocation(FVector(0,0,-5));
    TestTrue(TEXT("Support transform event wakes without a simulation tick"),C->IsComponentTickEnabled()&&!C->bCableSleeping);
    F.Step(C,120);
    if(!C->bCableSleeping)
    {
        const auto Before=F.Points(C);F.Step(C,1);const auto P=F.Points(C);
        AddInfo(FString::Printf(TEXT("Moved-floor sleep diagnostics: sleeping=%d unresolved=%d queries=%d"),C->SleepingPointCount,C->bHasUnresolvedCollision,C->CollisionQueryCount));
        for(int32 I=0;I<P.Num();++I)AddInfo(FString::Printf(TEXT("Point %d %s sleeping=%d displacement=%g span=%g"),I,*P[I].ToCompactString(),C->IsPointSleeping(I),FVector::Distance(P[I],Before[I]),I?FVector::Distance(P[I],P[I-1]):0));
    }
    TestTrue(TEXT("Cable settles again on the moved support"),C->bCableSleeping);
    TestTrue(TEXT("Moved support does not leave sleeping particles embedded in its new surface"),C->IsCableCollisionFree());
    for(const FVector& Point:F.Points(C))TestTrue(TEXT("Cached floor planes follow the actual moved floor height"),Point.Z>=10.49);
    auto* Impact=F.Box(FVector(1000,0,15),FVector(8));Impact->SetWorldLocation(FVector(0,0,12));
    C->CheckForWakeSources();
    TestTrue(TEXT("A nearby new collider wakes the cable"),!C->bCableSleeping);
    Impact->DestroyComponent();F.Step(C,180);
    Floor->DestroyComponent();C->CheckForWakeSources();
    TestTrue(TEXT("Support deletion wakes retained contacts"),!C->bCableSleeping&&C->IsComponentTickEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableNeighborWakeTest,"ColossalFizzCo.Cable.Features.NeighborWakeAndRelease",CableFeatureTests::Flags)
bool FCableNeighborWakeTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(-50,0,100),FVector(50,0,100),8);
    C->SleepDelay=.05f;C->SetCableFeatureEnabled(ECableFeature::Sleeping,true);F.Step(C,20);
    TestTrue(TEXT("Free, force-free points can sleep"),C->bCableSleeping);
    const FVector OriginalEnd=F.Points(C)[0];C->AddImpulseToPoint(4,FVector(100,0,0));bool EndWoke=false;double EndMotion=0;
    for(int32 Frame=0;Frame<10;++Frame){F.Step(C,1);EndWoke|=!C->IsPointSleeping(0);EndMotion=FMath::Max(EndMotion,FVector::Distance(F.Points(C)[0],OriginalEnd));}
    TestTrue(TEXT("A disturbed section wakes the endpoint before it may settle again"),EndWoke);
    TestTrue(TEXT("The formerly sleeping endpoint moves rather than remaining an anchor"),EndMotion>.005);
    C->SetCableFeatureEnabled(ECableFeature::GrabbingAndPulling,true);
    auto* Actor=F.Actor(F.Points(C)[4]);FCableGrabSettings S;S.InfluenceRadius=30;
    TestTrue(TEXT("A middle point can be grabbed"),C->BeginPointGrab(Actor,4,Actor->GetActorLocation()+FVector(0,100,0),S));
    F.Step(C,30);auto Before=F.Points(C);TestTrue(TEXT("Releasing an active participant succeeds"),C->EndPointGrab(Actor));auto After=F.Points(C);
    for(int32 I=0;I<After.Num();++I)TestTrue(TEXT("Release preserves current point state instead of resetting the cable"),After[I].Equals(Before[I],.0001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableMovingSupportAwakeTest,"ColossalFizzCo.Cable.Features.MovingSupportWithSleepingDisabled",CableFeatureTests::Flags)
bool FCableMovingSupportAwakeTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* Floor=F.Box(FVector(0,0,-10),FVector(500,500,10));
    auto* C=F.Cable(FVector(-50,0,5.51),FVector(50,0,5.51),8);
    C->bEnableCollision=true;C->bSmoothCollisionRecovery=true;C->CableGravityScale=1;C->Damping=2;
    F.Step(C,120);Floor->SetWorldLocation(FVector(0,0,-5));F.Step(C,180);
    TestFalse(TEXT("Sleeping remains independently disabled"),C->bCableSleeping);
    TestTrue(TEXT("World collision retains moved support planes with sleeping switched off"),C->IsCableCollisionFree());
    for(const auto& Point:F.Points(C))TestTrue(TEXT("Awake cable recovers above the moved floor"),Point.Z>=10.49);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableStaggeredSleepTest,"ColossalFizzCo.Cable.Features.StaggeredFloorSleeping",CableFeatureTests::Flags)
bool FCableStaggeredSleepTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;F.Box(FVector(0,0,-10),FVector(500,500,10));
    auto* C=F.Cable(FVector(-200,0,5.51),FVector(200,0,5.51),11);
    C->bEnableCollision=true;C->bSmoothCollisionRecovery=false;C->CableGravityScale=1;C->Damping=4;C->SleepDelay=.3f;
    TArray<FVector> Seed;for(int32 I=0;I<12;++I)Seed.Add(FVector(-200+400.*I/11,0,5.51+(I%3)*.25));
    C->SetCableParticleLocations(Seed);C->SetCableFeatureEnabled(ECableFeature::Sleeping,true);
    const float Times[]={1.f/180,1.f/90,1.f/50};
    for(int32 Frame=0;Frame<600;++Frame)F.Step(C,1,Times[Frame%3]);
    TestTrue(TEXT("Points settling at different times can reach complete floor sleep"),C->bCableSleeping);
    TestEqual(TEXT("Blocked gravity does not repeatedly wake neighboring points"),C->SleepingPointCount,12);
    TestTrue(TEXT("The staggered floor cable remains collision free"),C->IsCableCollisionFree());
    TestFalse(TEXT("Settled simulation tick is disabled"),C->IsComponentTickEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableUnresolvedAnchorSleepTest,"ColossalFizzCo.Cable.Features.UnresolvedAnchorsPreventSleep",CableFeatureTests::Flags)
bool FCableUnresolvedAnchorSleepTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector::ZeroVector,FVector(100,0,0),1);
    C->bAttachStart=C->bAttachEnd=true;C->CableLength=50;C->SleepDelay=.05f;C->ResetCable();
    C->SetCableFeatureEnabled(ECableFeature::Sleeping,true);F.Step(C,60);
    TestFalse(TEXT("Quiet fixed points do not hide an unresolved length constraint"),C->bCableSleeping);
    TestTrue(TEXT("The unresolved cable keeps its simulation tick"),C->IsComponentTickEnabled());
    C->EndLocation=FVector(50,0,0);F.Step(C,60);
    TestTrue(TEXT("The cable may sleep after its anchor separation becomes valid"),C->bCableSleeping);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableSilentColliderWakeTest,"ColossalFizzCo.Cable.Features.SleepWakeWithoutOverlapEvents",CableFeatureTests::Flags)
namespace CableFeatureTests
{
    class FWakeTimerCommand:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;TUniquePtr<FFixture> F;
        UCollisionCableComponent* C=nullptr;UBoxComponent* Floor=nullptr;UBoxComponent* Incoming=nullptr;
        int32 Phase=0,Before=0;
    public:
        explicit FWakeTimerCommand(FAutomationTestBase* InTest):Test(InTest){}
        virtual bool Update() override
        {
            if(Phase==0)
            {
                F=MakeUnique<FFixture>();Floor=F->Box(FVector(0,0,-10),FVector(500,500,10));Floor->SetGenerateOverlapEvents(false);
                C=F->Cable(FVector(-50,0,5.51),FVector(50,0,5.51),8);C->bEnableCollision=true;C->bSmoothCollisionRecovery=true;C->Damping=2;C->CableGravityScale=1;C->SleepDelay=.05f;
                C->SetCableFeatureEnabled(ECableFeature::Sleeping,true);F->Step(C,120);
                Test->TestTrue(TEXT("Floor contact sleeps without generating overlaps"),C->bCableSleeping);
                Test->TestTrue(TEXT("The registered game component has begun play"),C->HasBegunPlay());
                Incoming=F->Box(FVector(1000,0,12),FVector(8));Incoming->SetGenerateOverlapEvents(false);Incoming->SetWorldLocation(FVector(0,0,12));
                Before=C->WakeQueryCount;F->World->GetTimerManager().Tick(0);Phase=1;return false;
            }
            if(Phase==1)
            {
                // Advance on a new engine frame: pending timers are promoted at
                // the end of their first timer-manager tick, not immediately.
                F->World->GetTimerManager().Tick(.3f);
                Test->TestTrue(TEXT("Independent low-rate wake query runs with simulation ticking disabled"),C->WakeQueryCount>Before);
                Test->TestTrue(TEXT("A blocking mesh wakes the cable without overlap events"),!C->bCableSleeping&&C->IsComponentTickEnabled());
                Incoming->DestroyComponent();F->Step(C,240);
                Test->TestTrue(TEXT("Cable can settle again after the incoming object is removed"),C->bCableSleeping);
                Floor->DestroyComponent();Phase=2;return false;
            }
            F->World->GetTimerManager().Tick(.3f);
            Test->TestTrue(TEXT("Independent timer wakes the cable after support deletion"),!C->bCableSleeping&&C->IsComponentTickEnabled());
            return true;
        }
    };
}
bool FCableSilentColliderWakeTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(CableFeatureTests::FWakeTimerCommand(this));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableMassTest,"ColossalFizzCo.Cable.Features.PointMass",CableFeatureTests::Flags)
bool FCableMassTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* Light=F.Cable(FVector(0,0,100),FVector(10,0,100),1);auto* Heavy=F.Cable(FVector(100,0,100),FVector(110,0,100),1);
    Light->bEnablePointWeight=Heavy->bEnablePointWeight=true;Heavy->PointMassKg=10;
    for(int32 I=0;I<2;++I){Light->AddImpulseToPoint(I,FVector(0,100,0));Heavy->AddImpulseToPoint(I,FVector(0,100,0));}
    F.Step(Light,1);F.Step(Heavy,1);
    TestTrue(TEXT("Equal impulses move a heavy cable less"),F.Points(Light)[0].Y>F.Points(Heavy)[0].Y*5);
    Heavy->SetCableFeatureEnabled(ECableFeature::PointWeight,false);TestEqual(TEXT("Weight toggle restores unit mass"),Heavy->GetPointMass(0),1.f);
    auto* Weighted=F.Cable(FVector(0,200,100),FVector(10,200,100),1);Weighted->bEnablePointWeight=true;Weighted->SetPointMass(0,10);Weighted->SetPointMass(1,1);
    Weighted->SetCableParticleLocations({FVector(0,200,100),FVector(20,200,100)});Weighted->SolverIterations=1;F.Step(Weighted,1,1.f/120.f);
    auto P=F.Points(Weighted);TestTrue(TEXT("Distance constraints use inverse mass rather than equal corrections"),(20-P[1].X)>P[0].X*5);
    Weighted->SetPointFixed(0,true);const FVector Fixed=F.Points(Weighted)[0];
    TestFalse(TEXT("Intentionally fixed is distinct from heavy"),Weighted->AddImpulseToPoint(0,FVector(100,0,0)));
    F.Step(Weighted,1);TestTrue(TEXT("A fixed point stays fixed"),F.Points(Weighted)[0].Equals(Fixed,.001));
    TestFalse(TEXT("Zero mass does not silently create an anchor"),Weighted->SetPointMass(1,0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableCombinedGrabTest,"ColossalFizzCo.Cable.Features.CombinedGrabInfluence",CableFeatureTests::Flags)
bool FCableCombinedGrabTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(0,0,100),FVector(110,0,100),11);
    C->bEnableGrabbing=true;C->bEnablePointWeight=true;auto* P1=F.Actor(FVector(50,0,100));auto* P2=F.Actor(FVector(50,0,100));auto* Vehicle=F.Actor(FVector(50,0,100));
    FCableGrabSettings S;S.InfluenceRadius=30;S.MovementRate=3;const FVector Goal(50,100,100);
    TestTrue(TEXT("First participant grabs the sixth point"),C->BeginPointGrab(P1,5,Goal,S));
    TArray<FVector> V;C->GetDirectGrabVelocities(V);
    const double Expected[7]={0,1,2,3,2,1,0};for(int32 I=0;I<7;++I)TestTrue(TEXT("Influence follows cable distance and the requested linear falloff"),FMath::IsNearlyEqual(V[I+2].Y,Expected[I],.001));
    C->BeginPointGrab(P2,5,Goal,S);C->GetDirectGrabVelocities(V);TestTrue(TEXT("Two 3 cm/s players combine to 6 cm/s"),FMath::IsNearlyEqual(V[5].Y,6.,.001));
    S.MovementRate=15;C->BeginPointGrab(Vehicle,5,Goal,S);C->GetDirectGrabVelocities(V);TestTrue(TEXT("Two players and a 15 cm/s vehicle combine to 21 cm/s"),FMath::IsNearlyEqual(V[5].Y,21.,.001));
    C->EndPointGrab(Vehicle);C->UpdatePointGrab(P2,FVector(50,-100,100));C->GetDirectGrabVelocities(V);TestTrue(TEXT("Opposing equal contributors cancel directionally"),V[5].IsNearlyZero(.001));
    C->EndPointGrab(P2);C->SetPointMass(5,10);C->GetDirectGrabVelocities(V);TestTrue(TEXT("Mass changes the achieved drive rate"),FMath::IsNearlyEqual(V[5].Y,.3,.001));
    C->AddImpulseToPoint(0,FVector(0,0,100));const FVector Before=F.Points(C)[0];F.Step(C,1);
    TestFalse(TEXT("Zero direct influence does not freeze points outside its radius"),F.Points(C)[0].Equals(Before,.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableWeightedPullTest,"ColossalFizzCo.Cable.Features.WeightedPullMovement",CableFeatureTests::Flags)
bool FCableWeightedPullTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;
    auto* Light=F.Cable(FVector(0,0,100),FVector(10,0,100),1);
    auto* Heavy=F.Cable(FVector(100,0,100),FVector(110,0,100),1);
    Light->bEnableGrabbing=Heavy->bEnableGrabbing=true;Light->bEnablePointWeight=Heavy->bEnablePointWeight=true;Heavy->PointMassKg=10;
    Light->bEnableMovementRestrictions=Heavy->bEnableMovementRestrictions=true;
    auto* P1=F.Actor(FVector(0,0,100));auto* P2=F.Actor(FVector(100,0,100));FCableGrabSettings S;S.InfluenceRadius=1000;
    Light->BeginPointGrab(P1,0,P1->GetActorLocation(),S);Heavy->BeginPointGrab(P2,0,P2->GetActorLocation(),S);
    auto Pull=[&](UCollisionCableComponent* Cable,AActor* Participant)
    {
        for(int32 I=0;I<120;++I)
        {
            const FVector Old=Participant->GetActorLocation(),Requested=Old+FVector(0,10,0);
            Participant->SetActorLocation(Requested);
            Participant->SetActorLocation(Cable->ConstrainDriverLocation(Participant,Old,Requested));F.Step(Cable,1);
        }
    };
    Pull(Light,P1);Pull(Heavy,P2);
    TestTrue(TEXT("Finite force and mass affect actual simulated pull displacement"),F.Points(Light)[0].Y>F.Points(Heavy)[0].Y*5);
    TestTrue(TEXT("Heavy rope resistance limits actual participant progress"),P1->GetActorLocation().Y>P2->GetActorLocation().Y+2);
    TestTrue(TEXT("Free cable can move with its puller instead of becoming a fixed world anchor"),F.Points(Heavy)[0].Y>0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableOwnedInteractionTest,"ColossalFizzCo.Cable.Features.OwnedInteractionGuards",CableFeatureTests::Flags)
bool FCableOwnedInteractionTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(0,0,100),FVector(100,0,100),10);C->bEnableGrabbing=true;
    auto* Player=F.Actor(FVector(50,-100,100));auto* Interaction=NewObject<UShopPlayerInteractionComponent>(Player);
    Player->AddInstanceComponent(Interaction);Interaction->RegisterComponent();
    auto* Wall=F.Box(FVector(50,-50,100),FVector(30,5,30));
    TestFalse(TEXT("Owned interaction refuses a grab through a blocking wall"),Interaction->RequestCableGrab(C,FVector(50,0,100),FVector(50,0,100)));
    Wall->DestroyComponent();C->DefaultGrabSettings.MovementRate=7;
    TestTrue(TEXT("The existing owned interaction component can grab a middle point"),Interaction->RequestCableGrab(C,FVector(50,0,100),FVector(50,0,100)));
    const auto Grabs=C->GetActiveGrabs();TestEqual(TEXT("One authoritative participant is registered"),Grabs.Num(),1);
    if(!Grabs.IsEmpty())
    {
        TestEqual(TEXT("Server chooses the nearest middle point"),Grabs[0].PointIndex,5);
        TestEqual(TEXT("The cable's exposed default settings are used by player interaction"),Grabs[0].Settings.MovementRate,7.f);
    }
    Interaction->RequestCableRelease();TestEqual(TEXT("Owned release clears server state"),C->GetActiveGrabs().Num(),0);
    Interaction->bUseCableDefaultGrabSettings=false;Interaction->CableGrabSettings.MovementRate=15;
    TestTrue(TEXT("Participants may use server-configured strength overrides"),Interaction->RequestCableGrab(C,FVector(50,0,100),FVector(50,0,100)));
    const auto VehicleGrab=C->GetActiveGrabs();if(!VehicleGrab.IsEmpty())TestEqual(TEXT("Participant override reaches the real grab solver"),VehicleGrab[0].Settings.MovementRate,15.f);
    Interaction->RequestCableRelease();
    TestFalse(TEXT("Out-of-reach requests are rejected"),Interaction->RequestCableGrab(C,FVector(5000,0,100),FVector(5000,0,100)));
    C->SetCableFeatureEnabled(ECableFeature::GrabbingAndPulling,false);
    TestFalse(TEXT("Feature switch blocks the shared interaction entry point"),Interaction->RequestCableGrab(C,FVector(50,0,100),FVector(50,0,100)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableMovementLimitTest,"ColossalFizzCo.Cable.Features.HeldNozzleMovementLimits",CableFeatureTests::Flags)
bool FCableMovementLimitTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector::ZeroVector,FVector(50,0,0),2);
    auto* Character=F.World->SpawnActor<ACharacter>();Character->GetRootComponent()->SetWorldLocation(FVector(50,0,0));
    if(auto* Capsule=Cast<UPrimitiveComponent>(Character->GetRootComponent()))Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    auto* Nozzle=NewObject<USceneComponent>(C->GetOwner());C->GetOwner()->AddInstanceComponent(Nozzle);
    Nozzle->SetupAttachment(Character->GetRootComponent());Nozzle->RegisterComponent();
    C->bAttachStart=C->bAttachEnd=true;C->CableLength=100;C->EndLocation=FVector::ZeroVector;C->SetAttachEndToComponent(Nozzle);C->ResetCable();
    C->SetCableFeatureEnabled(ECableFeature::MovementRestrictions,true);
    TestTrue(TEXT("Driver resolution follows a held component beyond its immediate owner"),C->ResolveMovementDriver(Nozzle)==Character);
    auto Move=[&](FVector Old,FVector Requested)
    {Character->SetActorLocation(Old);C->ResetCable();Character->SetActorLocation(Requested);Character->GetCharacterMovement()->Velocity=(Requested-Old)*60;
     Character->OnCharacterMovementUpdated.Broadcast(1.f/60.f,Old,FVector::ZeroVector);return Character->GetActorLocation();};
    TestTrue(TEXT("A holder can use available horizontal slack freely"),Move(FVector(50,0,0),FVector(75,0,0)).Equals(FVector(75,0,0),.01));
    TestTrue(TEXT("Actual character movement stops at the horizontal length limit"),Move(FVector(50,0,0),FVector(150,0,0)).X<=102.1);
    TestTrue(TEXT("Falling is restricted at the vertical length limit"),Move(FVector(0,0,-50),FVector(0,0,-150)).Z>=-102.1);
    TestTrue(TEXT("Blocked falling velocity is removed"),Character->GetCharacterMovement()->Velocity.Z>=-.01);
    C->SetCableFeatureEnabled(ECableFeature::MovementRestrictions,false);
    TestTrue(TEXT("Disabling restrictions releases the actual movement callback"),Move(FVector(50,0,0),FVector(150,0,0)).Equals(FVector(150,0,0),.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableFreeEndsTest,"ColossalFizzCo.Cable.Features.FreeEndsNoWorldAnchor",CableFeatureTests::Flags)
bool FCableFreeEndsTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(0,0,100),FVector(100,0,100),4);C->bEnableMovementRestrictions=true;
    auto* Actor=F.Actor(FVector(50,0,100));
    TestTrue(TEXT("Both unattached ends introduce no world-space movement limit"),C->ConstrainDriverLocation(Actor,FVector(50,0,100),FVector(500,0,-500)).Equals(FVector(500,0,-500),.001));
    C->bEnableGrabbing=true;FCableGrabSettings S;S.InfluenceRadius=200;C->BeginPointGrab(Actor,2,FVector(50,0,100),S);
    const FVector First=C->ConstrainDriverLocation(Actor,Actor->GetActorLocation(),FVector(50,500,100));
    Actor->SetActorLocation(FVector(50,30,100));
    for(int32 I=0;I<5;++I)C->AddImpulseToPoint(I,FVector(0,30,0));F.Step(C,30);
    const FVector Later=C->ConstrainDriverLocation(Actor,Actor->GetActorLocation(),FVector(50,500,100));
    TestTrue(TEXT("Grab resistance follows the moving free cable instead of anchoring it to the world"),Later.Y>First.Y+1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableSubdivisionTest,"ColossalFizzCo.Cable.Features.RenderSubdivision",CableFeatureTests::Flags)
bool FCableSubdivisionTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(0,0,100),FVector(110,0,100),11);
    auto Sim=F.Points(C);Sim[5].Z+=20;C->SetCableParticleLocations(Sim);C->VisualSubdivisions=4;C->bLimitVisualDeviation=true;C->MaxVisualDeviation=1;
    C->SetCableFeatureEnabled(ECableFeature::VisualSubdivision,true);TArray<FVector> Render;C->GetCableRenderLocations(Render);
    TestEqual(TEXT("Four render samples per span give 45 visual points"),Render.Num(),45);
    TestEqual(TEXT("Simulation remains at twelve points"),F.Points(C).Num(),12);
    TestEqual(TEXT("Cross-section sides are independent"),C->NumSides,8);
    for(int32 I=0;I+1<Render.Num();++I)
    {const int32 Span=I/4;TestTrue(TEXT("Visual smoothing stays close to its simulated span"),FVector::Distance(Render[I],FMath::ClosestPointOnSegment(Render[I],Sim[Span],Sim[Span+1]))<=1.001);}
    C->SetCableFeatureEnabled(ECableFeature::VisualSubdivision,false);C->GetCableRenderLocations(Render);
    TestEqual(TEXT("The visual switch restores the unsplit path"),Render.Num(),12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableSmoothBendTest,"ColossalFizzCo.Cable.Rendering.RoundedBends",CableFeatureTests::Flags)
bool FCableSmoothBendTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(150,0,100),FVector(150,-50,100),11);
    TArray<FVector> Sim;for(int32 I=0;I<12;++I){const double A=2*PI*I/12;Sim.Add(FVector(150*FMath::Cos(A),150*FMath::Sin(A),100));}
    C->CableLength=0;for(int32 I=1;I<Sim.Num();++I)C->CableLength+=FVector::Distance(Sim[I-1],Sim[I]);
    C->SetCableParticleLocations(Sim);C->VisualSubdivisions=8;C->VisualSmoothing=1;C->bEnableCollision=true;
    C->SetCableFeatureEnabled(ECableFeature::VisualSubdivision,true);
    TArray<FVector> Curve;C->GetCableRenderLocations(Curve);
    TestEqual(TEXT("Eight visual intervals produce 89 render samples from 12 simulation points"),Curve.Num(),89);
    double MaxBow=0;
    for(int32 I=0;I+1<Curve.Num();++I)
    {const int32 Span=I/8;MaxBow=FMath::Max(MaxBow,FVector::Distance(Curve[I],FMath::ClosestPointOnSegment(Curve[I],Sim[Span],Sim[Span+1])));}
    TestTrue(TEXT("World collision no longer flattens a visible bend to the old 0.4 cm skin limit"),MaxBow>2);
    for(int32 I=1;I+1<Sim.Num();++I)
    {
        const int32 Knot=I*8;
        TestTrue(TEXT("The curve passes through each real simulation point"),Curve[Knot].Equals(Sim[I],.0001));
        const FVector In=(Curve[Knot]-Curve[Knot-1]).GetSafeNormal(),Out=(Curve[Knot+1]-Curve[Knot]).GetSafeNormal();
        TestTrue(TEXT("Neighboring visual spans smoothly share the particle tangent"),FVector::DotProduct(In,Out)>.98);
    }
    const FBox Bounds=C->CalcBounds(C->GetComponentTransform()).GetBox();
    for(const auto& P:Curve)TestTrue(TEXT("Render bounds include the rounded curve"),Bounds.IsInsideOrOn(P));
    Sim[5].Z+=10;C->SetCableParticleLocations(Sim);TArray<FVector> Moved;C->GetCableRenderLocations(Moved);
    TestFalse(TEXT("Visual points between particles follow a moved simulation point"),Moved[5*8-4].Equals(Curve[5*8-4],.001));
    TestEqual(TEXT("Rounding and movement add no simulation points"),F.Points(C).Num(),12);
    C->VisualSmoothing=0;C->SetCableFeatureEnabled(ECableFeature::VisualSubdivision,true);C->GetCableRenderLocations(Curve);
    for(int32 I=0;I+1<Curve.Num();++I){const int32 Span=I/8;TestTrue(TEXT("Zero smoothing deliberately restores straight spans"),Curve[I].Equals(FMath::Lerp(Sim[Span],Sim[Span+1],double(I%8)/8),.0001));}
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableTubeWindingTest,"ColossalFizzCo.Cable.Rendering.OutwardWindingAndTangents",CableFeatureTests::Flags)
bool FCableTubeWindingTest::RunTest(const FString& Parameters)
{
    const TArray<FVector> Points={FVector::ZeroVector,FVector(100,0,0)};
    TArray<FDynamicMeshVertex> Vertices;TArray<uint32> Indices;
    CollisionCableRendering::BuildMeshData(Points,10,8,1,Vertices,Indices);
    TestEqual(TEXT("A real proxy mesh contains two nine-vertex rings"),Vertices.Num(),18);
    TestEqual(TEXT("Eight tube sides have sixteen triangles"),Indices.Num(),48);
    for(int32 I=0;I<Vertices.Num();++I)
    {
        const auto& V=Vertices[I];const FVector Radial=(FVector(V.Position)-Points[I/9]).GetSafeNormal();
        TestTrue(TEXT("Shading normals point away from the centerline"),FVector::DotProduct(FVector(V.TangentZ.ToFVector3f()),Radial)>.99);
        const FVector ExpectedV=FVector::CrossProduct(FVector::ForwardVector,Radial);
        TestTrue(TEXT("The texture V tangent follows increasing ring UVs"),FVector::DotProduct(FVector(V.GetTangentY()),ExpectedV)>.99);
    }
    for(int32 I=0;I<Indices.Num();I+=3)
    {
        const FVector A(Vertices[Indices[I]].Position),B(Vertices[Indices[I+1]].Position),C(Vertices[Indices[I+2]].Position);
        const FVector Center=(A+B+C)/3,Radial=Center-FVector(Center.X,0,0);
        TestTrue(TEXT("The actual mesh uses Unreal clockwise exterior face winding"),FVector::DotProduct(FVector::CrossProduct(B-A,C-A),Radial)<0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableFullQueryModeTest,"ColossalFizzCo.Cable.CollisionQuality.FullAndBudgeted",CableFeatureTests::Flags)
bool FCableFullQueryModeTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;
    auto Make=[&](float Y,bool Full,int32 Interval)
    {
        auto* C=F.Cable(FVector(-100,Y,100),FVector(100,Y,100),8);C->bEnableCollision=true;
        C->CollisionMethod=ECableCollisionMethod::LineTraces;C->CollisionPointsPerFrame=1;
        C->bUseFullCollisionQueries=Full;C->FullCollisionSolverInterval=Interval;F.Step(C,1);return C;
    };
    auto* Full=Make(0,true,1);auto* Reduced=Make(50,true,8);auto* Budget=Make(100,false,1);
    TestEqual(TEXT("Full quality checks every awake point despite a saved one-point budget"),Full->LastCollisionPointIndices.Num(),9);
    TestEqual(TEXT("The reduced full mode still covers every awake point"),Reduced->LastCollisionPointIndices.Num(),9);
    TestEqual(TEXT("Lower CPU mode keeps its requested rotating point budget"),Budget->LastCollisionPointIndices.Num(),1);
    TestEqual(TEXT("Full mode preserves the stored lower-CPU point budget"),Full->CollisionPointsPerFrame,1);
    TestTrue(TEXT("Increasing the full solver interval measurably reduces query work"),Full->CollisionQueryCount>Reduced->CollisionQueryCount);
    TestTrue(TEXT("The budgeted option uses substantially fewer queries"),Reduced->CollisionQueryCount>Budget->CollisionQueryCount*5);
    Full->bUseFullCollisionQueries=false;F.Step(Full,1);
    TestEqual(TEXT("Switching full mode off restores the original budget"),Full->LastCollisionPointIndices.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableFullQueryWallTest,"ColossalFizzCo.Cable.CollisionQuality.FullSweptThinWall",CableFeatureTests::Flags)
bool FCableFullQueryWallTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;F.Box(FVector(0,0,100),FVector(1,500,500));
    auto* C=F.Cable(FVector(-100,-50,100),FVector(-100,50,100),8);
    C->bEnableCollision=true;C->bUseFullCollisionQueries=true;C->CollisionPointsPerFrame=1;
    C->CollisionMethod=ECableCollisionMethod::LineTraces; // Full mode must use thickness sweeps anyway.
    for(int32 I=0;I<9;++I)C->AddImpulseToPoint(I,FVector(60000,0,0));
    F.Step(C,4,1.f/30.f);
    for(const auto& P:F.Points(C))TestTrue(TEXT("Full swept motion stops every point before a thin wall at high speed"),P.X<=-6.49);
    TestTrue(TEXT("The whole simulated tube remains clear of the wall"),C->IsCableCollisionFree());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableVisualCollisionTest,"ColossalFizzCo.Cable.CollisionQuality.SmoothedPathCollision",CableFeatureTests::Flags)
bool FCableVisualCollisionTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* Obstacle=F.Box(FVector(-37.5,10.15625,100),FVector(.2,.2,5));
    auto* C=F.Cable(FVector(-100,0,100),FVector(50,50,100),3);C->bEnableCollision=true;C->CableWidth=.2;C->CollisionSkin=.05;
    C->VisualSubdivisions=8;C->bEnableVisualSubdivision=true;
    const TArray<FVector> Sim={FVector(-100,0,100),FVector(-50,0,100),FVector(0,50,100),FVector(50,50,100)};
    C->SetCableParticleLocations(Sim);TArray<FVector> Raw;C->GetCableRenderLocations(Raw);
    TestTrue(TEXT("The simulated straight spans clear the obstacle"),C->IsCableCollisionFree());
    TestTrue(TEXT("The fixture places an obstacle exactly on the freely rounded curve"),Raw[10].Equals(Obstacle->GetComponentLocation(),.001));
    C->bUseFullCollisionQueries=true;C->SetCableFeatureEnabled(ECableFeature::WorldCollision,true);
    TArray<FVector> Safe;C->GetCableRenderLocations(Safe);
    TestFalse(TEXT("Full quality reduces a blocked curve's bow instead of cutting the obstacle"),Safe[10].Equals(Raw[10],.01));
    TestTrue(TEXT("Visual path protection performs actual shape queries"),C->RenderCollisionQueryCount>0);
    const int32 Queries=C->RenderCollisionQueryCount;
    for(int32 I=0;I<10;++I)C->GetCableRenderLocations(Safe);
    TestEqual(TEXT("Render getters and additional views reuse validated geometry without querying"),C->RenderCollisionQueryCount,Queries);
    for(int32 I=0;I<Sim.Num();++I)TestTrue(TEXT("Visual collision does not move simulation points"),F.Points(C)[I].Equals(Sim[I],.0001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableSnapshotTest,"ColossalFizzCo.Cable.Features.SnapshotBudget",CableFeatureTests::Flags)
bool FCableSnapshotTest::RunTest(const FString& Parameters)
{
    CableFeatureTests::FFixture F;auto* C=F.Cable(FVector(0,0,100),FVector(100,0,100),4);C->bEnableNetworkSync=true;C->GetOwner()->SetReplicates(true);
    F.Step(C,120);const int32 First=C->SnapshotCount;F.Step(C,120);
    TestTrue(TEXT("An initial authoritative snapshot is published"),First>0);
    TestEqual(TEXT("Unchanged cable positions do not stream per-frame snapshots"),C->SnapshotCount,First);
    C->bEnableGrabbing=true;auto* A=F.Actor(FVector(50,0,100));FCableGrabSettings S;C->BeginPointGrab(A,2,FVector(50,100,100),S);
    const int32 Begin=C->SnapshotCount;F.Step(C,60);
    TestTrue(TEXT("Grab state forces an event snapshot"),Begin>First);
    TestTrue(TEXT("Moving positions obey the bounded snapshot rate"),C->SnapshotCount-Begin<=11);
    return true;
}
#endif
