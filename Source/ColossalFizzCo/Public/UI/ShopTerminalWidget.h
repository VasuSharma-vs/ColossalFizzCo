#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Shop/PlayerUpgradesComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "ShopTerminalWidget.generated.h"

class UShopTerminalWidget;
class UVerticalBox;
class UTextBlock;
class UProgressBar;
class UManualFillingPressureGauge;
class UCanvasPanel;
class UBorder;
class UFlowControllerIcon;
class AColaFlowController;
class ASocketColaFillingMachine;
class UFlowControllerPressureDial;

UCLASS()
class COLOSSALFIZZCO_API UShopTerminalButton : public UButton
{
	GENERATED_BODY()
public:
	void Configure(UShopTerminalWidget* Owner, const FString& Label);
	EShopScannerAction Action = EShopScannerAction::Save;
	FGuid SessionID, CaseID;
	int32 ISID = 0, Denomination = 0;
	FName SupplyOfferID;
	int32 SupplyQuantity=1, OtherISID=0;
	bool bProduction=false;
	bool bLever=false;
	EShopProductionAction ProductionAction=EShopProductionAction::Fill;
	bool bClose = false;
	TOptional<EFlowControllerUpgradeType> FlowUpgradeSelection;
	TOptional<EFillingMachineUpgradeType> FillingUpgradeSelection;
	TOptional<EPlayerUpgradeType> PlayerUpgradeSelection;
	bool bBuyFlowUpgrade=false;
private:
	TWeakObjectPtr<UShopTerminalWidget> Terminal;
	UFUNCTION() void HandleClick();
	UFUNCTION() void HandlePress();
	UFUNCTION() void HandleRelease();
};

UCLASS()
class COLOSSALFIZZCO_API UShopTerminalProductionButton : public UShopTerminalButton
{
	GENERATED_BODY()
public:
	UShopTerminalProductionButton() { InitIsFocusable(false); }
};

UCLASS()
class COLOSSALFIZZCO_API UShopTerminalLeverButton : public UShopTerminalButton
{
	GENERATED_BODY()
public:
	UShopTerminalLeverButton() { bLever=true; InitIsFocusable(false); }
};

/** Shared checkout/ATM UI reads replicated state and sends actions through the owned player component. */
UCLASS(Blueprintable)
class COLOSSALFIZZCO_API UShopTerminalWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Configure(UShopPlayerInteractionComponent* InActions, AActor* InStation, bool bOpenUpgrades=false);
	void ConfigurePlayerUpgrades(UShopPlayerInteractionComponent* InActions);
	void HandleButton(const UShopTerminalButton* Button);
	void FlowUpgradePurchaseResult(FGuid OperationID, EFlowControllerPurchaseResult Result);
	void FillingUpgradePurchaseResult(FGuid OperationID, EFillingMachinePurchaseResult Result);
	void Close(bool bReleaseCapRole=true);
	void BeginLever(int32 BottleISID);
	void EndLever();
	bool IsShowingStation(const AActor* InStation) const { return Station.Get()==InStation; }
	bool IsWorldReadout() const { return bWorldFillingDisplay || (bFlowControllerDisplay && !bFlowUpgradeMenu); }
	bool IsFlowUpgradeMenu() const { return bFlowUpgradeMenu; }
	bool IsFillingUpgradeMenu() const { return bFillingUpgradeMenu; }
	bool IsPlayerUpgradeMenu() const { return bPlayerUpgradeMenu; }
	bool IsUpgradeMenu() const { return bFlowUpgradeMenu || bFillingUpgradeMenu || bPlayerUpgradeMenu; }
protected:
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
	virtual void NativeOnFocusLost(const FFocusEvent& Event) override;
	virtual void NativeDestruct() override;
