#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopClockSubsystem.generated.h"

class AShopClock;
class UShopClockWidget;

UCLASS()
class UShopClockSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

	AShopClock* GetClock() const { return Clock.Get(); }
	void RegisterClock(AShopClock& SharedClock);

private:
	void UpdateLocalWidgets();

	TWeakObjectPtr<AShopClock> Clock;
	UPROPERTY(Transient) TArray<TObjectPtr<UShopClockWidget>> LocalWidgets;
	double NextUpdateTime = 0.0;
};
