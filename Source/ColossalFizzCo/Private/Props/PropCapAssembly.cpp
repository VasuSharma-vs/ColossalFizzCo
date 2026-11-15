#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Misc/ScopeExit.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropDataAsset.h"
#include "Engine/StaticMesh.h"

namespace
{
	bool StockMayChange(const APropManager* Manager, int32 ISID)
	{
		FShopStockEntry Entry;
		return !Manager->ShopCheckout || !Manager->ShopCheckout->GetStockEntry(ISID,Entry) || Manager->ShopCheckout->IsAvailableShopStock(ISID);
	}
}

bool APropManager::EnsureDefaultPhysicalCap(int32 BottleID)
{
	if (!HasAuthority() || GetNetMode()==NM_Client) return false;
	const auto* Record=FindISIDData(BottleID);
	const auto* Def=Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Def) return false;
	if (Def->bIsBottleCap || Def->BottleMaterialType!=EBottleMaterialType::Plastic
		|| Def->DefaultUniqueData.EventData.CapSealed<=0 || !Def->SourceDataAsset || !ShopSupplies) return true;
	const auto* Bottle=FindUniqueData(BottleID);
	if (!Bottle) return false;
	if (Bottle->EventData.AttachedCapISID)
	{
		const auto* Cap=FindUniqueData(Bottle->EventData.AttachedCapISID);
		return Cap && Cap->EventData.AttachedBottleISID==BottleID;
	}
	// Opening/ejecting a cap clears the seal. Never create another free cap for it.
	if (Bottle->EventData.CapSealed<=0) return true;
	const auto* Catalog=ShopSupplies->Catalog.LoadSynchronous();
	const FSoftObjectPath Source(Def->SourceDataAsset.Get());
	const bool bCatalogBottle=Catalog && Catalog->FinishedBottleProducts.ContainsByPredicate([&](const auto& Product){return Product.ToSoftObjectPath()==Source;});
	const bool bMigratedBottle=Def->LiquidVisualPID>0 && Def->StaticMesh
		&& Def->StaticMesh->GetPathName()==TEXT("/Game/Assest/meshs/PlasticBottle/SM_PlasticBottle.SM_PlasticBottle");
	if (!Catalog || (!bCatalogBottle && !bMigratedBottle)) return true;
	if (!ShopSupplies->IsNormalPrototypeActive())
	{
		if (!ShopSupplies->InitializeCatalog()) return false;
		// Catalog registration can reallocate maps and create the liquid child.
		return EnsureDefaultPhysicalCap(BottleID);
	}
	const auto Type=Def->DefaultCapType;
	const int32 SelectedCapPID=Def->SourceDataAsset ? Def->SourceDataAsset->InitialCapProduct.PID : 0;
	const FTransform Transform=Def->CapAttachmentTransform*GetTransform(BottleID);
	const double BottleCreationTime=Bottle->EventData.CreationTime;
	const auto* Offer=Catalog->Offers.FindByPredicate([&](const auto& Item)
	{
		const auto* Product=Item.Kind==EShopSupplyKind::Cap ? Item.Product.LoadSynchronous() : nullptr;
		return Product && Product->bIsBottleCap && Product->DefaultCapType==Type
			&& IsBottleCapTypeSupportedInMask(Def->SupportedCapMask,static_cast<uint8>(Type));
	});
	if (!Offer && !SelectedCapPID) return false;
	int32 PID=SelectedCapPID ? SelectedCapPID : RegisterProductFromDataAsset(Offer->Product.LoadSynchronous());
	const auto* InitialCap=FindProductDefinition(PID);
	if(!InitialCap || !InitialCap->StaticMesh)return false;
	const auto* CurrentRoot=FindISIDData(BottleID);const auto* RootDef=CurrentRoot ? FindProductDefinition(CurrentRoot->ProductID) : nullptr;
	if(!RootDef)return false;
	if(RootDef->Category==EPropProductCategory::Other && !SelectedCapPID && InitialCap->bUseBottlePIDCompatibility)
	{
		// Preserve the old material-based factory-cap entitlement in legacy saves
		// as an explicit runtime PID whitelist. New catalog caps keep strict rules.
		const int32 RootPID=CurrentRoot->ProductID;
		const int32 CanonicalCap=CanonicalProductPID(PID);int32 LegacyPID=0;
		for(const auto& Pair:ProductDefinitions)
			if(Pair.Key!=CanonicalCap && Pair.Value.bIsBottleCap && Pair.Value.CatalogPID==CanonicalCap && Pair.Value.SupportedBottlePIDs.Contains(RootPID)){LegacyPID=Pair.Key;break;}
		if(!LegacyPID)
		{
			auto Legacy=*InitialCap;Legacy.ProductID=AllocateProductID();Legacy.StaticMeshID=Legacy.ProductID;
			Legacy.SourceDataAsset=nullptr;Legacy.SupportedBottlePIDs={RootPID};Legacy.SupportedBottleMaterials={EBottleMaterialType::Plastic};
			LegacyPID=Legacy.ProductID;if(!RegisterProductDefinition(Legacy))return false;
		}
		PID=LegacyPID;
	}
	const int32 CapID=PID>0 ? GenerateProductInstance(PID,Transform) : 0;
	if (!CapID) return false;
	const auto* CurrentBottle = FindISIDData(BottleID);
	const auto* CapDef = FindProductDefinition(PID);
	if (!CurrentBottle || !CapDef || !IsAssemblyCompatible(CurrentBottle->ProductID, FindUniqueData(BottleID)->EventData.ContentID, PID)
		|| !InstallAssemblyEdge(BottleID, CapID, CapDef->CapMountOffset * FindProductDefinition(CurrentBottle->ProductID)->CapAttachmentTransform))
	{
		ReleaseInstanceToEmpty(CapID); return false;
	}
	// Registration/generation may reallocate canonical maps; do not retain their pointers.
	Bottle=FindUniqueData(BottleID);
	if (!Bottle || Bottle->EventData.AttachedCapISID || Bottle->EventData.CapSealed<=0)
	{
		ReleaseInstanceToEmpty(CapID); return false;
	}
	FPropEventSyncDelta B,C;
	B.ISID=BottleID; B.ExtendedDirtyMask=EPropExtendedEventMask::AttachedCap; B.AttachedCapISID=CapID;
	B.SetFlag(EPropEventDirtyMask::CapType); B.CapType=static_cast<uint8>(Type);
	C.ISID=CapID; C.ExtendedDirtyMask=EPropExtendedEventMask::AttachedBottle; C.AttachedBottleISID=BottleID;
	C.SetFlag(EPropEventDirtyMask::Grabbable); C.bIsGrabbable=false;
	C.SetFlag(EPropEventDirtyMask::CreationTime); C.CreationTime=BottleCreationTime;
	if (!SubmitAtomicInstanceEventUpdates({B,C})) { ReleaseInstanceToEmpty(CapID); return false; }
	SynchronizeCapAssembly(BottleID);
	return true;
}

