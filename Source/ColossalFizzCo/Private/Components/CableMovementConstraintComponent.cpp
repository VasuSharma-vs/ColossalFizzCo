#include "Components/CableMovementConstraintComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Resources/HoseCableActor.h"

namespace
{
    constexpr double MinimumCarrySpeedFraction = .25;
    constexpr double CarryReferenceMassFraction = .05;

    bool IsHeldByCharacter(const UCollisionCableComponent& Cable, const ACharacter& Character)
    {
        if (const auto* Hose = Cast<AHoseCableActor>(Cable.GetOwner()); Hose && Hose->Cable == &Cable
            && Hose->FindHandHeldConnector(&Character) != INDEX_NONE)
            return true;

        return Cable.GetActiveGrabs().ContainsByPredicate([&Character](const auto& Grab)
        {
            return !Grab.Settings.bApproachingConnector && IsValid(Grab.Participant)
                && (Grab.Participant == &Character || Grab.Participant == Character.GetController());
        });
    }
}

UCableMovementConstraintComponent::UCableMovementConstraintComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCableMovementConstraintComponent::RegisterCable(UCollisionCableComponent* Cable)
{
    if (!IsValid(Cable)) return;
    Cables.AddUnique(Cable);
    ResetMovementHistory();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        Character->OnCharacterMovementUpdated.AddUniqueDynamic(this, &UCableMovementConstraintComponent::OnCharacterMoved);
    SetComponentTickEnabled(!Cast<ACharacter>(GetOwner()));
}

void UCableMovementConstraintComponent::ResetMovementHistory()
{
    if(GetOwner())PreviousLocation=GetOwner()->GetActorLocation();
}

void UCableMovementConstraintComponent::UnregisterCable(UCollisionCableComponent* Cable)
{
    Cables.RemoveAll([&](const auto& Value) { return !Value.IsValid() || Value.Get() == Cable; });
    if (Cables.IsEmpty())
    {
        if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
            Character->OnCharacterMovementUpdated.RemoveDynamic(this, &UCableMovementConstraintComponent::OnCharacterMoved);
        SetComponentTickEnabled(false);
    }
}

FVector UCableMovementConstraintComponent::ConstrainRequestedLocation(const FVector& OldLocation, const FVector& RequestedLocation) const
{
    FVector Result = RequestedLocation;
    // Multiple cables can restrict the same holder; enforce their intersection.
    for (int32 Pass = 0; Pass < 3; ++Pass)
        for (const auto& Weak : Cables)
            if (const auto* Cable = Weak.Get()) Result = Cable->ConstrainDriverLocation(GetOwner(), OldLocation, Result);
    return Result;
}

float UCableMovementConstraintComponent::GetAvailablePullForce() const
{
    if(const auto* Response=GetOwner()->FindComponentByClass<UCablePullResponseComponent>())
        return Response->bFixed?0.f:FMath::Max(0.f,Response->PullForceNewtons);
    if(const auto* Character=Cast<ACharacter>(GetOwner()))
        return Character->GetCharacterMovement()->Mass*Character->GetCharacterMovement()->GetMaxAcceleration()*.01f;
    float Force=0;
    for(const auto& Weak:Cables)if(const auto* Cable=Weak.Get())
    {
        float CableForce=FMath::Max(0.f,Cable->DefaultGrabSettings.MaxForceNewtons);
        for(const auto& Grab:Cable->GetActiveGrabs())if(Grab.Participant==GetOwner())CableForce=FMath::Max(CableForce,Grab.Settings.MaxForceNewtons);
        Force=FMath::Max(Force,CableForce);
    }
    return Force;
}

