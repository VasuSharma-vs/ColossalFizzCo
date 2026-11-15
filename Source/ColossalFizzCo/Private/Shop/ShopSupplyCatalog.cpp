#include "Shop/ShopSupplyCatalog.h"

#include "Props/InteractablePropDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"

UShopSupplyCatalog::UShopSupplyCatalog()
{
	const FString Dir = TEXT("/Game/Assest/meshs/PlasticBottle/");
	auto MeshPath = [&](const FString& Name) { return FSoftObjectPath(Dir + Name + TEXT(".") + Name); };
	FShopBottleRow Plastic;
	Plastic.Shell = TSoftObjectPtr<UStaticMesh>(MeshPath(TEXT("SM_PlasticBottle")));
	Plastic.ActiveShell = TSoftObjectPtr<USkeletalMesh>(MeshPath(TEXT("SKM_PlasticBottle")));
	DefaultActiveLiquidMesh = TSoftObjectPtr<USkeletalMesh>(MeshPath(TEXT("SKM_PlasticBottelInnerContent")));
	EmptyLiquidMesh = TSoftObjectPtr<UStaticMesh>(MeshPath(TEXT("SM_PlasticBottelInnerContent")));
	const TSoftObjectPtr<UInteractablePropDataAsset> NormalCap(FSoftObjectPath(TEXT("/Game/NormalColaPrototype/DA_NormalTwistCap.DA_NormalTwistCap")));
	Plastic.CompatibleCaps.Add(NormalCap);
	Bottles.Add(EBottleMaterialType::Plastic, Plastic);
	FShopCapRow Cap;
	Cap.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Assest/meshs/PlasticBottle/SM_NormalPlasticCap.SM_NormalPlasticCap")));
	Cap.SupportedBottles.Add(EBottleMaterialType::Plastic);
	Caps.Add(NormalCap, Cap);
	for (int32 ID = 901; ID <= 905; ++ID)
	{
		FShopContentRow Row;
		Row.SupportedBottles.Add(EBottleMaterialType::Plastic);
		auto& Visual = Row.VisualByBottle.Add(EBottleMaterialType::Plastic);
		Visual.Mesh = EmptyLiquidMesh;
		Visual.ActiveMesh = TSoftObjectPtr<USkeletalMesh>(MeshPath(TEXT("SKM_PlasticBottelInnerContent")));
		Contents.Add(ID, Row);
	}
}

const FShopSupplyOffer* UShopSupplyCatalog::FindOffer(FName OfferID) const
{
	if (OfferID.IsNone()) return nullptr;
	const FShopSupplyOffer* Found = nullptr;
	for (const auto& Offer : Offers)
	{
		if (Offer.OfferID != OfferID) continue;
		if (Found) return nullptr; // Ambiguous authored IDs cannot select a price/product safely.
		Found = &Offer;
	}
	return Found;
}

void UShopSupplyCatalog::PostLoad()
{
	Super::PostLoad();
	for (auto& Entry : Caps)
	{
		if (Entry.Value.Mesh.ToSoftObjectPath().GetAssetPathString() != TEXT("/Game/NormalColaPrototype/SM_NormalPlasticCap_Loose.SM_NormalPlasticCap_Loose")) continue;
		Entry.Value.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Assest/meshs/PlasticBottle/SM_NormalPlasticCap.SM_NormalPlasticCap")));
		if (auto* Plastic = Bottles.Find(EBottleMaterialType::Plastic)) Plastic->CapMount = FTransform::Identity;
	}
}
