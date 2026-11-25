#include "UI/ShopTablePurchaseWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Math/Box2D.h"
#include "Props/InteractablePropManager.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UI/ShopMoneyText.h"

#define LOCTEXT_NAMESPACE "ShopTablePurchase"

namespace
{
	constexpr double MapWidth = 492.0;
	constexpr double MapHeight = 326.0;
	constexpr double MapPadding = 32.0;
	const FVector2D MarkerSize(34.0, 28.0);
	const FLinearColor Cream(.93f, .94f, .88f);
	const FLinearColor Muted(.57f, .68f, .69f);
	const FLinearColor Mint(.58f, .94f, .83f);
	const FLinearColor Ink(.025f, .065f, .08f);
	const FLinearColor ButtonColor(.08f, .18f, .21f);

	UTextBlock* MakeText(UWidgetTree& Tree, const FText& Text, int32 PointSize,
		const FLinearColor& Color, bool bBold = false, FName Name = NAME_None)
	{
		auto* Label = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(Text);
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", PointSize));
		Label->SetColorAndOpacity(Color);
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Label;
	}

	void StyleButton(UButton& Button, const FLinearColor& Color, const FMargin& Padding)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Color, 6.f));
		Style.SetHovered(FSlateRoundedBoxBrush(FMath::Lerp(Color, FLinearColor::White, .14f), 6.f));
		Style.SetPressed(FSlateRoundedBoxBrush(Color * .78f, 6.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.11f, .13f, .14f), 6.f));
		Style.SetNormalPadding(Padding);
		Style.SetPressedPadding(Padding);
		Button.SetStyle(Style);
	}

	void FillCanvas(UCanvasPanel& Canvas, UWidget& Widget, const FMargin& Padding = FMargin(0.f))
	{
		auto* Slot = Canvas.AddChildToCanvas(&Widget);
		Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		Slot->SetOffsets(Padding);
	}

	void AddMapLine(UWidgetTree& Tree, UCanvasPanel& Canvas, const FVector2D& Position, const FVector2D& Size)
	{
		auto* Line = Tree.ConstructWidget<UBorder>();
		Line->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.075f, .15f, .17f), 0.f));
		Line->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Slot = Canvas.AddChildToCanvas(Line);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}

	FText Money(int64 Cents)
	{
		return FText::FromString(ShopTerminal::Money(Cents));
	}
}

void UShopTableSelectionButton::Configure(UShopTablePurchaseWidget& InPicker, FName InOfferID)
{
	Picker = &InPicker;
	OfferID = InOfferID;
	OnClicked.AddUniqueDynamic(this, &UShopTableSelectionButton::Select);
}

void UShopTableSelectionButton::Select()
{
	if (auto* Widget = Picker.Get()) Widget->SelectOffer(OfferID);
}

void UShopTablePurchaseWidget::InitializePicker(TFunction<bool(FName, int64, FGuid)> InRequestPurchase,
	TFunction<void()> InCancel)
{
	RequestPurchase = MoveTemp(InRequestPurchase);
	OnCancel = MoveTemp(InCancel);
}

