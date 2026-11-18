#include "Components/CableMovementConstraintComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

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

void UCableMovementConstraintComponent::ApplyRestriction(const FVector& OldLocation,float DeltaSeconds)
{
    if (bApplying || !GetOwner()) return;
    const APawn* Pawn = Cast<APawn>(GetOwner());
    if (GetNetMode() == NM_Client && Pawn && !Pawn->IsLocallyControlled()) return;
    TGuardValue<bool> Guard(bApplying, true);
    const FVector Requested = GetOwner()->GetActorLocation();
    FVector Allowed = ConstrainRequestedLocation(OldLocation, Requested);
    if(GetOwner()->HasAuthority() && GetNetMode()!=NM_Client)
        Allowed=TryPullConnectedChain(OldLocation,Allowed,DeltaSeconds,GetAvailablePullForce()).AllowedLocation;
    if (!Allowed.Equals(Requested, .001))
    {
        FHitResult Hit;
        GetOwner()->SetActorLocation(Allowed, true, &Hit, ETeleportType::None);
        const FVector BlockedDirection = (Requested - Allowed).GetSafeNormal();
        if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        {
            auto* Movement = Character->GetCharacterMovement();
            const double Outward = FVector::DotProduct(Movement->Velocity, BlockedDirection);
            if (Outward > 0) Movement->Velocity -= BlockedDirection * Outward;
        }
        else if (auto* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()); Body && Body->IsSimulatingPhysics())
        {
            FVector Velocity = Body->GetPhysicsLinearVelocity();
            const double Outward = FVector::DotProduct(Velocity, BlockedDirection);
            if (Outward > 0) Body->SetPhysicsLinearVelocity(Velocity - BlockedDirection * Outward);
        }
    }
    PreviousLocation = GetOwner()->GetActorLocation();
}

void UCableMovementConstraintComponent::OnCharacterMoved(float Dt, FVector OldLocation, FVector OldVelocity)
{
    ApplyRestriction(OldLocation,Dt);
}

void UCableMovementConstraintComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt, TickType, TickFunction);
    ApplyRestriction(PreviousLocation,Dt);
}

void UCableMovementConstraintComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        Character->OnCharacterMovementUpdated.RemoveDynamic(this, &UCableMovementConstraintComponent::OnCharacterMoved);
    Cables.Reset();
    Super::EndPlay(Reason);
}
