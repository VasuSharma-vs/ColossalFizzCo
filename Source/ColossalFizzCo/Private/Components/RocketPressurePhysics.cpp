#include "Components/RocketProjectileComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Props/InteractableProp.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Engine/World.h"

FVector URocketProjectileComponent::GetPressureThrustDirection() const
{
	// Cola exits through the cap/nozzle; reaction force points the opposite way.
	FVector ExhaustDirection=GetNozzleLocalDirection();
	if (const auto* Prop=Cast<AActivePropActor>(GetOwner()))
		if (const auto* Definition=Prop->ResolveProductDefinition())
			if (Definition->BottleMaterialType==EBottleMaterialType::Plastic)
				ExhaustDirection=Definition->CapAttachmentTransform.GetRotation().RotateVector(FVector::UpVector);
	// Existing scripted rockets may configure NozzleAxis as their travel axis.
	// Physical bottle exhaust follows the registered cap opening instead.
	return TargetPrimitive ? -TargetPrimitive->GetComponentQuat().RotateVector(ExhaustDirection).GetSafeNormal() : FVector::ZeroVector;
}

bool URocketProjectileComponent::StartPressurePhysicsFlight()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client
		|| (CurrentState!=ERocketSimulationState::Idle && CurrentState!=ERocketSimulationState::AtRest)) return false;
	AutoResolveTargetPrimitive();
	if (!TargetPrimitive || TargetPrimitive!=GetOwner()->GetRootComponent() || !TargetPrimitive->GetBodyInstance()) return false;
	SavePrimitiveSettings();
	if (auto* Prop=Cast<AActivePropActor>(GetOwner())) Prop->SetInMotion(true);
	TargetPrimitive->SetMobility(EComponentMobility::Movable);
	ConfigureFlightCollision(true);
	ResetWorldHitHistory();
	TargetPrimitive->SetEnableGravity(true);
	TargetPrimitive->SetSimulatePhysics(true);
	if (!TargetPrimitive->IsSimulatingPhysics()) { RestorePrimitiveSettings(); return false; }
	FlightNetState=FRocketFlightNetState();
	FlightNetState.bPhysicsSimulating=true;
	FlightNetState.bPressurePhysicsFlight=true;
	FlightNetState.ServerLaunchTime=GetWorld()->GetTimeSeconds();
	TotalAirTime=PhysicsSettledTimeAccumulated=0;
	GetOwner()->SetReplicateMovement(true);
	SetSimulationState(ERocketSimulationState::Rocketing);
	SetActive(true); SetComponentTickEnabled(true);
	GetOwner()->ForceNetUpdate(); OnRocketLaunched.Broadcast();
	return true;
}

void URocketProjectileComponent::ApplyPressureThrust(float Acceleration)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !IsPressureThrustActive()
		|| !TargetPrimitive || !TargetPrimitive->IsSimulatingPhysics() || !FMath::IsFinite(Acceleration) || Acceleration<=0) return;
	const FVector Direction=GetPressureThrustDirection();
	// Applying force at the nozzle lets Chaos resolve torque from the actual mass distribution.
	const float NozzleDistance=TargetPrimitive->Bounds.BoxExtent.GetMax();
	TargetPrimitive->AddForceAtLocation(Direction*TargetPrimitive->GetMass()*FMath::Clamp(Acceleration,0.f,6000.f),
		TargetPrimitive->Bounds.Origin-Direction*NozzleDistance);
}

void URocketProjectileComponent::FinishPressureThrust()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client
		|| (!IsPoweredFlight() && !(CurrentState==ERocketSimulationState::SpentFalling && !FlightNetState.bPhysicsSimulating))) return;
	if (!FlightNetState.bPressurePhysicsFlight)
	{
		// The scripted rocket owns its falling/bounce/handoff policy; depletion must
		// not override the Blueprint's chosen transition or select Chaos replication.
		EndScriptedThrust();
		return;
	}
	const auto Previous=CurrentState;
	CurrentState=FlightNetState.State=ERocketSimulationState::SpentFalling;
	// Avoid the legacy scripted falling initializer: the existing Chaos body keeps its momentum.
	if (auto* Prop=Cast<AActivePropActor>(GetOwner()))
	{
		Prop->SetInMotion(false);
		if (Prop->PhysicsSimComponent) Prop->PhysicsSimComponent->SetStateUnsettled(1.f);
	}
	SetComponentTickEnabled(true);
	GetOwner()->SetReplicateMovement(true);
	GetOwner()->ForceNetUpdate(); OnRocketStateChanged.Broadcast(CurrentState,Previous); OnRocketFuelDepleted.Broadcast();
}

void URocketProjectileComponent::RestoreReplicatedFlightState(const FRocketFlightNetState& Received)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) FlightNetState=Received;
}

void URocketProjectileComponent::ReapplyReplicatedFlightState()
{
	if (GetOwner() && !GetOwner()->HasAuthority() && FlightNetState.State!=ERocketSimulationState::Idle) OnRep_FlightNetState();
}

void URocketProjectileComponent::ApplyPressurePhysicsNetState()
{
	SavePrimitiveSettings();
	// Built-in actor physics replication supplies authoritative root transforms and velocity,
	// including late joiners. Clients never run the legacy independent launch/handoff paths.
	const auto Previous=CurrentState; CurrentState=FlightNetState.State;
	if (TargetPrimitive)
	{
		ConfigureFlightCollision(true);
		TargetPrimitive->SetEnableGravity(true); TargetPrimitive->SetSimulatePhysics(true);
		if(CurrentState==ERocketSimulationState::SpentFalling)
		{
			TargetPrimitive->SetLinearDamping(FMath::Max(0.f,FallingSettings.HandoffLinearDamping));
			TargetPrimitive->SetAngularDamping(FMath::Max(0.f,FallingSettings.HandoffAngularDamping));
		}
	}
	if (auto* Prop=Cast<AActivePropActor>(GetOwner()))
	{
		Prop->SetInMotion(CurrentState==ERocketSimulationState::Rocketing);
		if (Prop->PhysicsSimComponent && CurrentState!=ERocketSimulationState::Rocketing) Prop->PhysicsSimComponent->SetStateUnsettled(1.f);
	}
	SetComponentTickEnabled(false);
	if (GetOwner()) GetOwner()->SetReplicateMovement(true);
	if (CurrentState!=Previous) OnRocketStateChanged.Broadcast(CurrentState,Previous);
}
