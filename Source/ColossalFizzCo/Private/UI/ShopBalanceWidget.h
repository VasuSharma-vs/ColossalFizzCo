#pragma once

#include "Blueprint/UserWidget.h"
#include "Shop/SharedWalletTypes.h"
#include "ShopBalanceWidget.generated.h"

class USharedTeamWalletComponent;
class UTextBlock;

/** Always-visible shared bank balance for one local player. */
UCLASS()
class UShopBalanceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetWallet(USharedTeamWalletComponent& InWallet);
	bool HasWallet() const { return Wallet.IsValid(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void UpdateBalance(FSharedWalletState State);
	TWeakObjectPtr<USharedTeamWalletComponent> Wallet;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BalanceText;
};
