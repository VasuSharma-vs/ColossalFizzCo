#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/WorldInteractionHitInterface.h"
#include "WorldHitResponseComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWorldHitReceived, const FWorldInteractionHit&, Data);

/** Optional actor-owned response adapter. Characters receive notification without automatic knockback. */
UCLASS(Blueprintable, ClassGroup=(Interaction), meta=(BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UWorldHitResponseComponent : public UActorComponent, public IWorldInteractionHitInterface
{
	GENERATED_BODY()
public:
	UWorldHitResponseComponent();
	UPROPERTY(BlueprintAssignable, Category="World Hit") FOnWorldHitReceived OnWorldHitReceived;
	UPROPERTY(Transient, BlueprintReadOnly, Category="World Hit") FWorldInteractionHit LastWorldHit;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Hit") bool bApplyHitForce = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Hit", meta=(ClampMin="0")) float ImpulseScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Hit", meta=(ClampMin="0", Units="cm/s")) float MaxSpeedChange = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Hit") TObjectPtr<UPrimitiveComponent> ResponsePrimitive;
	virtual void ReceiveWorldHit_Implementation(const FWorldInteractionHit& Data) override;
private:
	FGuid LastEventID;
};
