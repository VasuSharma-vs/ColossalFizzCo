#include "UI/ShopTerminalWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/InputComponent.h"
#include "Components/ProgressBar.h"
#include "UI/ManualFillingPressureGauge.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopATMComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

namespace ShopTerminal
{
	FString Money(int64 Cents)
	{
		const int64 Dollars = Cents / 100, Remainder = FMath::Abs(Cents % 100);
		return FString::Printf(TEXT("%s$%lld.%02lld"), Cents < 0 ? TEXT("-") : TEXT(""), FMath::Abs(Dollars), Remainder);
	}
}

void UShopTerminalButton::Configure(UShopTerminalWidget* Owner, const FString& Label)
{
	Terminal = Owner;
	auto* Text = NewObject<UTextBlock>(this); Text->SetText(FText::FromString(Label)); Text->SetAutoWrapText(true); Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Text->SetFont(FCoreStyle::GetDefaultFontStyle(bLever ? "Bold" : "Regular",bLever ? 15 : 12));
	SetContent(Text);
	const FLinearColor Base=bLever ? FLinearColor(.16f,.57f,.44f) : FLinearColor(.095f,.19f,.23f);
	FButtonStyle Style;
	Style.SetNormal(FSlateRoundedBoxBrush(Base,7.f));
	Style.SetHovered(FSlateRoundedBoxBrush(Base*1.3f,7.f));
	Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.15f,.43f,.36f),7.f));
	Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.07f,.11f,.13f),7.f));
	Style.SetNormalPadding(FMargin(14,bLever ? 16 : 10)); Style.SetPressedPadding(FMargin(14,bLever ? 17 : 11)); SetStyle(Style);
	if (auto* ContentSlot=Cast<UButtonSlot>(Text->Slot)) { ContentSlot->SetHorizontalAlignment(HAlign_Left); ContentSlot->SetVerticalAlignment(VAlign_Center); }
	OnClicked.AddDynamic(this, &UShopTerminalButton::HandleClick);
	OnPressed.AddDynamic(this,&UShopTerminalButton::HandlePress);
	OnReleased.AddDynamic(this,&UShopTerminalButton::HandleRelease);
}
void UShopTerminalButton::HandleClick() { if (!bLever && Terminal.IsValid()) Terminal->HandleButton(this); }
void UShopTerminalButton::HandlePress() { if (bLever && Terminal.IsValid()) Terminal->BeginLever(ISID); }
void UShopTerminalButton::HandleRelease() { if (bLever && Terminal.IsValid()) Terminal->EndLever(); }

TSharedRef<SWidget> UShopTerminalWidget::RebuildWidget()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
	const bool Production=Station.IsValid() && Station->FindComponentByClass<UShopProductionStationComponent>();
	if (Production)
	{
		auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(); Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		BuildProductionDashboard(Canvas); WidgetTree->RootWidget=Canvas; SetIsFocusable(!bWorldFillingDisplay); return Super::RebuildWidget();
	}
	auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(Production ? 1080 : 720); Size->SetHeightOverride(Production ? 740 : 650);
	auto* Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetPadding(FMargin(26)); Border->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.025f,.06f,.075f,.99f),16.f)); Size->SetContent(Border);
	auto* Layout = WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Layout);
	auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Layout->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	auto* Contents = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Contents);
	Summary = WidgetTree->ConstructWidget<UTextBlock>(); Summary->SetAutoWrapText(true); Contents->AddChild(Summary);
	ResultText = WidgetTree->ConstructWidget<UTextBlock>(); ResultText->SetAutoWrapText(true); Contents->AddChild(ResultText);
	Body = WidgetTree->ConstructWidget<UVerticalBox>(); Contents->AddChild(Body);
	WidgetTree->RootWidget = Size;
	SetIsFocusable(true);
	return Super::RebuildWidget();
}
void UShopTerminalWidget::Configure(UShopPlayerInteractionComponent* InActions, AActor* InStation)
{
	Actions = InActions; Station = InStation;
	bWorldFillingDisplay = InStation && InStation->IsA<ASocketColaFillingMachine>();
	if (bWorldFillingDisplay) SetVisibility(ESlateVisibility::HitTestInvisible);
	InActions->OnActionResult.AddUniqueDynamic(this, &UShopTerminalWidget::ActionResult);
	LastContents.Empty(); RefreshCountdown = 0;
}
void UShopTerminalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (bWorldFillingDisplay) BindWorldFillingControls();
	Refresh(); // Populate the first visible frame rather than flashing an empty tree.
}