FVector UCableMovementConstraintComponent::LimitHeldCharacterMovement(UCharacterMovementComponent& Movement,
    const FVector& OldLocation, const FVector& RequestedLocation, const FVector& OldVelocity, float DeltaSeconds) const
{
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= UE_SMALL_NUMBER) return RequestedLocation;
    const FVector RequestedStep = RequestedLocation - OldLocation;
    const FVector PlanarStep = Movement.ProjectToGravityFloor(RequestedStep);
    const double StepDistance = PlanarStep.Size();
    if (StepDistance <= UE_KINDA_SMALL_NUMBER) return RequestedLocation;

    const ACharacter& Character = *Movement.GetCharacterOwner();
    bool bHasConnectedLoad = false;
    double LooseCableMass = 0.;
    for (const auto& Weak : Cables)
    {
        const auto* Cable = Weak.Get();
        if (!Cable || !Cable->bEnableMovementRestrictions || !IsHeldByCharacter(*Cable, Character)) continue;
        const auto* Hose = Cast<AHoseCableActor>(Cable->GetOwner());
        if (Hose && Hose->Cable == Cable && Hose->IsLooseHoseHeldBy(&Character))
            LooseCableMass += Cable->GetCableMassKg();
        else
            bHasConnectedLoad = true;
    }

    double TargetSpeed = DBL_MAX;
    if (LooseCableMass > 0.)
    {
        const double ReferenceMass = FMath::Max(1., Movement.Mass * CarryReferenceMassFraction);
        const double CarryFraction = FMath::Lerp(MinimumCarrySpeedFraction, 1., ReferenceMass / (ReferenceMass + LooseCableMass));
        TargetSpeed = Movement.GetMaxSpeed() * CarryFraction;
    }
    const bool bWalking = Movement.IsMovingOnGround();
    const float Braking = Movement.GetMaxBrakingDeceleration();
    const double Deceleration = FMath::Max(1.f, Braking > 0.f ? Braking : Movement.GetMaxAcceleration());
    if (bWalking && bHasConnectedLoad)
        TargetSpeed = FMath::Min(TargetSpeed, GetConnectedBrakingSpeed(OldLocation, OldLocation + PlanarStep,
            DeltaSeconds, GetAvailablePullForce(), Deceleration));
    if (TargetSpeed == DBL_MAX) return RequestedLocation;

    const double PreviousSpeed = FVector::DotProduct(OldVelocity, PlanarStep / StepDistance);
    const double SpeedLimit = FMath::Max(TargetSpeed, PreviousSpeed - Deceleration * DeltaSeconds);
    const FVector PlanarVelocity = Movement.ProjectToGravityFloor(Movement.Velocity);
    Movement.Velocity += PlanarVelocity.GetClampedToMaxSize(SpeedLimit) - PlanarVelocity;

    const double TravelFraction = FMath::Min(1., SpeedLimit * DeltaSeconds / StepDistance);
    // Walking also scales the height of a step up a ramp. In the air keep the
    // full gravity displacement and only limit motion across the gravity plane.
    return bWalking ? OldLocation + RequestedStep * TravelFraction
        : RequestedLocation - PlanarStep * (1. - TravelFraction);
}

