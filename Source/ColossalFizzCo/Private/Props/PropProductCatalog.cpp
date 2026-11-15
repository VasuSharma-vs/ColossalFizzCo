#include "Props/PropProductCatalog.h"
#include "Engine/StaticMesh.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UBottleProductDataAsset::UBottleProductDataAsset()
{DefaultContentID=0;DefaultContentLevel=0;NumOfPortions=4;bEnableInstanceUniqueData=true;}
UPropContentDataAsset::UPropContentDataAsset()
{Properties.ContentID=0;}
void UBottleProductDataAsset::ImportLegacyBottle(const UInteractablePropDataAsset* Source)
{
 if(!Source)return;
 for(TFieldIterator<FProperty> P(UInteractablePropDataAsset::StaticClass(),EFieldIteratorFlags::ExcludeSuper);P;++P)
  P->CopyCompleteValue_InContainer(this,Source);
 DefaultContentLevel=0;DefaultContentID=0;
}
void UPropContentDataAsset::ImportLegacyContent(const UInteractablePropDataAsset* Source)
{
 if(!Source)return;const auto Legacy=FPropProductDefinition::FromDataAsset(Source,0);
 Properties.ContentID=Source->DefaultContentID;Properties.DisplayName=Source->ItemDisplayName;
 Properties.LiquidMaterial=Source->LiquidMaterial;Properties.DensityGPerMl=Source->LiquidDensityGPerMl;
 Properties.BasePortionPrice=Source->Price;Properties.IdealServingTemperature=Source->ConsumableTemperature;
 Properties.StimulusProfile=Legacy.StimulusProfile;Properties.TasteProfile=Legacy.TasteProfile;
 Properties.ExpiredTasteProfile=Legacy.ExpiredTasteProfile;Properties.ExpiryProfile=Legacy.ExpiryProfile;
}
const FPropCatalogEntry* UPropProductCatalog::Find(int32 PID) const
{return Products.FindByPredicate([&](const auto& P){return P.PID==PID;});}
const FPropCatalogEntry* UPropProductCatalog::FindByAsset(const UObject* Asset) const
{
 if(!Asset)return nullptr;const FSoftObjectPath Path(Asset);
 return Products.FindByPredicate([&](const auto& P){return P.Definition.ToSoftObjectPath()==Path && !P.CanonicalPID;});
}
int32 UPropProductCatalog::ResolveCanonicalPID(int32 PID) const
{
 TSet<int32> Seen;
 for(int32 Depth=0;Depth<64;++Depth)
 {const auto* P=Find(PID);if(!P || Seen.Contains(PID))return 0;if(!P->CanonicalPID)return PID;Seen.Add(PID);PID=P->CanonicalPID;}
 return 0;
}
FText UPropProductCatalog::GetProductName(FPropProductReference Product) const
{const auto* P=Find(Product.PID);return P ? P->Name : FText::FromString(Product.PID>0 ? FString::Printf(TEXT("Missing product (%d)"),Product.PID) : TEXT("None"));}
void UPropProductCatalog::AssignMissingPIDs()
{
 for(const auto& P:Products)if(P.PID>0){IssuedPIDs.Add(P.PID);NextPID=FMath::Max(NextPID,int64(P.PID)+1);}
 for(auto& P:Products)
 {
  if(!P.Identity.IsValid())P.Identity=FGuid::NewGuid();
  if(P.PID>0)continue;
  while(NextPID>0 && NextPID<=MAX_int32 && IssuedPIDs.Contains(int32(NextPID)))++NextPID;
  if(NextPID<=0 || NextPID>MAX_int32)continue;
  P.PID=int32(NextPID++);IssuedPIDs.Add(P.PID);
 }
}
void UPropProductCatalog::ReserveLegacyPIDs(const TArray<int32>& PIDs)
{for(int32 PID:PIDs)if(PID>0){IssuedPIDs.Add(PID);NextPID=FMath::Max(NextPID,int64(PID)+1);}}
int32 UPropProductCatalog::RegisterIdentity(UPrimaryDataAsset* Asset,EPropProductCategory Category,const FText& Name,int32 ExistingPID,int32 Canonical)
{
 if(ExistingPID>0)
 {
  if(const auto* Existing=Find(ExistingPID))
   return Existing->Category==Category && Existing->Definition.ToSoftObjectPath()==FSoftObjectPath(Asset) && Existing->CanonicalPID==Canonical ? ExistingPID : 0;
 }
 else if(const auto* Existing=FindByAsset(Asset))return Existing->PID;
 auto& P=Products.AddDefaulted_GetRef();P.PID=ExistingPID;P.Name=Name;P.Category=Category;P.Definition=Asset;P.CanonicalPID=Canonical;
 AssignMissingPIDs();return P.PID;
}
void UPropProductCatalog::RegisterDatabaseProducts()
{
 auto Add=[&](UPrimaryDataAsset* Asset,EPropProductCategory Type)
 {if(Asset && !FindByAsset(Asset))RegisterIdentity(Asset,Type,FText::FromString(Asset->GetName()));};
 if(Bottles)for(const auto& Ref:Bottles->Products)Add(Ref.LoadSynchronous(),EPropProductCategory::Bottle);
 if(Contents)for(const auto& Ref:Contents->Products)Add(Ref.LoadSynchronous(),EPropProductCategory::Content);
 if(Caps)for(const auto& Ref:Caps->Products)Add(Ref.LoadSynchronous(),EPropProductCategory::Cap);
 AssignMissingPIDs();MarkPackageDirty();
}
TArray<FString> UPropProductCatalog::ValidateCatalog() const
{
 TArray<FString> Errors;TSet<int32> IDs;TSet<FGuid> Keys;
 for(const auto& P:Products)
 {
  auto Error=[&](const FString& S){Errors.Add(FString::Printf(TEXT("PID %d (%s): %s"),P.PID,*P.Name.ToString(),*S));};
  if(P.PID<=0 || IDs.Contains(P.PID))Error(TEXT("Missing or duplicate PID"));IDs.Add(P.PID);
  if(!P.Identity.IsValid() || Keys.Contains(P.Identity))Error(TEXT("Missing or duplicate catalog identity"));Keys.Add(P.Identity);
  if(P.Name.IsEmpty())Error(TEXT("Product name is empty"));
  if(P.CanonicalPID)
  {const auto* Target=Find(ResolveCanonicalPID(P.PID));if(!Target || Target->Category!=P.Category)Error(TEXT("Invalid or circular compatibility alias"));continue;}
  auto* Asset=P.Definition.LoadSynchronous();
  if(!Asset){Error(TEXT("Missing product definition"));continue;}
  if(P.Category==EPropProductCategory::Bottle && !Cast<UBottleProductDataAsset>(Asset))Error(TEXT("Bottle entry requires a bottle definition"));
  if(P.Category==EPropProductCategory::Content && !Cast<UPropContentDataAsset>(Asset))Error(TEXT("Content entry requires a content definition"));
  if(P.Category==EPropProductCategory::Cap && !Cast<UPropCapDataAsset>(Asset))Error(TEXT("Cap entry requires a cap definition"));
  auto CheckBottle=[&](int32 ID){const auto* Bottle=Find(ID);if(!Bottle || Bottle->Category!=EPropProductCategory::Bottle)Error(FString::Printf(TEXT("Unsupported/missing bottle reference %d"),ID));};
  if(auto* Bottle=Cast<UBottleProductDataAsset>(Asset))if(Bottle->NumOfPortions<=0 || !Bottle->StaticMesh)Error(TEXT("Bottle needs a mesh and positive maximum portions"));
  if(auto* Content=Cast<UPropContentDataAsset>(Asset))
  {
   TSet<int32> Variants;
   for(const auto& V:Content->Bottles){CheckBottle(V.Bottle.PID);if(Variants.Contains(V.Bottle.PID))Error(TEXT("Duplicate bottle visual"));Variants.Add(V.Bottle.PID);if(!V.Mesh)Error(TEXT("Supported bottle is missing its liquid mesh"));}
   if(Content->Bottles.IsEmpty())Error(TEXT("Content has no supported bottles"));
  }
  if(auto* Cap=Cast<UPropCapDataAsset>(Asset))
  {if(!Cap->Mesh || Cap->SupportedBottles.IsEmpty())Error(TEXT("Cap needs a mesh and supported bottles"));for(const auto& B:Cap->SupportedBottles)CheckBottle(B.PID);}
 }
 return Errors;
}
#if WITH_EDITOR
void UPropProductCatalog::PostEditChangeProperty(FPropertyChangedEvent& Event)
{AssignMissingPIDs();Super::PostEditChangeProperty(Event);}
EDataValidationResult UPropProductCatalog::IsDataValid(FDataValidationContext& Context) const
{
 const auto Errors=ValidateCatalog();for(const auto& E:Errors)Context.AddError(FText::FromString(E));
 return Errors.IsEmpty() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
