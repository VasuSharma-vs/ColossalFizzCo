#include "UI/ShopTerminalWidget.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

namespace ShopTerminal { FString Money(int64 Cents); }

void UShopTerminalWidget::RefreshProduction(UShopProductionStationComponent* Production)
{
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->ShopSupplies) return;
	const auto* Catalog=Manager->ShopSupplies->Catalog.LoadSynchronous();
	const auto& View=Production->View;
	auto BottleDescription=[](const FShopProductionItem& Bottle)
	{
		return FString::Printf(TEXT("Bottle #%d — %.0f%% full, %.0f%% sealed | Residual pressure %.2f | Temporary seal %.1fs"),Bottle.ISID,Bottle.ContentLevel*100,Bottle.CapSealed*100,Bottle.ResidualPressure,Bottle.TemporarySealSeconds);
	};
	for (const auto& Bottle:View.Bottles)
		if (const auto* Label=ProductionBottleLabels.Find(Bottle.ISID); Label && IsValid(Label->Get()))
			(*Label)->SetText(FText::FromString(BottleDescription(Bottle)));
	FString Contents=View.SessionID.ToString()+FString::FromInt(Production->bManualLeverMode ? 0 : View.bFilling);
	if (Production->bManualLeverMode)
	{
		Contents+=FString::Printf(TEXT("/seat%d/source%d"),View.SeatedBottleISID,View.ConnectedSourceISID);
		Summary->SetText(FText::FromString(Summary->GetText().ToString()+FString::Printf(TEXT("Manual filling — no electricity\nSupply #%d | Bottle holder #%d | %s\nHold F or the lever button to fill; release to stop. Solo: temporary seal then R, or wait for residual pressure <= %.2f then R. A teammate can fill while you seal another bottle."),View.ConnectedSourceISID,View.SeatedBottleISID,View.bFilling ? TEXT("FLOWING") : TEXT("STOPPED"),Production->SafeResidualPressure)));
	}
	if (Production->bRequiresCooperativeCap || Production->bAllowSoloCap)
	{
		Contents+=FString::Printf(TEXT("/manual/%d/%s/%s"),View.ManualBottleISID,*GetNameSafe(View.CapHolder),*GetNameSafe(View.CapRotator));
		const FString State=FString::Printf(TEXT("\nManual Wheel Rotator — manual cap control, no electricity\nHold: %s | Turn: %s\nHolding force %.2f | Effectiveness %.0f%% | Rotation %.0f%%\nSolo: tap R after applying a temporary seal or venting. Optional helper: G holds while a teammate turns R. Escape leaves your role."),
			View.CapHolder ? (View.CapHolder==Actions->GetOwner() ? TEXT("you") : TEXT("teammate")) : TEXT("available"),
			View.CapRotator ? (View.CapRotator==Actions->GetOwner() ? TEXT("you") : TEXT("teammate")) : TEXT("available"),
			View.HoldingForce,View.HoldingEfficiency*100,View.RotationProgress*100);
		Summary->SetText(FText::FromString(Summary->GetText().ToString()+State));
	}
	auto Describe=[&](const TArray<FShopProductionItem>& Items)
	{
		// Quantities change during leakage/filling. Replacing buttons on each
		// change discards mouse-down events before mouse-up can claim a role.
		for (const auto& Item:Items) Contents+=FString::Printf(TEXT("/%d/%d/%d/%d"),Item.ISID,Item.CapSealed>=1,Item.AttachedCapISID,Item.bTemporarySealUsed);
	};
	Describe(View.Bottles); Describe(View.Cylinders); Describe(View.Caps);
	if (Contents==LastContents) return;
	LastContents=Contents; Body->ClearChildren(); ProductionBottleLabels.Reset();
	AddText(TEXT("Normal Cola supplies and bottling\nPlace a cylinder, bottle and compatible cap in their marked input areas."));
	if (!View.SessionID.IsValid()) { AddText(TEXT("Waiting for station synchronization...")); AddButton(TEXT("Close"))->bClose=true; return; }
	auto Action=[&](const FString& Label, EShopProductionAction Kind, int32 Bottle, int32 Other=0)
	{
		auto* Button=AddButton(Label); Button->bProduction=true; Button->ProductionAction=Kind;
		Button->ISID=Bottle; Button->OtherISID=Other; Button->SessionID=View.SessionID;
	};
	if (View.bFilling) Action(TEXT("Stop filling"),EShopProductionAction::StopFilling,0);
	if (Production->bManualLeverMode)
		for (const auto& Source:View.Cylinders)
			Action(FString::Printf(TEXT("Connect single supply pipe to %s #%d"),*Source.Name.ToString(),Source.ISID),EShopProductionAction::ConnectSupply,0,Source.ISID);
	for (const auto& Bottle:View.Bottles)
	{
		ProductionBottleLabels.Add(Bottle.ISID,AddText(BottleDescription(Bottle)));
		if (Production->bManualLeverMode)
		{
			Action(TEXT("Align and seat in bottle holder"),EShopProductionAction::AlignBottle,Bottle.ISID);
			if (View.SeatedBottleISID==Bottle.ISID && !Bottle.AttachedCapISID)
			{
				auto* Lever=WidgetTree->ConstructWidget<UShopTerminalLeverButton>();
				Lever->Configure(this,TEXT("HOLD filling lever — release to stop (or hold F)")); Lever->ISID=Bottle.ISID; Body->AddChild(Lever);
			}
			if (!Bottle.bTemporarySealUsed && Bottle.CapSealed<1)
				Action(FString::Printf(TEXT("Apply temporary seal — %.1f seconds, one use per fill"),Production->TemporarySealDuration),EShopProductionAction::ApplyTemporarySeal,Bottle.ISID);
		}
		if (!Bottle.AttachedCapISID && Bottle.CapSealed<=0)
		{
			if (!Production->bManualLeverMode && !View.bFilling) for (const auto& Source:View.Cylinders)
				Action(TEXT("Fill from ")+Source.Name.ToString()+FString::Printf(TEXT(" #%d"),Source.ISID),EShopProductionAction::Fill,Bottle.ISID,Source.ISID);
			for (const auto& Cap:View.Caps) Action(FString::Printf(TEXT("Attach cap #%d"),Cap.ISID),EShopProductionAction::AttachCap,Bottle.ISID,Cap.ISID);
		}
		if (Bottle.AttachedCapISID)
		{
			if (Bottle.CapSealed<1)
			{
				if (Production->bRequiresCooperativeCap || Production->bAllowSoloCap)
				{
					Action(TEXT("Hold cap role — tap G"),EShopProductionAction::HoldCapRole,Bottle.ISID);
					Action(TEXT("Twist cap — tap R"),EShopProductionAction::TurnCap,Bottle.ISID);
					if (View.CapHolder==Actions->GetOwner() || View.CapRotator==Actions->GetOwner()) Action(TEXT("Leave cap role"),EShopProductionAction::LeaveCapRole,Bottle.ISID);
				}
				else Action(TEXT("Close compatible cap"),EShopProductionAction::CloseCap,Bottle.ISID);
			}
			Action(TEXT("Open / remove cap"),EShopProductionAction::OpenCap,Bottle.ISID);
		}
	}
	if (Production->bOffersSupplies && Catalog)
	{
		for (const auto& Offer:Catalog->Offers)
		{
			auto* Button=AddButton(TEXT("Buy ")+Offer.DisplayName.ToString()+TEXT(" — ")+ShopTerminal::Money(Offer.UnitCostCents));
			Button->SupplyOfferID=Offer.OfferID; Button->SessionID=View.SessionID;
		}
		AddText(TEXT("Purchases use the shop bank. Deposit sale cash at the ATM to reinvest it."));
	}
	AddButton(TEXT("Close"))->bClose=true;
	if (Production->bRequiresCooperativeCap || Production->bAllowSoloCap || Production->bManualLeverMode) SetKeyboardFocus();
}
