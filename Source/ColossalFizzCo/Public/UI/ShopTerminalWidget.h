#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "ShopTerminalWidget.generated.h"

class UShopTerminalWidget;
class UVerticalBox;
class UTextBlock;

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
private:
	TWeakObjectPtr<UShopTerminalWidget> Terminal;
	UFUNCTION() void HandleClick();
	UFUNCTION() void HandlePress();
	UFUNCTION() void HandleRelease();
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
	void Configure(UShopPlayerInteractionComponent* InActions, AActor* InStation);
	void HandleButton(const UShopTerminalButton* Button);
	void Close();
	void BeginLever(int32 BottleISID);
	void EndLever();
	bool IsShowingStation(const AActor* InStation) const { return Station.Get()==InStation; }
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
	friend class FManualColaFillingLoopTest;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Body;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultText;
	UPROPERTY(Transient) TMap<int32,TObjectPtr<UTextBlock>> ProductionBottleLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CheckoutTextRows;
	TWeakObjectPtr<UShopPlayerInteractionComponent> Actions;
	TWeakObjectPtr<AActor> Station;
	FString LastContents;
	float RefreshCountdown = 0;
	int32 LocalLeverBottle=0;
	float LeverHeartbeat=0;
	void Refresh();
	void RefreshProduction(UShopProductionStationComponent* Production);
	UTextBlock* AddText(const FString& Text);
	UShopTerminalButton* AddButton(const FString& Label);
	UFUNCTION() void ActionResult(int64 Sequence, EShopCheckoutResult Result);
};
