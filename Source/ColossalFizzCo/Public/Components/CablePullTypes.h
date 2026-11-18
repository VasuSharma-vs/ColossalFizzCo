#pragma once

#include "CoreMinimal.h"
#include "CablePullTypes.generated.h"

class UCollisionCableComponent;
class USceneComponent;

USTRUCT(BlueprintType)
struct FCablePullResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FVector AllowedLocation=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) float TotalMassKg=0;
    UPROPERTY(BlueprintReadOnly) float RequiredForceNewtons=0;
    UPROPERTY(BlueprintReadOnly) int32 MovedActorCount=0;
    UPROPERTY(BlueprintReadOnly) int32 VisitedCableCount=0;
    UPROPERTY(BlueprintReadOnly) bool bBlocked=false;
    UPROPERTY(BlueprintReadOnly) bool bTraversalLimitReached=false;
    /** Actual remote moves, from the farthest connected body back toward the driver. */
    UPROPERTY(BlueprintReadOnly) TArray<TObjectPtr<AActor>> MovedActors;
};

/** Native snapshot of a cable restraint; free endpoints have no driver or source. */
struct FCableMovementAttachment
{
    int32 PointIndex=INDEX_NONE;
    FVector Position=FVector::ZeroVector;
    TWeakObjectPtr<USceneComponent> Source;
    TWeakObjectPtr<AActor> Driver;
    FName SocketName=NAME_None;
    bool bFixed=false;
};
