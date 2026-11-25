#include "UI/ShopBottleDispenserWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Styling/CoreStyle.h"
#include "UI/ShopMoneyText.h"

TSharedRef<SWidget> UShopBottleDispenserWidget::RebuildWidget()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);

	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
	auto* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.025f, .065f, .08f, .95f), 10.f));
	Card->SetPadding(FMargin(14.f, 9.f));
	auto* Placement = Canvas->AddChildToCanvas(Card);
	Placement->SetAnchors(FAnchors(1.f, 0.f));
	Placement->SetAlignment(FVector2D(1.f, 0.f));
	Placement->SetPosition(FVector2D(-18.f, 18.f));
	Placement->SetAutoSize(true);

	auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(Content);
	auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(Header);
	LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DispensedSupplyLabel"));
	LabelText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10));
	LabelText->SetColorAndOpacity(FLinearColor(.94f, .7f, .36f));
	auto* LabelSlot = Header->AddChildToHorizontalBox(LabelText);
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));

	PriceText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BottlePrice"));
	PriceText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 17));
	PriceText->SetColorAndOpacity(FLinearColor(.58f, .94f, .83f));
	Header->AddChildToHorizontalBox(PriceText)->SetVerticalAlignment(VAlign_Center);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BottleDispenserStatus"));
	StatusText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
	StatusText->SetColorAndOpacity(FLinearColor(.85f, .91f, .93f));
	Content->AddChildToVerticalBox(StatusText);
	return Super::RebuildWidget();
}

void UShopBottleDispenserWidget::UpdateReadout(const FText& SupplyLabel, int64 PriceCents, float CooldownRemaining,
	bool bAimingAtLever, bool bDropAreaBlocked)
{
	if (!LabelText || !PriceText || !StatusText) return;
	LabelText->SetText(SupplyLabel);
	PriceText->SetVisibility(PriceCents > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (PriceCents <= 0)
	{
		StatusText->SetText(NSLOCTEXT("ShopHUD", "DispenserUnavailable", "Unavailable"));
		return;
	}
	PriceText->SetText(FText::FromString(ShopTerminal::Money(PriceCents)));
	if (FMath::IsFinite(CooldownRemaining) && CooldownRemaining > 0.f)
	{
		FNumberFormattingOptions Options;
		Options.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
		const float RemainingTenths = FMath::CeilToFloat(CooldownRemaining * 10.f) / 10.f;
		StatusText->SetText(FText::Format(NSLOCTEXT("ShopHUD", "DispenserCooldown", "Ready in {0} s"),
			FText::AsNumber(RemainingTenths, &Options)));
		return;
	}
	if (bDropAreaBlocked)
	{
		StatusText->SetText(NSLOCTEXT("ShopHUD", "DispenserDropAreaBlocked", "Drop area blocked"));
		return;
	}
	StatusText->SetText(bAimingAtLever ? NSLOCTEXT("ShopHUD", "DispenserInteract", "E  Dispense")
		: NSLOCTEXT("ShopHUD", "DispenserAim", "Aim at the lever"));
}
