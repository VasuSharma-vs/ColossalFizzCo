#include "Shop/ManualColaFillingMachine.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractablePropManager.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/SocketColaFillingMachine.h"

int32 FFillingMachineUpgradeState::GetRank(EFillingMachineUpgradeType Type) const
{
	switch (Type)
	{
	case EFillingMachineUpgradeType::CapTray: return CapTrayRank;
	case EFillingMachineUpgradeType::SealTray: return SealTrayRank;
	case EFillingMachineUpgradeType::PressureRegulator: return PressureRegulatorRank;
	case EFillingMachineUpgradeType::TemperatureRegulator: return TemperatureRegulatorRank;
	}
	return INDEX_NONE;
}

bool FFillingMachineUpgradeState::SetRank(EFillingMachineUpgradeType Type, int32 Rank)
{
	if (Rank<0) return false;
	switch (Type)
	{
	case EFillingMachineUpgradeType::CapTray:
		if (Rank>=MaxTrayCapacity) return false;
		CapTrayRank=Rank;
		return true;
	case EFillingMachineUpgradeType::SealTray:
		if (Rank>=MaxTrayCapacity) return false;
		SealTrayRank=Rank;
		return true;
	case EFillingMachineUpgradeType::PressureRegulator:
		if (Rank>MaxPressureRank) return false;
		PressureRegulatorRank=Rank;
		return true;
	case EFillingMachineUpgradeType::TemperatureRegulator:
		if (Rank>TemperatureRankLimit) return false;
		TemperatureRegulatorRank=Rank;
		return true;
	}
	return false;
}

bool FFillingMachineUpgradeState::IsValid() const
{
	return CapTrayRank>=0 && CapTrayRank<MaxTrayCapacity && SealTrayRank>=0 && SealTrayRank<MaxTrayCapacity
		&& PressureRegulatorRank>=0 && PressureRegulatorRank<=MaxPressureRank
		&& TemperatureRegulatorRank>=0 && TemperatureRegulatorRank<=TemperatureRankLimit;
}

void AManualColaFillingMachine::OnApplyEventData(const FPropEventUpdateData& InEventData, uint8 DirtyMask)
{
	Super::OnApplyEventData(InEventData,DirtyMask);
	if (Production && bIsActive) Production->UpdateSupplyTrayCollision();
}

int32 AManualColaFillingMachine::GetUpgradeRank(EFillingMachineUpgradeType Type) const
{
	return bHasValidUniqueData ? LocalUniqueData.EventData.FillingMachineUpgrades.GetRank(Type)
		: FFillingMachineUpgradeState{}.GetRank(Type);
}

int32 AManualColaFillingMachine::GetMaxUpgradeRank(EFillingMachineUpgradeType Type) const
{
	switch (Type)
	{
	case EFillingMachineUpgradeType::CapTray:
	case EFillingMachineUpgradeType::SealTray: return FFillingMachineUpgradeState::MaxTrayCapacity-1;
	case EFillingMachineUpgradeType::PressureRegulator: return FFillingMachineUpgradeState::MaxPressureRank;
	case EFillingMachineUpgradeType::TemperatureRegulator:
		return FMath::Clamp(MaximumTemperatureRegulatorRank,1,FFillingMachineUpgradeState::TemperatureRankLimit);
	}
	return INDEX_NONE;
}

float AManualColaFillingMachine::GetUpgradeValue(EFillingMachineUpgradeType Type, int32 Rank) const
{
	switch (Type)
	{
	case EFillingMachineUpgradeType::CapTray:
	case EFillingMachineUpgradeType::SealTray:
		return 1.f+FMath::Clamp(Rank,0,FFillingMachineUpgradeState::MaxTrayCapacity-1);
	case EFillingMachineUpgradeType::PressureRegulator:
		return .05f*FMath::Clamp(Rank,0,FFillingMachineUpgradeState::MaxPressureRank);
	case EFillingMachineUpgradeType::TemperatureRegulator:
		return .02f*FMath::Clamp(Rank,0,FFillingMachineUpgradeState::TemperatureRankLimit);
	}
	return 0.f;
}

int64 AManualColaFillingMachine::GetNextUpgradeCostCents(EFillingMachineUpgradeType Type) const
{
	const int32 Rank=GetUpgradeRank(Type);
	if (Rank<0 || Rank>=GetMaxUpgradeRank(Type)) return 0;
	const FFillingMachineUpgradePrice* Pricing=nullptr;
	switch (Type)
	{
	case EFillingMachineUpgradeType::CapTray: Pricing=&CapTrayUpgradePrice; break;
	case EFillingMachineUpgradeType::SealTray: Pricing=&SealTrayUpgradePrice; break;
	case EFillingMachineUpgradeType::PressureRegulator: Pricing=&PressureRegulatorUpgradePrice; break;
	case EFillingMachineUpgradeType::TemperatureRegulator: Pricing=&TemperatureRegulatorUpgradePrice; break;
	}
	return Pricing ? FMath::Max(1,Pricing->StartingCostCents)+int64(Rank)*FMath::Max(1,Pricing->CostIncreaseCents) : 0;
}