private:
	friend class FShopTerminalWidgetStabilityTest;
	friend class FShopPlayerUpgradeMenuTest;
	friend class FManualColaFillingLoopTest;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Body;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HoseReadout;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ProductionSidebar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ProductionStatus;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ProductionHint;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FillReadout;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapTightnessReadout;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SealReadout;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> FillProgress;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> SealProgress;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> CapTightnessProgress;
	UPROPERTY(Transient) TObjectPtr<UManualFillingPressureGauge> PressureGauge;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SupplyTrayReadout;
	UPROPERTY(Transient) TObjectPtr<UShopTerminalButton> StopFlowButton;
	UPROPERTY(Transient) TObjectPtr<UShopTerminalButton> ResumeFlowButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> CornerPanels;
	UPROPERTY(Transient) TMap<int32,TObjectPtr<UTextBlock>> ProductionBottleLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CheckoutTextRows;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudFlavor;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudInPressure;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudOutPressure;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudInFlow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudOutFlow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudPerOutlet;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudBottleCount;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudLeverKey;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudUpgradeKey;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudMoveKey;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowHudPauseKey;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerIcon> FlowHudLeverIcon;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerIcon> FlowHudStatusIcon;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerIcon> FlowHudPauseIcon;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerPressureDial> FlowHudInletDial;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerPressureDial> FlowHudOutletDial;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> FlowHudInletMeter;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> FlowHudOutletMeter;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> FlowHudInPorts;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> FlowHudOutPorts;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> FlowHudLeverPips;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowUpgradeName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowUpgradeValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowUpgradeDescription;
	UPROPERTY(Transient) TObjectPtr<UShopTerminalButton> BuyFlowUpgradeButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UShopTerminalButton>> FlowUpgradeTabs;
	UPROPERTY(Transient) TArray<TObjectPtr<UFlowControllerIcon>> FlowUpgradeTabIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> FlowUpgradeRankPips;
	UPROPERTY(Transient) TObjectPtr<UBorder> FlowUpgradeNextCard;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerIcon> FlowUpgradeCompareArrow;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> FlowUpgradeCurrentGraphic;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> FlowUpgradeNextGraphic;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowUpgradeCurrentValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowUpgradeNextValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowUpgradeEffectValue;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerIcon> FlowUpgradeEffectIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FlowUpgradeBuyPrice;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerIcon> FlowUpgradeBuyIcon;
	UPROPERTY(Transient) TObjectPtr<UFlowControllerIcon> FlowUpgradeStatusIcon;
	EFlowControllerUpgradeType RenderedFlowUpgrade=EFlowControllerUpgradeType::InputPorts;
	int32 RenderedFlowRank=INDEX_NONE;
	TOptional<EFlowControllerPurchaseResult> LastFlowUpgradeResult;
	TWeakObjectPtr<UShopPlayerInteractionComponent> Actions;
	TWeakObjectPtr<AActor> Station;
	FString LastContents;
	float RefreshCountdown = 0;
	bool bWorldFillingDisplay = false;
	bool bFlowControllerDisplay = false;
	bool bFlowUpgradeMenu = false;
	bool bFillingUpgradeMenu = false;
	bool bPlayerUpgradeMenu = false;
	EFlowControllerUpgradeType SelectedFlowUpgrade=EFlowControllerUpgradeType::InputPorts;
	EFlowControllerUpgradeType PendingFlowUpgrade=EFlowControllerUpgradeType::InputPorts;
	EFillingMachineUpgradeType SelectedFillingUpgrade=EFillingMachineUpgradeType::CapTray;
	EFillingMachineUpgradeType PendingFillingUpgrade=EFillingMachineUpgradeType::CapTray;
	EPlayerUpgradeType SelectedPlayerUpgrade=EPlayerUpgradeType::EmptyBottleHandling;
	EPlayerUpgradeType PendingPlayerUpgrade=EPlayerUpgradeType::EmptyBottleHandling;
	TWeakObjectPtr<UPlayerUpgradesComponent> PlayerUpgrades;
	FGuid PendingUpgradeOperation;
	int32 DisplayedUpgradeRank=INDEX_NONE;
	int32 PendingUpgradeRank=INDEX_NONE;
	bool bWaitingForPurchasedRank=false;
	FText UpgradeFeedback;
	int32 LocalLeverBottle=0;
	float LeverHeartbeat=0;
	void Refresh();
	void BuildFlowControllerHud(UCanvasPanel* Canvas);
	void RefreshFlowController(const AColaFlowController& Flow);
	void BuildUpgradeMenu(UCanvasPanel* Canvas);
	void BuildFlowUpgradePanel(UVerticalBox& Column);
	void RefreshFlowUpgradeMenu(const AColaFlowController& Flow);
	void RefreshFlowUpgradePurchase(int64 Cost, int64 Balance, bool bHasWallet, bool bMaxed, bool bBusy);
	void HandleFlowUpgradeButton(const UShopTerminalButton& Button, AColaFlowController& Flow);
	void RefreshFillingUpgradeMenu(const ASocketColaFillingMachine& Table);
	void HandleFillingUpgradeButton(const UShopTerminalButton& Button, ASocketColaFillingMachine& Table);
	void RefreshPlayerUpgradeMenu();
	void HandlePlayerUpgradeButton(const UShopTerminalButton& Button);
	UFUNCTION() void PlayerUpgradePurchaseResult(FGuid OperationID, EPlayerUpgradePurchaseResult Result);
	void RefreshUpgradePurchaseButton(int64 Cost, bool bMaxed, bool bBusy);
	void RefreshProduction(UShopProductionStationComponent* Production);
	UTextBlock* AddText(const FString& Text);
	UShopTerminalButton* AddButton(const FString& Label);
	void BuildProductionDashboard(UCanvasPanel* Canvas);
	UTextBlock* AddSection(const FString& Title,const FString& Description,UVerticalBox* Target=nullptr);
	UShopTerminalButton* AddProductionAction(const FString& Label,EShopProductionAction Action,int32 BottleID=0,int32 OtherID=0,UVerticalBox* Target=nullptr);
	UFUNCTION() void ActionResult(int64 Sequence, EShopCheckoutResult Result);
};
