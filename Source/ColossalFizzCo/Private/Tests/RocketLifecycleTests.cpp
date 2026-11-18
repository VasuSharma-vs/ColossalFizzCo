#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Curves/CurveFloat.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopProcurementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace RocketLifecycleTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
constexpr float Dt=.01f;
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
        auto* Actor=World->SpawnActor<AActor>(); auto* Floor=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Floor); Actor->AddInstanceComponent(Floor); Floor->SetBoxExtent(FVector(30000,30000,50));
        Floor->SetCollisionProfileName(TEXT("BlockAll")); Floor->SetWorldLocation(FVector(0,0,-50)); Floor->RegisterComponent();
        World->Tick(LEVELTICK_All,.02f);
    }
    ~FFixture()
    {
        if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
    }
    URocketProjectileComponent* Rocket(FVector Position=FVector(0,0,500),bool GroundPhases=false)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Actor);
        Actor->SetRootComponent(Mesh); Actor->AddInstanceComponent(Mesh);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetWorldScale3D(FVector(.1,.1,.3)); Mesh->SetCollisionProfileName(TEXT("PhysicsActor")); Mesh->RegisterComponent();
        Mesh->SetMassOverrideInKg(NAME_None,1,true); Actor->SetActorLocation(Position);
        auto* Result=NewObject<URocketProjectileComponent>(Actor); Actor->AddInstanceComponent(Result);
        Result->bLaunchOnInitialized=false; Result->SetAllDebugEnabled(false); Result->RegisterComponent();
        Result->TippingSettings.bEnableTipping=Result->bTipOverToFly=GroundPhases;
        Result->DraggingSettings.bEnableDragging=GroundPhases;
        Result->TippingSettings.TippingDuration=.2f; Result->TippingSettings.PrecessionSpiralRadius=0;
        Result->DraggingSettings.MinDragDuration=Result->DraggingSettings.MaxDragDuration=.3f;
        Result->FlightSettings.bEnableErraticPath=false; Result->FlightSettings.MinPathDeviation=Result->FlightSettings.MaxPathDeviation=0;
        Result->FlightSettings.FlightBounceRandomDeflection=Result->FlightBounceRandomDeflection=0;
        Result->FlightSettings.LowFuelThrustDistanceMin=Result->FlightSettings.LowFuelThrustDistanceMax=150;
        Result->FlightSettings.MaxFuelDuration=Result->MaxFuelDuration=30;
        Result->FallingSettings.PhysicsHandoffMode=ERocketPhysicsHandoffMode::Disabled;
        Result->FallingSettings.bUsePhysicsSimulation=Result->FallingSettings.bHandoffToPhysicsOnDepleted=Result->bHandoffToPhysicsOnDepleted=false;
        Result->FallingSettings.RicochetRandomDeflection=Result->RicochetRandomDeflection=0;
        Result->SetFixedFallingBounces(3);
        return Result;
    }
    static void Step(URocketProjectileComponent* Rocket,int32 Frames=1)
    {
        for (int32 I=0;I<Frames;++I) static_cast<UActorComponent*>(Rocket)->TickComponent(Dt,LEVELTICK_All,nullptr);
    }
    static void Launch(URocketProjectileComponent* Rocket,float Distance=900,float Speed=1200)
    {
        Rocket->ConfigureBottleProjectile(0);
        Rocket->LaunchRocketHighAccuracy(FVector(1,0,.2).GetSafeNormal(),73,Distance,Speed);
    }
    static void ConstantCurve(URocketProjectileComponent* Rocket,float Value)
    {
        Rocket->PoweredMotionSettings.bUseLifecycleSpeedCurve=true;
        auto* Curve=Rocket->PoweredMotionSettings.SpeedCurve.GetRichCurve(); Curve->Reset(); Curve->AddKey(0,Value); Curve->AddKey(1,Value);
        Rocket->PoweredMotionSettings.Acceleration=Rocket->PoweredMotionSettings.Deceleration=10000;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketDepletionPolicyTest,"ColossalFizzCo.Props.RocketLifecycle.DepletionPolicies",RocketLifecycleTests::Flags)
