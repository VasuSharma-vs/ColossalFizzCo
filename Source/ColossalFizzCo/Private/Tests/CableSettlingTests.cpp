#include "Misc/AutomationTest.h"
#include "Components/CollisionCableComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CableSettlingTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
constexpr float FrameSeconds=1.f/60.f;
enum class EQueryMode { Lines, Sweeps, Full };
const TCHAR* ModeName(EQueryMode Mode)
{
    return Mode==EQueryMode::Full ? TEXT("Full") : Mode==EQueryMode::Sweeps ? TEXT("Sweeps") : TEXT("Lines");
}

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
    UBoxComponent* Floor(FQuat Rotation=FQuat::Identity)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(2000,2000,10)); Box->SetCollisionProfileName(TEXT("BlockAll"));
        Box->SetWorldTransform(FTransform(Rotation,Rotation.RotateVector(FVector(0,0,-10)))); Box->RegisterComponent();
        return Box;
    }
    AActor* Participant(FVector Position)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root); Actor->AddInstanceComponent(Root); Root->RegisterComponent(); Actor->SetActorLocation(Position);
        return Actor;
    }
    UCollisionCableComponent* Cable(const TArray<FVector>& Seed,EQueryMode Mode=EQueryMode::Lines,bool Sleeping=false)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Result=NewObject<UCollisionCableComponent>(Actor);
        Actor->SetRootComponent(Result); Actor->AddInstanceComponent(Result); Result->SetWorldLocation(Seed[0]);
        Result->NumSegments=Seed.Num()-1; Result->CableLength=0;
        for (int32 I=1;I<Seed.Num();++I) Result->CableLength+=FVector::Distance(Seed[I-1],Seed[I]);
        Result->EndLocation=Seed.Last()-Seed[0]; Result->bAttachStart=Result->bAttachEnd=false;
        Result->bEnableSleeping=Sleeping; Result->bEnableMovementRestrictions=false;
        Result->bEnableVisualSubdivision=Result->bEnableNetworkSync=false;
        Result->bUseFullCollisionQueries=Mode==EQueryMode::Full;
        Result->CollisionMethod=Mode==EQueryMode::Sweeps ? ECableCollisionMethod::Sweeps : ECableCollisionMethod::LineTraces;
        // The rotating modes must retain contacts between visits to each point.
        Result->CollisionPointsPerFrame=3;
        Result->RegisterComponent(); if (!Result->HasBegunPlay()) Result->BeginPlay();
        Result->SetCableParticleLocations(Seed);
        return Result;
    }
    static void Step(UCollisionCableComponent* Cable,int32 Frames)
    {
        for (int32 I=0;I<Frames;++I) Cable->TickComponent(FrameSeconds,LEVELTICK_All,nullptr);
    }
    static TArray<FVector> Points(UCollisionCableComponent* Cable)
    {
        TArray<FVector> Result; Cable->GetCableParticleLocations(Result); return Result;
    }
};

double MaxDisplacement(const TArray<FVector>& Before,const TArray<FVector>& After,bool Horizontal=false)
{
    if (Before.Num()!=After.Num()) return UE_BIG_NUMBER;
    double Result=0;
    for (int32 I=0;I<Before.Num();++I)
    {
        const FVector Delta=After[I]-Before[I]; if (Delta.ContainsNaN()) return UE_BIG_NUMBER;
        Result=FMath::Max(Result,Horizontal ? Delta.Size2D() : Delta.Size());
    }
    return Result;
}

double MinSelfSeparation(const TArray<FVector>& Points,int32 NeighborExclusion)
{
    double Result=UE_BIG_NUMBER;
    for (int32 A=0;A+1<Points.Num();++A)
        for (int32 B=A+NeighborExclusion+1;B+1<Points.Num();++B)
        {
            FVector NearestA,NearestB;
            FMath::SegmentDistToSegmentSafe(Points[A],Points[A+1],Points[B],Points[B+1],NearestA,NearestB);
            Result=FMath::Min(Result,FVector::Distance(NearestA,NearestB));
        }
    return Result;
}

