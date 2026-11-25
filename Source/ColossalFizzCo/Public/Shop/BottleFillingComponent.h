#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BottleFillingComponent.generated.h"
class UHoseTransferComponent;
class UResourceTransferEndpointComponent;

/** Add to filling equipment. Its owner supplies placement; Prop Manager owns the liquid transaction. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UBottleFillingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBottleFillingComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filling", meta = (ClampMin = "0.01")) float PortionsPerSecond = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filling", meta = (ClampMin = "1")) float MaxConnectionDistance = 300;
	/** Zero retains the equipment's physical-capacity behavior. Positive values stop at a selected content level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filling", meta = (ClampMin = "0")) float TargetContentLevel = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Filling") bool bIsFilling = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Filling") int32 SourceISID = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Filling") int32 TargetISID = 0;
	/** Measured receiver delivery, after the valve, hose and socket have accepted the flow. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Filling") float DeliveredPortionsPerSecond=0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Filling") float DeliveredSupplyPressure=0.f;
	UFUNCTION(BlueprintPure,Category="Filling") bool IsHoseFilling() const { return bIsFilling && ActiveHose.IsValid(); }
	void NotifyHoseDelivery(UHoseTransferComponent* Hose,float ActualUnits,float IncomingPressure,float ActualRate);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Filling") bool StartFilling(int32 Source, int32 Target);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Filling") void StopFilling();

private:
public:
	/** Server-only dead-man lease. Release RPC stops immediately; timeout handles lost focus/disconnect. */
	bool bRequireLever=false;
	TWeakObjectPtr<AActor> LeverOperator;
	double LeverExpiresAt=0;
	float PressurePerContentLevel=0;
private:
	FGuid SessionID;
	float AccumulatedSeconds = 0;
	TWeakObjectPtr<class APropManager> Manager;
	TWeakObjectPtr<UHoseTransferComponent> ActiveHose;
	TWeakObjectPtr<UResourceTransferEndpointComponent> ActiveReceivingPort;
	bool bRestoreHoseTick=false;
	bool ConnectionsInRange() const;
	float GetEffectiveTargetContentLevel() const;
};
