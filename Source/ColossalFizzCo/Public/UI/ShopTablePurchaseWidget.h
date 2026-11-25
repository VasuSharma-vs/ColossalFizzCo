#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Engine/TimerHandle.h"
#include "Shop/ShopTableOfferComponent.h"
#include "ShopTablePurchaseWidget.generated.h"

class UCanvasPanel;
class UScrollBox;
class UShopTablePurchaseWidget;
class UTextBlock;

UCLASS()
class UShopTableSelectionButton : public UButton
{
	GENERATED_BODY()

public:
	void Configure(UShopTablePurchaseWidget& InPicker, FName InOfferID);
	FName GetOfferID() const { return OfferID; }

private:
	UFUNCTION() void Select();

	TWeakObjectPtr<UShopTablePurchaseWidget> Picker;
	FName OfferID;
};

UCLASS()
class COLOSSALFIZZCO_API UShopTablePurchaseWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializePicker(TFunction<bool(FName, int64, FGuid)> InRequestPurchase, TFunction<void()> InCancel);
	void HandlePurchaseResult(FGuid OperationID, EShopTablePurchaseResult Result);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
	friend class UShopTableSelectionButton;

	void Refresh();
	void RebuildOffers();
	void RefreshSelection();
	void SelectOffer(FName OfferID);
	const FShopTableOffer* FindOffer(FName OfferID) const;
	bool IsOwned(const FShopTableOffer& Offer) const;
	bool CanPurchase(const FShopTableOffer& Offer) const;

	UFUNCTION() void Buy();
	UFUNCTION() void Cancel();

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Markers;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> OfferList;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmptyMapText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BalanceText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UButton> BuyButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyText;
	UPROPERTY(Transient) TArray<TObjectPtr<UShopTableSelectionButton>> MarkerButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UShopTableSelectionButton>> ListButtons;

	TArray<FShopTableOffer> Offers;
	TFunction<bool(FName, int64, FGuid)> RequestPurchase;
	TFunction<void()> OnCancel;
	TSet<FName> AwaitingOwnership;
	FName SelectedOffer;
	FName PendingOffer;
	FGuid PendingOperation;
	FText PurchaseFeedback;
	int64 BalanceCents = 0;
	FTimerHandle RefreshTimer;
	bool bWalletAvailable = false;
};
