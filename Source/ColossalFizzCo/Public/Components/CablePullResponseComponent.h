#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CablePullResponseComponent.generated.h"

/** Optional authored movement response for an object attached to one or more cables. */
UCLASS(ClassGroup=(Physics),meta=(BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UCablePullResponseComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCablePullResponseComponent();
    /** Mass used by connected pulls. Physics bodies without this component use their actual mass. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cable|Pull",meta=(ClampMin="0.001",Units="kg")) float WeightKg=10;
    /** Force needed before the object starts moving, in addition to its inertial load. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cable|Pull",meta=(ClampMin="0",Units="N")) float ResistanceNewtons=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cable|Pull",meta=(ClampMin="0",Units="N")) float PullForceNewtons=1000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cable|Pull") bool bFixed=false;
};
