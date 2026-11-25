#pragma once

#include "Components/ArrowComponent.h"
#include "GameFramework/Actor.h"
#include "Shop/FlowControllerUpgradeTypes.h"
#include "ColaFlowController.generated.h"

class APawn;
class AManualColaFillingMachine;
class UBoxComponent;
class UHoseSocketComponent;
class UPrimitiveComponent;
class UShopProductionStationComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UTextRenderComponent;

UCLASS(ClassGroup=(Shop), meta=(DisplayName="Supply Output Mount"))
class COLOSSALFIZZCO_API UFlowControllerOutputMountComponent : public UArrowComponent
{
	GENERATED_BODY()

public:
	UFlowControllerOutputMountComponent();

	/** Positions and orientations relative to this mount. Index 0 is OUT 1. */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadOnly, Category="Supply Outputs", meta=(MakeEditWidget=true))
	TArray<FTransform> OutletPositions;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	friend class AColaFlowController;
	void EnsureOutletPositions(int32 Count);
};

/** Selects one cola inlet and splits its available flow and pressure among connected outlet hoses. */
UCLASS(Blueprintable, meta=(DisplayName="Flow Controller"))
class COLOSSALFIZZCO_API AColaFlowController : public AActor
{
	GENERATED_BODY()

public:
	AColaFlowController();
	static constexpr int32 MaxInletPorts=4;
	static constexpr int32 MaxOutletPorts=12;
	static constexpr int32 MaxLeverLevel=4;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller")
	TObjectPtr<UStaticMeshComponent> Housing;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller")
	TObjectPtr<UStaticMeshComponent> SelectorDial;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller")
	TObjectPtr<UStaticMeshComponent> SelectorPointer;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller")
	TObjectPtr<UStaticMeshComponent> Lever;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller|Upgrades")
	TObjectPtr<UStaticMeshComponent> UpgradeControl;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller|Upgrades")
	TObjectPtr<UTextRenderComponent> UpgradeLabel;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller|Sockets")
	TObjectPtr<UBoxComponent> SupplyInputVolume;
	/** Move or rotate this component to adjust the whole output bank. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller|Sockets")
	TObjectPtr<UFlowControllerOutputMountComponent> SupplyOutputMount;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller|Upgrades")
	int32 SelectorSocketCount=1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller|Upgrades")
	int32 FillerSocketCount=1;
	/** 1: hold, 2: toggle, 3: release when any bottle fills, 4: shut each full bottle's valve. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flow Controller|Upgrades")
	int32 LeverLevel=1;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, ReplicatedUsing=OnRep_UpgradeState, Category="Flow Controller|Upgrades")
	FFlowControllerUpgradeState UpgradeState;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades")
	TSoftObjectPtr<UDataTable> UpgradeDefinitions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flow Controller|Sockets", meta=(MakeEditWidget=true))
	TArray<FTransform> SelectorMounts;
	UPROPERTY(BlueprintReadOnly, Category="Flow Controller|Sockets", meta=(DeprecatedProperty, DeprecationMessage="Use the Supply Output Mount's Outlet Positions."))
	TArray<FTransform> FillerMounts;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades|Balance", meta=(ClampMin="0.01"))
	float BaseIntakeRate=1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades|Balance", meta=(ClampMin="0.01"))
	float BaseOutputRate=1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades|Balance", meta=(ClampMin="0"))
	float IntakeRateStep=1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades|Balance", meta=(ClampMin="0"))
	float OutputRateStep=1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades|Balance", meta=(ClampMin="0"))
	float BaseInletPressureTolerance=6.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades|Balance", meta=(ClampMin="0"))
	float ToleranceBonusPerFlowRank=.1f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flow Controller|Upgrades|Balance", meta=(ClampMin="0"))
	float SuctionStrengthStep=.25f;
	/** Flow rating of each connector. Exceeding it can eject the hose. Zero removes the flow limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flow Controller|Sockets", meta=(ClampMin="0"))
	float MaximumSocketFlowPortionsPerSecond=10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flow Controller|Flow", meta=(ClampMin="0.25"))
	float InletBufferPortions=2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flow Controller", meta=(ClampMin="0.05", Units="s"))
	float LeverTravelSeconds=.75f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing=UpdateSelectorDial, Category="Flow Controller|Flow")
	int32 SelectedInputIndex=0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	int32 SelectedContentID=0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	float PortionsPerSecondPerFiller=0.f;
	/** Number of working outlet sockets with a hose plugged in. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	int32 ConnectedOutletCount=0;
	/** Pressure provided by the selected inlet's upstream socket. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	float InletSupplyPressure=0.f;
	/** Allocated pressure at each connected outlet before the hose and receiving socket apply their limits. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	float PressurePerOutlet=0.f;
	/** Actual transfer from the selected supply hose, averaged over the latest sample. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	float InletPortionsPerSecond=0.f;
	/** Actual transfer into all connected outlet hoses, averaged over the latest sample. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	float OutletPortionsPerSecond=0.f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	bool bLeverHeld=false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	bool bFlowPaused=false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	int32 ActiveFillerCount=0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Flow Controller|Flow")
	bool bBatchSafeToRelease=true;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Flow Controller|Sockets")
	TArray<TObjectPtr<UHoseSocketComponent>> SelectorSockets;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Flow Controller|Sockets")
	TArray<TObjectPtr<UHoseSocketComponent>> FillerSockets;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Flow Controller")
	bool SelectInput(int32 Index);
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	int32 GetUpgradeRank(EFlowControllerUpgradeType Type) const { return UpgradeState.GetRank(Type); }
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	static int32 GetMaxUpgradeRank(EFlowControllerUpgradeType Type) { return FFlowControllerUpgradeState::GetMaxRank(Type); }
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	int64 GetNextUpgradeCost(EFlowControllerUpgradeType Type) const;
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	float GetUpgradeValue(EFlowControllerUpgradeType Type, int32 Rank) const;
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	float GetIntakeRate() const { return GetUpgradeValue(EFlowControllerUpgradeType::Flow, UpgradeState.FlowRank); }
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	float GetOutputRate() const { return GetOutputRateForFlowRank(UpgradeState.FlowRank); }
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	float GetOutputRateForFlowRank(int32 Rank) const;
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	float GetIntakeSuctionStrength() const { return GetUpgradeValue(EFlowControllerUpgradeType::Suction, UpgradeState.SuctionRank); }
	UFUNCTION(BlueprintPure, Category="Flow Controller|Upgrades")
	float GetInletPressureTolerance() const;
	float GetInletToleranceMultiplier(int32 Rank) const;
	bool CanOpenUpgrades(const APawn* Player, float Reach) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Flow Controller|Upgrades")
	EFlowControllerPurchaseResult PurchaseUpgrade(const APawn* Player, float Reach, EFlowControllerUpgradeType Type, int32 ExpectedRank, const FGuid& OperationID);
	void RestoreUpgradeState(const FFlowControllerUpgradeState& State);
	bool UsesLatchedLever() const { return LeverLevel>1; }
	bool UseSelector(const APawn* Player, float Reach);
	bool BeginLever(APawn* Player, float Reach);
	bool ReleaseLeverInput(const APawn* Player);
	bool EndLever(const APawn* Player);
	bool SetFlowPaused(const APawn* Player, bool bPaused);
	bool IsLeverHeldBy(const APawn* Player) const;
	bool IsDrivingTable(const AManualColaFillingMachine& Table, const AActor* Player) const;
	APawn* GetPlacementOperator() const { return PlacementOperator.Get(); }
	bool IsMachinePlacementActive() const { return bMachinePlacementActive; }
	bool BeginMachinePlacement(APawn& Player);
	bool FinishMachinePlacement(APawn& Player, const FTransform& Pose);
	void CancelMachinePlacement(APawn& Player);

private:
	UPROPERTY() TObjectPtr<UStaticMesh> SocketMesh;
	UPROPERTY(Transient) TArray<TObjectPtr<UHoseSocketComponent>> AllSelectorSockets;
	UPROPERTY(Transient) TArray<TObjectPtr<UHoseSocketComponent>> AllFillerSockets;
	UPROPERTY(Transient, Replicated) TObjectPtr<APawn> LeverOperator;
	UPROPERTY(Transient, Replicated) TObjectPtr<APawn> PlacementOperator;
	UPROPERTY(Transient, ReplicatedUsing=OnRep_MachinePlacement)
	bool bMachinePlacementActive=false;
	FQuat DialRestRotation=FRotator(0,0,90).Quaternion();
	float LeverReach=0.f;
	float FlowSampleSeconds=0.f;
	double LastReceivedUnits=0.;
	double LastSuppliedUnits=0.;
	TArray<TWeakObjectPtr<UShopProductionStationComponent>> ActiveStations;
	bool CanReachControl(const APawn* Player, const UPrimitiveComponent& Control, float Reach) const;
	bool CanUseControl(const APawn* Player, const UPrimitiveComponent& Control, float Reach) const;
	AManualColaFillingMachine* GetFillerTable(const UHoseSocketComponent& Outlet) const;
	void ReleaseBatch();
	void UpdateBatch();
	void RefreshOutletPositions();
	void RebuildSockets();
	void BuildBank(TArray<TObjectPtr<UHoseSocketComponent>>& Bank, const TArray<FTransform>& Mounts, int32 Count, bool bInlet);
	void UpdateSocketAvailability();
	void UpdateFlow();
	void SampleFlowRates(float DeltaSeconds);
	void UpdateMachinePlacement();
	void EndMachinePlacement();
	UFUNCTION() void OnRep_MachinePlacement();
	UFUNCTION() void OnRep_UpgradeState();
	UFUNCTION() void UpdateSelectorDial();
};
