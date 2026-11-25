#include "UI/ShopBalanceWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Styling/CoreStyle.h"
#include "UI/ShopMoneyText.h"

void UShopBalanceWidget::SetWallet(USharedTeamWalletComponent& InWallet)
{
	Wallet = &InWallet;
}

TSharedRef<SWidget> UShopBalanceWidget::RebuildWidget()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);

	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
	auto* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.025f, .065f, .08f, .95f), 10.f));
	Card->SetPadding(FMargin(14.f, 8.f));
	auto* Placement = Canvas->AddChildToCanvas(Card);
	Placement->SetAnchors(FAnchors(1.f, 1.f));
	Placement->SetAlignment(FVector2D(1.f, 1.f));
	Placement->SetPosition(FVector2D(-18.f, -18.f));
	Placement->SetAutoSize(true);

	auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Card->SetContent(Row);
	auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetText(NSLOCTEXT("ShopHUD", "BankLabel", "SHOP BANK"));
	Label->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10));
	Label->SetColorAndOpacity(FLinearColor(.94f, .7f, .36f));
	auto* LabelSlot = Row->AddChildToHorizontalBox(Label);
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));

	BalanceText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShopBankAmount"));
	BalanceText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 17));
	BalanceText->SetColorAndOpacity(FLinearColor(.58f, .94f, .83f));
	Row->AddChildToHorizontalBox(BalanceText)->SetVerticalAlignment(VAlign_Center);

	auto* Upgrades = WidgetTree->ConstructWidget<UTextBlock>();
	Upgrades->SetText(NSLOCTEXT("ShopHUD", "PlayerUpgradesShortcut", "H  Upgrades"));
	Upgrades->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 11));
	Upgrades->SetColorAndOpacity(FLinearColor(.75f, .83f, .82f));
	auto* UpgradesSlot = Row->AddChildToHorizontalBox(Upgrades);
	UpgradesSlot->SetVerticalAlignment(VAlign_Center);
	UpgradesSlot->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
	return Super::RebuildWidget();
}

void UShopBalanceWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (auto* Source = Wallet.Get())
	{
		Source->OnWalletChanged.AddUniqueDynamic(this, &UShopBalanceWidget::UpdateBalance);
		UpdateBalance(Source->GetWalletState());
	}
}

void UShopBalanceWidget::NativeDestruct()
{
	if (auto* Source = Wallet.Get()) Source->OnWalletChanged.RemoveDynamic(this, &UShopBalanceWidget::UpdateBalance);
	Super::NativeDestruct();
}

void UShopBalanceWidget::UpdateBalance(FSharedWalletState State)
{
	BalanceText->SetText(FText::FromString(ShopTerminal::Money(State.BalanceCents)));
}
