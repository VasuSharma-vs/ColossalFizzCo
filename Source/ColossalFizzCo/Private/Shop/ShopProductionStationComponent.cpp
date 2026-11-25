#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UShopProductionStationComponent::UShopProductionStationComponent() { PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(true); }
void UShopProductionStationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UShopProductionStationComponent,View);
	DOREPLIFETIME(UShopProductionStationComponent,bAutoApplySealOnRelease);
}
void UShopProductionStationComponent::BeginPlay()
{
	Super::BeginPlay();
	TArray<UBoxComponent*> Boxes; GetOwner()->GetComponents(Boxes);
	for (auto* Box:Boxes)
	{
		if (!BottleInput && Box->ComponentHasTag(TEXT("ShopBottleInput"))) BottleInput=Box;
		if (!CylinderInput && Box->ComponentHasTag(TEXT("ShopCylinderInput"))) CylinderInput=Box;
		if (!CapInput && Box->ComponentHasTag(TEXT("ShopCapInput"))) CapInput=Box;
		if (!SealInput && Box->ComponentHasTag(TEXT("ShopSealInput"))) SealInput=Box;
	}
	if (!SupplyOutput)
	{
		TArray<USceneComponent*> Components; GetOwner()->GetComponents(Components);
		for (auto* Component:Components) if (Component->ComponentHasTag(TEXT("ShopSupplyOutput"))) { SupplyOutput=Component; break; }
	}
	if (!Filler) Filler=GetOwner()->FindComponentByClass<UBottleFillingComponent>();
	if (Filler && bManualLeverMode) { bOffersSupplies=false; Filler->bRequireLever=true; Filler->TargetContentLevel=0; }
	if (GetOwner()->HasAuthority() && GetNetMode()!=NM_Client)
	{
		if (auto* Manager=APropManager::Get(this)) if (Manager->ShopSupplies) Manager->ShopSupplies->InitializeCatalog();
		View.SessionID=FGuid::NewGuid(); RefreshInputs();
		GetWorld()->GetTimerManager().SetTimer(RefreshTimer,this,&UShopProductionStationComponent::RefreshInputs,.2f,true);
	}
}
void UShopProductionStationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* ComponentWorld=GetWorld()) ComponentWorld->GetTimerManager().ClearTimer(RefreshTimer);
	if (GetOwner() && GetOwner()->HasAuthority()) EndManualLever();
	if (IsValid(Filler)) Filler->StopFilling();
	if (GetOwner() && GetOwner()->HasAuthority()) { ReleaseSeatedBottle(); ResetManualCapping(); }
	Super::EndPlay(Reason);
}
void UShopProductionStationComponent::RefreshInputs()
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	if (const auto* Machine = Cast<ASocketColaFillingMachine>(GetOwner()); Machine && Machine->IsMachinePlacementActive()) return;
	const double Now=GetWorld()->GetTimeSeconds();
	const float Elapsed=LastManualUpdate<0 ? 0.f : float(FMath::Max(0.,Now-LastManualUpdate));
	LastManualUpdate=Now;
	if (bRequiresCooperativeCap || bAllowSoloCap) UpdateManualCapping(Elapsed);
	auto* Manager=APropManager::Get(this); if (!Manager) return;
	if (bManualLeverMode) RefreshSupplyTrays();
	auto Read=[&](const UBoxComponent* Box, EShopSupplyKind Kind)
	{
		TArray<FShopProductionItem> Items;
		TArray<int32> IDs=ShopInstanceVolumeQuery::Query(Box).Array();
		if (bManualLeverMode && Kind==EShopSupplyKind::EmptyBottle && IsBottleAligned(View.SeatedBottleISID))
			IDs.AddUnique(View.SeatedBottleISID);
		if (bManualLeverMode && Kind==EShopSupplyKind::Cap)
			if (const auto* Machine=Cast<AActivePropActor>(GetOwner())) if (const auto* Equipment=Manager->FindUniqueData(Machine->ISID)) IDs=Equipment->EventData.LoadedCapISIDs;
		if (bManualLeverMode && Kind==EShopSupplyKind::ColaCylinder && Filler)
		{
			IDs.Reset();
			const auto* Machine = Cast<AManualColaFillingMachine>(GetOwner());
			const FVector Origin = Machine ? Machine->GetSupplyConnectionOrigin() : GetOwner()->GetActorLocation();
			for (const auto& Pair:Manager->ISIDData) if (Manager->ShopSupplies && Manager->ShopSupplies->IsCylinderProduct(Pair.Value.ProductID)
				&& FVector::DistSquared(Manager->GetTransform(Pair.Key).GetLocation(),Origin)<=FMath::Square(Filler->MaxConnectionDistance)) IDs.Add(Pair.Key);
		}
		if (!bManualLeverMode || Kind!=EShopSupplyKind::Cap) IDs.Sort();
		for (int32 ID:IDs)
		{
			const auto* Record=Manager->FindISIDData(ID); const auto* Unique=Manager->FindUniqueData(ID);
			const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
			if (!Unique || !Def || Items.Num()>=(bManualLeverMode && Kind==EShopSupplyKind::Cap ? GetCapTrayCapacity() : 16)) continue;
			const bool Cylinder=Manager->ShopSupplies && Manager->ShopSupplies->IsCylinderProduct(Record->ProductID);
			if (Kind==EShopSupplyKind::Cap ? (!Def->bIsBottleCap || Unique->EventData.AttachedBottleISID)
				: Kind==EShopSupplyKind::ColaCylinder ? !Cylinder : (Def->bIsBottleCap || Cylinder || Def->ConsumableProfile.NumOfPortions<=0)) continue;
			FShopProductionItem Item; Item.ISID=ID; Item.Name=Def->DisplayName;
			Item.ContentLevel=Unique->EventData.ContentLevel; Item.CapSealed=Unique->EventData.CapSealed; Item.AttachedCapISID=Unique->EventData.AttachedCapISID;
			Item.ResidualPressure=Unique->ContinuousData.SealedPressure;
			Item.TemporarySealSeconds=float(FMath::Max(0.,Unique->EventData.TemporarySealUntil-Manager->WorldElapsedSeconds));
			Item.bTemporarySealArmed=Unique->EventData.bTemporarySealArmed;
			if (const auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID)))
			{
				Item.TemporarySealSeconds=Bottle->GetManufacturingSealSecondsRemaining();
				Item.TemporarySealIntegrity=Bottle->GetManufacturingSealIntegrity();
				Item.SafeResidualPressure=Bottle->SafeResidualPressure;
			}
			Item.bTemporarySealUsed=Unique->EventData.bTemporarySealUsed;
			Items.Add(Item);
		}
		return Items;
	};
	View.Bottles=Read(BottleInput,EShopSupplyKind::EmptyBottle); View.Cylinders=Read(CylinderInput,EShopSupplyKind::ColaCylinder); View.Caps=Read(CapInput,EShopSupplyKind::Cap);
	if (!bManualLeverMode && Filler && Filler->bIsFilling
		&& (!View.Bottles.ContainsByPredicate([&](const auto& Item){return Item.ISID==Filler->TargetISID;})
			|| !View.Cylinders.ContainsByPredicate([&](const auto& Item){return Item.ISID==Filler->SourceISID;}))) Filler->StopFilling();
	if (bManualLeverMode) UpdateManualEquipment();
	else View.bFilling=Filler && Filler->bIsFilling;
}
EShopCheckoutResult UShopProductionStationComponent::Purchase(FGuid Session, FGuid ID, FName Offer, int32 Quantity)
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return EShopCheckoutResult::NotAuthority;
	if (bManualLeverMode) return EShopCheckoutResult::Unavailable;
	if (!Session.IsValid() || Session!=View.SessionID) return EShopCheckoutResult::OperationConflict;
	auto* Manager=APropManager::Get(this);
	if (!bOffersSupplies || !SupplyOutput || !Manager || !Manager->ShopSupplies) return EShopCheckoutResult::Unavailable;
	TArray<int32> Delivered;
	const auto Result=Manager->ShopSupplies->Purchase(ID,Offer,Quantity,SupplyOutput->GetComponentTransform(),Delivered);
	if (Result==EShopPurchaseResult::Success) { LastDeliveredSupplies=MoveTemp(Delivered); return EShopCheckoutResult::Success; }
	if (Result==EShopPurchaseResult::AlreadyPurchased) return EShopCheckoutResult::AlreadyCompleted;
	if (Result==EShopPurchaseResult::InsufficientFunds) return EShopCheckoutResult::CashUnavailable;
	if (Result==EShopPurchaseResult::DeliveryFailed) return EShopCheckoutResult::Unavailable;
	if (Result==EShopPurchaseResult::OperationConflict) return EShopCheckoutResult::OperationConflict;
	return EShopCheckoutResult::InvalidRequest;
}
EShopCheckoutResult UShopProductionStationComponent::Execute(FGuid Session, EShopProductionAction Action, int32 BottleID, int32 OtherID)
{
	return ExecuteForPlayer(Session,Action,BottleID,OtherID,nullptr);
}

