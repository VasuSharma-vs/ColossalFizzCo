#pragma once

#include "Blueprint/UserWidget.h"
#include "Shop/ShopClock.h"
#include "ShopClockWidget.generated.h"

class UTextBlock;

UCLASS()
class UShopClockWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateClock(const FShopClockState& State);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void RefreshReadout();

	FShopClockState ClockState;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DayText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PhaseText;
};
