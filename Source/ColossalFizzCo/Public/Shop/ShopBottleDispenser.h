#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopBottleDispenser.generated.h"

class APropManager;
class UArrowComponent;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EShopDispenserSupply : uint8
{
	Bottle,
	Cap,
	Seal
};

/** Sells one supply item and animates the authored Blueprint lever. */
UCLASS(Blueprintable, meta=(DisplayName="Shop Supply Dispenser"))
class COLOSSALFIZZCO_API AShopBottleDispenser : public AActor
{
	GENERATED_BODY()

public:
	AShopBottleDispenser();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dispenser", meta=(ClampMin="0.1", UIMin="0.1", Units="s"))
	float LeverCycleSeconds=1.25f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dispenser")
	EShopDispenserSupply DispensedSupply=EShopDispenserSupply::Bottle;

	UFUNCTION(BlueprintPure, Category="Dispenser") bool IsLeverComponent(const UPrimitiveComponent* Component) const;
	UFUNCTION(BlueprintPure, Category="Dispenser") int64 GetBottlePriceCents() const { return GetDispensePriceCents(); }
	UFUNCTION(BlueprintPure, Category="Dispenser") int64 GetDispensePriceCents() const;
	FText GetDispensedSupplyLabel() const;
	UFUNCTION(BlueprintPure, Category="Dispenser") float GetCooldownRemaining() const;
	bool IsDropAreaBlocked() const { return bDropAreaBlocked; }
	bool TryDispense(APawn* Player, float MaxDistance);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void FindControls();
	bool HasLeverInView(APawn& Player, float MaxDistance) const;
	APropManager* FindShopManager() const;
	FName GetDispenseOffer() const;
	FTransform GetOutletSpawnTransform() const;
	void SetDropAreaBlocked(bool bBlocked);
	void RefreshDropAreaClearance();
	double GetServerTime() const;
	float GetCycleDuration() const;
	UFUNCTION() void OnRep_CycleStartTime();
	void UpdateLever();

	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> LeverJoint;
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> LeverComponents;
	UPROPERTY(Transient) TObjectPtr<UArrowComponent> SpawnArrow;
	UPROPERTY(ReplicatedUsing=OnRep_CycleStartTime) double CycleStartTime=-1.0;
	UPROPERTY(Replicated) bool bDropAreaBlocked=false;
	FTimerHandle DropAreaTimer;
	mutable TWeakObjectPtr<APropManager> CachedManager;
	FRotator RestRotation=FRotator::ZeroRotator;
	bool bDispensing=false;
};