EShopCheckoutResult UShopProductionStationComponent::ExecuteForPlayer(FGuid Session, EShopProductionAction Action, int32 BottleID, int32 OtherID,AActor* Player)
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return EShopCheckoutResult::NotAuthority;
	if (!Session.IsValid() || Session!=View.SessionID) return EShopCheckoutResult::OperationConflict;
	if (Action==EShopProductionAction::FitOrTightenCap)
	{
		if (!bManualLeverMode || BottleID<=0 || View.SeatedBottleISID!=BottleID || !IsBottleAligned(BottleID) || !OperatorInRange(Player))
			return EShopCheckoutResult::Unavailable;
		const auto* Manager=APropManager::Get(this);
		const auto* Bottle=Manager ? Manager->FindUniqueData(BottleID) : nullptr;
		if (!Bottle) return EShopCheckoutResult::Unavailable;
		if (Bottle->EventData.AttachedCapISID>0)
		{
			if (!bAllowSoloCap && !bRequiresCooperativeCap) return EShopCheckoutResult::Unavailable;
			const auto Result=ManualCapAction(EShopProductionAction::TurnCap,BottleID,Player);
			if (Result==EShopCheckoutResult::Success) RefreshInputs();
			return Result;
		}
		if (View.Caps.IsEmpty()) return EShopCheckoutResult::Unavailable;
		Action=EShopProductionAction::AttachCap;
		OtherID=View.Caps[0].ISID;
	}
	if (Action>=EShopProductionAction::ConnectSupply) return bManualLeverMode ? ManualEquipmentAction(Action,BottleID,OtherID,Player) : EShopCheckoutResult::Unavailable;
	if (Action>=EShopProductionAction::HoldCapRole && Action<=EShopProductionAction::LeaveCapRole) return bRequiresCooperativeCap || bAllowSoloCap ? ManualCapAction(Action,BottleID,Player) : EShopCheckoutResult::Unavailable;
	if (bManualLeverMode && Action==EShopProductionAction::Fill) return EShopCheckoutResult::Unavailable;
	auto* Manager=APropManager::Get(this); if (!Manager) return EShopCheckoutResult::Unavailable;
	if (Action==EShopProductionAction::StopFilling)
	{
		if (!Filler) return EShopCheckoutResult::Unavailable;
		if (bManualLeverMode) return ManualEquipmentAction(EShopProductionAction::StopFilling,0,0,Player);
		Filler->StopFilling(); RefreshInputs(); return EShopCheckoutResult::Success;
	}
	if (!(bManualLeverMode && View.SeatedBottleISID==BottleID && IsBottleAligned(BottleID))
		&& !ShopInstanceVolumeQuery::Query(BottleInput).Contains(BottleID)) return EShopCheckoutResult::Unavailable;
	bool Success=false;
	switch (Action)
	{
	case EShopProductionAction::Fill:
		if (const auto* Source=Manager->FindISIDData(OtherID))
			if (Filler && Manager->ShopSupplies && Manager->ShopSupplies->IsCylinderProduct(Source->ProductID) && ShopInstanceVolumeQuery::Query(CylinderInput).Contains(OtherID))
			{
				const auto* Bottle=Manager->FindISIDData(BottleID);const auto* SourceData=Manager->FindUniqueData(OtherID);
    if(Bottle && SourceData && !Manager->IsAssemblyCompatible(Bottle->ProductID,SourceData->EventData.ContentID,0))return EShopCheckoutResult::ContentNotSupported;
    Filler->TargetContentLevel=1.f;
				Success=Filler->StartFilling(OtherID,BottleID);
			}
		break;
	case EShopProductionAction::AttachCap:
  if(const auto* B=Manager->FindISIDData(BottleID))if(const auto* C=Manager->FindISIDData(OtherID))
   if(const auto* U=Manager->FindUniqueData(BottleID))
    if(!Manager->IsAssemblyCompatible(B->ProductID,U->EventData.ContentID,C->ProductID))return EShopCheckoutResult::CapNotSupported;
		if ((!Filler || !Filler->bIsFilling || Filler->TargetISID!=BottleID)
			&& (bManualLeverMode ? View.Caps.ContainsByPredicate([&](const auto& Cap){return Cap.ISID==OtherID;}) : ShopInstanceVolumeQuery::Query(CapInput).Contains(OtherID)))
		{
			auto* TrayCap=bManualLeverMode ? Manager->GetActiveActorFromISID(OtherID) : nullptr;
			// The assembly service recycles the cap's independent actor. Release the
			// tray's demotion guard for that transfer, restoring it if fitting fails.
			if (TrayCap) TrayCap->SetSeatedInFiller(false);
			if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID))) Success=Bottle->AttachManufacturingCap(OtherID);
			else Success=Manager->AttachPhysicalCap(BottleID,OtherID);
			if (Success && bManualLeverMode) ReleaseStoredSupply(OtherID,false);
			else if (IsValid(TrayCap) && TrayCap->ISID==OtherID) TrayCap->SetSeatedInFiller(true);
		}
		break;
	case EShopProductionAction::CloseCap:
	{
		if (bRequiresCooperativeCap || bAllowSoloCap) return EShopCheckoutResult::Unavailable;
		const auto* Record=Manager->FindISIDData(BottleID); const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (Def)
		{
			const float Torque=FMath::Lerp(Def->TorqueSweetSpotMin,Def->TorqueSweetSpotMax,.5f);
			if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID))) Success=Bottle->CloseManufacturingCap(Torque);
			else Success=Manager->ClosePhysicalCap(BottleID,Torque);
		}
		break;
	}
	case EShopProductionAction::OpenCap: Success=Manager->DetachPhysicalCap(BottleID); break;
	default: break;
	}
	RefreshInputs(); return Success ? EShopCheckoutResult::Success : EShopCheckoutResult::Unavailable;
}
