#include "Props/InteractablePropManager.h"
#include "Props/PropProductCatalog.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"

int32 APropManager::FindCatalogAssetPID(const UObject* Asset) const
{
 const auto* C=ProductCatalog.Get();if(!C || !Asset)return 0;
 if(const auto* Preset=Cast<UInteractablePropDataAsset>(Asset))if(!Preset->bIsBottleCap && Preset->BottleProduct.IsSet())
  if(const auto* Bottle=C->Find(Preset->BottleProduct.PID);Bottle && Bottle->Category==EPropProductCategory::Bottle)return C->ResolveCanonicalPID(Bottle->PID);
 if(const auto* E=C->FindByAsset(Asset))return E->PID;
 if(const auto* P=C->LegacyAssets.Find(TSoftObjectPtr<UPrimaryDataAsset>(FSoftObjectPath(Asset))))return P->PID;
 return 0;
}
int32 APropManager::CanonicalProductPID(int32 PID) const
{
 if(const auto* D=FindProductDefinition(PID))if(D->CatalogPID)return D->CatalogPID;
 if(const auto* C=ProductCatalog.Get())if(const int32 Canonical=C->ResolveCanonicalPID(PID))return Canonical;
 return PID;
}
const FPropBottleContentVisual* APropManager::FindBottleContentVisual(int32 ContentPID,int32 BottlePID) const
{
 const auto* C=FindContentDefinition(CanonicalProductPID(ContentPID));if(!C)return nullptr;
 const int32 B=CanonicalProductPID(BottlePID);
 return C->BottleVisuals.FindByPredicate([&](const auto& V){return V.Bottle.PID==B;});
}
FPropProductDefinition APropManager::ResolveVisualDefinition(int32 PID,int32 BottlePID) const
{
 auto D=ProductDefinitions.FindRef(PID);
 if(const auto* V=FindBottleContentVisual(PID,BottlePID))
 {D.StaticMesh=V->Mesh;D.SkeletalMesh=V->ActiveMesh;D.MaterialOverrides=V->Materials;}
 return D;
}
bool APropManager::InitializeProductCatalog()
{
 if(!HasAuthority() || GetNetMode()==NM_Client)return false;
 if(bRegisteringCatalog)return true;
 auto* C=ProductCatalog.LoadSynchronous();if(!C)return false;
 const auto Errors=C->ValidateCatalog();
 if(!Errors.IsEmpty()){for(const auto& E:Errors)UE_LOG(LogTemp,Error,TEXT("Product catalog: %s"),*E);return false;}
 TGuardValue<bool> Guard(bRegisteringCatalog,true);
 // Preflight all identity collisions before any definition is changed.
 for(const auto& E:C->Products)if(!E.CanonicalPID)
  if(const auto* Old=FindProductDefinition(E.PID))
   if(Old->CatalogPID!=E.PID && Old->SourceDataAsset!=E.Definition.Get())return false;
 for(const auto& E:C->Products)
 {
  if(E.CanonicalPID)continue;
  auto* Asset=E.Definition.LoadSynchronous();FPropProductDefinition D;
  if(auto* B=Cast<UBottleProductDataAsset>(Asset)){D=FPropProductDefinition::FromDataAsset(B,E.PID,E.PID);D.InteractionProfile.bCanConsume=true;D.ConsumableProfile.ConsumableType=EConsumablePropType::Drink;}
  else if(auto* Content=Cast<UPropContentDataAsset>(Asset))
  {
   auto Definition=Content->Properties;Definition.ContentID=E.PID;Definition.DisplayName=E.Name;
   Definition.bUseBottlePIDCompatibility=true;Definition.BottleVisuals=Content->Bottles;Definition.SupportedBottlePIDs.Reset();
   for(const auto& V:Content->Bottles)Definition.SupportedBottlePIDs.AddUnique(V.Bottle.PID);
   if(!RegisterContentDefinition(Definition))return false;
   const auto& V=Content->Bottles[0];D.StaticMesh=V.Mesh;D.SkeletalMesh=V.ActiveMesh;D.MaterialOverrides=V.Materials;
   D.bLiquidVisualOnly=true;D.InteractionProfile.bCanGrab=false;D.RuntimeDefaults.bIsGrabbable=false;
   D.ConsumableProfile.NumOfPortions=0;
  }
  else if(auto* Cap=Cast<UPropCapDataAsset>(Asset))
  {
   D.StaticMesh=Cap->Mesh;D.MaterialOverrides=Cap->Materials;D.CustomActiveActorClass=Cap->ActiveActorClass;
   D.GameplayMassKg=Cap->MassKg;D.PhysicsProfile=Cap->Physics;D.bIsBottleCap=true;
   D.DefaultCapType=Cap->Type;D.CapMountOffset=Cap->MountOffset;D.CapReleasePressure=Cap->ReleasePressure;
   D.SafeOpenPressureThreshold=Cap->SafeOpenPressure;D.TorqueSweetSpotMin=Cap->TorqueMinimum;D.TorqueSweetSpotMax=Cap->TorqueMaximum;
   D.bUseBottlePIDCompatibility=true;for(const auto& Supported:Cap->SupportedBottles)D.SupportedBottlePIDs.AddUnique(Supported.PID);
   D.bEnableInstanceUniqueData=true;D.ConsumableProfile.NumOfPortions=0;
  }
  else return false;
  D.ProductID=E.PID;D.StaticMeshID=E.PID;D.CatalogPID=E.PID;D.Category=E.Category;D.DisplayName=E.Name;
  if(!RegisterProductDefinition(D))return false;
 }
 // Saved maps used world-local PIDs. Resolve those through their authoring asset
 // identity after the canonical definitions exist; never renumber saved records.
 TArray<FPropProductDefinition> Legacy;
 for(const auto& Pair:ProductDefinitions)
  if(Pair.Value.SourceDataAsset && FindCatalogAssetPID(Pair.Value.SourceDataAsset)!=Pair.Key && FindCatalogAssetPID(Pair.Value.SourceDataAsset)>0)Legacy.Add(Pair.Value);
 for(auto D:Legacy){D.CatalogPID=0;if(!RegisterProductDefinition(D))return false;}
 return true;
}

