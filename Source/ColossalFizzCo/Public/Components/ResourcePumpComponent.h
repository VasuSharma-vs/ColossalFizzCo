#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ResourcePumpComponent.generated.h"

/** A motor or pump that supplies pressure to connected resource ports. */
UCLASS(ClassGroup=(Resources), meta=(BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UResourcePumpComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UResourcePumpComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Pump") bool bRunning=false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Pump", meta=(ClampMin="0")) float PushPressure=2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Pump", meta=(ClampMin="0")) float PullPressure=0;
};