void UShopTerminalWidget::BindWorldFillingControls()
{
	auto* PC = GetOwningPlayer();
	if (!PC || !PC->IsLocalController() || WorldFillingInput) return;
	WorldFillingInput = NewObject<UInputComponent>(PC);
	WorldFillingInput->RegisterComponent();
	auto Bind = [this](const FKey& Key, EShopProductionAction Action, bool bShift = false)
	{
		FInputKeyBinding Binding(FInputChord(Key, bShift, false, false, false), IE_Pressed);
		Binding.bConsumeInput = false;
		Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &UShopTerminalWidget::UseWorldFillingControl, Action);
		WorldFillingInput->KeyBindings.Add(MoveTemp(Binding));
	};
	Bind(EKeys::X, EShopProductionAction::StopFilling);
	Bind(EKeys::V, EShopProductionAction::ResumeFilling);
	Bind(EKeys::R, EShopProductionAction::AttachCap);
	Bind(EKeys::R, EShopProductionAction::OpenCap, true);
	Bind(EKeys::T, EShopProductionAction::ApplyTemporarySeal);
	PC->PushInputComponent(WorldFillingInput);
}

void UShopTerminalWidget::UnbindWorldFillingControls()
{
	if (!IsValid(WorldFillingInput)) return;
	if (auto* PC = Cast<APlayerController>(WorldFillingInput->GetOwner())) PC->PopInputComponent(WorldFillingInput);
	WorldFillingInput->DestroyComponent();
	WorldFillingInput = nullptr;
}

void UShopTerminalWidget::UseWorldFillingControl(EShopProductionAction Action)
{
	if (!IsInViewport() || !Station.IsValid() || !Actions.IsValid() || !Actions->IsStationReachable(Station.Get())) return;
	auto* Production = Station->FindComponentByClass<UShopProductionStationComponent>();
	if (!Production) return;
	const auto& View = Production->View;
	if (!View.SessionID.IsValid() || View.SeatedBottleISID <= 0) return;
	int32 CapID = 0;
	if (Action == EShopProductionAction::StopFilling || Action == EShopProductionAction::ResumeFilling)
	{
		if (!View.bLeverHeld || View.LeverOperator != Actions->GetOwner()) return;
	}
	else
	{
		const auto* Bottle = View.Bottles.FindByPredicate([&View](const auto& Item) { return Item.ISID == View.SeatedBottleISID; });
		if (!Bottle) return;
		if (Action == EShopProductionAction::AttachCap)
		{
			if (Bottle->AttachedCapISID > 0)
			{
				if (Bottle->CapSealed >= 1) return;
				Action = EShopProductionAction::TurnCap;
			}
			else
			{
				if (View.Caps.IsEmpty()) return;
				CapID = View.Caps[0].ISID;
			}
		}
		else if (Action == EShopProductionAction::OpenCap && Bottle->AttachedCapISID <= 0) return;
		else if (Action == EShopProductionAction::ApplyTemporarySeal && (Bottle->bTemporarySealUsed || View.SealTrayCount <= 0)) return;
	}
	Actions->RequestProductionAction(Production, View.SessionID, Action, View.SeatedBottleISID, CapID);
}

void UShopTerminalWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	if (!CornerPanels.IsEmpty() && GetOwningPlayer())
	{
		int32 Width=0,Height=0; GetOwningPlayer()->GetViewportSize(Width,Height);
		if (Width>0 && Height>0)
		{
			const float DPI=FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this));
			const FVector2D LayoutSize = bWorldFillingDisplay ? FVector2D(660, 260) : FVector2D(1000, 650);
			const float Scale=FMath::Clamp(FMath::Min((Width/DPI-32.f)/float(LayoutSize.X),(Height/DPI-32.f)/float(LayoutSize.Y)),.35f,1.f);
			for (UWidget* Panel:CornerPanels) if (Panel) Panel->SetRenderScale(FVector2D(Scale,Scale));
		}
	}
	if (LocalLeverBottle>0)
	{
		LeverHeartbeat-=DeltaSeconds;
		if (!Actions.IsValid() || !Station.IsValid()) EndLever();
		else if (LeverHeartbeat<=0) { LeverHeartbeat=.1f; BeginLever(LocalLeverBottle); }
	}
	RefreshCountdown -= DeltaSeconds;
	if (RefreshCountdown <= 0) { RefreshCountdown = .2f; Refresh(); }
}
FReply UShopTerminalWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (bWorldFillingDisplay) return Super::NativeOnKeyDown(Geometry, Event);
	if (Event.GetKey() == EKeys::Escape) { Close(); return FReply::Handled(); }
	if (Event.GetKey()==EKeys::F && Station.IsValid())
		if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>(); Production && Production->bManualLeverMode)
		{ if (!Event.IsRepeat()) BeginLever(Production->View.SeatedBottleISID); return FReply::Handled(); }
	if ((Event.GetKey()==EKeys::X || Event.GetKey()==EKeys::V) && Station.IsValid() && Actions.IsValid())
		if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>(); Production && Production->bManualLeverMode)
		{
			if (!Event.IsRepeat()) Actions->RequestProductionAction(Production,Production->View.SessionID,
				Event.GetKey()==EKeys::X ? EShopProductionAction::StopFilling : EShopProductionAction::ResumeFilling,Production->View.SeatedBottleISID);
			return FReply::Handled();
		}
	if (Event.GetKey()==EKeys::R && Station.IsValid() && Actions.IsValid())
		if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>(); Production && Production->bManualLeverMode && !Event.IsRepeat())
		{
			const auto* Bottle=Production->View.Bottles.FindByPredicate([&](const auto& Item){return Item.ISID==Production->View.SeatedBottleISID;});
			if (Bottle && !Bottle->AttachedCapISID && !Production->View.Caps.IsEmpty())
			{ Actions->RequestProductionAction(Production,Production->View.SessionID,EShopProductionAction::AttachCap,Bottle->ISID,Production->View.Caps[0].ISID); return FReply::Handled(); }
		}
	if (Station.IsValid() && Actions.IsValid())
		if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>())
			if ((Production->bRequiresCooperativeCap || Production->bAllowSoloCap) && (Event.GetKey()==EKeys::G || Event.GetKey()==EKeys::R))
			{
				int32 BottleID=Production->View.ManualBottleISID;
				if (!BottleID)
				{
					for (const auto& Candidate:Production->View.Bottles) if (Candidate.AttachedCapISID>0 && Candidate.CapSealed<1)
					{
						if (BottleID) { BottleID=0; break; } // Explicit selection when several bottles are eligible.
						BottleID=Candidate.ISID;
					}
				}
				if (!Event.IsRepeat() && BottleID>0)
					Actions->RequestProductionAction(Production,Production->View.SessionID,
						Event.GetKey()==EKeys::G ? EShopProductionAction::PressCap : EShopProductionAction::TurnCap,BottleID);
				return FReply::Handled();
			}
	return Super::NativeOnKeyDown(Geometry, Event);
}
UTextBlock* UShopTerminalWidget::AddText(const FString& Value)
{
	auto* Text = WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Value)); Text->SetAutoWrapText(true);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",12)); Text->SetColorAndOpacity(FLinearColor(.68f,.78f,.79f));
	Body->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,8,0,6));
	return Text;
}
UShopTerminalButton* UShopTerminalWidget::AddButton(const FString& Label)
{
	auto* Button = WidgetTree->ConstructWidget<UShopTerminalButton>(); Button->Configure(this, Label); Body->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3)); return Button;
}
void UShopTerminalWidget::Refresh()
{
	if (!Summary) return;
	if (!Actions.IsValid() || !Station.IsValid() || !Actions->IsStationReachable(Station.Get())) { Close(); return; }
	if (bWorldFillingDisplay)
	{
		const auto* Machine = CastChecked<ASocketColaFillingMachine>(Station.Get());
		if (!Machine->bIsActive || Machine->bIsHeld) { Close(); return; }
		RefreshProduction(Machine->Production);
		return;
	}
	if (!Body) return;
	auto* Manager = APropManager::Get(this);
	if (!Manager || !Manager->SharedWallet || !Manager->ShopCheckout) return;
	Summary->SetText(FText::FromString(FString::Printf(TEXT("Shop bank: %s    Physical shop cash: %s\n"), *ShopTerminal::Money(Manager->SharedWallet->GetWalletState().BalanceCents), *ShopTerminal::Money(Manager->ShopCheckout->TeamCashCents))));
	if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>()) { RefreshProduction(Production); return; }
	auto* Scanner = Station->FindComponentByClass<UShopScannerComponent>();
	if (!Scanner)
	{
		if (LastContents == TEXT("ATM")) return;
		LastContents = TEXT("ATM"); Body->ClearChildren(); AddText(TEXT("ATM\nWithdraw a coin. Drop shop-owned coins into the deposit tray to bank them."));
		for (int32 D : {1,2,5,10,20,50,100}) AddButton(FString::Printf(TEXT("Withdraw $%d coin"), D))->Denomination = D;
		AddButton(TEXT("Close"))->bClose = true; return;
	}
	const auto State = Scanner->GetScannerState();
	FString Contents = State.SessionID.ToString()+FString::FromInt(int32(State.Phase));
	for (const auto& Item : State.Checkout.Items) Contents += FString::Printf(TEXT("/%d/%d/%d/%d"),Item.ISID,Item.bExpiredAtSale || Item.Snapshot.Price.bExpired,Item.bSaved,Item.bAvailable);
	for (const auto& Case : Manager->ShopCheckout->PendingExpiryCases) if (Case.CheckoutID==State.Checkout.CheckoutID) Contents+=Case.CaseID.ToString()+FString::FromInt(int32(Case.Choice));
	for (const auto& Candidate : State.ReplacementCandidates) Contents+=FString::Printf(TEXT("/R%d/%d/%d"),Candidate.ISID,Candidate.ContentID,Candidate.NominalPortions);
	TArray<FString> TextRows;
	TextRows.Add(FString::Printf(TEXT("%s\nBill: %s   Tender: %s   Change counted: %s\nChange required: %s"),
		State.Phase == EShopScannerPhase::Scanning ? TEXT("Scan bottles") : State.Phase == EShopScannerPhase::Checkout ? TEXT("Invoice locked — count physical payment") : TEXT("Sale completed"),
		*ShopTerminal::Money(State.Checkout.TotalCents), *ShopTerminal::Money(State.Checkout.TenderCents), *ShopTerminal::Money(State.Checkout.ChangeCents),
		*ShopTerminal::Money(FMath::Max(int64(0), State.Checkout.TenderCents - State.Checkout.TotalCents))));
	for (const auto& Group : State.Groups)
	{
		TextRows.Add(FString::Printf(TEXT("\n%s x%d   %s\nBase %s   Content discount %s   Temperature discount %s"), *Group.DisplayName.ToString(), Group.BottleISIDs.Num(),
			*ShopTerminal::Money(Group.FinalSubtotalCents), *ShopTerminal::Money(Group.OriginalSubtotalCents), *ShopTerminal::Money(Group.ContentDiscountCents), *ShopTerminal::Money(Group.TemperatureDiscountCents)));
	}
	for (const auto& Item : State.Checkout.Items)
	{
		TextRows.Add(FString::Printf(TEXT("Bottle #%d: %.0f%% full, %.1f C (optimum %.1f C), %s%s"), Item.ISID, Item.Snapshot.ContentLevel * 100,
			Item.Snapshot.Temperature, Item.Snapshot.OptimumTemperature, (Item.bExpiredAtSale || Item.Snapshot.Price.bExpired) ? TEXT("EXPIRED") : TEXT("fresh"), Item.bSaved ? TEXT(" — saved") : TEXT("")));
	}
	if (State.Phase==EShopScannerPhase::Completed)
	{
		int64 Adjusted=0,Owed=0; Manager->ShopCheckout->GetAdjustedRevenue(State.Checkout.CheckoutID,Adjusted,Owed);
		TextRows.Add(FString::Printf(TEXT("Adjusted sale revenue: %s   Bank deductions still owed: %s"),*ShopTerminal::Money(Adjusted),*ShopTerminal::Money(Owed)));
	}
	if (Contents==LastContents && CheckoutTextRows.Num()==TextRows.Num())
	{
		for (int32 I=0;I<TextRows.Num();++I) CheckoutTextRows[I]->SetText(FText::FromString(TextRows[I]));
		return; // Price/temperature/payment updates retain buttons, focus and scroll state.
	}
	LastContents=Contents; Body->ClearChildren(); CheckoutTextRows.Empty();
	int32 TextIndex=0;
	auto AddRow=[&](){ auto* Row=AddText(TextRows[TextIndex++]); CheckoutTextRows.Add(Row); };
	AddRow();
	for (const auto& Group:State.Groups) AddRow();
	for (const auto& Item:State.Checkout.Items)
	{
		AddRow();
		if (State.Phase == EShopScannerPhase::Scanning)
		{
			auto* Remove = AddButton(FString::Printf(TEXT("Remove bottle #%d"), Item.ISID)); Remove->Action = EShopScannerAction::RemoveItem; Remove->ISID = Item.ISID; Remove->SessionID = State.SessionID;
		}
	}
	auto ScannerButton = [&](const FString& Label, EShopScannerAction Action)
	{
		auto* Button = AddButton(Label); Button->Action = Action; Button->SessionID = State.SessionID; return Button;
	};
	if (State.Phase == EShopScannerPhase::Scanning)
	{
		ScannerButton(TEXT("Save scanned bottles"), EShopScannerAction::Save);
		ScannerButton(TEXT("Finalize and lock bill"), EShopScannerAction::LockBill);
	}
	if (State.Phase == EShopScannerPhase::Checkout) ScannerButton(TEXT("Complete paid checkout"), EShopScannerAction::Complete);
	if (State.Phase == EShopScannerPhase::Completed)
	{
		AddRow();
		for (const auto& Case : Manager->ShopCheckout->PendingExpiryCases) if (Case.CheckoutID == State.Checkout.CheckoutID && Case.Choice == EShopCompensationChoice::Unselected)
		{
			AddText(FString::Printf(TEXT("Expired bottle #%d: mandatory penalty %s. Choose compensation:"), Case.OriginalISID, *ShopTerminal::Money(Case.OriginalBaseCents)));
			ScannerButton(TEXT("Deduct another ") + ShopTerminal::Money(Case.OriginalBaseCents), EShopScannerAction::CashCompensation)->CaseID = Case.CaseID;
			for (const auto& Candidate : State.ReplacementCandidates) if (Candidate.ContentID == Case.ContentID && Candidate.NominalPortions == Case.NominalPortions)
			{
				auto* Replace = ScannerButton(FString::Printf(TEXT("Give fresh bottle #%d free"), Candidate.ISID), EShopScannerAction::ProvideReplacement); Replace->CaseID = Case.CaseID; Replace->ISID = Candidate.ISID;
			}
			AddText(TEXT("Place a full, fresh bottle of the same cola and capacity inside the scanner for a free replacement."));
		}
	}
	ScannerButton(State.Phase == EShopScannerPhase::Completed ? TEXT("Next customer") : TEXT("Clear / cancel checkout"), EShopScannerAction::Clear);
	AddButton(TEXT("Close"))->bClose = true;
}
void UShopTerminalWidget::HandleButton(const UShopTerminalButton* Button)
{
	if (Button->bClose) { Close(); return; }
	if (!Actions.IsValid() || !Station.IsValid()) return;
	if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>())
	{
		if (!Button->SupplyOfferID.IsNone()) Actions->RequestSupplyPurchase(Production,Button->SessionID,Button->SupplyOfferID,Button->SupplyQuantity);
		else if (Button->bProduction) Actions->RequestProductionAction(Production,Button->SessionID,Button->ProductionAction,Button->ISID,Button->OtherISID);
		return;
	}
	if (Button->Denomination > 0) Actions->RequestATMWithdrawal(Station->FindComponentByClass<UShopATMComponent>(), {Button->Denomination});
	else Actions->RequestScannerAction(Station->FindComponentByClass<UShopScannerComponent>(), Button->SessionID, Button->Action, Button->ISID, Button->CaseID);
}
void UShopTerminalWidget::ActionResult(int64 Sequence, EShopCheckoutResult Result)
{
	if (ResultText) ResultText->SetText(StaticEnum<EShopCheckoutResult>()->GetDisplayNameTextByValue(int64(Result)));
}
FReply UShopTerminalWidget::NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event)
{
	if (Event.GetKey()==EKeys::F && LocalLeverBottle>0) { EndLever(); return FReply::Handled(); }
	return Super::NativeOnKeyUp(Geometry,Event);
}
void UShopTerminalWidget::NativeOnFocusLost(const FFocusEvent& Event) { EndLever(); Super::NativeOnFocusLost(Event); }
void UShopTerminalWidget::NativeDestruct() { UnbindWorldFillingControls(); EndLever(); Super::NativeDestruct(); }
void UShopTerminalWidget::BeginLever(int32 BottleID)
{
	if (BottleID<=0 || !Actions.IsValid() || !Station.IsValid()) return;
	auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>();
	if (!Production || !Production->bManualLeverMode) return;
	LocalLeverBottle=BottleID; LeverHeartbeat=.1f;
	Actions->RequestProductionAction(Production,Production->View.SessionID,EShopProductionAction::HoldFillingLever,BottleID);
}
void UShopTerminalWidget::EndLever()
{
	if (!LocalLeverBottle) return;
	LocalLeverBottle=0;
	if (Actions.IsValid() && Station.IsValid())
		if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>())
			Actions->RequestProductionAction(Production,Production->View.SessionID,EShopProductionAction::ReleaseFillingLever,0);
}
void UShopTerminalWidget::Close()
{
	EndLever();
	if (Actions.IsValid() && Station.IsValid())
		if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>())
			if (Production->View.CapHolder==Actions->GetOwner() || Production->View.CapRotator==Actions->GetOwner())
				Actions->RequestProductionAction(Production,Production->View.SessionID,EShopProductionAction::LeaveCapRole,Production->View.ManualBottleISID);
	if (Actions.IsValid()) Actions->OnActionResult.RemoveDynamic(this, &UShopTerminalWidget::ActionResult);
	if (bWorldFillingDisplay) UnbindWorldFillingControls();
	RemoveFromParent();
	if (bWorldFillingDisplay) return;
	if (auto* PC = GetOwningPlayer()) { PC->SetInputMode(FInputModeGameOnly()); PC->SetShowMouseCursor(false); }
}
