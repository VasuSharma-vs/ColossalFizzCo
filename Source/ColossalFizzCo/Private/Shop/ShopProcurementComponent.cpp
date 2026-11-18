#include "Shop/ShopProcurementComponent.h"
#include "Props/PropProductCatalog.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Engine/World.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"

UShopProcurementComponent::UShopProcurementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Catalog = TSoftObjectPtr<UShopSupplyCatalog>(FSoftObjectPath(TEXT("/Game/NormalColaPrototype/DA_NormalColaSupplyCatalog.DA_NormalColaSupplyCatalog")));
}

bool UShopProcurementComponent::InitializeCatalog()
{
	if (bCatalogInitialized) return true;
	auto* Manager=Cast<APropManager>(GetOwner()); const auto* Data=Catalog.LoadSynchronous();
	if (!Manager || !Manager->HasAuthority() || GetNetMode()==NM_Client || !Data || Data->FinishedBottleProducts.Num()!=5) return false;
 if(Manager->ProductCatalog.LoadSynchronous())
 {
  if(!Manager->InitializeProductCatalog())return false;
  // Legacy authoring references are initial-content presets, not bottle capacities.
  for(const auto& Ref:Data->FinishedBottleProducts)if(!Manager->RegisterProductFromDataAsset(Ref.LoadSynchronous()))return false;
  for(const auto& Offer:Data->Offers)if(!Manager->RegisterProductFromDataAsset(Offer.Product.LoadSynchronous()))return false;
  RuntimeCatalog=Catalog.Get();bCatalogInitialized=true;return true;
 }
	auto* Empty=Data->EmptyBottleProduct.LoadSynchronous();
	if (!Empty || Empty->NumOfPortions<=0 || Empty->DefaultContentLevel!=0) return false;
	TSet<int32> Contents; TArray<UInteractablePropDataAsset*> Bottles;
	for (const auto& Ref:Data->FinishedBottleProducts)
	{
		auto* Product=Ref.LoadSynchronous();
		if (!Product || Product->DefaultContentID<=0 || Product->NumOfPortions!=Empty->NumOfPortions
			|| Product->BottleMaterialType!=Empty->BottleMaterialType || Contents.Contains(Product->DefaultContentID)) return false;
		Contents.Add(Product->DefaultContentID); Bottles.Add(Product);
	}
	const FShopBottleRow* BottleRow = Data->Bottles.Find(Empty->BottleMaterialType);
	if (!BottleRow || !BottleRow->Shell.LoadSynchronous() || !Data->EmptyLiquidMesh.LoadSynchronous()) return false;
	TArray<int32> CapPIDs;
	TMap<int32,FVector> PreviousCapCenters;
	TMap<int32,FTransform> PreviousBottleMounts;
	for (const auto& Pair : Manager->ProductDefinitions)
	{
		const auto& Def = Pair.Value;
		if (Def.bIsBottleCap && Def.StaticMesh) PreviousCapCenters.Add(Pair.Key,Def.StaticMesh->GetBoundingBox().GetCenter());
		if (Def.LiquidVisualPID) PreviousBottleMounts.Add(Pair.Key,Def.CapAttachmentTransform);
	}
	for (const auto& Entry : Data->Caps)
	{
		auto* Asset = Entry.Key.LoadSynchronous();
		const int32 PID = Asset ? Manager->RegisterProductFromDataAsset(Asset) : 0;
		if (!PID || !Asset->bIsBottleCap || !Entry.Value.Mesh.LoadSynchronous() || !FMath::IsFinite(Entry.Value.MassKg) || Entry.Value.MassKg < 0) return false;
		auto Def = *Manager->FindProductDefinition(PID);
		Def.StaticMesh = Entry.Value.Mesh.Get();
		Def.MaterialOverrides.Reset();
		Def.SkeletalMesh = nullptr;
		Def.GameplayMassKg = Entry.Value.MassKg;
		Def.SupportedBottleMaterials = Entry.Value.SupportedBottles;
		Def.CapMountOffset = Entry.Value.MountOffset;
		if (!Manager->RegisterProductDefinition(Def)) return false;
		if (BottleRow->CompatibleCaps.Contains(Entry.Key)) CapPIDs.Add(PID);
	}
	if (CapPIDs.IsEmpty()) return false;
	for (auto* Product:Bottles)
	{
		const auto Definition=FPropProductDefinition::FromDataAsset(Product,0,Product->DefaultContentID);
		FPropContentDefinition Content; Content.ContentID=Product->DefaultContentID; Content.DisplayName=Product->ItemDisplayName;
		Content.BasePortionPrice=Product->Price; Content.IdealServingTemperature=Product->ConsumableTemperature;
		Content.DensityGPerMl=FMath::IsFinite(Product->LiquidDensityGPerMl) ? FMath::Max(.001f,Product->LiquidDensityGPerMl) : 1.04f;
		Content.ExpiryProfile=Definition.ExpiryProfile; Content.TasteProfile=Definition.TasteProfile; Content.ExpiredTasteProfile=Definition.ExpiredTasteProfile;
		Content.StimulusProfile=Definition.StimulusProfile; Content.HazardType=EColaHazardType::FoamRocket;
		if (Product->LiquidMaterial) Content.LiquidMaterial=Product->LiquidMaterial;
		else if (Product->MaterialOverrides.Num()>1) Content.LiquidMaterial=Product->MaterialOverrides[1];
		if (const auto* Row = Data->Contents.Find(Content.ContentID))
		{
			Content.SupportedBottleMaterials = Row->SupportedBottles;
			for (const auto& Ref : Row->SupportedCaps)
			{
				const int32 PID = Manager->FindProductIDByDataAsset(Ref.LoadSynchronous());
				if (!PID) return false;
				Content.SupportedCapPIDs.Add(PID);
			}
			if (const auto* Visual = Row->VisualByBottle.Find(Empty->BottleMaterialType))
				if (!Visual->Material.IsNull()) Content.LiquidMaterial = Visual->Material.LoadSynchronous();
		}
		if (!Manager->RegisterContentDefinition(Content)) return false;
	}
	const int32 EmptyPID=Manager->RegisterProductFromDataAsset(Empty);
	if (EmptyPID<=0) return false;
	TMap<int32,int32> Mapping;
	for (auto* Product:Bottles)
	{
		const int32 PID=Manager->RegisterProductFromDataAsset(Product); if (PID<=0) return false;
		Mapping.Add(Product->DefaultContentID,PID);
	}
	TArray<int32> PIDs; Mapping.GenerateValueArray(PIDs); PIDs.Add(EmptyPID);
	// Legacy products keep their PID, price and content identity. Reuse the same
	// physical assembly recipe rather than keeping a second bottle renderer.
	for (const auto& Entry:Manager->ProductDefinitions)
	{
		const auto& Def=Entry.Value;
		if (Def.StaticMesh==BottleRow->Shell.Get() && Def.BottleMaterialType==EBottleMaterialType::Plastic
			&& Def.ConsumableProfile.NumOfPortions>0 && !Def.bLiquidVisualOnly && !Def.bIsBottleCap)
			PIDs.AddUnique(Entry.Key);
	}
	PIDs.Sort();
	for (int32 PID:PIDs)
	{
		FPropProductDefinition Product=*Manager->FindProductDefinition(PID);
		const bool bPrototypeProduct=PID==EmptyPID || Mapping.FindKey(PID)!=nullptr;
		Product.EmptyBottleProductID=EmptyPID;
		const int32 OriginalContentID=Product.DefaultUniqueData.EventData.ContentID;
		Product.FilledProductIDByContentID=Mapping;
		if (bPrototypeProduct) Product.SupportedContentIDs=Contents.Array();
		else
		{
			// An empty legacy whitelist means unrestricted compatible contents/caps.
			// Do not narrow it merely to install the shared visual recipe.
			for (const auto& Entry:Manager->ProductDefinitions)
				if (Entry.Value.BottleMaterialType==EBottleMaterialType::Plastic && Entry.Value.StaticMesh==BottleRow->Shell.Get()
					&& Entry.Value.DefaultUniqueData.EventData.ContentID>0)
					Product.FilledProductIDByContentID.Add(Entry.Value.DefaultUniqueData.EventData.ContentID,Entry.Key);
		}
		if (OriginalContentID>0 && !Contents.Contains(OriginalContentID))
		{
			Product.FilledProductIDByContentID.Add(OriginalContentID,PID);
			Product.SupportedContentIDs.AddUnique(OriginalContentID);
		}
		Product.StaticMesh = BottleRow->Shell.Get();
		Product.SkeletalMesh = BottleRow->ActiveShell.LoadSynchronous();
		Product.MaterialOverrides.Reset();
		// The shell owns its authored material. The separate liquid mesh owns its shader.
		Product.CapAttachmentTransform = BottleRow->CapMount;
		Product.LiquidAttachment = BottleRow->LiquidMount;
		if (bPrototypeProduct || !Product.SupportedCapPIDs.IsEmpty())
			for (int32 CapPID:CapPIDs) Product.SupportedCapPIDs.AddUnique(CapPID);
		const int32 ContentID = PID == EmptyPID ? 0 : Product.DefaultUniqueData.EventData.ContentID;
		UStaticMesh* Mesh = Data->EmptyLiquidMesh.Get();
		if (const auto* Row = Data->Contents.Find(ContentID))
		{
			const auto* Visual = Row->VisualByBottle.Find(Empty->BottleMaterialType);
			if (Visual) Mesh = Visual->Mesh.LoadSynchronous();
		}
		if (!Mesh) return false;
		FPropProductDefinition Liquid;
		Liquid.ProductID = Product.LiquidVisualPID ? Product.LiquidVisualPID : Manager->AllocateProductID();
		Liquid.StaticMeshID = Liquid.ProductID;
		Liquid.bLiquidVisualOnly = true;
		Liquid.StaticMesh = Mesh;
		Liquid.SkeletalMesh = Data->DefaultActiveLiquidMesh.LoadSynchronous();
		if (const auto* Row = Data->Contents.Find(ContentID ? ContentID : Bottles[0]->DefaultContentID))
			if (const auto* Visual = Row->VisualByBottle.Find(Empty->BottleMaterialType))
				if (!Visual->ActiveMesh.IsNull()) Liquid.SkeletalMesh = Visual->ActiveMesh.LoadSynchronous();
		Liquid.DisplayName = Product.DisplayName;
		Liquid.ConsumableProfile.NumOfPortions = 0;
		Liquid.RuntimeDefaults.bIsGrabbable = false;
		Liquid.InteractionProfile.bCanGrab = false;
		const auto* Content = Manager->FindContentDefinition(ContentID ? ContentID : Bottles[0]->DefaultContentID);
		UMaterialInterface* Material=Product.SourceDataAsset && Product.SourceDataAsset->LiquidMaterial ? Product.SourceDataAsset->LiquidMaterial.Get() : (Content ? Content->LiquidMaterial.Get() : nullptr);
		if (!Material)
			if (const auto* Row=Data->Contents.Find(Bottles[0]->DefaultContentID))
				if (const auto* Visual=Row->VisualByBottle.Find(EBottleMaterialType::Plastic)) Material=Visual->Material.LoadSynchronous();
		if (Material) Liquid.MaterialOverrides.Add(Material);
		if (!Manager->RegisterProductDefinition(Liquid)) return false;
		Product.LiquidVisualPID = Liquid.ProductID;
		if (!Manager->RegisterProductDefinition(Product)) return false;
	}
	// Rebase saved caps from their previous authored geometry and mount. Preserve world visuals and twist.
	for (const auto& Pair : Manager->ISIDData)
	{
		const int32 ID = Pair.Key;
		const auto* Def = Manager->FindProductDefinition(Pair.Value.ProductID);
		const auto* OldCenter = PreviousCapCenters.Find(Pair.Value.ProductID);
		if (!Def || !Def->bIsBottleCap || !Def->StaticMesh || !OldCenter) continue;
		if (Pair.Value.Attachment.OwnerISID)
		{
			const auto* Parent = Manager->FindISIDData(Pair.Value.Attachment.OwnerISID);
			const auto* ParentDef = Parent ? Manager->FindProductDefinition(Parent->ProductID) : nullptr;
			const auto* OldMount = Parent ? PreviousBottleMounts.Find(Parent->ProductID) : nullptr;
			if (ParentDef && OldMount && !OldMount->Equals(ParentDef->CapAttachmentTransform))
			{
				auto Edge = Pair.Value.Attachment;
				Edge.RelativeTransform = Edge.RelativeTransform.GetRelativeTransform(*OldMount) * ParentDef->CapAttachmentTransform;
				Manager->SetAssemblyEdge(ID,Edge);
				Manager->RefreshAssembly(Edge.OwnerISID);
				Manager->MarkInstanceDirty(ID,EPropDirtyFlags::Transform);
			}
		}
		else if (!OldCenter->Equals(Def->StaticMesh->GetBoundingBox().GetCenter()))
		{
			auto Pose = Manager->GetTransform(ID);
			Pose.AddToTranslation(Pose.TransformVector(*OldCenter-Def->StaticMesh->GetBoundingBox().GetCenter()));
			Manager->InitializeInstanceTransform(ID,Pose);
		}
	}
	RuntimeCatalog=Catalog.Get();
	bCatalogInitialized=true;
	TArray<int32> ExistingBottles;
	for (const auto& Pair : Manager->ISIDData) if (PIDs.Contains(Pair.Value.ProductID)) ExistingBottles.Add(Pair.Key);
	for (int32 ID : ExistingBottles) if (!Manager->EnsureLiquidChild(ID)) { bCatalogInitialized=false; return false; }
	return true;
}

