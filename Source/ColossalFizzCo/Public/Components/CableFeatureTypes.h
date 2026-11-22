#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "CableFeatureTypes.generated.h"

class USceneComponent;

UENUM(BlueprintType)
enum class ECableFeature : uint8
{
    WorldCollision, SelfCollision, Sleeping, PointWeight, GrabbingAndPulling, MovementRestrictions, VisualSubdivision
};

/** Gameplay drive rate and physical force are deliberately separate units. */
USTRUCT(BlueprintType)
struct FCableGrabSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm/s")) float MovementRate = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="N")) float MaxForceNewtons = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm")) float InfluenceRadius = 150;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float FalloffExponent = 1;
    /** A connector moving toward a hand applies force to the hose without tethering the player. */
    UPROPERTY() bool bApproachingConnector = false;
};

USTRUCT(BlueprintType)
struct FCableGrabState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Participant = nullptr;
    UPROPERTY(BlueprintReadOnly) int32 PointIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantize10 Target = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FCableGrabSettings Settings;
    UPROPERTY() TObjectPtr<USceneComponent> TargetComponent = nullptr;
    UPROPERTY() FName SocketName;
    UPROPERTY() FVector TargetOffset = FVector::ZeroVector;
    UPROPERTY() FVector ParticipantOffset = FVector::ZeroVector;
};

/** Bounded-rate authoritative snapshots; sleeping cables do not stream frames. */
USTRUCT()
struct FCableSimulationSnapshot
{
    GENERATED_BODY()
    UPROPERTY() TArray<FVector_NetQuantize10> Positions;
    UPROPERTY() TArray<uint8> SleepingPoints;
    UPROPERTY() TArray<FCableGrabState> Grabs;
    UPROPERTY() bool bSleeping = false;
    UPROPERTY() uint32 Revision = 0;
};
