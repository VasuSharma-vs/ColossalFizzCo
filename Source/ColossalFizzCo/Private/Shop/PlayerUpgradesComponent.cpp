#include "Shop/PlayerUpgradesComponent.h"
#include "PlayerUpgradesSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/SecureHash.h"
#include "Net/UnrealNetwork.h"
#include "Props/InteractablePropManager.h"
#include "Shop/SharedTeamWalletComponent.h"

namespace
{
	constexpr EPlayerUpgradeType PlayerUpgradeTypes[]={EPlayerUpgradeType::EmptyBottleHandling,
		EPlayerUpgradeType::CapHandling,EPlayerUpgradeType::SealHandling,EPlayerUpgradeType::HandStability};

	APropManager* FindShopManager(UWorld* World)
	{
		if (World)
			for (TActorIterator<APropManager> It(World); It; ++It) return *It;
		return nullptr;
	}

	FString GetProfileKey(const APlayerState& State)
	{
		const auto& NetId=State.GetUniqueId();
		const FName Provider=NetId.IsV1() ? NetId.GetType() : NAME_None;
		const auto* Controller=State.GetPlayerController();
		const auto* LocalPlayer=Controller ? Controller->GetLocalPlayer() : nullptr;
		if (LocalPlayer && (!NetId.IsValid() || Provider==FName(TEXT("NULL"))))
		{
			const int32 ProfileIndex=LocalPlayer->GetIndexInGameInstance();
			if (ProfileIndex>=0) return FString::Printf(TEXT("Local:%d"),ProfileIndex);
		}
		if (!NetId.IsValid()) return {};
		const FString Account=NetId.ToString();
		if (Account.IsEmpty()) return {};
		const FString Platform=NetId.IsV1() ? Provider.ToString()
			: FString::Printf(TEXT("V2:%d"), int32(NetId.GetV2Unsafe().GetOnlineServicesType()));
		const FString Identity=Platform+TEXT(":")+Account;
		const FTCHARToUTF8 IdentityBytes(*Identity);
		return TEXT("Online:")+FMD5::HashBytes(reinterpret_cast<const uint8*>(IdentityBytes.Get()),IdentityBytes.Length());
	}

	float DefaultUpgradeValue(EPlayerUpgradeType Type, int32 Rank)
	{
		Rank=FMath::Clamp(Rank,0,FMath::Max(0,FPlayerUpgradeState::GetMaxRank(Type)));
		constexpr int32 SmallPartCapacities[]={1,3,6,9,12};
		switch (Type)
		{
		case EPlayerUpgradeType::EmptyBottleHandling: return 1+Rank;
		case EPlayerUpgradeType::CapHandling:
		case EPlayerUpgradeType::SealHandling: return SmallPartCapacities[Rank];
		case EPlayerUpgradeType::HandStability: return (10-Rank)*.1f;
		}
		return 0.f;
	}

	int64 DefaultPriceCents(EPlayerUpgradeType Type, int32 Rank)
	{
		constexpr int64 EmptyBottlePrices[]={2000,4500};
		constexpr int64 SmallPartPrices[]={1500,3000,5000,7500};
		constexpr int64 StabilityPrices[]={1500,3000,5000,7500,10500};
		switch (Type)
		{
		case EPlayerUpgradeType::EmptyBottleHandling: return EmptyBottlePrices[Rank-1];
		case EPlayerUpgradeType::CapHandling:
		case EPlayerUpgradeType::SealHandling: return SmallPartPrices[Rank-1];
		case EPlayerUpgradeType::HandStability: return StabilityPrices[Rank-1];
		}
		return 0;
	}

	const APlayerState* FindPlayerState(const AActor* Player)
	{
		if (const auto* Pawn=Cast<APawn>(Player)) return Pawn->GetPlayerState();
		if (const auto* Controller=Cast<AController>(Player)) return Controller->GetPlayerState<APlayerState>();
		return Cast<APlayerState>(Player);
	}
}

int32 FPlayerUpgradeState::GetMaxRank(EPlayerUpgradeType Type)
{
	switch (Type)
	{
	case EPlayerUpgradeType::EmptyBottleHandling: return 2;
	case EPlayerUpgradeType::CapHandling:
	case EPlayerUpgradeType::SealHandling: return 4;
	case EPlayerUpgradeType::HandStability: return 5;
	}
	return INDEX_NONE;
}