bool UShopProcurementComponent::EnsureBottleAssemblyConfigured(int32 ProductID)
{
	auto* Manager=Cast<APropManager>(GetOwner());
	const auto* Def=Manager ? Manager->FindProductDefinition(ProductID) : nullptr;
	if (!Def || !Manager->HasAuthority() || GetNetMode()==NM_Client || Def->bIsBottleCap || Def->bLiquidVisualOnly
		|| Def->ConsumableProfile.NumOfPortions<=0 || Def->BottleMaterialType!=EBottleMaterialType::Plastic) return true;
	if(Def->CatalogPID && Def->Category==EPropProductCategory::Bottle)return true;
 const auto* Data=Catalog.LoadSynchronous();
	if(Manager->ProductCatalog.Get() && Data)
	{
		const auto* LegacyRow=Data->Bottles.Find(EBottleMaterialType::Plastic);
		if(!LegacyRow || Def->StaticMesh!=LegacyRow->Shell.LoadSynchronous())return true;
		if(Def->Category==EPropProductCategory::Bottle)return true;
		// Older maps can contain bottle presets with their own saved capacity and
		// world-local content IDs. Adapt their existing recipe without renumbering.
		if(const auto* Visual=Manager->FindProductDefinition(Def->LiquidVisualPID);Visual && Visual->bLiquidVisualOnly)return true;
		auto Bottle=*Def;FPropProductDefinition Liquid;
		Liquid.ProductID=Manager->AllocateProductID();Liquid.StaticMeshID=Liquid.ProductID;
		Liquid.StaticMesh=Data->EmptyLiquidMesh.LoadSynchronous();Liquid.SkeletalMesh=Data->DefaultActiveLiquidMesh.LoadSynchronous();
		Liquid.bLiquidVisualOnly=true;Liquid.ConsumableProfile.NumOfPortions=0;Liquid.InteractionProfile.bCanGrab=false;Liquid.RuntimeDefaults.bIsGrabbable=false;
		if(Bottle.SourceDataAsset && Bottle.SourceDataAsset->LiquidMaterial)Liquid.MaterialOverrides.Add(Bottle.SourceDataAsset->LiquidMaterial);
		else if(const auto* Content=Manager->FindContentDefinition(Bottle.DefaultUniqueData.EventData.ContentID))if(Content->LiquidMaterial)Liquid.MaterialOverrides.Add(Content->LiquidMaterial);
		if(!Liquid.StaticMesh || !Manager->RegisterProductDefinition(Liquid))return false;
		Bottle.LiquidVisualPID=Liquid.ProductID;Bottle.LiquidAttachment=LegacyRow->LiquidMount;Bottle.CapAttachmentTransform=LegacyRow->CapMount;
		return Manager->RegisterProductDefinition(Bottle);
	}
	// Custom supply catalogs may intentionally omit the Normal Cola recipe set.
	// Their props must not depend on initializing this prototype-only catalog.
	if (!Data || Data->FinishedBottleProducts.Num()!=5) return true;
	const auto* Row=Data ? Data->Bottles.Find(EBottleMaterialType::Plastic) : nullptr;
	if (!Row || Def->StaticMesh!=Row->Shell.LoadSynchronous()) return true;
	const auto* Liquid=Manager->FindProductDefinition(Def->LiquidVisualPID);
	if (Liquid && Liquid->bLiquidVisualOnly && Liquid->StaticMesh==Data->EmptyLiquidMesh.LoadSynchronous()
		&& Liquid->SkeletalMesh==Data->DefaultActiveLiquidMesh.LoadSynchronous()) return true;
	bCatalogInitialized=false;
	if (!InitializeCatalog()) return false;
	Def=Manager->FindProductDefinition(ProductID);
	return Def && Def->LiquidVisualPID>0;
}