bool APropManager::AttachPhysicalCap(int32 BottleID, int32 CapID)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || BottleID==CapID || !StockMayChange(this,BottleID) || !StockMayChange(this,CapID)) return false;
	const auto* BottleRecord=FindISIDData(BottleID); const auto* CapRecord=FindISIDData(CapID);
	const auto* Bottle=FindUniqueData(BottleID); const auto* Cap=FindUniqueData(CapID);
	const auto* BottleDef=BottleRecord ? FindProductDefinition(BottleRecord->ProductID) : nullptr;
	const auto* CapDef=CapRecord ? FindProductDefinition(CapRecord->ProductID) : nullptr;
	if (!Bottle || !Cap || !BottleDef || !CapDef || !CapDef->bIsBottleCap || BottleDef->bIsBottleCap
		|| BottleDef->ConsumableProfile.NumOfPortions<=0 || Bottle->EventData.AttachedCapISID || Cap->EventData.AttachedBottleISID
		|| Bottle->EventData.CapSealed>0 || CapRecord->State==EPropState::Held || Cap->EventData.Strength<=0
		|| !IsAssemblyCompatible(BottleRecord->ProductID, Bottle->EventData.ContentID, CapRecord->ProductID)) return false;
	const uint8 Type=static_cast<uint8>(CapDef->DefaultCapType);
	const FTransform Mount = CapDef->CapMountOffset * BottleDef->CapAttachmentTransform;
	const FTransform BottleWorld = GetTransform(BottleID);
	const FVector CapCenter = CapDef->StaticMesh ? CapDef->StaticMesh->GetBoundingBox().GetCenter() : FVector::ZeroVector;
	const FVector CapLocation = GetTransform(CapID).TransformPosition(CapCenter);
	// Some existing trays locate the cap near the bottle base. A player may also hold it directly at the neck.
	if (FMath::Min(FVector::DistSquared(BottleWorld.GetLocation(), CapLocation),
		FVector::DistSquared((Mount * BottleWorld).TransformPosition(CapCenter), CapLocation)) > FMath::Square(150.f)) return false;
	AActivePropActor* BottleActor = nullptr;
	if (!PromoteInstanceToASM(BottleID,EPropActivationReason::Interaction,false,BottleActor) || !BottleActor
		|| !InstallAssemblyEdge(BottleID, CapID, Mount)) return false;
	FPropEventSyncDelta B,C;
	B.ISID=BottleID; B.ExtendedDirtyMask=EPropExtendedEventMask::AttachedCap; B.AttachedCapISID=CapID;
	B.SetFlag(EPropEventDirtyMask::CapType); B.CapType=Type;
	C.ISID=CapID; C.ExtendedDirtyMask=EPropExtendedEventMask::AttachedBottle; C.AttachedBottleISID=BottleID;
	C.SetFlag(EPropEventDirtyMask::Grabbable); C.bIsGrabbable=false;
	if (!SubmitAtomicInstanceEventUpdates({B,C}))
	{
		SetAssemblyEdge(CapID, FPropAttachment()); InitializeInstanceTransform(CapID, Mount * GetTransform(BottleID)); return false;
	}
	SynchronizeCapAssembly(BottleID);
	return true;
}

