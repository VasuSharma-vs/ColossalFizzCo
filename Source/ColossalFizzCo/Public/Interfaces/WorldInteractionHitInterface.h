#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "UObject/Interface.h"
#include "WorldInteractionHitInterface.generated.h"

class UPrimitiveComponent;

/** Contact information only. Impulse is momentum in kg cm/s, never an instruction to launch a character. */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FWorldInteractionHit
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category="World Hit") FGuid EventID;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") TObjectPtr<AActor> SourceActor = nullptr;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") TObjectPtr<UPrimitiveComponent> SourceComponent = nullptr;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") TObjectPtr<AActor> TargetActor = nullptr;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") TObjectPtr<UPrimitiveComponent> TargetComponent = nullptr;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") FHitResult Hit;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") FVector Velocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") FVector RelativeVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") FVector Impulse = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") float MassKg = 0;
	/** Request only: the receiver can decline force while still processing damage/events. */
	UPROPERTY(BlueprintReadWrite, Category="World Hit") bool bApplyHitForce = true;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") int32 ICID = 0;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") int32 ISMI = INDEX_NONE;
	UPROPERTY(BlueprintReadWrite, Category="World Hit") int32 ISID = 0;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UWorldInteractionHitInterface : public UInterface
{
	GENERATED_BODY()
};

/** Implement on an actor or component to own its response to reported contacts. */
class COLOSSALFIZZCO_API IWorldInteractionHitInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="World Interaction|Hit")
	void ReceiveWorldHit(const FWorldInteractionHit& Data);
};
