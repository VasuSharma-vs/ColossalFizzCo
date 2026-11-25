#pragma once

#include "Blueprint/UserWidget.h"
#include "ShopBottleDispenserWidget.generated.h"

class UTextBlock;

UCLASS()
class UShopBottleDispenserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateReadout(const FText& SupplyLabel, int64 PriceCents, float CooldownRemaining, bool bAimingAtLever, bool bDropAreaBlocked);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LabelText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
};