int32 FPlayerUpgradeState::GetRank(EPlayerUpgradeType Type) const
{
	switch (Type)
	{
	case EPlayerUpgradeType::EmptyBottleHandling: return EmptyBottleCarryRank;
	case EPlayerUpgradeType::CapHandling: return CapCarryRank;
	case EPlayerUpgradeType::SealHandling: return SealCarryRank;
	case EPlayerUpgradeType::HandStability: return HandStabilityRank;
	}
	return INDEX_NONE;
}

bool FPlayerUpgradeState::SetRank(EPlayerUpgradeType Type, int32 Rank)
{
	if (Rank<0 || Rank>GetMaxRank(Type)) return false;
	switch (Type)
	{
	case EPlayerUpgradeType::EmptyBottleHandling: EmptyBottleCarryRank=Rank; break;
	case EPlayerUpgradeType::CapHandling: CapCarryRank=Rank; break;
	case EPlayerUpgradeType::SealHandling: SealCarryRank=Rank; break;
	case EPlayerUpgradeType::HandStability: HandStabilityRank=Rank; break;
	}
	return true;
}

bool FPlayerUpgradeState::IsValid() const
{
	for (const auto Type:PlayerUpgradeTypes)
	{
		const int32 Rank=GetRank(Type);
		if (Rank<0 || Rank>GetMaxRank(Type)) return false;
	}
	return true;
}

UPlayerUpgradesComponent::UPlayerUpgradesComponent()
{
	PrimaryComponentTick.bCanEverTick=false;
	SetIsReplicatedByDefault(true);
	for (const auto Type:PlayerUpgradeTypes)
		for (int32 Rank=1; Rank<=GetMaxUpgradeRank(Type); ++Rank)
		{
			auto& Row=UpgradeDefinitions.AddDefaulted_GetRef();
			Row.Type=Type;
			Row.Rank=Rank;
			Row.RequiredPreviousRank=Rank-1;
			Row.PriceCents=DefaultPriceCents(Type,Rank);
			if (Type==EPlayerUpgradeType::HandStability) Row.ShakeMultiplier=DefaultUpgradeValue(Type,Rank);
			else Row.CarryCapacity=int32(DefaultUpgradeValue(Type,Rank));
		}
}

void UPlayerUpgradesComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UPlayerUpgradesComponent,UpgradeState,COND_OwnerOnly);
}

UPlayerUpgradesComponent* UPlayerUpgradesComponent::Find(const AActor* Player)
{
	const auto* State=FindPlayerState(Player);
	return IsValid(State) ? State->FindComponentByClass<UPlayerUpgradesComponent>() : nullptr;
}

int32 UPlayerUpgradesComponent::GetCarryCapacityFor(const AActor* Player, EPlayerUpgradeType Type)
{
	if (const auto* Upgrades=Find(Player)) return Upgrades->GetCarryCapacity(Type);
	return Type==EPlayerUpgradeType::HandStability ? 0 : int32(DefaultUpgradeValue(Type,0));
}

float UPlayerUpgradesComponent::GetCarryShakeMultiplierFor(const AActor* Player)
{
	if (const auto* Upgrades=Find(Player)) return Upgrades->GetCarryShakeMultiplier();
	return 1.f;
}

int32 UPlayerUpgradesComponent::GetMaxUpgradeRank(EPlayerUpgradeType Type)
{
	return FPlayerUpgradeState::GetMaxRank(Type);
}

const FPlayerUpgradeRow* UPlayerUpgradesComponent::FindUpgradeRow(EPlayerUpgradeType Type, int32 Rank) const
{
	const int32 MaxRank=GetMaxUpgradeRank(Type);
	if (Rank<=0 || Rank>MaxRank) return nullptr;
	const FPlayerUpgradeRow* Match=nullptr;
	for (const auto& Row:UpgradeDefinitions)
	{
		if (Row.Type!=Type || Row.Rank!=Rank) continue;
		if (Match || Row.RequiredPreviousRank!=Rank-1) return nullptr;
		if (Type==EPlayerUpgradeType::HandStability)
		{
			if (!FMath::IsFinite(Row.ShakeMultiplier) || Row.ShakeMultiplier<.5f || Row.ShakeMultiplier>1.f) return nullptr;
		}
		else if (Row.CarryCapacity<1 || Row.CarryCapacity>DefaultUpgradeValue(Type,MaxRank)) return nullptr;
		Match=&Row;
	}
	return Match;
}