bool UShopProcurementComponent::RefreshLoadedCatalog()
{
	const auto* Data = Catalog.LoadSynchronous();
	auto* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Data) return true;
	const int32 EmptyID=Manager->FindProductIDByDataAsset(Data->EmptyBottleProduct.LoadSynchronous());
	const auto* Empty=Manager->FindProductDefinition(EmptyID);
	if (!Empty) return true;

	bCatalogInitialized = false;
	return InitializeCatalog();
}

bool UShopProcurementComponent::IsNormalContent(int32 ID) const
{
	if (ID<=0) return false;
	const auto* Data=Catalog.Get(); if (!Data) return false;
	for (const auto& Ref:Data->FinishedBottleProducts) if (const auto* Product=Ref.Get()) if (Product->DefaultContentID==ID) return true;
	return false;
}

bool UShopProcurementComponent::IsCylinderProduct(int32 PID) const
{
	const auto* Manager=Cast<APropManager>(GetOwner()); const auto* Data=Catalog.Get();
	const auto* Product=Manager ? Manager->FindProductDefinition(PID) : nullptr;
	if (!Data || !Product || !Product->SourceDataAsset) return false;
	for (const auto& Offer:Data->Offers)
		if (Offer.Kind==EShopSupplyKind::ColaCylinder && Offer.Product.Get()==Product->SourceDataAsset.Get()) return true;
	return false;
}

