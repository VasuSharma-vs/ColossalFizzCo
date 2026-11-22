#include "UI/ShopTerminalWidget.h"
#include "UI/ManualFillingPressureGauge.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"

namespace ShopTerminal { FString Money(int64 Cents); }

void UShopTerminalWidget::RefreshProduction(UShopProductionStationComponent* Production)
{
	const auto& View=Production->View;
	if (!Production->bManualLeverMode)
	{
		auto* Manager=APropManager::Get(this);
		if (!Manager || !Manager->ShopSupplies) return;
		if (LastContents==TEXT("SUPPLY_DEPOT")) return;
		LastContents=TEXT("SUPPLY_DEPOT"); Body->ClearChildren();
		AddSection(TEXT("SUPPLIES"),TEXT("Orders arrive as physical items. Carry caps and seals to the machine trays."));
		if (const auto* Catalog=Manager->ShopSupplies->Catalog.LoadSynchronous())
			for (const auto& Offer:Catalog->Offers)
			{
				auto* Button=AddButton(Offer.DisplayName.ToString()+TEXT("  ")+ShopTerminal::Money(Offer.UnitCostCents));
				Button->SupplyOfferID=Offer.OfferID; Button->SessionID=View.SessionID;
			}
		return;
	}
	const auto* Bottle=View.Bottles.FindByPredicate([&](const auto& Item){return Item.ISID==View.SeatedBottleISID;});
	const auto* Selected=View.SupplyPorts.FindByPredicate([&](const auto& Port){return Port.PortIndex==View.SelectedContentPort;});
	const FString Content=Selected && !Selected->ContentName.IsEmpty() ? Selected->ContentName.ToString() : TEXT("No hose connected");
	const auto* Filler=Production->GetOwner()->FindComponentByClass<UBottleFillingComponent>();
	const float FlowRate = Filler ? Filler->DeliveredPortionsPerSecond : 0.f;
	const float SupplyPressure = Filler ? Filler->DeliveredSupplyPressure : 0.f;
	if (bWorldFillingDisplay)
	{
		const FString Supply = Selected && Selected->ContentID > 0 && !Selected->bInRange ? TEXT("Supply unavailable") : Content;
		Summary->SetText(FText::FromString(FString::Printf(TEXT("Socket %d of %d\n%s"),
			View.SelectedContentPort+1, Production->GetSupportedContentCount(), *Supply)));
		HoseReadout->SetText(FText::FromString(FString::Printf(TEXT("%.2f portions/s  ·  Pressure %.2f"), FlowRate, SupplyPressure)));
	}
	else Summary->SetText(FText::FromString(FString::Printf(TEXT("INPUT %02d  /  %s\nFLOW %.2f portions/s  |  SUPPLY PRESSURE %.2f"),View.SelectedContentPort+1,*Content, FlowRate, SupplyPressure)));
	const bool Safe=View.bSafeToRelease;
	const float SealSeconds=Bottle ? Bottle->TemporarySealSeconds : 0;
	const bool bCapFitted=Bottle && Bottle->AttachedCapISID>0;
	const float CapTightness=bCapFitted ? FMath::Clamp(Bottle->CapSealed,0.f,1.f) : 0.f;
	if (CapTightnessProgress) CapTightnessProgress->SetPercent(CapTightness);
	if (CapTightnessReadout)
	{
		const int32 Percent=CapTightness>=1.f ? 100 : FMath::Min(99,FMath::RoundToInt(CapTightness*100.f));
		FString CapLabel=TEXT("CAP —");
		if (bCapFitted) CapLabel=FString::Printf(TEXT("CAP %d%% TIGHT"),Percent);
		else if (Bottle) CapLabel=View.CapTrayCount>0 ? TEXT("CAP NOT FITTED") : TEXT("CAP TRAY EMPTY");
		CapTightnessReadout->SetText(FText::FromString(CapLabel));
	}
	if (PressureGauge) { PressureGauge->Pressure=Bottle ? Bottle->ResidualPressure : 0; PressureGauge->SafePressure=Bottle ? Bottle->SafeResidualPressure : .1f; }
	if (FillProgress) FillProgress->SetPercent(Bottle ? FMath::Clamp(Bottle->ContentLevel,0.f,1.f) : 0);
	if (FillReadout) FillReadout->SetText(FText::FromString(FString::Printf(TEXT("%.0f%% FULL"),Bottle ? Bottle->ContentLevel*100 : 0)));
	if (SealReadout) SealReadout->SetText(FText::FromString(Bottle && Bottle->bTemporarySealArmed ? TEXT("SEAL READY") : SealSeconds>0 ? FString::Printf(TEXT("SEAL %.1f s"),SealSeconds) : TEXT("SEAL INACTIVE")));
	if (SealProgress) SealProgress->SetPercent(Bottle ? Bottle->TemporarySealIntegrity : 0);
	if (SupplyTrayReadout)
	{
		const FString Supplies = bWorldFillingDisplay
			? FString::Printf(TEXT("Caps %d / %d    Seals %d / %d"), View.CapTrayCount, Production->CapTrayCapacity, View.SealTrayCount, Production->SealTrayCapacity)
			: FString::Printf(TEXT("Caps   %d / %d%s\nSeals   %d / %d%s"), View.CapTrayCount, Production->CapTrayCapacity,
				View.CapTrayCount>=Production->CapTrayCapacity ? TEXT("  FULL") : TEXT(""), View.SealTrayCount, Production->SealTrayCapacity,
				View.SealTrayCount>=Production->SealTrayCapacity ? TEXT("  FULL") : TEXT(""));
		SupplyTrayReadout->SetText(FText::FromString(Supplies));
	}
	FString Status,Hint; FLinearColor Color(.91f,.93f,.87f);
	const auto* Machine=Cast<AManualColaFillingMachine>(Production->GetOwner());
	if (!Bottle) { Status=TEXT("Ready for a bottle"); Hint=TEXT("Lay a bottle in the cradle. Connect a supply hose and load the cap and seal trays."); }
	else if (View.bLeverHeld && Machine && !Machine->IsCradleInFillPosition())
	{ Status=TEXT("Tilting into filling position"); Hint=TEXT("Keep F held. Filling starts when the bottle neck reaches the nozzle."); }
	else if (View.bFilling)
	{
		Status=Bottle->ContentLevel>=1 ? TEXT("FULL — PRESS X NOW") : TEXT("Filling — hold F");
		Hint=TEXT("Press X to stop the flow at the desired level. Overfilling can cause a pressure failure.");
		Color=Bottle->ContentLevel>=1 ? FLinearColor(.95f,.32f,.3f) : FLinearColor(.23f,.85f,.65f);
	}
	else if (View.bLeverHeld && !Safe) { Status=TEXT("Venting — keep holding F"); Hint=TEXT("Flow is stopped. Keep the lever pulled until the needle returns to the green normal-pressure zone."); Color=FLinearColor(.94f,.7f,.36f); }
	else if (View.bLeverHeld && !View.bFlowStopped) { Status=TEXT("Waiting for supply"); Hint=TEXT("Check the selected hose and open the container valve gradually."); }
	else if (Bottle->CapSealed>=1) { Status=TEXT("Cap secured"); Hint=TEXT("The bottle is ready to remove from the cradle."); Color=FLinearColor(.23f,.85f,.65f); }
	else if (Safe && Bottle->ContentLevel>0) { Status=TEXT("Normal — safe to release"); Hint=TEXT("Release F safely. Press R to fit a cap from the tray, then tap R to tighten it."); Color=FLinearColor(.23f,.85f,.65f); }
	else if (SealSeconds>0) { Status=TEXT("Seal degrading — fit and tighten cap"); Hint=TEXT("The seal buys time to secure the cap. Unsafe pressure when it reaches zero triggers this bottle's failure reaction."); Color=FLinearColor(.94f,.7f,.36f); }
	else { Status=TEXT("Bottle seated"); Hint=TEXT("Hold F to fill, press X to stop flow, then keep holding until pressure is normal."); }
	if (bWorldFillingDisplay)
	{
		if (!Bottle) Hint = TEXT("Place a bottle in the cradle.");
		else if (View.bLeverHeld && Machine && !Machine->IsCradleInFillPosition()) Hint = TEXT("Keep holding E. Flow starts when the bottle lines up with the nozzle.");
		else if (View.bFilling)
		{
			Status = Bottle->ContentLevel >= 1 ? TEXT("Full — press X") : TEXT("Filling");
			Hint = TEXT("Press X to stop. Hold E until bottle pressure returns to normal.");
		}
		else if (View.bLeverHeld && !Safe)
		{
			Status = TEXT("Pressure falling");
			Hint = TEXT("Keep holding E until the needle reaches green. Press V to resume flow.");
		}
		else if (View.bLeverHeld && View.bFlowStopped)
		{
			Status = TEXT("Safe to release");
			Hint = TEXT("Release E, then use R to fit and tighten a cap. Press V to resume flow.");
		}
		else if (View.bLeverHeld) Hint = TEXT("Check the selected hose and open the tank valve.");
		else if (CapTightness >= 1) Hint = TEXT("Hold G on the bottle to pick it up. Shift+R removes the cap.");
		else if (bCapFitted) Hint = TEXT("Tap R to tighten the cap to 100%.");
		else if (SealSeconds > 0 || (Safe && Bottle->ContentLevel > 0))
			Hint = View.CapTrayCount>0 ? TEXT("Press R to fit a cap, then tap R to tighten it.") : TEXT("Load a cap into the tray, then press R to fit it.");
		else Hint = TEXT("Hold E on the lever to fill. Press X to stop flow.");
	}
	if (ProductionStatus) { ProductionStatus->SetText(FText::FromString(Status)); ProductionStatus->SetColorAndOpacity(Color); }
	if (ProductionHint) ProductionHint->SetText(FText::FromString(Hint));
	if (bWorldFillingDisplay) return;
	if (StopFlowButton) StopFlowButton->SetIsEnabled(View.bLeverHeld && !View.bFlowStopped);
	if (ResumeFlowButton) ResumeFlowButton->SetIsEnabled(View.bLeverHeld && View.bFlowStopped);
	for (const auto& Item:View.Bottles)
		if (const auto* Label=ProductionBottleLabels.Find(Item.ISID); Label && IsValid(Label->Get()))
			(*Label)->SetText(FText::FromString(FString::Printf(TEXT("Bottle #%d  /  %.0f%% full"),Item.ISID,Item.ContentLevel*100)));
	FString Structure=View.SessionID.ToString()+FString::FromInt(View.SeatedBottleISID);
	for (const auto& Item:View.Bottles) Structure+=FString::Printf(TEXT("/B%d/%d/%d/%d"),Item.ISID,Item.AttachedCapISID,Item.CapSealed>=1,Item.bTemporarySealUsed);
	for (const auto& Item:View.Caps) Structure+=FString::Printf(TEXT("/C%d"),Item.ISID);
	for (const auto& Port:View.SupplyPorts) Structure+=FString::Printf(TEXT("/P%d/%d/%d/%d/%d"),Port.PortIndex,Port.SourceISID,Port.ContentID,Port.bInRange,Port.PortIndex==View.SelectedContentPort);
	if (Structure==LastContents || (!LastContents.IsEmpty() && (LocalLeverBottle>0 || View.bLeverHeld))) return;
	LastContents=Structure; Body->ClearChildren(); ProductionSidebar->ClearChildren(); ProductionBottleLabels.Reset(); StopFlowButton=nullptr; ResumeFlowButton=nullptr;
	if (!View.SessionID.IsValid()) { AddText(TEXT("Waiting for station synchronization...")); return; }
	if (View.Bottles.IsEmpty()) AddText(TEXT("Place one bottle in the bottle input volume."));
	for (const auto& Item:View.Bottles)
	{
		ProductionBottleLabels.Add(Item.ISID,AddText(FString::Printf(TEXT("Bottle #%d"),Item.ISID)));
		if (Item.ISID!=View.SeatedBottleISID) { AddProductionAction(TEXT("Lay bottle in cradle"),EShopProductionAction::AlignBottle,Item.ISID); continue; }
		if (!Item.AttachedCapISID)
		{
			auto* Lever=WidgetTree->ConstructWidget<UShopTerminalLeverButton>(); Lever->Configure(this,TEXT("F   HOLD LEVER")); Lever->ISID=Item.ISID; Body->AddChildToVerticalBox(Lever)->SetPadding(FMargin(0,3));
			StopFlowButton=AddProductionAction(TEXT("X   STOP LIQUID FLOW"),EShopProductionAction::StopFilling,Item.ISID);
			ResumeFlowButton=AddProductionAction(TEXT("V   Resume liquid flow"),EShopProductionAction::ResumeFilling,Item.ISID);
			if (!Item.bTemporarySealUsed) AddProductionAction(TEXT("Apply temporary seal"),EShopProductionAction::ApplyTemporarySeal,Item.ISID);
			if (!View.Caps.IsEmpty()) AddProductionAction(TEXT("R   Fit a tray cap"),EShopProductionAction::AttachCap,Item.ISID,View.Caps[0].ISID);
		}
		else
		{
			if (Item.CapSealed<1) AddProductionAction(TEXT("R   Twist cap closed"),EShopProductionAction::TurnCap,Item.ISID);
			AddProductionAction(TEXT("Remove cap"),EShopProductionAction::OpenCap,Item.ISID);
		}
	}
	AddSection(TEXT("ROTATING CONTENT DIAL"),TEXT("Choose a connected input. Buy supplies at the separate depot."),ProductionSidebar);
	const int32 Count=Production->GetSupportedContentCount();
	AddProductionAction(TEXT("<   Previous content"),EShopProductionAction::SelectContentPort,(View.SelectedContentPort+Count-1)%Count,0,ProductionSidebar);
	AddProductionAction(TEXT("Next content   >"),EShopProductionAction::SelectContentPort,(View.SelectedContentPort+1)%Count,0,ProductionSidebar);
	for (const auto& Port:View.SupplyPorts)
	{
		const FString Name=Port.ContentID>0 ? Port.ContentName.ToString() : TEXT("Attach a physical hose");
		AddProductionAction(FString::Printf(TEXT("%s%02d  %s%s"),Port.PortIndex==View.SelectedContentPort ? TEXT("> ") : TEXT(""),Port.PortIndex+1,*Name,
			Port.ContentID>0 && !Port.bInRange ? TEXT("  [out of reach]") : TEXT("")),EShopProductionAction::SelectContentPort,Port.PortIndex,0,ProductionSidebar);
	}
	if (Selected && Selected->ContentID>0) AddProductionAction(TEXT("Disconnect selected hose"),EShopProductionAction::DisconnectSupply,View.SelectedContentPort,0,ProductionSidebar);
	AddSection(TEXT("CONNECT A HOSE"),TEXT("E: grab the container hose end. Aim at an input sphere and press E again to connect."),ProductionSidebar);
}
