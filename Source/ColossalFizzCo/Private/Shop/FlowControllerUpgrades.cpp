#include "Shop/ColaFlowController.h"

#include "Misc/SecureHash.h"
#include "Props/InteractablePropManager.h"
#include "Shop/SharedTeamWalletComponent.h"

namespace
{
	constexpr EFlowControllerUpgradeType UpgradeTypes[]={
		EFlowControllerUpgradeType::InputPorts, EFlowControllerUpgradeType::OutputPorts,
		EFlowControllerUpgradeType::Flow, EFlowControllerUpgradeType::Suction, EFlowControllerUpgradeType::Lever
	};

	// Matches the shipped playtest table. Native defaults also keep standalone fixtures usable.
	int64 DefaultUpgradeCost(EFlowControllerUpgradeType Type, int32 Rank)
	{
		static constexpr int32 InletCosts[]={20,40,70};
		static constexpr int32 OutletCosts[]={15,25,40,60,85,115,150,190,235,285,340};
		static constexpr int32 FlowCosts[]={20,35,55,80,110,145,185};
		static constexpr int32 SuctionCosts[]={15,25,40,60,85,115};
		static constexpr int32 LeverCosts[]={10,45,100};
		const int32 Index=Rank-1;
		switch (Type)
		{
		case EFlowControllerUpgradeType::InputPorts: return InletCosts[Index]*100;
		case EFlowControllerUpgradeType::OutputPorts: return OutletCosts[Index]*100;
		case EFlowControllerUpgradeType::Flow: return FlowCosts[Index]*100;
		case EFlowControllerUpgradeType::Suction: return SuctionCosts[Index]*100;
		case EFlowControllerUpgradeType::Lever: return LeverCosts[Index]*100;
		}
		return 0;
	}
}

int32 FFlowControllerUpgradeState::GetMaxRank(EFlowControllerUpgradeType Type)
{
	switch (Type)
	{
	case EFlowControllerUpgradeType::InputPorts: return AColaFlowController::MaxInletPorts-1;
	case EFlowControllerUpgradeType::OutputPorts: return AColaFlowController::MaxOutletPorts-1;
	case EFlowControllerUpgradeType::Flow: return 7;
	case EFlowControllerUpgradeType::Suction: return 6;
	case EFlowControllerUpgradeType::Lever: return AColaFlowController::MaxLeverLevel-1;
	}
	return INDEX_NONE;
}

int32 FFlowControllerUpgradeState::GetRank(EFlowControllerUpgradeType Type) const
{
	switch (Type)
	{
	case EFlowControllerUpgradeType::InputPorts: return InputPortsRank;
	case EFlowControllerUpgradeType::OutputPorts: return OutputPortsRank;
	case EFlowControllerUpgradeType::Flow: return FlowRank;
	case EFlowControllerUpgradeType::Suction: return SuctionRank;
	case EFlowControllerUpgradeType::Lever: return LeverRank;
	}
	return INDEX_NONE;
}

bool FFlowControllerUpgradeState::SetRank(EFlowControllerUpgradeType Type, int32 Rank)
{
	if (Rank<0 || Rank>GetMaxRank(Type)) return false;
	switch (Type)
	{
	case EFlowControllerUpgradeType::InputPorts: InputPortsRank=Rank; break;
	case EFlowControllerUpgradeType::OutputPorts: OutputPortsRank=Rank; break;
	case EFlowControllerUpgradeType::Flow: FlowRank=Rank; break;
	case EFlowControllerUpgradeType::Suction: SuctionRank=Rank; break;
	case EFlowControllerUpgradeType::Lever: LeverRank=Rank; break;
	}
	return true;
}

bool FFlowControllerUpgradeState::IsValid() const
{
	for (const auto Type:UpgradeTypes)
		if (const int32 Rank=GetRank(Type); Rank<0 || Rank>GetMaxRank(Type)) return false;
	return true;
}

float AColaFlowController::GetUpgradeValue(EFlowControllerUpgradeType Type, int32 Rank) const
{
	Rank=FMath::Clamp(Rank,0,FMath::Max(0,GetMaxUpgradeRank(Type)));
	switch (Type)
	{
	case EFlowControllerUpgradeType::InputPorts:
	case EFlowControllerUpgradeType::OutputPorts: return 1.f+Rank;
	case EFlowControllerUpgradeType::Flow: return FMath::Max(.01f,BaseIntakeRate+Rank*IntakeRateStep);
	case EFlowControllerUpgradeType::Suction: return FMath::Max(0.f,Rank*SuctionStrengthStep);
	case EFlowControllerUpgradeType::Lever: return Rank;
	}
	return 0.f;
}

