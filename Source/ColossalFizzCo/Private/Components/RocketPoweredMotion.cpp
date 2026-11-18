#include "Components/RocketProjectileComponent.h"

FRocketPoweredMotionSettings::FRocketPoweredMotionSettings()
{
    FRichCurve* Curve=SpeedCurve.GetRichCurve();
    const float Times[]={0.f,.1f,.25f,.45f,.8f,1.f};
    const float Values[]={.04f,.2f,.45f,1.f,.4f,.03f};
    for(int32 I=0;I<UE_ARRAY_COUNT(Times);++I)
        Curve->SetKeyInterpMode(Curve->AddKey(Times[I],Values[I]),RCIM_Linear);
}

float URocketProjectileComponent::EvaluatePoweredSpeedMultiplier(ERocketSimulationState Phase,float PhaseProgress)
{
    if(!PoweredMotionSettings.bUseLifecycleSpeedCurve)return 1.f;
    const float Progress=FMath::IsFinite(PhaseProgress)?FMath::Clamp(PhaseProgress,0.f,1.f):0.f;
    float Start=0,End=0;
    switch(Phase)
    {
    case ERocketSimulationState::TippingOver: Start=0;End=.1f;break;
    case ERocketSimulationState::Dragging: Start=.1f;End=.25f;break;
    case ERocketSimulationState::Rocketing: Start=.25f;End=.8f;break;
    case ERocketSimulationState::LowFuelSputter: Start=.8f;End=1;break;
    default:return 0.f;
    }
    const float Previous=FMath::IsFinite(PoweredCurveProgress)?FMath::Clamp(PoweredCurveProgress,0.f,1.f):0.f;
    PoweredCurveProgress=FMath::Max(Previous,FMath::Lerp(Start,End,Progress));
    const FRichCurve* Curve=PoweredMotionSettings.SpeedCurve.GetRichCurveConst();
    const float Multiplier=Curve?Curve->Eval(PoweredCurveProgress,1.f):1.f;
    return FMath::IsFinite(Multiplier)?FMath::Clamp(Multiplier,0.f,4.f):0.f;
}

float URocketProjectileComponent::UpdatePoweredSpeed(float TargetSpeed,float DeltaTime)
{
    constexpr float MaximumSpeed=100000.f;
    PoweredMotionSpeed=FMath::IsFinite(PoweredMotionSpeed)?FMath::Clamp(PoweredMotionSpeed,0.f,MaximumSpeed):0.f;
    if(!FMath::IsFinite(DeltaTime) || DeltaTime<=0)return PoweredMotionSpeed;
    const float Target=FMath::IsFinite(TargetSpeed)?FMath::Clamp(TargetSpeed,0.f,MaximumSpeed):0.f;
    const bool Accelerating=Target>PoweredMotionSpeed;
    const float ConfiguredRate=Accelerating?PoweredMotionSettings.Acceleration:PoweredMotionSettings.Deceleration;
    const float DefaultRate=Accelerating?2200.f:1800.f;
    const double Rate=FMath::Clamp(double(FMath::IsFinite(ConfiguredRate)?ConfiguredRate:DefaultRate),1.,1000000.);
    const double Distance=double(Target)-PoweredMotionSpeed;
    const double Step=FMath::Min(FMath::Abs(Distance),Rate*double(DeltaTime));
    PoweredMotionSpeed=float(double(PoweredMotionSpeed)+(Distance>=0?Step:-Step));
    return PoweredMotionSpeed;
}