TArray<FVector> CrossedLoop(double Height)
{
    // Four equal 200 cm spans, with the first and fourth crossing. Equal span
    // lengths avoid mistaking relaxation of an invalid seed for contact jitter.
    const double TurnY=200-FMath::Sqrt(30000.0);
    return {FVector(-100,0,Height),FVector(100,0,Height),FVector(100,200,Height),
        FVector(0,TurnY,Height),FVector(0,TurnY-200,Height)};
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableRestingFloorDriftTest,"ColossalFizzCo.Cable.Settling.FloorDrift",CableSettlingTests::Flags)
bool FCableRestingFloorDriftTest::RunTest(const FString& Parameters)
{
    using namespace CableSettlingTests;
    for (const auto Mode:{EQueryMode::Lines,EQueryMode::Sweeps,EQueryMode::Full})
    {
        FFixture F; if (!F.World) return false; F.Floor();
        TArray<FVector> Seed; for (int32 I=0;I<9;++I) Seed.Add(FVector(-120+30*I,(I%2 ? 8 : -8),40));
        auto* Cable=F.Cable(Seed,Mode); F.Step(Cable,360);
        const auto Before=F.Points(Cable); F.Step(Cable,120); const auto After=F.Points(Cable);
        const double Drift=MaxDisplacement(Before,After,true);
        AddInfo(FString::Printf(TEXT("Floor mode=%s late horizontal drift=%.5f cm"),ModeName(Mode),Drift));
        TestTrue(FString::Printf(TEXT("%s floor contact stops late horizontal drift without relying on sleep"),ModeName(Mode)),Drift<.5);
        TestFalse(TEXT("Drift fixture keeps the physical simulation awake"),Cable->bCableSleeping);
        bool Supported=true; for (const auto& Point:After) Supported&=!Point.ContainsNaN() && Point.Z>=4.95 && Point.Z<8;
        TestTrue(FString::Printf(TEXT("%s slack cable remains resting above the floor"),ModeName(Mode)),Supported);

        // Match the selected level rope's wide, 12-span profile. A nearly
        // complete loop supplies slack without compressed initial constraints.
        constexpr double AngleStep=PI*11.0/72.0;
        const double Radius=100.0/(2*FMath::Sin(AngleStep*.5));
        TArray<FVector> Coil;
        for (int32 I=0;I<=12;++I)
            Coil.Add(FVector(800+Radius*FMath::Cos(I*AngleStep),Radius*FMath::Sin(I*AngleStep),35));
        auto* Wide=F.Cable(Coil,Mode); Wide->CableWidth=23; Wide->CollisionFriction=1;
        Wide->SelfCollisionIterations=4; Wide->bAttachStart=true;
        Wide->SetCableFeatureEnabled(ECableFeature::SelfCollision,true);
        Wide->SetCableParticleLocations(Coil); F.Step(Wide,480);
        const auto WideBefore=F.Points(Wide); F.Step(Wide,120); const auto WideAfter=F.Points(Wide);
        const double WideDrift=MaxDisplacement(WideBefore,WideAfter,true);
        AddInfo(FString::Printf(TEXT("Wide floor loop mode=%s width=23 friction=1 late horizontal drift=%.5f cm"),ModeName(Mode),WideDrift));
        TestTrue(FString::Printf(TEXT("%s maximum friction keeps the wide attached floor loop from creeping"),ModeName(Mode)),WideDrift<.5);
        TestTrue(TEXT("Wide loop keeps its authored start attachment"),WideAfter[0].Equals(Coil[0],.01));
        bool WideSupported=true; int32 RestingPoints=0;
        for (const auto& Point:WideAfter)
        {
            WideSupported&=!Point.ContainsNaN() && Point.Z>=11.45;
            if (Point.Z<14) ++RestingPoints;
        }
        TestTrue(TEXT("Most of the wide slack loop rests on the floor without sinking"),WideSupported && RestingPoints>=8);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableInclineFrictionTest,"ColossalFizzCo.Cable.Settling.ShallowInclineAndImpulse",CableSettlingTests::Flags)
bool FCableInclineFrictionTest::RunTest(const FString& Parameters)
{
    using namespace CableSettlingTests;
    for (const auto Mode:{EQueryMode::Lines,EQueryMode::Sweeps,EQueryMode::Full})
    {
        FFixture F; if (!F.World) return false;
        const FQuat Rotation=FRotator(5,0,0).Quaternion(); F.Floor(Rotation);
        const FVector Normal=Rotation.RotateVector(FVector::UpVector);
        const FVector Downhill=FVector::VectorPlaneProject(FVector(0,0,-1),Normal).GetSafeNormal();
        TArray<FVector> Seed; for (int32 I=0;I<9;++I) Seed.Add(Rotation.RotateVector(FVector(0,-120+30*I,5.51)));
        auto* Cable=F.Cable(Seed,Mode); F.Step(Cable,120);
        const auto Before=F.Points(Cable); F.Step(Cable,120); const auto Rested=F.Points(Cable);
        const double Drift=MaxDisplacement(Before,Rested);
        AddInfo(FString::Printf(TEXT("Incline mode=%s drift=%.5f cm"),ModeName(Mode),Drift));
        TestTrue(FString::Printf(TEXT("%s static friction holds a five-degree slope with sleeping disabled"),ModeName(Mode)),Drift<1);
        for (int32 I=0;I<Rested.Num();++I)
            TestTrue(TEXT("A deliberate tangent impulse is accepted"),Cable->AddImpulseToPoint(I,Downhill*200*Cable->GetPointMass(I)));
        F.Step(Cable,15); const auto Pushed=F.Points(Cable);
        double MeanTravel=0; for (int32 I=0;I<Pushed.Num();++I) MeanTravel+=FVector::DotProduct(Pushed[I]-Rested[I],Downhill)/Pushed.Num();
        AddInfo(FString::Printf(TEXT("Incline mode=%s deliberate impulse travel=%.5f cm"),ModeName(Mode),MeanTravel));
        TestTrue(FString::Printf(TEXT("%s friction permits intentional sliding above its resistance"),ModeName(Mode)),MeanTravel>5);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableSelfContactSettlingTest,"ColossalFizzCo.Cable.Settling.SelfContact",CableSettlingTests::Flags)
bool FCableSelfContactSettlingTest::RunTest(const FString& Parameters)
{
    using namespace CableSettlingTests;
    for (int32 Case=0;Case<4;++Case)
    {
        const bool OnFloor=Case>0;
        const auto Mode=Case==3 ? EQueryMode::Full : Case==2 ? EQueryMode::Sweeps : EQueryMode::Lines;
        FFixture F; if (!F.World) return false; if (OnFloor) F.Floor();
        const auto Seed=CrossedLoop(OnFloor ? 35 : 100);
        auto* Cable=F.Cable(Seed,Mode);
        Cable->bEnableCollision=OnFloor; Cable->CableGravityScale=OnFloor ? 1 : 0;
        Cable->bSmoothCollisionRecovery=OnFloor;
        Cable->SetCableFeatureEnabled(ECableFeature::SelfCollision,true);
        F.Step(Cable,60);
        if (!OnFloor)
            TestTrue(TEXT("Immediate self-contact repair does not launch a force-free cable"),MaxDisplacement(Seed,F.Points(Cable))<20);
        F.Step(Cable,420); const auto Before=F.Points(Cable); auto Previous=Before;
        double MaxFrameMotion=0,MinimumSeparation=UE_BIG_NUMBER;
        for (int32 I=0;I<120;++I)
        {
            F.Step(Cable,1); const auto Current=F.Points(Cable);
            MaxFrameMotion=FMath::Max(MaxFrameMotion,MaxDisplacement(Previous,Current));
            MinimumSeparation=FMath::Min(MinimumSeparation,MinSelfSeparation(Current,Cable->SelfCollisionNeighborExclusion)); Previous=Current;
        }
        const double Drift=MaxDisplacement(Before,Previous);
        AddInfo(FString::Printf(TEXT("Self contact floor=%d mode=%s late drift=%.5f max frame motion=%.5f minimum separation=%.5f cm"),OnFloor,ModeName(Mode),Drift,MaxFrameMotion,MinimumSeparation));
        auto Diagnose=[&](const TCHAR* Stage,const TArray<FVector>& Start,const TArray<FVector>& End)
        {
            const double RestLength=Cable->CableLength/Cable->NumSegments;
            AddInfo(FString::Printf(TEXT("Self contact detail stage=%s floor=%d mode=%s sleeping=%d/%d unresolved=%d restLength=%.5f"),
                Stage,OnFloor,ModeName(Mode),Cable->SleepingPointCount,End.Num(),Cable->bHasUnresolvedCollision,RestLength));
            for (int32 I=0;I<End.Num();++I)
            {
                const double SpanError=I>0 ? FVector::Distance(End[I-1],End[I])-RestLength : 0;
                AddInfo(FString::Printf(TEXT("Self point=%d before=(%s) after=(%s) delta=(%s) displacement=%.6f previousSpanError=%.6f sleeping=%d"),
                    I,*Start[I].ToString(),*End[I].ToString(),*(End[I]-Start[I]).ToString(),FVector::Distance(Start[I],End[I]),SpanError,Cable->IsPointSleeping(I)));
            }
        };
        if (MaxFrameMotion>=.05 || Drift>=.5 || MinimumSeparation<Cable->CableWidth-.05)
            Diagnose(TEXT("late awake motion"),Before,Previous);
        TestTrue(TEXT("Resting self-contact has no persistent visible jitter with sleeping disabled"),MaxFrameMotion<.05 && Drift<.5);
        TestTrue(TEXT("Settling preserves non-neighbor span separation"),MinimumSeparation>=Cable->CableWidth-.05);
        Cable->SetCableFeatureEnabled(ECableFeature::Sleeping,true); F.Step(Cable,180);
        if (!Cable->bCableSleeping) Diagnose(TEXT("sleep timeout"),Previous,F.Points(Cable));
        TestTrue(TEXT("Persistent settled self-contact permits the normal sleep delay to complete"),Cable->bCableSleeping);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableRestingWakeTest,"ColossalFizzCo.Cable.Settling.WakeAfterImpulseAndGrab",CableSettlingTests::Flags)
bool FCableRestingWakeTest::RunTest(const FString& Parameters)
{
    using namespace CableSettlingTests;
    FFixture F; if (!F.World) return false; F.Floor();
    TArray<FVector> Seed; for (int32 I=0;I<9;++I) Seed.Add(FVector(-120+30*I,0,5.51));
    auto* Cable=F.Cable(Seed,EQueryMode::Lines,true); F.Step(Cable,240);
    if (!TestTrue(TEXT("Ordinary floor cable sleeps using its default delay and damping"),Cable->bCableSleeping)) return false;
    const auto BeforeImpulse=F.Points(Cable);
    TestTrue(TEXT("Sleeping point accepts an upward impulse"),Cable->AddImpulseToPoint(4,FVector(0,0,300)));
    TestTrue(TEXT("Impulse immediately wakes the sleeping simulation"),!Cable->bCableSleeping && Cable->IsComponentTickEnabled());
    F.Step(Cable,12);
    TestTrue(TEXT("Waking impulse produces real cable movement"),MaxDisplacement(BeforeImpulse,F.Points(Cable))>2);
    F.Step(Cable,480);
    if (!TestTrue(TEXT("Disturbed floor cable settles back to sleep"),Cable->bCableSleeping)) return false;
    const auto BeforeGrab=F.Points(Cable); auto* Participant=F.Participant(BeforeGrab[4]);
    FCableGrabSettings Settings; Settings.MovementRate=120; Settings.MaxForceNewtons=1000; Settings.InfluenceRadius=60;
    TestTrue(TEXT("Sleeping cable accepts a deliberate grab"),Cable->BeginPointGrab(Participant,4,BeforeGrab[4]+FVector(0,0,60),Settings));
    TestTrue(TEXT("Grab immediately wakes its sleeping section"),!Cable->bCableSleeping && Cable->IsComponentTickEnabled());
    F.Step(Cable,30);
    TestTrue(TEXT("Grab can lift the settled cable against its contact friction"),F.Points(Cable)[4].Z-BeforeGrab[4].Z>2);
    TestTrue(TEXT("Participant can release the cable after lifting"),Cable->EndPointGrab(Participant));
    return true;
}
#endif
