#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/CablePullTypes.h"
#include "CableMovementConstraintComponent.generated.h"

class UCollisionCableComponent;
class UCharacterMovementComponent;
class AHoseCableActor;

/** Adapter on an existing holder/driver; does not replace its movement component. */
UCLASS(NotBlueprintable, Transient)
class COLOSSALFIZZCO_API UCableMovementConstraintComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCableMovementConstraintComponent();
    void RegisterCable(UCollisionCableComponent* Cable);
    void UnregisterCable(UCollisionCableComponent* Cable);
    /** Explicit cable resets/pool teleports establish a new movement baseline. */
    void ResetMovementHistory();
    static void RegisterWorldCable(UCollisionCableComponent* Cable);
    /** Queries the connected load without moving any actor. OldLocation is the driver's pre-movement location. */
    UFUNCTION(BlueprintCallable,Category="Cable|Pull") FCablePullResult QueryConnectedPull(FVector OldLocation,FVector RequestedLocation,float DeltaSeconds,float AvailableForceNewtons) const;
    /** Server only. Moves remote bodies with sweeps; the caller applies AllowedLocation to the driver. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Cable|Pull") FCablePullResult TryPullConnectedChain(FVector OldLocation,FVector RequestedLocation,float DeltaSeconds,float AvailableForceNewtons);
    FVector ConstrainRequestedLocation(const FVector& OldLocation, const FVector& RequestedLocation) const;
    virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    TArray<TWeakObjectPtr<UCollisionCableComponent>> Cables;
    FVector PreviousLocation = FVector::ZeroVector;
    bool bApplying = false;
    UFUNCTION() void OnCharacterMoved(float Dt, FVector OldLocation, FVector OldVelocity);
    void ApplyRestriction(const FVector& OldLocation, const FVector& OldVelocity, float DeltaSeconds);
    FVector LimitHeldCharacterMovement(UCharacterMovementComponent& Movement, const FVector& OldLocation,
        const FVector& RequestedLocation, const FVector& OldVelocity, float DeltaSeconds) const;
    double GetConnectedBrakingSpeed(const FVector& OldLocation, const FVector& PlanarRequest, float DeltaSeconds,
        float AvailableForceNewtons, double Deceleration) const;
    bool ConstrainHoseSwing(const AHoseCableActor& Hose, const FVector& OldLocation, const FVector& RequestedLocation,
        FVector& AllowedLocation, FVector& Pivot) const;
    FVector ConstrainToFixedReach(const FVector& OldLocation, const FVector& RequestedLocation, float DeltaSeconds) const;
    float GetAvailablePullForce() const;
};