bool FRocketDepletionPolicyTest::RunTest(const FString& Parameters)
{
    using namespace RocketLifecycleTests;
    for (int32 Case=0;Case<3;++Case)
    {
        FFixture F; if (!F.World) return false; auto* Rocket=F.Rocket();
        Rocket->FallingSettings.PhysicsHandoffMode=Case==0 ? ERocketPhysicsHandoffMode::Disabled : Case==1 ? ERocketPhysicsHandoffMode::UseLegacySettings : ERocketPhysicsHandoffMode::OnFuelDepleted;
        Rocket->FallingSettings.bUsePhysicsSimulation=Rocket->FallingSettings.bHandoffToPhysicsOnDepleted=Rocket->bHandoffToPhysicsOnDepleted=Case==0;
        F.Launch(Rocket);
        bool SawLowFuel=false;
        for (int32 I=0;I<3000 && Rocket->IsPoweredFlight();++I)
        {
            SawLowFuel|=Rocket->GetSimulationState()==ERocketSimulationState::LowFuelSputter;
            F.Step(Rocket);
        }
        AddInfo(FString::Printf(TEXT("Depletion case=%d state=%d physics=%d distance=%.3f budget=%.3f"),Case,int32(Rocket->GetSimulationState()),Rocket->TargetPrimitive->IsSimulatingPhysics(),Rocket->GetTotalFlightDistanceTraveled(),Rocket->GetPoweredTravelDistance()));
        TestTrue(TEXT("Natural exhaustion traverses the dying powered climb"),SawLowFuel);
        TestEqual(TEXT("Natural depletion enters spent falling"),Rocket->GetSimulationState(),ERocketSimulationState::SpentFalling);
        TestEqual(TEXT("Only explicit immediate policy enables Chaos at depletion"),Rocket->TargetPrimitive->IsSimulatingPhysics(),Case==2);
        TestEqual(TEXT("Replicated ownership agrees with the selected handoff policy"),Rocket->GetReplicatedFlightState().bPhysicsSimulating,Case==2);
        if (Case<2)
        {
            TestFalse(TEXT("Disabled handoff retains component movement ownership"),Rocket->GetOwner()->IsReplicatingMovement());
            const FTransform Pose=Rocket->TargetPrimitive->GetComponentTransform(); const FVector Velocity=Rocket->GetRocketVelocity();
            Rocket->EndScriptedThrust(); Rocket->EndScriptedThrust();
            TestTrue(TEXT("Repeated depletion does not reset a scripted fall"),Rocket->TargetPrimitive->GetComponentTransform().Equals(Pose,.0001) && Rocket->GetRocketVelocity().Equals(Velocity,.0001));
            F.Step(Rocket,5);
            TestFalse(TEXT("Subsequent falling ticks cannot override disabled handoff"),Rocket->TargetPrimitive->IsSimulatingPhysics());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketImmediateHandoffTest,"ColossalFizzCo.Props.RocketLifecycle.ImmediateMomentum",RocketLifecycleTests::Flags)
bool FRocketImmediateHandoffTest::RunTest(const FString& Parameters)
{
    using namespace RocketLifecycleTests;
    FFixture F; if (!F.World) return false; auto* Rocket=F.Rocket(FVector(0,0,2000));
    auto* Body=CastChecked<UPrimitiveComponent>(Rocket->GetOwner()->GetRootComponent());
    Body->SetLinearDamping(.3f); Body->SetAngularDamping(.7f);
    Rocket->FallingSettings.PhysicsHandoffMode=ERocketPhysicsHandoffMode::OnFuelDepleted;
    F.Launch(Rocket); F.Step(Rocket,10);
    const FTransform Pose=Body->GetComponentTransform(); const FVector Velocity=Rocket->GetRocketVelocity(),Angular=Rocket->GetRocketAngularVelocityDegrees();
    Rocket->EndScriptedThrust();
    TestTrue(TEXT("Opt-in immediate handoff preserves the current pose"),Body->GetComponentTransform().Equals(Pose,.001));
    TestTrue(TEXT("Opt-in immediate handoff preserves scripted momentum"),Body->GetPhysicsLinearVelocity().Equals(Velocity,.1));
    TestTrue(TEXT("Opt-in immediate handoff preserves measured tumble"),Body->GetPhysicsAngularVelocityInDegrees().Equals(Angular,.1));
    TestEqual(TEXT("Handoff applies its chosen linear damping"),Body->GetLinearDamping(),Rocket->FallingSettings.HandoffLinearDamping);
    TestEqual(TEXT("Handoff applies its chosen angular damping"),Body->GetAngularDamping(),Rocket->FallingSettings.HandoffAngularDamping);
    Rocket->ResetRocketSimulation();
    TestEqual(TEXT("Pool reset restores authored linear damping"),Body->GetLinearDamping(),.3f);
    TestEqual(TEXT("Pool reset restores authored angular damping"),Body->GetAngularDamping(),.7f);

    auto* NearFloor=F.Rocket(FVector(1000,0,100));
    NearFloor->FallingSettings.PhysicsHandoffMode=ERocketPhysicsHandoffMode::OnFuelDepleted;
    NearFloor->FallingSettings.HandoffRestVelocityThreshold=100000;
    NearFloor->FallingSettings.HandoffRestAngularVelocityThreshold=100000;
    NearFloor->AtRestSettings.SettlingLockDuration=.05f;
    F.Launch(NearFloor); NearFloor->EndScriptedThrust();
    auto* NearBody=NearFloor->TargetPrimitive.Get();
    auto PlaceLowerBounds=[&](double Gap)
    {
        NearBody->UpdateBounds();
        const double Bottom=NearBody->Bounds.Origin.Z-NearBody->Bounds.BoxExtent.Z;
        NearBody->SetWorldLocation(NearBody->GetComponentLocation()+FVector(0,0,Gap-Bottom),false,nullptr,ETeleportType::TeleportPhysics);
        NearBody->UpdateBounds();
    };
    auto TickStationary=[&]()
    {
        for (int32 I=0;I<30;++I)
        {
            NearBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
            NearBody->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            F.Step(NearFloor);
        }
    };
    PlaceLowerBounds(10); TickStationary();
    TestEqual(TEXT("High rest thresholds cannot settle a body ten centimeters above the floor"),NearFloor->GetSimulationState(),ERocketSimulationState::SpentFalling);
    TestTrue(TEXT("A stationary airborne body keeps authoritative physics"),NearBody->IsSimulatingPhysics());
    PlaceLowerBounds(1); TickStationary();
    TestEqual(TEXT("The same stationary body can settle within floor contact tolerance"),NearFloor->GetSimulationState(),ERocketSimulationState::AtRest);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketReboundHandoffTest,"ColossalFizzCo.Props.RocketLifecycle.ReboundApex",RocketLifecycleTests::Flags)
bool FRocketReboundHandoffTest::RunTest(const FString& Parameters)
{
    using namespace RocketLifecycleTests;
    for (bool Planned:{false,true})
    {
        FFixture F; if (!F.World) return false; auto* Rocket=F.Rocket(FVector(0,0,700));
        Rocket->FallingSettings.PhysicsHandoffMode=ERocketPhysicsHandoffMode::AfterBounceApex; Rocket->FallingSettings.HandoffAfterBounces=1;
        Rocket->FallingSettings.SpentBounciness=.5f; F.Launch(Rocket);
        if (!Planned) { F.Step(Rocket,5); Rocket->StopRocket(false); }
        bool SawFalling=false,SawGroundImpact=false,SawRebound=false,EarlyPhysics=false;
        bool SawScriptedFall=false,MatchedFallingPath=true; float ReboundPeak=-MAX_flt;
        for (int32 I=0;I<3000 && !Rocket->TargetPrimitive->IsSimulatingPhysics() && Rocket->GetSimulationState()!=ERocketSimulationState::AtRest;++I)
        {
            F.Step(Rocket);
            SawFalling|=Rocket->GetSimulationState()==ERocketSimulationState::SpentFalling;
            if (Rocket->GetSimulationState()==ERocketSimulationState::SpentFalling && !Rocket->TargetPrimitive->IsSimulatingPhysics())
            {
                SawScriptedFall=true;
                MatchedFallingPath&=Rocket->GetReplicatedFlightState().bScriptedBallisticFall==!Planned;
            }
            if (SawFalling && Rocket->GetBounceCount()>0) SawGroundImpact=true;
            if (SawGroundImpact && Rocket->GetRocketVelocity().Z>10) SawRebound=true;
            if (SawRebound) ReboundPeak=FMath::Max(ReboundPeak,float(Rocket->TargetPrimitive->GetComponentLocation().Z));
            if (Rocket->TargetPrimitive->IsSimulatingPhysics() && !SawRebound) EarlyPhysics=true;
        }
        AddInfo(FString::Printf(TEXT("Rebound planned=%d state=%d impacts=%d rebound=%d physics=%d height=%.3f peak=%.3f"),Planned,int32(Rocket->GetSimulationState()),Rocket->GetBounceCount(),SawRebound,Rocket->TargetPrimitive->IsSimulatingPhysics(),Rocket->TargetPrimitive->GetComponentLocation().Z,ReboundPeak));
        TestTrue(TEXT("Both planned and interrupted flight descend under scripted control"),SawFalling);
        TestTrue(Planned?TEXT("Natural depletion preserves its predicted falling plan"):TEXT("Interrupted thrust uses a ballistic fall from its current pose"),SawScriptedFall && MatchedFallingPath);
        TestTrue(TEXT("Physics waits for a real impact and upward rebound"),SawGroundImpact && SawRebound && !EarlyPhysics);
        TestTrue(TEXT("First rebound apex hands control to physics"),Rocket->TargetPrimitive->IsSimulatingPhysics());
        TestEqual(TEXT("Handoff occurs after the selected first bounce"),Rocket->GetBounceCount(),1);
        TestTrue(TEXT("Handoff is near the rebound peak rather than at ground impact"),FMath::Abs(Rocket->TargetPrimitive->GetComponentLocation().Z-ReboundPeak)<5);
        TestTrue(TEXT("Rebound handoff has almost no vertical launch speed"),FMath::Abs(Rocket->TargetPrimitive->GetPhysicsLinearVelocity().Z)<100);
    }
    for (bool HigherLanding:{false,true})
    {
        FFixture F; if (!F.World) return false; auto* Rocket=F.Rocket(FVector(0,0,1800));
        Rocket->FallingSettings.PhysicsHandoffMode=ERocketPhysicsHandoffMode::AfterBounceApex;
        Rocket->FallingSettings.HandoffAfterBounces=1; F.Launch(Rocket);
        // Receive a deterministic terrain plan through the production RPC.
        // Its middle bounce node is at t=.5, whereas unequal landing heights
        // put the actual vertical maximum before or after that node.
        struct FPlanParameters
        {
            TArray<FVector_NetQuantize> InRocketingLocations;
            FVector_NetQuantize InLowFuelThrustLocation;
            bool bInLowFuelHitObstacle=false;
            FVector_NetQuantizeNormal InLowFuelHitNormal=FVector::DownVector;
            TArray<FVector_NetQuantize> InBounceLocations;
            FRotator InRestRotation=FRotator::ZeroRotator;
        } Plan;
        Plan.InRocketingLocations={FVector(0,0,1800),FVector(200,0,1900)};
        Plan.InLowFuelThrustLocation=FVector(300,0,2000);
        Plan.InBounceLocations={FVector(500,0,400),FVector(800,0,1000),FVector(1100,0,HigherLanding?800:0)};
        auto* Receiver=Rocket->FindFunction(TEXT("Multicast_SendFullFlightPlan"));
        if (!TestNotNull(TEXT("Full flight plan receiver exists"),Receiver)) return false;
        Rocket->GetOwner()->SwapRoles(); Rocket->ProcessEvent(Receiver,&Plan); Rocket->GetOwner()->SwapRoles();
        bool SawRebound=false;
        for (int32 I=0;I<3000 && !Rocket->TargetPrimitive->IsSimulatingPhysics() && Rocket->GetSimulationState()!=ERocketSimulationState::AtRest;++I)
        {
            F.Step(Rocket);
            SawRebound|=Rocket->GetBounceCount()>0 && Rocket->GetRocketVelocity().Z>10;
        }
        const FVector ExpectedPeak=HigherLanding?FVector(875,0,1025):FVector(762.5,0,1012.5);
        AddInfo(FString::Printf(TEXT("Unequal-height rebound higher=%d location=%s expected=%s physics=%d"),HigherLanding,*Rocket->TargetPrimitive->Bounds.Origin.ToString(),*ExpectedPeak.ToString(),Rocket->TargetPrimitive->IsSimulatingPhysics()));
        TestTrue(TEXT("Unequal-height plan reaches an upward first rebound"),SawRebound);
        TestTrue(TEXT("Unequal-height rebound hands off to physics"),Rocket->TargetPrimitive->IsSimulatingPhysics());
        TestTrue(TEXT("Handoff reaches the actual vertical peak on either side of the middle node"),Rocket->TargetPrimitive->Bounds.Origin.Equals(ExpectedPeak,1));
        TestTrue(TEXT("Unequal-height apex transfers no artificial vertical momentum"),FMath::Abs(Rocket->TargetPrimitive->GetPhysicsLinearVelocity().Z)<1);
        TestEqual(TEXT("Unequal-height apex handoff still follows the first bounce"),Rocket->GetBounceCount(),1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketSharedCurveTest,"ColossalFizzCo.Props.RocketLifecycle.SharedPoweredCurve",RocketLifecycleTests::Flags)
bool FRocketSharedCurveTest::RunTest(const FString& Parameters)
{
    using namespace RocketLifecycleTests;
    struct FTrace { float Times[4]={}; double Distances[4]={}; float Progress[4]={-1,-1,-1,-1}; bool Monotonic=true; bool Finished=false; };
    auto Run=[&](float Multiplier)
    {
        FTrace Trace; FFixture F; if (!F.World) return Trace; auto* Rocket=F.Rocket(FVector(0,0,16),true);
        F.ConstantCurve(Rocket,Multiplier);
        // The lifecycle envelope must own all four phases even when an obsolete
        // per-flight curve is still authored on the component.
        auto* OldCurve=NewObject<UCurveFloat>(Rocket); OldCurve->FloatCurve.AddKey(0,0); OldCurve->FloatCurve.AddKey(1,0); Rocket->FuelThrustCurve=OldCurve;
        F.Launch(Rocket,900,1200); float LastProgress=0;
        for (int32 I=0;I<6000 && Rocket->IsPoweredFlight();++I)
        {
            const int32 Phase=int32(Rocket->GetSimulationState())-int32(ERocketSimulationState::TippingOver);
            if (Phase>=0 && Phase<4) { Trace.Times[Phase]+=Dt; Trace.Progress[Phase]=FMath::Max(Trace.Progress[Phase],Rocket->GetPoweredCurveProgress()); }
            const FVector BeforeStep=Rocket->TargetPrimitive->GetComponentLocation();
            F.Step(Rocket); const float Progress=Rocket->GetPoweredCurveProgress();
            if (Phase>=0 && Phase<4) Trace.Distances[Phase]+=FVector::Distance(BeforeStep,Rocket->TargetPrimitive->GetComponentLocation());
            Trace.Monotonic&=FMath::IsFinite(Progress) && Progress>=LastProgress-.0001f && Progress<=1.0001f; LastProgress=Progress;
        }
        Trace.Finished=!Rocket->IsPoweredFlight();
        AddInfo(FString::Printf(TEXT("Curve %.2f phase times tip=%.3f drag=%.3f flight=%.3f low=%.3f progress=%.3f/%.3f/%.3f/%.3f"),Multiplier,Trace.Times[0],Trace.Times[1],Trace.Times[2],Trace.Times[3],Trace.Progress[0],Trace.Progress[1],Trace.Progress[2],Trace.Progress[3]));
        AddInfo(FString::Printf(TEXT("Curve %.2f mean actual speeds tip=%.3f drag=%.3f flight=%.3f low=%.3f cm/s"),Multiplier,
            Trace.Distances[0]/FMath::Max(Dt,Trace.Times[0]),Trace.Distances[1]/FMath::Max(Dt,Trace.Times[1]),
            Trace.Distances[2]/FMath::Max(Dt,Trace.Times[2]),Trace.Distances[3]/FMath::Max(Dt,Trace.Times[3])));
        return Trace;
    };
    const auto Fast=Run(1),Slow=Run(.25f);
    TestTrue(TEXT("Both actual powered lifecycles finish"),Fast.Finished && Slow.Finished);
    TestTrue(TEXT("Shared curve progress never restarts at a phase boundary"),Fast.Monotonic && Slow.Monotonic);
    for (int32 I=0;I<4;++I)
    {
        TestTrue(FString::Printf(TEXT("Phase %d is physically simulated under both curves"),I+1),Fast.Times[I]>0 && Slow.Times[I]>0);
        if (I==1)
        {
            // Drag duration is authored independently; its envelope controls
            // translational speed during that fixed interval.
            const double FastSpeed=Fast.Distances[I]/FMath::Max(Dt,Fast.Times[I]);
            const double SlowSpeed=Slow.Distances[I]/FMath::Max(Dt,Slow.Times[I]);
            TestTrue(TEXT("Lower shared curve reduces actual dragging speed"),FastSpeed>1 && SlowSpeed<FastSpeed*.8);
        }
        else TestTrue(FString::Printf(TEXT("Lower shared curve slows actual phase %d"),I+1),Slow.Times[I]>Fast.Times[I]*1.3f);
    }
    TestTrue(TEXT("Shared phase ranges reach tipping, dragging, flight and low-fuel sections"),Fast.Progress[0]>.08f && Fast.Progress[1]>.22f && Fast.Progress[2]>.75f && Fast.Progress[3]>.95f);
    FFixture F; if (!F.World) return false; auto* Zero=F.Rocket(FVector(0,0,16),true); F.ConstantCurve(Zero,0);
    Zero->FlightSettings.MaxFuelDuration=Zero->MaxFuelDuration=1; F.Launch(Zero);
    TestEqual(TEXT("Launching preserves the authored one-second fuel timeout"),Zero->MaxFuelDuration,1.f);
    bool LeftGroundPhases=false;
    for (int32 I=0;I<1500 && Zero->IsPoweredFlight();++I)
    { F.Step(Zero); LeftGroundPhases|=Zero->GetSimulationState()==ERocketSimulationState::Rocketing || Zero->GetSimulationState()==ERocketSimulationState::LowFuelSputter; }
    TestTrue(TEXT("Zero envelope does not trap tipping or dragging indefinitely"),LeftGroundPhases);
    TestFalse(TEXT("Fuel timeout eventually ends a zero-speed launch"),Zero->IsPoweredFlight());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketFallingControlsTest,"ColossalFizzCo.Props.RocketLifecycle.FallingControls",RocketLifecycleTests::Flags)
bool FRocketFallingControlsTest::RunTest(const FString& Parameters)
{
    using namespace RocketLifecycleTests;
    auto Measure=[&](float Multiplier,float Terminal,float Acceleration,int32 Frames)
    {
        FFixture F; if (!F.World) return FVector::ZeroVector; auto* Rocket=F.Rocket(FVector(0,0,10000));
        Rocket->FallingSettings.FallingSpeedMultiplier=Multiplier; Rocket->FallingSettings.TerminalVelocity=Terminal;
        Rocket->FallingSettings.FallAcceleration=Acceleration; Rocket->FallingSettings.ApexHangDuration=0;
        F.Launch(Rocket); Rocket->EndScriptedThrust(); F.Step(Rocket,Frames);
        TestFalse(TEXT("Scripted fall controls retain kinematic ownership"),Rocket->TargetPrimitive->IsSimulatingPhysics());
        return Rocket->GetRocketVelocity();
    };
    const auto Slow=Measure(.5f,3000,2000,60),Fast=Measure(2,3000,2000,60);
    TestTrue(TEXT("Falling speed multiplier changes actual descent"),Fast.Z<Slow.Z-100);
    const auto Capped=Measure(1,600,10000,120),HigherCap=Measure(1,1200,10000,120);
    AddInfo(FString::Printf(TEXT("Falling speeds multiplier=.5 %.3f multiplier=2 %.3f terminal600 %.3f terminal1200 %.3f"),Slow.Z,Fast.Z,Capped.Z,HigherCap.Z));
    TestTrue(TEXT("Authored terminal speed caps high-acceleration descent"),Capped.Z<=-500 && Capped.Z>=-601);
    TestTrue(TEXT("Higher terminal setting permits faster descent"),HigherCap.Z<Capped.Z-300 && HigherCap.Z>=-1201);
    FFixture F; if (!F.World) return false; auto* NoBounce=F.Rocket(FVector(0,0,300));
    NoBounce->FallingSettings.bBounceAfterFuelDepleted=NoBounce->bBounceAfterFuelDepleted=false;
    F.Launch(NoBounce); NoBounce->EndScriptedThrust(); bool Rebounded=false;
    for (int32 I=0;I<2000 && NoBounce->GetSimulationState()!=ERocketSimulationState::AtRest;++I)
    { F.Step(NoBounce); if (NoBounce->GetBounceCount()>0 && NoBounce->GetRocketVelocity().Z>10) Rebounded=true; }
    TestEqual(TEXT("Disabling spent bounces allows a direct scripted landing"),NoBounce->GetSimulationState(),ERocketSimulationState::AtRest);
    TestFalse(TEXT("No-bounce landing does not invent a rebound"),Rebounded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketBottlePolicyTest,"ColossalFizzCo.Props.RocketLifecycle.AuthoredBottlePolicy",RocketLifecycleTests::Flags)
bool FRocketBottlePolicyTest::RunTest(const FString& Parameters)
{
    using namespace RocketLifecycleTests;
    FFixture F; if (!F.World) return false;
    auto* Manager=F.World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
    if (!TestTrue(TEXT("Real bottle catalog initializes"),Manager->ShopSupplies->InitializeCatalog())) return false;
    auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalLemonade.DA_NormalLemonade"));
    if (!TestNotNull(TEXT("Real bottle product loads"),Product)) return false;
    const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,2000)));
    if (!TestTrue(TEXT("Real bottle activates"),Manager->SetInstanceToUnsettled(ID))) return false;
    auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
    if (!TestTrue(TEXT("Real bottle retains its authored rocket"),Bottle && Bottle->RocketComp)) return false;
    auto* Rocket=Bottle->RocketComp.Get();
    TestEqual(TEXT("Saved BP_Cola selects rebound-apex handoff"),Rocket->FallingSettings.PhysicsHandoffMode,ERocketPhysicsHandoffMode::AfterBounceApex);
    TestEqual(TEXT("Saved BP_Cola selects one controlled bounce"),Rocket->FallingSettings.HandoffAfterBounces,1);
    TestTrue(TEXT("Saved BP_Cola uses the shared launch envelope"),Rocket->PoweredMotionSettings.bUseLifecycleSpeedCurve);
    const auto* SavedCurve=Rocket->PoweredMotionSettings.SpeedCurve.GetRichCurveConst();
    TestTrue(TEXT("Saved launch envelope starts with low thrust"),SavedCurve && FMath::IsNearlyEqual(SavedCurve->Eval(0),.04f,.0001f));
    TestTrue(TEXT("Saved launch envelope contains its full-thrust middle key"),SavedCurve && FMath::IsNearlyEqual(SavedCurve->Eval(.45f),1.f,.0001f));
    Rocket->FallingSettings.PhysicsHandoffMode=ERocketPhysicsHandoffMode::Disabled;
    Rocket->TippingSettings.bEnableTipping=Rocket->DraggingSettings.bEnableDragging=false;
    Bottle->RocketRandomSeed=71; Bottle->RocketMinDistance=Bottle->RocketMaxDistance=1000;
    const float InitialPortions=Bottle->GetContentLevel()*Bottle->GetNumPortions();
    Manager->SetBottleAgitationPressure(ID,Bottle->GetBottlePressureTolerance()+1); Bottle->AddAgitationPressure(.01f);
    if (!TestTrue(TEXT("Actual pressure failure launches the configured bottle"),Rocket->IsPoweredFlight())) return false;
    F.Step(Rocket,5); Manager->ReleaseBottleLiquid(ID); Bottle->UpdatePlasticPressure(.01f);
    TestEqual(TEXT("Bottle-side depletion respects scripted falling policy"),Rocket->GetSimulationState(),ERocketSimulationState::SpentFalling);
    TestFalse(TEXT("Bottle accounting does not force Chaos after disabled handoff"),Bottle->PropMesh->IsSimulatingPhysics());
    TestFalse(TEXT("Bottle accounting retains component movement ownership while falling"),Bottle->IsReplicatingMovement());
    if (!TestNotNull(TEXT("Real bottle retains its physics lifecycle helper"),Bottle->PhysicsSimComponent.Get())) return false;
    Bottle->PhysicsSimComponent->RetriggerUnsettledTimer(.1f);
    TestFalse(TEXT("Retriggering the prop lifecycle cannot enable physics during disabled scripted falling"),Bottle->PropMesh->IsSimulatingPhysics());
    TestFalse(TEXT("Retriggering the prop lifecycle preserves scripted movement ownership"),Bottle->IsReplicatingMovement());
    TestEqual(TEXT("External depletion updates the liquid material to empty"),Bottle->ColaLevel,0.f);
    double Released=0; for (const auto& Row:Manager->ReleasedLiquids) if (Row.SourceISID==ID) Released+=Row.Portions;
    TestTrue(TEXT("Policy-aware depletion still conserves all canonical liquid"),FMath::IsNearlyEqual(Released,double(InitialPortions),1.e-4));
    return true;
}
#endif
