#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Props/ColaPropActor.h"
#include "Shop/ShopCheckoutComponent.h"

bool UShopProductionStationComponent::OperatorInRange(const AActor* Player) const
{
	const auto* Pawn=Cast<APawn>(Player);
	const auto* Actions=IsValid(Pawn) ? Pawn->FindComponentByClass<UShopPlayerInteractionComponent>() : nullptr;
	return IsValid(Pawn) && IsValid(Pawn->GetController()) && Pawn->GetController()->IsPlayerController()
		&& Pawn->GetWorld()==GetWorld() && GetOwner() && Actions && Actions->IsStationReachable(GetOwner());
}

float UShopProductionStationComponent::RequiredHoldingForce(int32 BottleID) const
{
	const auto* Manager=APropManager::Get(this);
	const auto* Bottle=Manager ? Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID)) : nullptr;
	return Bottle ? Bottle->GetBottlePressure() : 0.f;
}

bool UShopProductionStationComponent::HasSufficientCapHold(int32 BottleID) const
{
	return (bRequiresCooperativeCap || bAllowSoloCap) && View.ManualBottleISID==BottleID && OperatorInRange(View.CapHolder)
		&& View.HoldingForce>=RequiredHoldingForce(BottleID);
}

void UShopProductionStationComponent::ResetManualCapping()
{
	if (auto* Manager=APropManager::Get(this)) if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(View.ManualBottleISID)))
		if (Bottle->CapHoldingStation.Get()==this) Bottle->CapHoldingStation.Reset();
	View.ManualBottleISID=0; View.CapHolder=nullptr; View.CapRotator=nullptr;
	View.HoldingForce=0; View.HoldingEfficiency=1; View.RotationProgress=0;
	LastHoldingPress=LastRotationTurn=-1; LiquidReleaseEpisode.Invalidate();
}

EShopCheckoutResult UShopProductionStationComponent::ManualCapAction(EShopProductionAction Action,int32 BottleID,AActor* Player)
{
	if (!OperatorInRange(Player)) return EShopCheckoutResult::Unavailable;
	if (Action==EShopProductionAction::LeaveCapRole)
	{
		if (View.CapHolder==Player) { View.CapHolder=nullptr; View.HoldingForce=0; }
		if (View.CapRotator==Player) View.CapRotator=nullptr;
		GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success;
	}
	auto* Manager=APropManager::Get(this);
	const auto* Unique=Manager ? Manager->FindUniqueData(BottleID) : nullptr;
	const auto* Record=Manager ? Manager->FindISIDData(BottleID) : nullptr;
	const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	FShopStockEntry Stock;
	if (Manager && Manager->ShopCheckout && Manager->ShopCheckout->GetStockEntry(BottleID,Stock)
		&& !Manager->ShopCheckout->IsAvailableShopStock(BottleID)) return EShopCheckoutResult::Unavailable;
	if (auto* Active=Manager ? Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID)) : nullptr)
		if (Active->CapHoldingStation.IsValid() && Active->CapHoldingStation.Get()!=this) return EShopCheckoutResult::OperationConflict;
	auto* Bottle=Manager ? Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID)) : nullptr;
	if (!Unique || !Def || !Bottle || !Unique->EventData.AttachedCapISID
		|| Unique->EventData.CapSealed>=1
		|| !(bManualLeverMode && View.SeatedBottleISID==BottleID && IsBottleAligned(BottleID))
			&& !ShopInstanceVolumeQuery::Query(BottleInput).Contains(BottleID)
		|| (View.ManualBottleISID && View.ManualBottleISID!=BottleID)) return EShopCheckoutResult::Unavailable;
	const auto* CapRecord=Manager->FindISIDData(Unique->EventData.AttachedCapISID);
	if(!CapRecord || !Manager->IsAssemblyCompatible(Record->ProductID,Unique->EventData.ContentID,CapRecord->ProductID))return EShopCheckoutResult::CapNotSupported;
	const bool ClaimHolding=Action==EShopProductionAction::HoldCapRole || (Action==EShopProductionAction::PressCap && !IsValid(View.CapHolder));
	const bool ClaimRotating=Action==EShopProductionAction::RotateCapRole || (Action==EShopProductionAction::TurnCap && !IsValid(View.CapRotator));
	if (ClaimHolding || ClaimRotating)
	{
		auto& Role=ClaimHolding ? View.CapHolder : View.CapRotator;
		const auto& Other=ClaimHolding ? View.CapRotator : View.CapHolder;
		if ((IsValid(Role) && Role!=Player) || Other==Player) return EShopCheckoutResult::OperationConflict;
		if (!View.ManualBottleISID)
		{
			View.ManualBottleISID=BottleID; LiquidReleaseEpisode=FGuid::NewGuid();
			View.RotationProgress=FMath::Clamp(Unique->EventData.CapSealed,0.f,1.f);
		}
		Bottle->CapHoldingStation=this;
		Role=Player;
		if (Action==EShopProductionAction::HoldCapRole || Action==EShopProductionAction::RotateCapRole)
		{ GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success; }
		// The first real G/R contribution can claim its available role in this
		// same request. No unheld interval is required between selection and G.
	}
	const bool Protected=bAllowSoloCap && (Bottle->IsManufacturingSafeToRelease() || Bottle->HasTemporaryManufacturingSeal());
	const double Now=GetWorld()->GetTimeSeconds();
	if (Action==EShopProductionAction::PressCap)
	{
		if (View.CapHolder!=Player || (LastHoldingPress>=0 && Now-LastHoldingPress<FMath::Max(.02f,MinimumInputInterval))) return EShopCheckoutResult::OperationConflict;
		LastHoldingPress=Now;
		View.HoldingForce=FMath::Clamp(View.HoldingForce+FMath::Max(0.f,HoldingForcePerPress)*View.HoldingEfficiency,0.f,FMath::Max(0.f,MaximumHoldingForce)*View.HoldingEfficiency);
	}
	else if (Action==EShopProductionAction::TurnCap)
	{
		if (View.CapRotator!=Player || (!Protected && !HasSufficientCapHold(BottleID)) || (LastRotationTurn>=0 && Now-LastRotationTurn<FMath::Max(.02f,MinimumInputInterval))) return EShopCheckoutResult::OperationConflict;
		LastRotationTurn=Now;
		if (!Bottle->AdvanceManufacturingCap(FMath::Clamp(RotationPerTurn,.01f,1.f),HasSufficientCapHold(BottleID))) return EShopCheckoutResult::Unavailable;
		if (Bottle->GetCapSealed()>=1) ResetManualCapping();
		else View.RotationProgress=Bottle->GetCapSealed();
	}
	else return EShopCheckoutResult::InvalidRequest;
	GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success;
}

