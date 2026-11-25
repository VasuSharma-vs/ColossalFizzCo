#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopClock.generated.h"

UENUM(BlueprintType)
enum class EShopClockPhase : uint8
{
	Preparing,
	Open,
	Rush,
	Closed
};

UENUM()
enum class EShopClockMode : uint8
{
	Demo,
	RealWorld
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopClockState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Shop Clock") int32 DayNumber = 1;
	UPROPERTY(BlueprintReadOnly, Category="Shop Clock") int32 MinuteOfDay = 360;
	UPROPERTY(BlueprintReadOnly, Category="Shop Clock") EShopClockPhase Phase = EShopClockPhase::Preparing;
	UPROPERTY(BlueprintReadOnly, Category="Shop Clock") bool bIsDemo = true;

	bool operator==(const FShopClockState& Other) const = default;
};

namespace ShopClock
{
	COLOSSALFIZZCO_API FShopClockState EvaluateDemo(double ElapsedSeconds, double DaySeconds = 10800.0,
		double ClosingSeconds = 10.0);
	COLOSSALFIZZCO_API FShopClockState EvaluateRealWorld(const FDateTime& LocalTime, const FDateTime& FirstLocalTime);
}

/** The host publishes a single schedule for everyone in the opening shop. */
UCLASS(Config=Game, NotPlaceable)
class COLOSSALFIZZCO_API AShopClock : public AActor
{
	GENERATED_BODY()

public:
	AShopClock();
	static const AShopClock* Find(const UWorld* World);

	UFUNCTION(BlueprintPure, Category="Shop Clock") FShopClockState GetClockState() const { return State; }
	UFUNCTION(BlueprintPure, Category="Shop Clock") bool IsShopOpen() const { return State.Phase == EShopClockPhase::Open || IsRush(); }
	UFUNCTION(BlueprintPure, Category="Shop Clock") bool IsRush() const { return State.Phase == EShopClockPhase::Rush; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	friend class UShopClockSubsystem;
	void UpdateAuthorityState();

	UPROPERTY(Config) EShopClockMode ClockMode = EShopClockMode::Demo;
	UPROPERTY(Config) double DemoDaySeconds = 10800.0;
	UPROPERTY(Config) double DemoClosingSeconds = 10.0;
	UPROPERTY(Replicated) FShopClockState State;

	double DemoStartSeconds = 0.0;
	FDateTime FirstLocalTime;
};