float AColaFlowController::GetOutputRateForFlowRank(int32 Rank) const
{
	return FMath::Max(.01f,BaseOutputRate+FMath::Clamp(Rank,0,GetMaxUpgradeRank(EFlowControllerUpgradeType::Flow))*OutputRateStep);
}

float AColaFlowController::GetInletToleranceMultiplier(int32 Rank) const
{
	return 1.f+FMath::Clamp(Rank,0,GetMaxUpgradeRank(EFlowControllerUpgradeType::Flow))*FMath::Max(0.f,ToleranceBonusPerFlowRank);
}

float AColaFlowController::GetInletPressureTolerance() const
{
	return FMath::Max(0.f,BaseInletPressureTolerance)*GetInletToleranceMultiplier(UpgradeState.FlowRank);
}

int64 AColaFlowController::GetNextUpgradeCost(EFlowControllerUpgradeType Type) const
{
	const int32 CurrentRank=GetUpgradeRank(Type);
	if (CurrentRank<0 || CurrentRank>=GetMaxUpgradeRank(Type)) return 0;
	const int32 NextRank=CurrentRank+1;
	const UDataTable* Table=UpgradeDefinitions.LoadSynchronous();
	if (!Table) return DefaultUpgradeCost(Type,NextRank);
	if (Table->GetRowStruct()!=FFlowControllerUpgradeRow::StaticStruct()) return 0;
	int64 Cost=0;
	for (const auto& Entry:Table->GetRowMap())
	{
		const auto& Row=*reinterpret_cast<const FFlowControllerUpgradeRow*>(Entry.Value);
		if (Row.Type!=Type || Row.Rank!=NextRank) continue;
		if (Cost>0 || Row.RequiredRank!=CurrentRank || Row.CostCents<=0) return 0;
		Cost=Row.CostCents;
	}
	return Cost;
}

bool AColaFlowController::CanOpenUpgrades(const APawn* Player, float Reach) const
{
	return UpgradeControl && CanUseControl(Player,*UpgradeControl,Reach);
}

EFlowControllerPurchaseResult AColaFlowController::PurchaseUpgrade(const APawn* Player, float Reach,
	EFlowControllerUpgradeType Type, int32 ExpectedRank, const FGuid& OperationID)
{
	using EResult=EFlowControllerPurchaseResult;
	if (!HasAuthority() || !OperationID.IsValid() || !CanOpenUpgrades(Player,Reach) || ExpectedRank<0
		|| GetMaxUpgradeRank(Type)<0) return EResult::Unavailable;
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->SharedWallet) return EResult::Unavailable;
	const FString ControllerKey=Manager->GetFlowControllerUpgradeKey(*this);
	if (ControllerKey.IsEmpty()) return EResult::Unavailable;
	const FTCHARToUTF8 EncodedKey(*ControllerKey);
	const FString KeyHash=FMD5::HashBytes(reinterpret_cast<const uint8*>(EncodedKey.Get()),EncodedKey.Length());
	const FName Reason(*FString::Printf(TEXT("FlowControllerUpgrade|%s|%d|%d"),*KeyHash,int32(Type),ExpectedRank));
	FWalletReceipt Receipt;
	if (Manager->SharedWallet->FindReceipt(OperationID,Receipt))
		return Receipt.Reason==Reason && Receipt.DeltaCents<0 ? EResult::AlreadyPurchased : EResult::Unavailable;
	if (bLeverHeld) return EResult::Unavailable;
	if (ExpectedRank!=GetUpgradeRank(Type)) return EResult::RankChanged;
	if (ExpectedRank==GetMaxUpgradeRank(Type)) return EResult::MaxRank;
	const int64 Price=GetNextUpgradeCost(Type);
	FFlowControllerUpgradeState Next=UpgradeState;
	if (Price<=0 || !Next.SetRank(Type,ExpectedRank+1)) return EResult::Unavailable;
	const auto Result=Manager->SharedWallet->CommitWithResources(OperationID,-Price,Reason,[&]
	{
		if (!Manager->RecordFlowControllerUpgrades(*this,Next)) return false;
		RestoreUpgradeState(Next);
		return true;
	});
	if (Result==EWalletOperationResult::Applied) return EResult::Purchased;
	if (Result==EWalletOperationResult::InsufficientFunds) return EResult::InsufficientFunds;
	return EResult::Unavailable;
}

void AColaFlowController::RestoreUpgradeState(const FFlowControllerUpgradeState& State)
{
	if (!HasAuthority() || !State.IsValid() || UpgradeState==State) return;
	ReleaseBatch();
	UpgradeState=State;
	OnRep_UpgradeState();
	UpdateFlow();
	ForceNetUpdate();
}
