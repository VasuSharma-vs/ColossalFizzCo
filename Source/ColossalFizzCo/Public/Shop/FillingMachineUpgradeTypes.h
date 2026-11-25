#pragma once

#include "CoreMinimal.h"
#include "FillingMachineUpgradeTypes.generated.h"

UENUM(BlueprintType)
enum class EFillingMachineUpgradeType : uint8
{
	CapTray,
	SealTray,
	PressureRegulator,
	TemperatureRegulator
};

UENUM(BlueprintType)
enum class EFillingMachinePurchaseResult : uint8
{
	Purchased,
	AlreadyPurchased,
	InsufficientFunds,
	RankChanged,
	MaxRank,
	Unavailable
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FFillingMachineUpgradePrice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="1"))
	int32 StartingCostCents=500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="1"))
	int32 CostIncreaseCents=200;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FFillingMachineUpgradeState
{
	GENERATED_BODY()

	static constexpr int32 MaxTrayCapacity=100;
	static constexpr int32 MaxPressureRank=100;
	static constexpr int32 TemperatureRankLimit=10000;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 CapTrayRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 SealTrayRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 PressureRegulatorRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 TemperatureRegulatorRank=0;

	int32 GetRank(EFillingMachineUpgradeType Type) const;
	bool SetRank(EFillingMachineUpgradeType Type, int32 Rank);
	bool IsValid() const;
};