bool APropManager::ClosePhysicalCap(int32 BottleID, float Torque)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(Torque) || Torque<0 || Torque>1 || !StockMayChange(this,BottleID)) return false;
	const auto* Record=FindISIDData(BottleID); const auto* Bottle=FindUniqueData(BottleID);
	const auto* Def=Record ? FindProductDefinition(Record->ProductID) : nullptr;
	const auto* Cap=Bottle ? FindUniqueData(Bottle->EventData.AttachedCapISID) : nullptr;
	if (!Bottle || !Def || !Cap || Cap->EventData.AttachedBottleISID!=BottleID || Cap->EventData.Strength<=0) return false;
	const auto* CapRecord=FindISIDData(Bottle->EventData.AttachedCapISID);
	if(!CapRecord || !IsAssemblyCompatible(Record->ProductID,Bottle->EventData.ContentID,CapRecord->ProductID))return false;
	FPropEventSyncDelta D; D.ISID=BottleID; D.SetFlag(EPropEventDirtyMask::CapSealed);
	const bool SweetSpot=Torque>=Def->TorqueSweetSpotMin && Torque<=Def->TorqueSweetSpotMax;
	if (SweetSpot && Bottle->EventData.TemporarySealUntil>0)
	{
		D.ExtendedDirtyMask|=EPropExtendedEventMask::TemporarySeal; D.TemporarySealUntil=0;
		D.bTemporarySealUsed=Bottle->EventData.bTemporarySealUsed; D.bManualFillPressure=Bottle->EventData.bManualFillPressure;
	}
	D.CapSealed=SweetSpot ? 1.f : FMath::Min(.99f,Torque/FMath::Max(.01f,Def->TorqueSweetSpotMin));
	if (Torque>Def->TorqueSweetSpotMax)
	{
		D.SetFlag(EPropEventDirtyMask::Strength);
		D.Strength=FMath::Max(0.f,Bottle->EventData.Strength-(Torque-Def->TorqueSweetSpotMax)*FMath::Max(0.f,Def->StrengthDamageRate));
	}
	return SubmitInstanceEventUpdate(BottleID,D);
}

bool APropManager::DetachPhysicalCap(int32 BottleID, FVector Velocity)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || Velocity.ContainsNaN() || !StockMayChange(this,BottleID)) return false;
	const auto* Bottle=FindUniqueData(BottleID);
	const int32 CapID=Bottle ? Bottle->EventData.AttachedCapISID : 0;
	const auto* Cap=FindUniqueData(CapID);
	if (!Bottle || !Cap || Cap->EventData.AttachedBottleISID!=BottleID) return false;
	FTransform Transform=GetTransform(CapID);
	if (const auto* Actor=GetActiveActorFromISID(CapID)) Transform=Actor->GetActorTransform();
	FPropEventSyncDelta B,C;
	B.ISID=BottleID; B.ExtendedDirtyMask=EPropExtendedEventMask::AttachedCap; B.AttachedCapISID=0;
	B.SetFlag(EPropEventDirtyMask::CapSealed); B.SetFlag(EPropEventDirtyMask::CapType); B.CapSealed=0; B.CapType=0;
	C.ISID=CapID; C.ExtendedDirtyMask=EPropExtendedEventMask::AttachedBottle; C.AttachedBottleISID=0;
	C.SetFlag(EPropEventDirtyMask::Grabbable); C.bIsGrabbable=true;
	if (!SubmitAtomicInstanceEventUpdates({B,C})) return false;
	SetAssemblyEdge(CapID, FPropAttachment());
	InitializeInstanceTransform(CapID,Transform);
	MarkInstanceDirty(CapID, EPropDirtyFlags::Representation | EPropDirtyFlags::Transform);
	if (SetInstanceToUnsettled(CapID))
	{
		if (auto* Loose=GetActiveActorFromISID(CapID); Loose && Loose->PropMesh && Loose->PropMesh->IsSimulatingPhysics())
			Loose->PropMesh->SetPhysicsLinearVelocity(Velocity);
	}
	return true;
}

void APropManager::SynchronizeCapAssembly(int32 ISID)
{
	RefreshAssembly(ISID);
}
