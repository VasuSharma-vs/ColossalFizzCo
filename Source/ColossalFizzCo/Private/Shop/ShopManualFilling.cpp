#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPropActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Components/RocketProjectileComponent.h"

void UShopProductionStationComponent::ResetEquipmentSession()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	ReleaseSeatedBottle();
	if (Filler) Filler->StopFilling();
	ResetManualCapping(); View=FShopProductionView(); View.SessionID=FGuid::NewGuid();
	GetOwner()->ForceNetUpdate();
}

bool UShopProductionStationComponent::IsBottleAligned(int32 ID) const
{
	const auto* Manager=APropManager::Get(this);
	const auto* Record=Manager ? Manager->FindISIDData(ID) : nullptr;
	const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	if (!Record || !Product || Product->BottleMaterialType!=EBottleMaterialType::Plastic || Record->State==EPropState::Held || !BottleHolder) return false;
	if (const auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID))) if (Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight()) return false;
	const FTransform Transform=Manager->GetTransform(ID);
	return FVector::DistSquared(Transform.GetLocation(),BottleHolder->GetComponentLocation())<=FMath::Square(8.f)
		&& FVector::DotProduct(Transform.GetUnitAxis(EAxis::Z),BottleHolder->GetUpVector())>=.99f;
}

void UShopProductionStationComponent::ReleaseSeatedBottle()
{
	auto* Manager = APropManager::Get(this);
	auto* Previous = Manager ? Manager->GetActiveActorFromISID(View.SeatedBottleISID) : nullptr;
	View.SeatedBottleISID = 0;
	if (!Previous || !Previous->IsSeatedInFiller()) return;
	Previous->SetSeatedInFiller(false);
	const auto* Bottle = Cast<AColaPropActor>(Previous);
	if (Previous->bIsHeld || Previous->bIsInMotion || (Bottle && Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight())) return;
	if (Previous->PhysicsSimComponent) Previous->PhysicsSimComponent->SetStateUnsettled();
	else if (Previous->PropMesh) Previous->PropMesh->SetSimulatePhysics(true);
}

void UShopProductionStationComponent::UpdateManualEquipment()
{
	auto* Manager=APropManager::Get(this); if (!Manager || !Filler) return;
	auto* Machine=Cast<AActivePropActor>(GetOwner());
	const auto* State=Machine ? Manager->FindUniqueData(Machine->ISID) : nullptr;
	if (State) View.ConnectedSourceISID=State->EventData.LinkedSupplyISID;
	const bool MovingMachine=Machine && Machine->bIsHeld;
	if (MovingMachine || !IsBottleAligned(View.SeatedBottleISID))
	{
		ReleaseSeatedBottle();
	}
	if (MovingMachine) { Filler->StopFilling(); View.LeverOperator=nullptr; return; }
	if (!View.SeatedBottleISID) for (const auto& Bottle:View.Bottles) if (IsBottleAligned(Bottle.ISID)) { View.SeatedBottleISID=Bottle.ISID; break; }
	if (View.SeatedBottleISID) if (auto* Active=Manager->GetActiveActorFromISID(View.SeatedBottleISID)) Active->SetSeatedInFiller(true);
	if (Filler->bIsFilling && (!OperatorInRange(View.LeverOperator) || Filler->TargetISID!=View.SeatedBottleISID
		|| Filler->SourceISID!=View.ConnectedSourceISID || GetWorld()->GetTimeSeconds()>=Filler->LeverExpiresAt)) Filler->StopFilling();
	if (!Filler->bIsFilling && GetWorld()->GetTimeSeconds()>=Filler->LeverExpiresAt) View.LeverOperator=nullptr;
}