void UShopProductionStationComponent::UpdateManualCapping(float DeltaSeconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0 || !View.ManualBottleISID) return;
	auto* Manager=APropManager::Get(this);
	const auto* Unique=Manager ? Manager->FindUniqueData(View.ManualBottleISID) : nullptr;
	const auto* Record=Manager ? Manager->FindISIDData(View.ManualBottleISID) : nullptr;
	const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	FShopStockEntry Stock;
	const bool Locked=Manager && Manager->ShopCheckout && Manager->ShopCheckout->GetStockEntry(View.ManualBottleISID,Stock)
		&& !Manager->ShopCheckout->IsAvailableShopStock(View.ManualBottleISID);
	if (Locked || !Unique || !Product || !Unique->EventData.AttachedCapISID || Unique->EventData.CapSealed>=1
		|| (!(bManualLeverMode && View.SeatedBottleISID==View.ManualBottleISID && IsBottleAligned(View.ManualBottleISID))
			&& !ShopInstanceVolumeQuery::Query(BottleInput).Contains(View.ManualBottleISID))) { ResetManualCapping(); return; }
	if (!OperatorInRange(View.CapHolder)) { View.CapHolder=nullptr; View.HoldingForce=0; }
	if (!OperatorInRange(View.CapRotator)) View.CapRotator=nullptr;
	View.HoldingEfficiency=FMath::Max(0.f,View.HoldingEfficiency-FMath::Max(0.f,HoldingEfficiencyDecayPerSecond)*DeltaSeconds);
	View.HoldingForce=FMath::Clamp(View.HoldingForce-FMath::Max(0.f,HoldingForceDecayPerSecond)*DeltaSeconds,0.f,FMath::Max(0.f,MaximumHoldingForce)*View.HoldingEfficiency);
	if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(View.ManualBottleISID)))
		Bottle->UpdateManufacturingCapLeakage(DeltaSeconds,HasSufficientCapHold(View.ManualBottleISID),LiquidReleaseEpisode);
	GetOwner()->ForceNetUpdate();
}