TSharedRef<SWidget> UShopTablePurchaseWidget::RebuildWidget()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
	SetIsFocusable(true);
	auto& Tree = *WidgetTree;
	auto* Canvas = Tree.ConstructWidget<UCanvasPanel>();
	Tree.RootWidget = Canvas;

	auto* Scrim = Tree.ConstructWidget<UBorder>();
	Scrim->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, .58f), 0.f));
	Scrim->SetVisibility(ESlateVisibility::HitTestInvisible);
	FillCanvas(*Canvas, *Scrim);
	auto* Scale = Tree.ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	Scale->SetStretchDirection(EStretchDirection::DownOnly);
	FillCanvas(*Canvas, *Scale, FMargin(16.f));

	auto* Card = Tree.ConstructWidget<UBorder>();
	Card->SetBrush(FSlateRoundedBoxBrush(Ink, 16.f, FLinearColor(.12f, .22f, .24f), 1.f));
	Card->SetPadding(FMargin(22.f, 18.f));
	auto* CardSlot = CastChecked<UScaleBoxSlot>(Scale->AddChild(Card));
	CardSlot->SetHorizontalAlignment(HAlign_Center);
	CardSlot->SetVerticalAlignment(VAlign_Center);
	auto* Column = Tree.ConstructWidget<UVerticalBox>();
	Card->SetContent(Column);

	auto* Header = Tree.ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	auto* Title = MakeText(Tree, LOCTEXT("Title", "Buy shop tables"), 23, Cream, true);
	Header->AddChildToHorizontalBox(Title)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	BalanceText = MakeText(Tree, FText::GetEmpty(), 14, Mint, true, TEXT("TablePurchaseBalance"));
	Header->AddChildToHorizontalBox(BalanceText)->SetVerticalAlignment(VAlign_Center);

	auto* Body = Tree.ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Body);
	auto* MapFrame = Tree.ConstructWidget<USizeBox>();
	MapFrame->SetWidthOverride(MapWidth);
	MapFrame->SetHeightOverride(MapHeight);
	Body->AddChildToHorizontalBox(MapFrame);
	auto* MapBackground = Tree.ConstructWidget<UBorder>();
	MapBackground->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.035f, .09f, .11f), 9.f));
	MapFrame->SetContent(MapBackground);
	auto* Map = Tree.ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TableMap"));
	MapBackground->SetContent(Map);

	constexpr double GridStep = 34.0;
	for (double X = GridStep; X < MapWidth - GridStep / 2.0; X += GridStep)
		AddMapLine(Tree, *Map, FVector2D(X, 11.0), FVector2D(1.0, MapHeight - 22.0));
	for (double Y = GridStep; Y < MapHeight - GridStep / 2.0; Y += GridStep)
		AddMapLine(Tree, *Map, FVector2D(11.0, Y), FVector2D(MapWidth - 22.0, 1.0));
	EmptyMapText = MakeText(Tree, LOCTEXT("NoOffers", "No tables are listed here"), 14, Cream, true);
	auto* EmptySlot = Map->AddChildToCanvas(EmptyMapText);
	EmptySlot->SetAnchors(FAnchors(.5f, .5f));
	EmptySlot->SetAlignment(FVector2D(.5f, .5f));
	EmptySlot->SetAutoSize(true);
	Markers = Tree.ConstructWidget<UCanvasPanel>();
	FillCanvas(*Map, *Markers);

	auto* DetailsFrame = Tree.ConstructWidget<USizeBox>();
	DetailsFrame->SetWidthOverride(226.f);
	DetailsFrame->SetHeightOverride(MapHeight);
	Body->AddChildToHorizontalBox(DetailsFrame)->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
	auto* Details = Tree.ConstructWidget<UVerticalBox>();
	DetailsFrame->SetContent(Details);
	SelectedNameText = MakeText(Tree, FText::GetEmpty(), 17, Cream, true);
	SelectedNameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Details->AddChildToVerticalBox(SelectedNameText);
	PriceText = MakeText(Tree, FText::GetEmpty(), 25, Mint, true, TEXT("TablePurchasePrice"));
	Details->AddChildToVerticalBox(PriceText)->SetPadding(FMargin(0.f, 3.f, 0.f, 4.f));
	StatusText = MakeText(Tree, FText::GetEmpty(), 12, Muted);
	StatusText->SetAutoWrapText(true);
	Details->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.f, 0.f, 0.f, 11.f));
	OfferList = Tree.ConstructWidget<UScrollBox>();
	Details->AddChildToVerticalBox(OfferList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto* Footer = Tree.ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
	auto* CancelButton = Tree.ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TablePurchaseCancel"));
	StyleButton(*CancelButton, ButtonColor, FMargin(16.f, 9.f));
	CancelButton->SetContent(MakeText(Tree, LOCTEXT("Cancel", "Cancel"), 13, Cream, true));
	CancelButton->OnClicked.AddUniqueDynamic(this, &UShopTablePurchaseWidget::Cancel);
	auto* CancelSlot = Footer->AddChildToHorizontalBox(CancelButton);
	CancelSlot->SetHorizontalAlignment(HAlign_Left);
	CancelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	BuyButton = Tree.ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TablePurchaseBuy"));
	StyleButton(*BuyButton, Mint, FMargin(18.f, 9.f));
	BuyText = MakeText(Tree, LOCTEXT("Buy", "Buy table"), 13, Ink, true);
	BuyButton->SetContent(BuyText);
	BuyButton->OnClicked.AddUniqueDynamic(this, &UShopTablePurchaseWidget::Buy);
	Footer->AddChildToHorizontalBox(BuyButton);
	RebuildOffers();
	RefreshSelection();
	return Super::RebuildWidget();
}

void UShopTablePurchaseWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Refresh();
	if (auto* World = GetWorld())
		World->GetTimerManager().SetTimer(RefreshTimer, this, &UShopTablePurchaseWidget::Refresh, .2f, true);
}

void UShopTablePurchaseWidget::NativeDestruct()
{
	if (auto* World = GetWorld()) World->GetTimerManager().ClearTimer(RefreshTimer);
	Super::NativeDestruct();
}

FReply UShopTablePurchaseWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UShopTablePurchaseWidget::Refresh()
{
	auto* World = GetWorld();
	if (!World || !Markers) return;
	TActorIterator<APropManager> Manager(World);
	const auto* Wallet = Manager ? Manager->SharedWallet.Get() : nullptr;
	const int64 NewBalance = Wallet ? Wallet->GetWalletState().BalanceCents : 0;
	bool bSelectionChanged = BalanceCents != NewBalance || bWalletAvailable != (Wallet != nullptr);
	BalanceCents = NewBalance;
	bWalletAvailable = Wallet != nullptr;

	TArray<FShopTableOffer> NewOffers;
	UShopTableOfferComponent::GetOffers(World, NewOffers);
	bool bLayoutChanged = NewOffers.Num() != Offers.Num();
	for (int32 I = 0; !bLayoutChanged && I < NewOffers.Num(); ++I)
	{
		const auto& Old = Offers[I];
		const auto& New = NewOffers[I];
		bLayoutChanged = Old.OfferID != New.OfferID || !Old.WorldLocation.Equals(New.WorldLocation, .5)
			|| !Old.DisplayName.EqualTo(New.DisplayName);
		bSelectionChanged |= Old.PriceCents != New.PriceCents || Old.bOwned != New.bOwned;
	}
	Offers = MoveTemp(NewOffers);
	for (const auto& Offer : Offers)
		if (Offer.bOwned) AwaitingOwnership.Remove(Offer.OfferID);
	if (!SelectedOffer.IsNone() && !FindOffer(SelectedOffer))
	{
		SelectedOffer = NAME_None;
		PurchaseFeedback = FText::GetEmpty();
		bSelectionChanged = true;
	}
	if (bLayoutChanged) RebuildOffers();
	if (bLayoutChanged || bSelectionChanged) RefreshSelection();
}