EShopCheckoutResult UShopProductionStationComponent::ManualEquipmentAction(EShopProductionAction Action,int32 BottleID,int32 OtherID,AActor* Player)
{
	if (!OperatorInRange(Player) || !Filler) return EShopCheckoutResult::Unavailable;
	auto* Manager=APropManager::Get(this); if (!Manager) return EShopCheckoutResult::Unavailable;
	if (auto* Machine=Cast<AActivePropActor>(GetOwner())) if (Machine->bIsHeld) return EShopCheckoutResult::Unavailable;
	UpdateManualEquipment();
	if (Action==EShopProductionAction::ConnectSupply)
	{
		const auto* Source=Manager->FindISIDData(OtherID);
		if (Filler->bIsFilling || !Source || !Manager->ShopSupplies || !Manager->ShopSupplies->IsCylinderProduct(Source->ProductID)
			|| FVector::DistSquared(GetOwner()->GetActorLocation(),Manager->GetTransform(OtherID).GetLocation())>FMath::Square(Filler->MaxConnectionDistance)) return EShopCheckoutResult::Unavailable;
		if (auto* Machine=Cast<AActivePropActor>(GetOwner()))
		{
			FPropEventSyncDelta D; D.ExtendedDirtyMask=EPropExtendedEventMask::LinkedSupply; D.LinkedSupplyISID=OtherID;
			if (!Manager->SubmitInstanceEventUpdate(Machine->ISID,D)) return EShopCheckoutResult::Unavailable;
		}
		View.ConnectedSourceISID=OtherID;
	}
	else if (Action==EShopProductionAction::AlignBottle)
	{
		if (!BottleHolder || Filler->bIsFilling || !ShopInstanceVolumeQuery::Query(BottleInput).Contains(BottleID)) return EShopCheckoutResult::Unavailable;
		const auto* Record=Manager->FindISIDData(BottleID);
		const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (!Product || Product->BottleMaterialType!=EBottleMaterialType::Plastic || Product->ConsumableProfile.NumOfPortions<=0
			|| (View.SeatedBottleISID && View.SeatedBottleISID!=BottleID)) return EShopCheckoutResult::OperationConflict;
		AActivePropActor* Bottle=nullptr;
		if (!Manager->PromoteInstanceToASM(BottleID,EPropActivationReason::Interaction,false,Bottle) || !Bottle || Bottle->bIsHeld) return EShopCheckoutResult::Unavailable;
		const FTransform Placement(BottleHolder->GetComponentQuat(),BottleHolder->GetComponentLocation(),Manager->GetTransform(BottleID).GetScale3D());
		Manager->InitializeInstanceTransform(BottleID,Placement);
		Bottle->SetSeatedInFiller(true);
		View.SeatedBottleISID=BottleID;
	}
	else if (Action==EShopProductionAction::HoldFillingLever)
	{
		if (View.SeatedBottleISID!=BottleID || !IsBottleAligned(BottleID) || View.ConnectedSourceISID<=0) return EShopCheckoutResult::Unavailable;
		if (IsValid(View.LeverOperator) && View.LeverOperator!=Player && GetWorld()->GetTimeSeconds()<Filler->LeverExpiresAt) return EShopCheckoutResult::OperationConflict;
		const auto* B=Manager->FindISIDData(BottleID);const auto* S=Manager->FindUniqueData(View.ConnectedSourceISID);
  if(B && S && !Manager->IsAssemblyCompatible(B->ProductID,S->EventData.ContentID,0))return EShopCheckoutResult::ContentNotSupported;
  Filler->LeverOperator=Player; Filler->LeverExpiresAt=GetWorld()->GetTimeSeconds()+FMath::Max(.15f,LeverLeaseSeconds);
		if (!Filler->bIsFilling && !Filler->StartFilling(View.ConnectedSourceISID,BottleID)) return EShopCheckoutResult::Unavailable;
		View.LeverOperator=Player;
	}
	else if (Action==EShopProductionAction::ReleaseFillingLever)
	{
		if (View.LeverOperator && View.LeverOperator!=Player) return EShopCheckoutResult::OperationConflict;
		Filler->StopFilling(); Filler->LeverOperator.Reset(); Filler->LeverExpiresAt=0; View.LeverOperator=nullptr;
	}
	else if (Action==EShopProductionAction::ApplyTemporarySeal)
	{
		const auto* Data=Manager->FindUniqueData(BottleID);
		if (!Data || !IsBottleAligned(BottleID) || Filler->bIsFilling || Data->EventData.ContentLevel<=0
			|| Data->EventData.CapSealed>=1 || Data->EventData.bTemporarySealUsed || !FMath::IsFinite(TemporarySealDuration) || TemporarySealDuration<=0) return EShopCheckoutResult::Unavailable;
		FPropEventSyncDelta D; D.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal;
		D.TemporarySealUntil=Manager->WorldElapsedSeconds+TemporarySealDuration; D.bTemporarySealUsed=true; D.bManualFillPressure=true;
		if (!Manager->SubmitInstanceEventUpdate(BottleID,D)) return EShopCheckoutResult::Unavailable;
	}
	else return EShopCheckoutResult::InvalidRequest;
	GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success;
}
