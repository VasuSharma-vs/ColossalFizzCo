#include "Components/RocketProjectileComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

ERocketPhysicsHandoffMode URocketProjectileComponent::GetPhysicsHandoffMode() const
{
	if(FallingSettings.PhysicsHandoffMode!=ERocketPhysicsHandoffMode::UseLegacySettings)
		return FallingSettings.PhysicsHandoffMode;
	return FallingSettings.bUsePhysicsSimulation || FallingSettings.bHandoffToPhysicsOnDepleted || bHandoffToPhysicsOnDepleted
		? ERocketPhysicsHandoffMode::OnFuelDepleted : ERocketPhysicsHandoffMode::Disabled;
}

float URocketProjectileComponent::GetPoweredTravelDistance() const
{
	if(AuthoritativeFlightNodes.Num()>=2 && CumulativeFlightPlanDistances.IsValidIndex(LowFuelIndex) && LowFuelIndex>0)
		return FMath::Max(1.f,CumulativeFlightPlanDistances[LowFuelIndex]);
	return FMath::Max(1.f,FlightSettings.MaxFuelDistance>0.f?FlightSettings.MaxFuelDistance:MaxFuelDistance);
}

void URocketProjectileComponent::EndScriptedThrust()
{
	if(!GetOwner() || !GetOwner()->HasAuthority() || FlightNetState.bPhysicsSimulating
		|| CurrentState==ERocketSimulationState::Idle || CurrentState==ERocketSimulationState::AtRest)return;
	if(GetPhysicsHandoffMode()==ERocketPhysicsHandoffMode::OnFuelDepleted)
	{
		HandoffToPhysicsSimulation();
		return;
	}
	if(CurrentState==ERocketSimulationState::SpentFalling)return;
	// Reaching the end of the low-fuel climb is the planned cutoff. Preserve
	// its predicted descent and rebounds; only an early interruption goes ballistic.
	if(LowFuelIndex>0 && AuthoritativeFlightNodes.IsValidIndex(LowFuelIndex+1)
		&& CurrentFlightPlanNodeIndex>=LowFuelIndex)
	{
		SetSimulationState(ERocketSimulationState::SpentFalling);
		GetOwner()->ForceNetUpdate();
		return;
	}
	// An external content/pressure cutoff may happen anywhere on the powered plan.
	// Continue from the current pose and momentum, never jump to its future apex.
	AuthoritativeFlightNodes.Reset();CumulativeFlightPlanDistances.Reset();
	FlightWaypoints.Reset();CurrentFlightPlanNodeIndex=0;CurrentSegmentDistTraveled=0;
	FlightNetState.bScriptedBallisticFall=true;
	bReboundApexArmed=false;
	SetSimulationState(ERocketSimulationState::SpentFalling);
	GetOwner()->ForceNetUpdate();
}