void UCableMovementConstraintComponent::ApplyRestriction(const FVector& OldLocation, const FVector& OldVelocity, float DeltaSeconds)
{
    if (bApplying || !GetOwner()) return;
    const APawn* Pawn = Cast<APawn>(GetOwner());
    const bool bPredicting = GetNetMode() == NM_Client;
    if (bPredicting && (!Pawn || !Pawn->IsLocallyControlled())) return;
    TGuardValue<bool> Guard(bApplying, true);
    const FVector Requested = GetOwner()->GetActorLocation();
    auto* Character = Cast<ACharacter>(GetOwner());
    auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    const auto* Grip = Movement ? Cables.FindByPredicate([Character](const auto& Weak)
    {
        const auto* Cable = Weak.Get();
        return Cable && Cable->bEnableMovementRestrictions && IsHeldByCharacter(*Cable, *Character);
    }) : nullptr;
    const bool bFallingWithGrip = Grip && Movement->IsFalling();
    const bool bWalkingWithGrip = Grip && Movement->IsMovingOnGround();
    const auto* Hose = bFallingWithGrip ? Cast<AHoseCableActor>((*Grip)->GetOwner()) : nullptr;
    FVector Allowed = Requested;
    FVector SwingPivot = FVector::ZeroVector;
    const bool bSwinging = Cables.Num() == 1 && Hose && Hose->Cable == Grip->Get()
        && ConstrainHoseSwing(*Hose, OldLocation, Requested, Allowed, SwingPivot);
    const FVector MovementRequest = !bSwinging && (bFallingWithGrip || bWalkingWithGrip)
        ? LimitHeldCharacterMovement(*Movement, OldLocation, Requested, OldVelocity, DeltaSeconds) : Requested;
    if (!bSwinging)
    {
        // Only lateral movement spends the player's pull force while airborne.
        const FVector PullOrigin = bFallingWithGrip ? OldLocation + Movement->GetGravitySpaceComponentZ(Requested - OldLocation) : OldLocation;
        Allowed = ConstrainRequestedLocation(PullOrigin, MovementRequest);
        if (bFallingWithGrip)
            Allowed = PullOrigin + Movement->ProjectToGravityFloor(Allowed - PullOrigin);
        if (!Allowed.Equals(PullOrigin, .001))
        {
            const float PullForce = GetAvailablePullForce();
            // Predict the same force and collision limits as the server. Only the server moves the connected bodies.
            if (bPredicting) Allowed = QueryConnectedPull(PullOrigin, Allowed, DeltaSeconds, PullForce).AllowedLocation;
            else if (GetOwner()->HasAuthority()) Allowed = TryPullConnectedChain(PullOrigin, Allowed, DeltaSeconds, PullForce).AllowedLocation;
        }
        if (bFallingWithGrip)
            Allowed = ConstrainToFixedReach(OldLocation, Allowed, DeltaSeconds);
    }

    FHitResult Hit;
    if (!Allowed.Equals(Requested, .001))
    {
        GetOwner()->SetActorLocation(Allowed, true, &Hit, ETeleportType::None);
        if (bSwinging && Hit.bBlockingHit)
        {
            // The projected arc can intersect a wall. Use the reachable part
            // of the attempted movement rather than pushing through the obstacle.
            const FVector Reachable = ConstrainToFixedReach(OldLocation, Requested, DeltaSeconds);
            GetOwner()->SetActorLocation(Reachable, true, nullptr, ETeleportType::None);
        }
        const FVector BlockedDirection = (MovementRequest - Allowed).GetSafeNormal();
        if (Movement && !bSwinging)
        {
            // A movable load may allow part of the step. Keep the speed that
            // actually made progress so the next frame starts from that speed.
            const double AllowedSpeed = bWalkingWithGrip && DeltaSeconds > UE_SMALL_NUMBER
                ? FMath::Max(0., FVector::DotProduct(GetOwner()->GetActorLocation() - OldLocation, BlockedDirection) / DeltaSeconds)
                : 0.;
            const double Outward = FVector::DotProduct(Movement->Velocity, BlockedDirection);
            Movement->Velocity -= BlockedDirection * FMath::Max(0., Outward - AllowedSpeed);
        }
        else if (auto* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()); Body && Body->IsSimulatingPhysics())
        {
            FVector Velocity = Body->GetPhysicsLinearVelocity();
            const double Outward = FVector::DotProduct(Velocity, BlockedDirection);
            if (Outward > 0) Body->SetPhysicsLinearVelocity(Velocity - BlockedDirection * Outward);
        }
    }
    if (bSwinging)
    {
        if (Hit.bBlockingHit)
            Movement->Velocity -= Hit.Normal * FMath::Min(0., FVector::DotProduct(Movement->Velocity, Hit.Normal));
        const FVector Outward = (GetOwner()->GetActorLocation() - SwingPivot).GetSafeNormal();
        Movement->Velocity -= Outward * FMath::Max(0., FVector::DotProduct(Movement->Velocity, Outward));
    }
    PreviousLocation = GetOwner()->GetActorLocation();
}

void UCableMovementConstraintComponent::OnCharacterMoved(float Dt, FVector OldLocation, FVector OldVelocity)
{
    ApplyRestriction(OldLocation, OldVelocity, Dt);
}

void UCableMovementConstraintComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt, TickType, TickFunction);
    ApplyRestriction(PreviousLocation, FVector::ZeroVector, Dt);
}

void UCableMovementConstraintComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        Character->OnCharacterMovementUpdated.RemoveDynamic(this, &UCableMovementConstraintComponent::OnCharacterMoved);
    Cables.Reset();
    Super::EndPlay(Reason);
}