bool AManualColaFillingMachine::CanOpenUpgrades(const APawn* Player, float Reach) const
{
	const auto* Controller=IsValid(Player) ? Cast<APlayerController>(Player->GetController()) : nullptr;
	if (!Controller || Controller->GetPawn()!=Player || Player->GetWorld()!=GetWorld()
		|| !bIsActive || bIsHeld || bIsInMotion || ISID<=0 || !PropMesh || !Production
		|| !FMath::IsFinite(Reach) || Reach<=0.f
		|| PropMesh->Bounds.GetBox().ComputeSquaredDistanceToPoint(Player->GetActorLocation())>FMath::Square(Reach)) return false;
	if (const auto* Layout=Cast<ASocketColaFillingMachine>(this); Layout && Layout->IsMachinePlacementActive()) return false;
	FVector Eye;
	FRotator View;
	Player->GetActorEyesViewPoint(Eye,View);
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(FillingMachineUpgradeReach),false,Player);
	if (!GetWorld()->LineTraceSingleByChannel(Hit,Eye,PropMesh->Bounds.Origin,ECC_Visibility,Query) || Hit.GetActor()==this)
		return true;
	const auto* Manager=PropManagerReference.Get();
	return Manager && Production->View.SeatedBottleISID>0
		&& Hit.GetActor()==Manager->GetActiveActorFromISID(Production->View.SeatedBottleISID);
}

EFillingMachinePurchaseResult AManualColaFillingMachine::PurchaseUpgrade(const APawn* Player, float Reach,
	EFillingMachineUpgradeType Type, int32 ExpectedRank, const FGuid& OperationID)
{
	using EResult=EFillingMachinePurchaseResult;
	const int32 MaxRank=GetMaxUpgradeRank(Type);
	if (!HasAuthority() || !OperationID.IsValid() || ExpectedRank<0 || MaxRank<0 || !CanOpenUpgrades(Player,Reach))
		return EResult::Unavailable;
	auto* Manager=PropManagerReference.Get();
	const auto* Record=Manager ? Manager->FindISIDData(ISID) : nullptr;
	const auto* Data=Record ? Manager->FindUniqueData(ISID) : nullptr;
	if (!Data || Record->ActiveActor!=this || !Manager->SharedWallet) return EResult::Unavailable;
	const FName Reason(*FString::Printf(TEXT("FillingMachineUpgrade|%d|%d|%d"),ISID,int32(Type),ExpectedRank));
	FWalletReceipt Receipt;
	if (Manager->SharedWallet->FindReceipt(OperationID,Receipt))
		return Receipt.Reason==Reason && Receipt.DeltaCents<0 ? EResult::AlreadyPurchased : EResult::Unavailable;
	if (Production->View.bLeverHeld || (BottleFiller && BottleFiller->bIsFilling)) return EResult::Unavailable;
	const auto& Upgrades=Data->EventData.FillingMachineUpgrades;
	if (ExpectedRank!=Upgrades.GetRank(Type)) return EResult::RankChanged;
	if (ExpectedRank>=MaxRank) return EResult::MaxRank;
	const int64 CostCents=GetNextUpgradeCostCents(Type);
	FPropEventSyncDelta Delta;
	Delta.ExtendedDirtyMask=EPropExtendedEventMask::FillingMachineUpgrades;
	Delta.FillingMachineUpgrades=Upgrades;
	if (CostCents<=0 || !Delta.FillingMachineUpgrades.SetRank(Type,ExpectedRank+1)) return EResult::Unavailable;
	const auto Result=Manager->SharedWallet->CommitWithResources(OperationID,-CostCents,Reason,[&]
	{
		return Manager->SubmitInstanceEventUpdate(ISID,Delta);
	});
	if (Result==EWalletOperationResult::Applied)
	{
		if (Type==EFillingMachineUpgradeType::CapTray || Type==EFillingMachineUpgradeType::SealTray)
			Production->RefreshSupplyTrays();
		return EResult::Purchased;
	}
	if (Result==EWalletOperationResult::InsufficientFunds) return EResult::InsufficientFunds;
	return EResult::Unavailable;
}