int32 APropManager::GenerateAssemblyContent(int32 ContentPID,int32 BottlePID,const FTransform& World)
{
 if(!HasAuthority() || !World.IsValid())return 0;
 auto* Comp=GetOrCreateIISMComponent(ContentPID,BottlePID);const auto* Def=FindProductDefinition(ContentPID);
 if(!Comp || !Def || !Def->bLiquidVisualOnly)return 0;
 const int32 ID=AllocateISID();
 const int32 Slot=Comp->ReuseOrAddInstanceWithISID(ID,World,Def->RuntimeDefaults,INDEX_NONE,true);
 if(Slot==INDEX_NONE)return 0;
 if(!InitializeGeneratedInstance(ID,ContentPID,Comp->IISMComponentID,Slot,World))
 {Comp->ReleaseInstanceByISIDToEmpty(ID);return 0;}
 FindISIDData(ID)->VisualBottlePID=BottlePID;return ID;
}
bool APropManager::EnsureAssemblyVisualBatch(int32 ID,int32 BottlePID)
{
 auto* R=FindISIDData(ID);auto* Old=R ? GetComponentFromISID(ID) : nullptr;
 if(!R || !Old)return false;
 if(R->VisualBottlePID==BottlePID && Old->VisualBottlePID==BottlePID)return true;
 auto* Comp=GetOrCreateIISMComponent(R->ProductID,BottlePID);if(!Comp)return false;
 const int32 Slot=Comp->ReuseOrAddInstanceWithISID(ID,R->ActiveTransform,R->PackedRuntimeData,INDEX_NONE,true);
 if(Slot==INDEX_NONE)return false;
 Old->ReleaseInstanceByISIDToEmpty(ID);
 R->VisualBottlePID=BottlePID;R->IISMComponentID=Comp->IISMComponentID;R->ISMI=Slot;
 if(HasAuthority()){IncrementInstanceVersion(ID);MarkInstanceDirty(ID,EPropDirtyFlags::Representation|EPropDirtyFlags::Transform);}
 return true;
}