void UShopTablePurchaseWidget::RebuildOffers()
{
	Markers->ClearChildren();
	OfferList->ClearChildren();
	MarkerButtons.Reset();
	ListButtons.Reset();
	EmptyMapText->SetVisibility(Offers.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (Offers.IsEmpty()) return;

	FBox2D Bounds(ForceInit);
	for (const auto& Offer : Offers) Bounds += FVector2D(Offer.WorldLocation.X, Offer.WorldLocation.Y);
	const FVector2D Center = Bounds.GetCenter();
	const FVector2D Span = Bounds.GetSize();
	const double PixelsPerCm = FMath::Min((MapWidth - 2.0 * MapPadding) / FMath::Max(Span.X, 400.0),
		(MapHeight - 2.0 * MapPadding) / FMath::Max(Span.Y, 400.0));
	for (const auto& Offer : Offers)
	{
		auto* Marker = WidgetTree->ConstructWidget<UShopTableSelectionButton>();
		Marker->Configure(*this, Offer.OfferID);
		Marker->SetToolTipText(Offer.DisplayName);
		auto* MarkerIcon = WidgetTree->ConstructWidget<UVerticalBox>();
		for (int32 Shelf = 0; Shelf < 3; ++Shelf)
		{
			auto* LineSize = WidgetTree->ConstructWidget<USizeBox>();
			LineSize->SetWidthOverride(14.f);
			LineSize->SetHeightOverride(1.f);
			auto* Line = WidgetTree->ConstructWidget<UBorder>();
			Line->SetBrush(FSlateRoundedBoxBrush(Cream, 0.f));
			LineSize->SetContent(Line);
			auto* LineSlot = MarkerIcon->AddChildToVerticalBox(LineSize);
			LineSlot->SetPadding(FMargin(0.f, 1.f));
		}
		auto* IconSlot = CastChecked<UButtonSlot>(Marker->AddChild(MarkerIcon));
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetVerticalAlignment(VAlign_Center);
		auto* MarkerSlot = Markers->AddChildToCanvas(Marker);
		const FVector2D Offset = FVector2D(Offer.WorldLocation.X, Offer.WorldLocation.Y) - Center;
		MarkerSlot->SetPosition(FVector2D(MapWidth / 2.0, MapHeight / 2.0)
			+ FVector2D(Offset.X, -Offset.Y) * PixelsPerCm);
		MarkerSlot->SetSize(MarkerSize);
		MarkerSlot->SetAlignment(FVector2D(.5, .5));
		MarkerButtons.Add(Marker);

		auto* ListButton = WidgetTree->ConstructWidget<UShopTableSelectionButton>();
		ListButton->Configure(*this, Offer.OfferID);
		auto* Label = MakeText(*WidgetTree, Offer.DisplayName, 12, Cream, true);
		Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		ListButton->SetContent(Label);
		CastChecked<UScrollBoxSlot>(OfferList->AddChild(ListButton))->SetPadding(FMargin(0.f, 0.f, 6.f, 4.f));
		ListButtons.Add(ListButton);
	}
}

void UShopTablePurchaseWidget::RefreshSelection()
{
	if (!BalanceText) return;
	BalanceText->SetText(bWalletAvailable
		? FText::Format(LOCTEXT("BankAmount", "Shop bank  {0}"), Money(BalanceCents))
		: LOCTEXT("BankUnavailable", "Shop bank unavailable"));
	const auto* Offer = FindOffer(SelectedOffer);
	const bool bOwned = Offer && IsOwned(*Offer);
	const bool bCanBuy = Offer && CanPurchase(*Offer);
	SelectedNameText->SetText(Offer ? Offer->DisplayName : LOCTEXT("SelectTable", "Select a table"));
	SelectedNameText->SetVisibility(Offers.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	PriceText->SetVisibility(Offer ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (Offer) PriceText->SetText(bOwned ? LOCTEXT("Owned", "Owned") : Money(Offer->PriceCents));

	FText Status = PurchaseFeedback;
	if (PendingOperation.IsValid())
		Status = LOCTEXT("Purchasing", "Purchasing…");
	else if (Status.IsEmpty() && Offer && !bOwned && bWalletAvailable && Offer->PriceCents > BalanceCents)
		Status = FText::Format(LOCTEXT("NeedFunds", "Need {0} more in the shop bank."), Money(Offer->PriceCents - BalanceCents));
	StatusText->SetText(Status);
	StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	BuyButton->SetIsEnabled(bCanBuy);
	BuyText->SetColorAndOpacity(bCanBuy ? Ink : Muted);
	BuyText->SetText(bOwned ? LOCTEXT("OwnedButton", "Already owned") : LOCTEXT("Buy", "Buy table"));

	const auto Update = [this](UShopTableSelectionButton& Button, const FMargin& ButtonPadding)
	{
		const auto* Item = FindOffer(Button.GetOfferID());
		if (!Item) return;
		const bool bSelected = Item->OfferID == SelectedOffer;
		const FLinearColor Color = bSelected ? FLinearColor(.16f, .45f, .38f)
			: IsOwned(*Item) ? FLinearColor(.11f, .15f, .16f) : ButtonColor;
		StyleButton(Button, Color, ButtonPadding);
		Button.SetIsEnabled(!PendingOperation.IsValid());
	};
	for (UShopTableSelectionButton* Marker : MarkerButtons)
	{
		Update(*Marker, FMargin(1.f));
		CastChecked<UCanvasPanelSlot>(Marker->Slot)->SetZOrder(Marker->GetOfferID() == SelectedOffer ? 1 : 0);
	}
	for (UShopTableSelectionButton* ListButton : ListButtons) Update(*ListButton, FMargin(9.f, 5.f));
}

const FShopTableOffer* UShopTablePurchaseWidget::FindOffer(FName OfferID) const
{
	return Offers.FindByPredicate([OfferID](const FShopTableOffer& Offer) { return Offer.OfferID == OfferID; });
}

bool UShopTablePurchaseWidget::IsOwned(const FShopTableOffer& Offer) const
{
	return Offer.bOwned || AwaitingOwnership.Contains(Offer.OfferID);
}

bool UShopTablePurchaseWidget::CanPurchase(const FShopTableOffer& Offer) const
{
	return RequestPurchase && bWalletAvailable && !PendingOperation.IsValid() && !IsOwned(Offer)
		&& Offer.PriceCents > 0 && BalanceCents >= Offer.PriceCents;
}

void UShopTablePurchaseWidget::SelectOffer(FName OfferID)
{
	if (PendingOperation.IsValid() || !FindOffer(OfferID)) return;
	SelectedOffer = OfferID;
	PurchaseFeedback = FText::GetEmpty();
	RefreshSelection();
}

void UShopTablePurchaseWidget::Buy()
{
	// The price the player saw is also the price sent for server validation.
	const auto* Offer = FindOffer(SelectedOffer);
	if (!Offer || !CanPurchase(*Offer)) return;
	const int64 Price = Offer->PriceCents;
	PendingOperation = FGuid::NewGuid();
	PendingOffer = Offer->OfferID;
	PurchaseFeedback = FText::GetEmpty();
	RefreshSelection();
	const FGuid Operation = PendingOperation;
	if (!RequestPurchase(PendingOffer, Price, Operation))
		HandlePurchaseResult(Operation, EShopTablePurchaseResult::Unavailable);
}

void UShopTablePurchaseWidget::Cancel()
{
	if (OnCancel) OnCancel();
}

void UShopTablePurchaseWidget::HandlePurchaseResult(FGuid OperationID, EShopTablePurchaseResult Result)
{
	if (!PendingOperation.IsValid() || PendingOperation != OperationID) return;
	switch (Result)
	{
	case EShopTablePurchaseResult::Purchased:
		AwaitingOwnership.Add(PendingOffer);
		PurchaseFeedback = LOCTEXT("Purchased", "Table purchased.");
		break;
	case EShopTablePurchaseResult::AlreadyOwned:
		AwaitingOwnership.Add(PendingOffer);
		PurchaseFeedback = LOCTEXT("AlreadyOwned", "This table is already owned.");
		break;
	case EShopTablePurchaseResult::InsufficientFunds:
		PurchaseFeedback = LOCTEXT("InsufficientFunds", "The shop bank has insufficient funds.");
		break;
	case EShopTablePurchaseResult::PriceChanged:
		PurchaseFeedback = LOCTEXT("PriceChanged", "The price changed. Review the updated price.");
		break;
	case EShopTablePurchaseResult::Unavailable:
		PurchaseFeedback = LOCTEXT("Unavailable", "This table is unavailable.");
		break;
	}
	PendingOperation.Invalidate();
	PendingOffer = NAME_None;
	Refresh();
	RefreshSelection();
}

#undef LOCTEXT_NAMESPACE
