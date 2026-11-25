#pragma once

#include "CoreMinimal.h"
#include "PlayerUpgradeTypes.generated.h"

UENUM(BlueprintType)
enum class EPlayerUpgradeType : uint8
{
	EmptyBottleHandling,
	CapHandling,
	SealHandling,
	HandStability
};

UENUM(BlueprintType)
enum class EPlayerUpgradePurchaseResult : uint8
{
	Purchased,
	AlreadyPurchased,
	InsufficientFunds,
	RankChanged,
	MaxRank,
	Unavailable
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPlayerUpgradeRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade") EPlayerUpgradeType Type=EPlayerUpgradeType::EmptyBottleHandling;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="1")) int32 Rank=1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="0")) int32 RequiredPreviousRank=0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="1")) int64 PriceCents=0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="0")) int32 CarryCapacity=0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="0.5", ClampMax="1")) float ShakeMultiplier=1.f;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPlayerUpgradeState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 EmptyBottleCarryRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 CapCarryRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 SealCarryRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 HandStabilityRank=0;

	static int32 GetMaxRank(EPlayerUpgradeType Type);
	int32 GetRank(EPlayerUpgradeType Type) const;
	bool SetRank(EPlayerUpgradeType Type, int32 Rank);
	bool IsValid() const;
	bool operator==(const FPlayerUpgradeState& Other) const = default;
};