float UPlayerUpgradesComponent::GetUpgradeValue(EPlayerUpgradeType Type, int32 Rank) const
{
	if (const auto* Row=FindUpgradeRow(Type,Rank))
		return Type==EPlayerUpgradeType::HandStability ? Row->ShakeMultiplier : Row->CarryCapacity;
	return DefaultUpgradeValue(Type,Rank);
}

int32 UPlayerUpgradesComponent::GetCarryCapacity(EPlayerUpgradeType Type) const
{
	return Type==EPlayerUpgradeType::HandStability ? 0 : int32(GetUpgradeValue(Type,GetUpgradeRank(Type)));
}

float UPlayerUpgradesComponent::GetCarryShakeMultiplier() const
{
	return GetUpgradeValue(EPlayerUpgradeType::HandStability,UpgradeState.HandStabilityRank);
}

int64 UPlayerUpgradesComponent::GetNextUpgradeCostCents(EPlayerUpgradeType Type) const
{
	const int32 CurrentRank=GetUpgradeRank(Type);
	if (CurrentRank<0) return 0;
	const auto* Row=FindUpgradeRow(Type,CurrentRank+1);
	if (!Row || Row->PriceCents<=0) return 0;
	const float CurrentValue=GetUpgradeValue(Type,CurrentRank);
	const bool bImproves=Type==EPlayerUpgradeType::HandStability ? Row->ShakeMultiplier<CurrentValue : Row->CarryCapacity>CurrentValue;
	return bImproves ? Row->PriceCents : 0;
}

UPlayerUpgradesComponent* UPlayerUpgradesComponent::AttachFor(APlayerController& Player)
{
	auto* State=Player.GetPlayerState<APlayerState>();
	if (!IsValid(State) || !Player.HasAuthority() || Player.GetNetMode()==NM_Client || State->IsABot() || State->IsInactive()) return nullptr;
	if (auto* Existing=Find(State)) return Existing;
	auto* Component=NewObject<UPlayerUpgradesComponent>(State);
	State->AddInstanceComponent(Component);
	Component->RegisterComponent();
	State->ForceNetUpdate();
	return Component;
}

void UPlayerUpgradesComponent::RestoreFrom(const APropManager* Manager)
{
	auto* State=Cast<APlayerState>(GetOwner());
	if (!State || !State->HasAuthority() || State->GetNetMode()==NM_Client) return;
	const auto* Saved=Manager ? Manager->FindPlayerUpgrades(GetProfileKey(*State)) : nullptr;
	const FPlayerUpgradeState Restored=Saved ? *Saved : FPlayerUpgradeState{};
	if (UpgradeState==Restored) return;
	UpgradeState=Restored;
	State->ForceNetUpdate();
	OnRep_UpgradeState();
}

