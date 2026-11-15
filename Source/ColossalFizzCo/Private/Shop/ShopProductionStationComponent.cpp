#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UShopProductionStationComponent::UShopProductionStationComponent() { PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(true); }
void UShopProductionStationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UShopProductionStationComponent,View);
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
	}
	if (!SupplyOutput)
	{
		TArray<USceneComponent*> Components; GetOwner()->GetComponents(Components);
		for (auto* Component:Components) if (Component->ComponentHasTag(TEXT("ShopSupplyOutput"))) { SupplyOutput=Component; break; }
	}
	if (!Filler) Filler=GetOwner()->FindComponentByClass<UBottleFillingComponent>();
	if (Filler && bManualLeverMode) { Filler->bRequireLever=true; Filler->PressurePerContentLevel=FillPressurePerLevel; }
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
	if (IsValid(Filler)) Filler->StopFilling();
	if (GetOwner() && GetOwner()->HasAuthority()) { ReleaseSeatedBottle(); ResetManualCapping(); }
	Super::EndPlay(Reason);
}
void UShopProductionStationComponent::RefreshInputs()
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	const double Now=GetWorld()->GetTimeSeconds();
	const float Elapsed=LastManualUpdate<0 ? 0.f : float(FMath::Max(0.,Now-LastManualUpdate));
	LastManualUpdate=Now;
	if (bRequiresCooperativeCap || bAllowSoloCap) UpdateManualCapping(Elapsed);
	auto* Manager=APropManager::Get(this); if (!Manager) return;
	auto Read=[&](const UBoxComponent* Box, EShopSupplyKind Kind)
	{
		TArray<FShopProductionItem> Items;
		TArray<int32> IDs=ShopInstanceVolumeQuery::Query(Box).Array();
		if (bManualLeverMode && Kind==EShopSupplyKind::ColaCylinder && Filler)
		{
			IDs.Reset();
			for (const auto& Pair:Manager->ISIDData) if (Manager->ShopSupplies && Manager->ShopSupplies->IsCylinderProduct(Pair.Value.ProductID)
				&& FVector::DistSquared(Manager->GetTransform(Pair.Key).GetLocation(),GetOwner()->GetActorLocation())<=FMath::Square(Filler->MaxConnectionDistance)) IDs.Add(Pair.Key);
		}
		IDs.Sort();
		for (int32 ID:IDs)
		{
			const auto* Record=Manager->FindISIDData(ID); const auto* Unique=Manager->FindUniqueData(ID);
			const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
			if (!Unique || !Def || Items.Num()>=16) continue;
			const bool Cylinder=Manager->ShopSupplies && Manager->ShopSupplies->IsCylinderProduct(Record->ProductID);
			if (Kind==EShopSupplyKind::Cap ? (!Def->bIsBottleCap || Unique->EventData.AttachedBottleISID)
				: Kind==EShopSupplyKind::ColaCylinder ? !Cylinder : (Def->bIsBottleCap || Cylinder || Def->ConsumableProfile.NumOfPortions<=0)) continue;
			FShopProductionItem Item; Item.ISID=ID; Item.Name=Def->DisplayName;
			Item.ContentLevel=Unique->EventData.ContentLevel; Item.CapSealed=Unique->EventData.CapSealed; Item.AttachedCapISID=Unique->EventData.AttachedCapISID;
			Item.ResidualPressure=Unique->ContinuousData.SealedPressure;
			Item.TemporarySealSeconds=float(FMath::Max(0.,Unique->EventData.TemporarySealUntil-Manager->WorldElapsedSeconds));
			Item.bTemporarySealUsed=Unique->EventData.bTemporarySealUsed;
			Items.Add(Item);
		}
		return Items;
	};
	View.Bottles=Read(BottleInput,EShopSupplyKind::EmptyBottle); View.Cylinders=Read(CylinderInput,EShopSupplyKind::ColaCylinder); View.Caps=Read(CapInput,EShopSupplyKind::Cap);
	if (Filler && Filler->bIsFilling
		&& (!View.Bottles.ContainsByPredicate([&](const auto& Item){return Item.ISID==Filler->TargetISID;})
			|| !View.Cylinders.ContainsByPredicate([&](const auto& Item){return Item.ISID==Filler->SourceISID;}))) Filler->StopFilling();
	if (bManualLeverMode) UpdateManualEquipment();
	View.bFilling=Filler && Filler->bIsFilling;
}
EShopCheckoutResult UShopProductionStationComponent::Purchase(FGuid Session, FGuid ID, FName Offer, int32 Quantity)
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return EShopCheckoutResult::NotAuthority;
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
	if (Action>=EShopProductionAction::ConnectSupply) return bManualLeverMode ? ManualEquipmentAction(Action,BottleID,OtherID,Player) : EShopCheckoutResult::Unavailable;
	if (Action>=EShopProductionAction::HoldCapRole && Action<=EShopProductionAction::LeaveCapRole) return bRequiresCooperativeCap || bAllowSoloCap ? ManualCapAction(Action,BottleID,Player) : EShopCheckoutResult::Unavailable;
	if (bManualLeverMode && Action==EShopProductionAction::Fill) return EShopCheckoutResult::Unavailable;
	auto* Manager=APropManager::Get(this); if (!Manager) return EShopCheckoutResult::Unavailable;
	if (Action==EShopProductionAction::StopFilling) { if (!Filler) return EShopCheckoutResult::Unavailable; Filler->StopFilling(); RefreshInputs(); return EShopCheckoutResult::Success; }
	if (!ShopInstanceVolumeQuery::Query(BottleInput).Contains(BottleID)) return EShopCheckoutResult::Unavailable;
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
		if ((!Filler || !Filler->bIsFilling || Filler->TargetISID!=BottleID) && ShopInstanceVolumeQuery::Query(CapInput).Contains(OtherID)) Success=Manager->AttachPhysicalCap(BottleID,OtherID);
		break;
	case EShopProductionAction::CloseCap:
	{
		if (bRequiresCooperativeCap || bAllowSoloCap) return EShopCheckoutResult::Unavailable;
		const auto* Record=Manager->FindISIDData(BottleID); const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (Def) Success=Manager->ClosePhysicalCap(BottleID,FMath::Lerp(Def->TorqueSweetSpotMin,Def->TorqueSweetSpotMax,.5f));
		break;
	}
	case EShopProductionAction::OpenCap: Success=Manager->DetachPhysicalCap(BottleID); break;
	default: break;
	}
	RefreshInputs(); return Success ? EShopCheckoutResult::Success : EShopCheckoutResult::Unavailable;
}
