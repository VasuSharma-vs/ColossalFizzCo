#include "Shop/ColaFlowController.h"

#include "Components/HoseSocketComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/HoseCableActor.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"

namespace
{
	AHoseCableActor* GetFlowControllerOutletHose(const UHoseSocketComponent& Outlet)
	{
		auto* Hose=Outlet.ConnectedHose.Get();
		return !Outlet.bIsBroken && Outlet.bCanSupply && IsValid(Hose)
			&& Hose->StartPort==&Outlet && IsValid(Hose->Transfer) ? Hose : nullptr;
	}
}

AManualColaFillingMachine* AColaFlowController::GetFillerTable(const UHoseSocketComponent& Outlet) const
{
	const auto* Hose=GetFlowControllerOutletHose(Outlet);
	const auto* Feed=Hose ? Cast<UHoseSocketComponent>(Hose->EndPort) : nullptr;
	auto* Table=IsValid(Feed) ? Cast<AManualColaFillingMachine>(Feed->GetOwner()) : nullptr;
	return IsValid(Table) && IsValid(Table->Production) && Table->bUseFlowController && Table->bIsActive && !Table->bIsHeld
		&& Table->GetHoseSocket(0)==Feed && Feed->ConnectedHose==Hose
		&& Hose->Transfer->HasValidConnection() && Table->IsSupplyInRange(0) ? Table : nullptr;
}

void AColaFlowController::UpdateBatch()
{
	if (!bLeverHeld) return;
	auto* Player=LeverOperator.Get();
	const auto* PC=IsValid(Player) ? Cast<APlayerController>(Player->GetController()) : nullptr;
	if (!PC || PC->GetPawn()!=Player || Player->GetWorld()!=GetWorld()
		|| (!UsesLatchedLever() && !CanReachControl(Player,*Lever,LeverReach)))
	{
		ReleaseBatch();
		return;
	}

	bool bSafe=true;
	bool bReachedCapacity=false;
	ActiveStations.RemoveAllSwap([this,Player,&bSafe,&bReachedCapacity](const TWeakObjectPtr<UShopProductionStationComponent>& Entry)
	{
		auto* Station=Entry.Get();
		if (!Station || !Station->View.bLeverHeld || Station->FlowLeverController.Get()!=this) return true;
		const auto* Table=Cast<AManualColaFillingMachine>(Station->GetOwner());
		if (!Table || !IsDrivingTable(*Table,Player))
		{
			Station->ReleaseFlowControllerHold(*this);
			return true;
		}
		if (LeverLevel>=3 && Station->IsFlowControllerBottleFull())
		{
			if (LeverLevel==3) bReachedCapacity=true;
			else Station->CloseFlowControllerValve(*this);
		}
		if (LeverLevel==4 && Station->TryReleaseCompletedFlowControllerBottle(*this)) return true;
		bSafe &= Station->View.bSafeToRelease;
		return false;
	});
	if (bReachedCapacity) { ReleaseBatch(); return; }

	if (LeverLevel==4 && !bFlowPaused && SelectedContentID>0)
	{
		for (const UHoseSocketComponent* Outlet:FillerSockets)
		{
			const auto* Table=GetFillerTable(*Outlet);
			auto* Station=Table ? Table->Production.Get() : nullptr;
			if (Station && !Station->View.bLeverHeld && Station->View.SeatedBottleISID>0
				&& Station->BeginFlowControllerHold(*this,*Player))
				ActiveStations.AddUnique(Station);
		}
	}
	if (bBatchSafeToRelease==bSafe && ActiveFillerCount==ActiveStations.Num()) return;
	bBatchSafeToRelease=bSafe;
	ActiveFillerCount=ActiveStations.Num();
	ForceNetUpdate();
}