void UPlayerUpgradesComponent::RestoreForWorld(APropManager& Manager)
{
	if (!Manager.HasAuthority() || Manager.GetNetMode()==NM_Client) return;
	for (auto It=Manager.GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (auto* Player=It->Get())
			if (auto* Component=AttachFor(*Player)) Component->RestoreFrom(&Manager);
}

bool UPlayerUpgradesComponent::RequestPurchase(EPlayerUpgradeType Type, int32 ExpectedRank, const FGuid& OperationID)
{
	const auto* State=Cast<APlayerState>(GetOwner());
	const auto* Player=State ? State->GetPlayerController() : nullptr;
	if (!Player || !OperationID.IsValid() || (!Player->HasAuthority() && !Player->IsLocalController())) return false;
	if (Player->HasAuthority()) OnPurchaseResult.Broadcast(OperationID,Purchase(Type,ExpectedRank,OperationID));
	else Server_Purchase(Type,ExpectedRank,OperationID);
	return true;
}

EPlayerUpgradePurchaseResult UPlayerUpgradesComponent::Purchase(EPlayerUpgradeType Type, int32 ExpectedRank, const FGuid& OperationID)
{
	using EResult=EPlayerUpgradePurchaseResult;
	auto* State=Cast<APlayerState>(GetOwner());
	const auto* Player=State ? State->GetPlayerController() : nullptr;
	if (!Player || Player->GetPlayerState<APlayerState>()!=State || !IsValid(Player->GetPawn()) || !State->HasAuthority()
		|| State->GetNetMode()==NM_Client || !OperationID.IsValid() || ExpectedRank<0 || GetMaxUpgradeRank(Type)<0)
		return EResult::Unavailable;

	auto* Manager=FindShopManager(GetWorld());
	const FString ProfileKey=GetProfileKey(*State);
	if (!Manager || !Manager->SharedWallet || ProfileKey.IsEmpty()) return EResult::Unavailable;
	RestoreFrom(Manager);
	const FName Reason(*FString::Printf(TEXT("PlayerUpgrade|%s|%d|%d"),*ProfileKey,int32(Type),ExpectedRank));
	FWalletReceipt Receipt;
	if (Manager->SharedWallet->FindReceipt(OperationID,Receipt))
		return Receipt.Reason==Reason && Receipt.DeltaCents<0 ? EResult::AlreadyPurchased : EResult::Unavailable;
	if (ExpectedRank!=GetUpgradeRank(Type)) return EResult::RankChanged;
	if (ExpectedRank==GetMaxUpgradeRank(Type)) return EResult::MaxRank;
	const int64 Price=GetNextUpgradeCostCents(Type);
	FPlayerUpgradeState Next=UpgradeState;
	if (Price<=0 || !Next.SetRank(Type,ExpectedRank+1)) return EResult::Unavailable;

	const auto Result=Manager->SharedWallet->CommitWithResources(OperationID,-Price,Reason,[&]
	{
		if (!Manager->RecordPlayerUpgrades(ProfileKey,Next)) return false;
		UpgradeState=Next;
		return true;
	});
	if (Result==EWalletOperationResult::Applied)
	{
		State->ForceNetUpdate();
		OnRep_UpgradeState();
		return EResult::Purchased;
	}
	if (Result==EWalletOperationResult::InsufficientFunds) return EResult::InsufficientFunds;
	return EResult::Unavailable;
}

void UPlayerUpgradesComponent::OnRep_UpgradeState()
{
	OnUpgradesChanged.Broadcast();
}

void UPlayerUpgradesComponent::Server_Purchase_Implementation(EPlayerUpgradeType Type, int32 ExpectedRank, FGuid OperationID)
{
	Client_PurchaseResult(OperationID,Purchase(Type,ExpectedRank,OperationID));
}

void UPlayerUpgradesComponent::Client_PurchaseResult_Implementation(FGuid OperationID, EPlayerUpgradePurchaseResult Result)
{
	OnPurchaseResult.Broadcast(OperationID,Result);
}

void UPlayerUpgradesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FGameModeEvents::OnGameModePostLoginEvent().AddUObject(this,&UPlayerUpgradesSubsystem::OnPlayerLoggedIn);
}

void UPlayerUpgradesSubsystem::Deinitialize()
{
	FGameModeEvents::OnGameModePostLoginEvent().RemoveAll(this);
	Super::Deinitialize();
}

bool UPlayerUpgradesSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType==EWorldType::Game || WorldType==EWorldType::PIE;
}

void UPlayerUpgradesSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	for (auto It=InWorld.GetPlayerControllerIterator(); It; ++It)
		OnPlayerLoggedIn(InWorld.GetAuthGameMode(),It->Get());
}

void UPlayerUpgradesSubsystem::OnPlayerLoggedIn(AGameModeBase*, APlayerController* Player)
{
	if (IsValid(Player) && Player->GetWorld()==GetWorld())
		if (auto* Component=UPlayerUpgradesComponent::AttachFor(*Player)) Component->RestoreFrom(FindShopManager(GetWorld()));
}