bool UShopProcurementComponent::IsTemporarySealProduct(int32 PID) const
{
	const auto* Manager=Cast<APropManager>(GetOwner()); const auto* Data=Catalog.Get();
	const auto* Product=Manager ? Manager->FindProductDefinition(PID) : nullptr;
	if (!Data || !Product || !Product->SourceDataAsset) return false;
	for (const auto& Offer:Data->Offers)
		if (Offer.Kind==EShopSupplyKind::TemporarySeal && Offer.Product.Get()==Product->SourceDataAsset.Get()) return true;
	return false;
}

bool UShopProcurementComponent::Quote(FName OfferID, int32 Quantity, int64& TotalCents) const
{
	TotalCents = 0;
	if (Quantity < 1 || Quantity > FMath::Clamp(MaxQuantityPerPurchase,1,64)) return false;
	const auto* Data = Catalog.LoadSynchronous();
	const auto* Offer = Data ? Data->FindOffer(OfferID) : nullptr;
	if (!Offer || Offer->Product.IsNull() || Offer->UnitCostCents <= 0 || Offer->UnitCostCents > MAX_int64 / Quantity) return false;
	TotalCents = Offer->UnitCostCents * Quantity;
	return true;
}

EShopPurchaseResult UShopProcurementComponent::Purchase(FGuid ID, FName OfferID, int32 Quantity, const FTransform& Delivery, TArray<int32>& Delivered)
{
	Delivered.Reset();
	auto* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Manager->HasAuthority() || Manager->GetNetMode() == NM_Client) return EShopPurchaseResult::NotAuthority;
	if (!ID.IsValid() || Delivery.ContainsNaN() || !Delivery.IsValid() || !Manager->SharedWallet) return EShopPurchaseResult::DeliveryFailed;
	if (Quantity < 1 || Quantity > FMath::Clamp(MaxQuantityPerPurchase,1,64)) return EShopPurchaseResult::InvalidQuantity;
	int64 Total = 0;
	if (!Quote(OfferID, Quantity, Total)) return EShopPurchaseResult::InvalidOffer;
	const FName Reason(*FString::Printf(TEXT("Supply:%s:%d"), *OfferID.ToString(), Quantity));
	FWalletReceipt Previous;
	if (Manager->SharedWallet->FindReceipt(ID, Previous))
		return Previous.Reason == Reason && Previous.DeltaCents < 0 ? EShopPurchaseResult::AlreadyPurchased : EShopPurchaseResult::OperationConflict;
	const FShopSupplyOffer Offer = *Catalog.LoadSynchronous()->FindOffer(OfferID);
	if (!Catalog.Get()->FinishedBottleProducts.IsEmpty() && !InitializeCatalog()) return EShopPurchaseResult::InvalidOffer;
	UInteractablePropDataAsset* Product = Offer.Product.LoadSynchronous();
	if (!Product || !Product->StaticMesh || !FMath::IsFinite(Product->NormalCapacity)) return EShopPurchaseResult::InvalidOffer;
	const auto Result = Manager->SharedWallet->CommitWithResources(ID, -Total, Reason, [&]()
	{
		const int32 PID = Manager->RegisterProductFromDataAsset(Product);
		const auto* DeliveryDef = Manager->FindProductDefinition(PID);
		if (!DeliveryDef || !DeliveryDef->StaticMesh) return false;
		UStaticMesh* DeliveryMesh = DeliveryDef->StaticMesh;
		FVector CellSize=ShopInteractionGeometry::MeshGridSpacing(DeliveryMesh,Delivery.GetScale3D());
		TArray<FTransform> Placements;
		if (bUseDeliveryArea)
		{
			// One shared footprint keeps mixed orders separated, including after save/load.
			for (const auto& Entry:Catalog.Get()->Offers) if (const auto* Other=Entry.Product.LoadSynchronous())
				CellSize=CellSize.ComponentMax(ShopInteractionGeometry::MeshGridSpacing(Other->StaticMesh,Delivery.GetScale3D()));
			TArray<FBox> Occupied;
			for (const auto& Pair:Manager->ISIDData)
				if (const auto* Def=Manager->FindProductDefinition(Pair.Value.ProductID)) if (Def->StaticMesh)
					Occupied.Add(Def->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Pair.Key)));
			const int32 Columns=FMath::Clamp(DeliveryColumns,1,8), Rows=FMath::Clamp(DeliveryRows,1,16);
			const int32 Capacity=FMath::Min(64,Columns*Rows);
			for (int32 Slot=0;Slot<Capacity && Placements.Num()<Quantity;++Slot)
			{
				auto Transform=ShopInteractionGeometry::GridItemTransform(Delivery,CellSize,Slot,Capacity,FVector::OneVector,Columns,Rows);
				if (Offer.Kind==EShopSupplyKind::ManualFillingMachine || Offer.Kind==EShopSupplyKind::Cap) Transform.AddToTranslation(-Transform.TransformVector(FVector(0,0,DeliveryMesh->GetBoundingBox().Min.Z)));
				const FBox Bounds=DeliveryMesh->GetBoundingBox().TransformBy(Transform);
				const FBox Interior=Bounds.ExpandBy(-.5f);
				if (Occupied.ContainsByPredicate([&](const auto& Other){return Interior.Intersect(Other);})) continue;
				const FVector Extent=Interior.GetExtent().ComponentMax(FVector(.1));
				if (GetWorld()->OverlapBlockingTestByChannel(Interior.GetCenter(),FQuat::Identity,ECC_PhysicsBody,
					FCollisionShape::MakeBox(Extent),FCollisionQueryParams(SCENE_QUERY_STAT(SupplyDeliverySpace),false))) continue;
				Placements.Add(Transform); Occupied.Add(Bounds);
			}
			if (Placements.Num()!=Quantity) return false;
		}
		for (int32 Index=0; Index<Quantity; ++Index)
		{
			FTransform Transform=bUseDeliveryArea ? Placements[Index] : ShopInteractionGeometry::GridItemTransform(Delivery,CellSize,Index,Quantity);
			if (!bUseDeliveryArea && (Offer.Kind==EShopSupplyKind::ManualFillingMachine || Offer.Kind==EShopSupplyKind::Cap)) Transform.AddToTranslation(-Transform.TransformVector(FVector(0,0,DeliveryMesh->GetBoundingBox().Min.Z)));
			const int32 ISID = Manager->GenerateProductInstance(PID,Transform);
			if (ISID <= 0)
			{
				for (int32 Created : Delivered) Manager->ReleaseInstanceToEmpty(Created);
				Delivered.Reset();
				return false;
			}
			Delivered.Add(ISID);
			AActivePropActor* Actor=nullptr;
			if (!Manager->PromoteInstanceToASM(ISID,EPropActivationReason::Drop,true,Actor) || !Actor)
			{
				for (int32 Created:Delivered) Manager->ReleaseInstanceToEmpty(Created);
				Delivered.Reset(); return false;
			}
		}
		return true;
	});
	switch (Result)
	{
	case EWalletOperationResult::Applied: return EShopPurchaseResult::Success;
	case EWalletOperationResult::AlreadyApplied: return EShopPurchaseResult::AlreadyPurchased;
	case EWalletOperationResult::InsufficientFunds: return EShopPurchaseResult::InsufficientFunds;
	case EWalletOperationResult::OperationConflict: return EShopPurchaseResult::OperationConflict;
	default: return EShopPurchaseResult::DeliveryFailed;
	}
}