void AColaFlowController::UpdateFlow()
{
	if (!HasAuthority() || !SelectorSockets.IsValidIndex(SelectedInputIndex) || FillerSockets.IsEmpty()) return;
	auto* Inlet=SelectorSockets[SelectedInputIndex].Get();
	const auto* Hose=Inlet->GetReceivingHose();
	const auto* Source=Hose ? Hose->GetOtherEndpoint(Inlet) : nullptr;
	const bool bConnected=Source && !Inlet->bIsBroken && !Hose->bBurst;
	int32 ContentID=bConnected ? Hose->ResourceID : 0;
	if (ContentID!=SelectedContentID && bLeverHeld) ContentID=0;
	const float IntakeRate=GetIntakeRate();
	const float PressureTolerance=GetInletPressureTolerance();
	const float SuctionStrength=GetIntakeSuctionStrength();
	for (UHoseSocketComponent* Selector:SelectorSockets)
	{
		const bool bOpen=Selector==Inlet && ContentID>0 && !Selector->bIsBroken;
		Selector->MaxReceiveUnitsPerSecond=IntakeRate;
		Selector->MaxIntakePressure=PressureTolerance;
		Selector->ControllerIntakeSuctionStrength=bOpen ? SuctionStrength : 0.f;
		if (Selector->bRequestsFlow!=bOpen) Selector->SetFlowRequested(bOpen);
	}

	const float TotalRate=ContentID>0 ? FMath::Min(GetOutputRate(),Hose->GetContentFeedRate(*Inlet)) : 0.f;
	const float SupplyPressure=bConnected ? Source->GetOperatingPressure() : 0.f;
	const bool bIndependentValves=LeverLevel==4 && bLeverHeld;
	TArray<bool,TInlineAllocator<MaxOutletPorts>> OutletReady;
	OutletReady.Reserve(FillerSockets.Num());
	int32 ConnectedOutlets=0;
	int32 OpenOutlets=0;
	for (UHoseSocketComponent* Outlet:FillerSockets)
	{
		if (ContentID>0 && !Outlet->bIsBroken && Outlet->ResourceID!=ContentID)
		{
			Outlet->SetSupplyEnabled(false);
			Outlet->ResourceID=ContentID;
			ForceNetUpdate();
		}
		auto* OutletHose=GetFlowControllerOutletHose(*Outlet);
		ConnectedOutlets+=OutletHose!=nullptr;
		// A manually opened, ruptured hose can still spill until it is unplugged.
		const bool bConnectedToSupply=ContentID>0 && OutletHose && (OutletHose->Transfer->bBurst
			? OutletHose->Transfer->ResourceID==ContentID : OutletHose->RefreshSourceContent());
		const auto* Table=bIndependentValves && bConnectedToSupply ? GetFillerTable(*Outlet) : nullptr;
		const bool bReady=bConnectedToSupply && (!bIndependentValves || (Table && Table->Production->IsFlowControllerValveOpen(*this)));
		OutletReady.Add(bReady);
		OpenOutlets+=bReady;
	}
	const float Fraction=OpenOutlets>0 ? 1.f/OpenOutlets : 0.f;
	const float Share=TotalRate*Fraction;
	const float SharePressure=Share>0.f ? (SupplyPressure>0.f ? SupplyPressure : 1.f)*Fraction : 0.f;
	for (int32 Index=0; Index<FillerSockets.Num(); ++Index)
	{
		auto* Outlet=FillerSockets[Index].Get();
		const bool bReady=OutletReady[Index];
		Outlet->SetLocalContentSupply(bReady ? Inlet : nullptr);
		Outlet->MaxSupplyUnitsPerSecond=bReady ? Share : 0.f;
		const bool bDeliver=bReady && Share>0.f && bLeverHeld && !bFlowPaused;
		if (Outlet->bSupplyEnabled!=bDeliver) Outlet->SetSupplyEnabled(bDeliver);
		Outlet->SetOperatingPressure(bDeliver ? SharePressure : 0.f);
	}
	if (ContentID==SelectedContentID && ConnectedOutlets==ConnectedOutletCount
		&& FMath::IsNearlyEqual(Share,PortionsPerSecondPerFiller) && FMath::IsNearlyEqual(SharePressure,PressurePerOutlet)
		&& FMath::IsNearlyEqual(SupplyPressure,InletSupplyPressure)) return;
	SelectedContentID=ContentID;
	PortionsPerSecondPerFiller=Share;
	ConnectedOutletCount=ConnectedOutlets;
	InletSupplyPressure=SupplyPressure;
	PressurePerOutlet=SharePressure;
	ForceNetUpdate();
}
