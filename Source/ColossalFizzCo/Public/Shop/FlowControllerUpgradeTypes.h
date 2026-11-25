#pragma once

#include "Engine/DataTable.h"
#include "FlowControllerUpgradeTypes.generated.h"

UENUM(BlueprintType)
enum class EFlowControllerUpgradeType : uint8
{
	InputPorts,
	OutputPorts,
	Flow,
	Suction,
	Lever
};

UENUM(BlueprintType)
enum class EFlowControllerPurchaseResult : uint8
{
	Purchased,
	AlreadyPurchased,
	InsufficientFunds,
	RankChanged,
	MaxRank,
	Unavailable
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FFlowControllerUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade")
	EFlowControllerUpgradeType Type=EFlowControllerUpgradeType::InputPorts;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="1"))
	int32 Rank=1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="0"))
	int32 RequiredRank=0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrade", meta=(ClampMin="1"))
	int64 CostCents=0;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FFlowControllerUpgradeState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 InputPortsRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 OutputPortsRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 FlowRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 SuctionRank=0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Upgrades") int32 LeverRank=0;

	static int32 GetMaxRank(EFlowControllerUpgradeType Type);
	int32 GetRank(EFlowControllerUpgradeType Type) const;
	bool SetRank(EFlowControllerUpgradeType Type, int32 Rank);
	bool IsValid() const;
	bool operator==(const FFlowControllerUpgradeState& Other) const = default;
};
