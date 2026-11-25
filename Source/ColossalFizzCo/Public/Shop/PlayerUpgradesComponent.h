#pragma once

#include "Components/ActorComponent.h"
#include "Shop/PlayerUpgradeTypes.h"
#include "PlayerUpgradesComponent.generated.h"

class APlayerController;
class APropManager;
class UPlayerUpgradesSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerUpgradesChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerUpgradePurchaseResult, FGuid, OperationID, EPlayerUpgradePurchaseResult, Result);

/** Per-profile ranks replicated to the owner independently of the currently possessed pawn. */
UCLASS(Config=Game, BlueprintType, ClassGroup=(Shop))
class COLOSSALFIZZCO_API UPlayerUpgradesComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerUpgradesComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static constexpr int32 LoadedBottleCarryLimit=1;

	UFUNCTION(BlueprintPure, Category="Player Upgrades") static UPlayerUpgradesComponent* Find(const AActor* Player);
	UFUNCTION(BlueprintPure, Category="Player Upgrades") static int32 GetCarryCapacityFor(const AActor* Player, EPlayerUpgradeType Type);
	UFUNCTION(BlueprintPure, Category="Player Upgrades") static float GetCarryShakeMultiplierFor(const AActor* Player);
	UFUNCTION(BlueprintPure, Category="Player Upgrades") static int32 GetMaxUpgradeRank(EPlayerUpgradeType Type);

	UFUNCTION(BlueprintPure, Category="Player Upgrades") FPlayerUpgradeState GetUpgradeState() const { return UpgradeState; }
	UFUNCTION(BlueprintPure, Category="Player Upgrades") int32 GetUpgradeRank(EPlayerUpgradeType Type) const { return UpgradeState.GetRank(Type); }
	UFUNCTION(BlueprintPure, Category="Player Upgrades") float GetUpgradeValue(EPlayerUpgradeType Type, int32 Rank) const;
	UFUNCTION(BlueprintPure, Category="Player Upgrades") int32 GetCarryCapacity(EPlayerUpgradeType Type) const;
	UFUNCTION(BlueprintPure, Category="Player Upgrades") float GetCarryShakeMultiplier() const;
	UFUNCTION(BlueprintPure, Category="Player Upgrades") int64 GetNextUpgradeCostCents(EPlayerUpgradeType Type) const;

	UFUNCTION(BlueprintCallable, Category="Player Upgrades")
	bool RequestPurchase(EPlayerUpgradeType Type, int32 ExpectedRank, const FGuid& OperationID);

	UPROPERTY(BlueprintAssignable, Category="Player Upgrades") FOnPlayerUpgradesChanged OnUpgradesChanged;
	UPROPERTY(BlueprintAssignable, Category="Player Upgrades") FOnPlayerUpgradePurchaseResult OnPurchaseResult;

	static void RestoreForWorld(APropManager& Manager);

private:
	friend class UPlayerUpgradesSubsystem;

	/** Editable rank definitions from Config/DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category="Player Upgrades") TArray<FPlayerUpgradeRow> UpgradeDefinitions;
	UPROPERTY(ReplicatedUsing=OnRep_UpgradeState) FPlayerUpgradeState UpgradeState;

	static UPlayerUpgradesComponent* AttachFor(APlayerController& Player);
	const FPlayerUpgradeRow* FindUpgradeRow(EPlayerUpgradeType Type, int32 Rank) const;
	void RestoreFrom(const APropManager* Manager);
	EPlayerUpgradePurchaseResult Purchase(EPlayerUpgradeType Type, int32 ExpectedRank, const FGuid& OperationID);

	UFUNCTION() void OnRep_UpgradeState();
	UFUNCTION(Server, Reliable) void Server_Purchase(EPlayerUpgradeType Type, int32 ExpectedRank, FGuid OperationID);
	UFUNCTION(Client, Reliable) void Client_PurchaseResult(FGuid OperationID, EPlayerUpgradePurchaseResult Result);
};
