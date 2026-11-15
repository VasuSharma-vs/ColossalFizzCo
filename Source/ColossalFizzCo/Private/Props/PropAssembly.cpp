#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/ColaPropActor.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/PropAssemblyMesh.h"
#include "Engine/StaticMesh.h"
#include "Shop/ShopProcurementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

namespace
{
 void DestroyAssemblyPiece(UStaticMeshComponent* Mesh)
 {
  if(!Mesh)return;
  TArray<USceneComponent*> Children;Mesh->GetChildrenComponents(true,Children);
  for(auto* Child:Children)if(auto* Effect=Cast<UFXSystemComponent>(Child))
  {Effect->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);Effect->DeactivateImmediate();Effect->ReleaseToPool();}
  Mesh->DestroyComponent();
 }
 void SpawnContentEffect(const FPropBottleContentVisual* Visual,UStaticMeshComponent* Mesh)
 {
  if(!Visual || !Visual->Effects || !Mesh || Mesh->GetNetMode()==NM_DedicatedServer)return;
  // Engine pools and pre-culling apply locally. Resting ISMs create no emitters.
  if(auto* Niagara=Cast<UNiagaraSystem>(Visual->Effects))
   UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara,Mesh,NAME_None,FVector::ZeroVector,FRotator::ZeroRotator,EAttachLocation::KeepRelativeOffset,false,true,ENCPoolMethod::ManualRelease,true);
  else if(auto* Cascade=Cast<UParticleSystem>(Visual->Effects))
   UGameplayStatics::SpawnEmitterAttached(Cascade,Mesh,NAME_None,FVector::ZeroVector,FRotator::ZeroRotator,FVector::OneVector,EAttachLocation::KeepRelativeOffset,false,EPSCPoolMethod::ManualRelease,true);
 }
}

int32 APropManager::ResolveRootISID(int32 ID) const
{
	// Each installed edge is validated at creation. Also bound traversal for malformed saves/wire data.
	for (int32 Depth = 0; Depth < 64; ++Depth)
	{
		const auto* Record = FindISIDData(ID);
		if (!Record) return 0;
		if (!Record->Attachment.OwnerISID) return ID;
		ID = Record->Attachment.OwnerISID;
	}
	return 0;
}

TArray<int32> APropManager::GetChildISIDs(int32 ID) const
{
	TArray<int32> Result;
	ChildrenByOwner.MultiFind(ID, Result);
	return Result;
}

UStaticMeshComponent* APropManager::GetAssemblyComponent(int32 ID) const
{
	const auto* Entry = AssemblyComponents.Find(ID);
	return Entry && IsValid(*Entry) ? Entry->Get() : nullptr;
}

void APropManager::SetAssemblyEdge(int32 Child, const FPropAttachment& Edge)
{
	auto* Record = FindISIDData(Child);
	if (!Record || !Edge.RelativeTransform.IsValid() || Edge.OwnerISID == Child) return;
	int32 Ancestor=Edge.OwnerISID;
	for(int32 Depth=0;Ancestor;++Depth)
	{
		if(Depth>=64 || Ancestor==Child)return;
		const auto* Parent=FindISIDData(Ancestor);if(!Parent)break;
		Ancestor=Parent->Attachment.OwnerISID;
	}
	if (Record->Attachment.OwnerISID != Edge.OwnerISID)
	{
		if(auto* Old=FindISIDData(Record->Attachment.OwnerISID))Old->ChildISIDs.Remove(Child);
        ChildrenByOwner.RemoveSingle(Record->Attachment.OwnerISID, Child);
		if (Edge.OwnerISID) ChildrenByOwner.AddUnique(Edge.OwnerISID, Child);
		if (auto* Mesh = GetAssemblyComponent(Child)) DestroyAssemblyPiece(Mesh);
		AssemblyComponents.Remove(Child);
	}
	// Explicit authoritative edge edits must reach an already-created component
	// too. Otherwise saving the component's old pose can undo a repaired edge.
	if (Record->Attachment.OwnerISID==Edge.OwnerISID && !Record->Attachment.RelativeTransform.Equals(Edge.RelativeTransform))
		if (auto* Mesh=GetAssemblyComponent(Child)) Mesh->SetRelativeTransform(Edge.RelativeTransform);
	Record->Attachment = Edge;
 if(auto* Parent=FindISIDData(Edge.OwnerISID))Parent->ChildISIDs.AddUnique(Child);
 Record->ChildISIDs.Reset();ChildrenByOwner.MultiFind(Child,Record->ChildISIDs);
}

void APropManager::ForgetAssembly(int32 ID)
{
	const auto* Child = FindISIDData(ID);
	const int32 ParentID = Child ? Child->Attachment.OwnerISID : 0;
	SetAssemblyEdge(ID, FPropAttachment());
	ChildrenByOwner.Remove(ID);
	if (HasAuthority() && ParentID)
	{
		const auto* Data = FindUniqueData(ParentID);
		if (Data && Data->EventData.AttachedCapISID == ID)
		{
			FPropEventSyncDelta Update;
			Update.ExtendedDirtyMask = EPropExtendedEventMask::AttachedCap;
			Update.SetFlag(EPropEventDirtyMask::CapSealed);
			Update.SetFlag(EPropEventDirtyMask::CapType);
			SubmitInstanceEventUpdate(ParentID, Update);
		}
	}
}

void APropManager::ClearAssemblyComponents()
{
	for (auto& Pair : AssemblyComponents) if (IsValid(Pair.Value)) DestroyAssemblyPiece(Pair.Value);
	AssemblyComponents.Reset();
	ChildrenByOwner.Reset();
}

bool APropManager::IsAssemblyCompatible(int32 BottlePID, int32 ContentID, int32 CapPID) const
{
	const auto* Bottle = FindProductDefinition(BottlePID);
	if (!Bottle || Bottle->bIsBottleCap || Bottle->bLiquidVisualOnly || Bottle->ConsumableProfile.NumOfPortions <= 0) return false;
	const auto* Content = FindContentDefinition(ContentID);
 if(Content && Content->bUseBottlePIDCompatibility && !Content->SupportedBottlePIDs.Contains(CanonicalProductPID(BottlePID)))return false;
 if(CapPID)if(const auto* C=FindProductDefinition(CapPID))
 {
  const auto* Rules=Bottle->Category==EPropProductCategory::Bottle ? FindProductDefinition(CanonicalProductPID(CapPID)) : C;
  if(Rules && Rules->bUseBottlePIDCompatibility && !Rules->SupportedBottlePIDs.Contains(CanonicalProductPID(BottlePID)))return false;
 }
	if (ContentID && (!Content || (!Bottle->SupportedContentIDs.IsEmpty() && !Bottle->SupportedContentIDs.Contains(ContentID))
		|| (!Content->SupportedBottleMaterials.IsEmpty() && !Content->SupportedBottleMaterials.Contains(Bottle->BottleMaterialType)))) return false;
	if (!CapPID) return true;
	const auto* Cap = FindProductDefinition(CapPID);
	return Cap && Cap->bIsBottleCap && IsBottleCapTypeSupportedInMask(Bottle->SupportedCapMask, static_cast<uint8>(Cap->DefaultCapType))
		&& (Bottle->SupportedCapPIDs.IsEmpty() || Bottle->SupportedCapPIDs.Contains(CapPID))
		&& (Cap->SupportedBottleMaterials.IsEmpty() || Cap->SupportedBottleMaterials.Contains(Bottle->BottleMaterialType))
		&& (!Content || Content->SupportedCapPIDs.IsEmpty() || Content->SupportedCapPIDs.Contains(CapPID));
}

bool APropManager::AttachPropChild(int32 Parent, int32 Child, const FTransform& Relative)
{
	const auto* R = FindISIDData(Child);
	const auto* Def = R ? FindProductDefinition(R->ProductID) : nullptr;
	if (!Def || Def->bIsBottleCap || Def->bLiquidVisualOnly) return false;
	return InstallAssemblyEdge(Parent, Child, Relative);
}

bool APropManager::InstallAssemblyEdge(int32 Parent, int32 Child, const FTransform& Relative)
{
	if (!HasAuthority() || GetNetMode() == NM_Client || !Relative.IsValid() || !FindISIDData(Child)
		|| FindISIDData(Child)->Attachment.OwnerISID || !ResolveRootISID(Parent)) return false;
	int32 ParentDepth = 0;
	for (int32 Ancestor = Parent; Ancestor; Ancestor = FindISIDData(Ancestor)->Attachment.OwnerISID)
	{
		if (Ancestor == Child) return false;
		++ParentDepth;
	}
	TArray<int32> Level{Child};
	while (!Level.IsEmpty())
	{
		if (++ParentDepth > 64) return false;
		TArray<int32> Next;
		for (int32 ID : Level) Next.Append(GetChildISIDs(ID));
		Level = MoveTemp(Next);
	}
	const auto* ChildDef = FindProductDefinition(FindISIDData(Child)->ProductID);
	const auto* ParentRecord = FindISIDData(Parent);
	const auto* ParentData = FindUniqueData(Parent);
	if (ChildDef && ChildDef->bIsBottleCap && (!ParentData || !IsAssemblyCompatible(ParentRecord->ProductID, ParentData->EventData.ContentID, ChildDef->ProductID))) return false;
	if (GetActiveActorFromISID(Child))
	{
		// The volume still owns the active root. Only the child's independent body is recycled.
		const int32 Root = ResolveRootISID(Parent);
		if (IsInstanceKeptActive(Child) && !GetActiveActorFromISID(Root))
		{
			AActivePropActor* ActiveRoot = nullptr;
			if (!PromoteInstanceToASM(Root, EPropActivationReason::Interaction, false, ActiveRoot)) return false;
		}
		TGuardValue<int32> Joining(ChildJoiningAssembly, Child);
		if (!ConvertASMToISM(Child, EPropState::Settling, true, true)) return false;
	}
	FPropAttachment Edge; Edge.OwnerISID = Parent; Edge.RelativeTransform = Relative;
	SetAssemblyEdge(Child, Edge);
	IncrementInstanceVersion(Child);
	MarkInstanceDirty(Child, EPropDirtyFlags::Representation | EPropDirtyFlags::Transform);
	RefreshAssembly(Parent);
	return true;
}

bool APropManager::EnsureLiquidChild(int32 BottleID)
{
	const auto* Record = FindISIDData(BottleID);
	if (Record && HasAuthority() && ShopSupplies && !ShopSupplies->EnsureBottleAssemblyConfigured(Record->ProductID)) return false;
	Record=FindISIDData(BottleID);
	const auto* Def = Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Def || Def->bIsBottleCap || Def->bLiquidVisualOnly)return true;
 const auto* State=FindUniqueData(BottleID);
 const auto* Content=State ? FindContentDefinition(State->EventData.ContentID) : nullptr;
 if(Def->Category==EPropProductCategory::Bottle && (!State || !State->EventData.ContentID || State->EventData.ContentLevel<=0))
 {
  for(int32 Child:GetChildISIDs(BottleID))if(const auto* R=FindISIDData(Child))
   if(const auto* D=FindProductDefinition(R->ProductID);D && D->bLiquidVisualOnly)ReleaseInstanceToEmpty(Child);
  return true;
 }
 if(Def->Category==EPropProductCategory::Bottle && Content && Content->bUseBottlePIDCompatibility)
 {
  const int32 BottlePID=CanonicalProductPID(Record->ProductID);
  const auto* V=FindBottleContentVisual(Content->ContentID,BottlePID);
  if(!V || !V->Mesh || !IsAssemblyCompatible(Record->ProductID,Content->ContentID,0))return false;
  const int32 PID=Content->ContentID;const FTransform Mount=V->Attachment;
  for(int32 Child:GetChildISIDs(BottleID))
  {
   auto* R=FindISIDData(Child);const auto* D=R ? FindProductDefinition(R->ProductID) : nullptr;
   if(!D || !D->bLiquidVisualOnly)continue;
   if(!D->CatalogPID && R->ProductID!=PID)
   {
    auto Legacy=*D;Legacy.CatalogPID=PID;Legacy.Category=EPropProductCategory::Content;
    if(!RegisterProductDefinition(Legacy))return false;
    R=FindISIDData(Child);
   }
   if(CanonicalProductPID(R->ProductID)!=PID && !ChangePropProduct(Child,PID,false))return false;
   if(!EnsureAssemblyVisualBatch(Child,BottlePID))return false;
   RefreshAssembly(BottleID);return true;
  }
  const int32 ID=GenerateAssemblyContent(PID,BottlePID,Mount*GetTransform(BottleID));
  if(ID && InstallAssemblyEdge(BottleID,ID,Mount))return true;
  if(ID)ReleaseInstanceToEmpty(ID);return false;
 }
 if (!Def->LiquidVisualPID) return true;
	const auto* Contents = FindUniqueData(BottleID);
	const auto* InstalledCap = Contents ? FindISIDData(Contents->EventData.AttachedCapISID) : nullptr;
	if (!Contents || !IsAssemblyCompatible(Record->ProductID, Contents->EventData.ContentID, InstalledCap ? InstalledCap->ProductID : 0)) return false;
	const int32 Visual = Def->LiquidVisualPID;
	const auto* VisualDef = FindProductDefinition(Visual);
	if (!VisualDef || !VisualDef->bLiquidVisualOnly || VisualDef->LiquidVisualPID) return false;
	for (int32 Child : GetChildISIDs(BottleID))
	{
		auto* ChildRecord = FindISIDData(Child);
		const auto* Existing = ChildRecord ? FindProductDefinition(ChildRecord->ProductID) : nullptr;
		if (!Existing || !Existing->bLiquidVisualOnly) continue;
		if (ChildRecord->ProductID != Visual)
		{
			if (!ChangePropProduct(Child, Visual, false)) return false;
			if (auto* Old = GetAssemblyComponent(Child)) DestroyAssemblyPiece(Old);
			AssemblyComponents.Remove(Child);
		}
		RefreshAssembly(BottleID);
		return true;
	}
	const FTransform Relative = Def->LiquidAttachment;
	const int32 ID = GenerateProductInstance(Visual, Relative * GetTransform(BottleID));
	if (!ID) return false;
	if (InstallAssemblyEdge(BottleID, ID, Relative)) return true;
	ReleaseInstanceToEmpty(ID);
	return false;
}

void APropManager::RefreshAssembly(int32 ID)
{
	const int32 RootID = ResolveRootISID(ID);
	if (!RootID || !ChildrenByOwner.Contains(RootID) || SynchronizingCapBottles.Contains(RootID)) return;
	SynchronizingCapBottles.Add(RootID);
	auto* Actor = GetActiveActorFromISID(RootID);
	if (Actor && (!Actor->bIsActive || Actor->ISID != RootID || FindISIDData(RootID)->Representation != EPropRepresentation::ASM)) Actor = nullptr;
	TArray<int32> Parents{RootID};
	for (int32 Index = 0; Index < Parents.Num(); ++Index)
	{
		const int32 Parent = Parents[Index];
		USceneComponent* Mount = Parent == RootID ? (Actor ? Actor->GetRootComponent() : nullptr) : GetAssemblyComponent(Parent);
		for (int32 Child : GetChildISIDs(Parent))
		{
			auto* R = FindISIDData(Child);
			const auto Definition = R ? ResolveVisualDefinition(R->ProductID,R->VisualBottlePID) : FPropProductDefinition();
            const auto* D = R ? &Definition : nullptr;
			auto* ISM = R ? GetComponentFromISID(Child) : nullptr;
			if (!D || !ISM || R->Attachment.OwnerISID != Parent) continue;
			Parents.Add(Child);
			auto* Mesh = GetAssemblyComponent(Child);
			if (Actor && Mount)
			{
				const auto* ExistingPiece = Cast<UPropAssemblyMesh>(Mesh);
				const bool ChangedSkeletalVisual = D->bLiquidVisualOnly && ExistingPiece
					&& (ExistingPiece->DeformableVisual ? ExistingPiece->DeformableVisual->GetSkeletalMeshAsset() : nullptr) != D->SkeletalMesh;
				if ((ExistingPiece && ExistingPiece->ProductID != R->ProductID) || ChangedSkeletalVisual || !Mesh || Mesh->GetOwner() != Actor || Mesh->GetAttachParent() != Mount || Mesh->GetStaticMesh() != D->StaticMesh)
				{
					if (Mesh) DestroyAssemblyPiece(Mesh);
					auto* Piece = NewObject<UPropAssemblyMesh>(Actor);
					Piece->ISID = Child;
					Piece->ProductID = R->ProductID;
					Mesh = Piece;
					Mesh->SetupAttachment(Mount);
					Mesh->SetStaticMesh(D->StaticMesh);
					for (int32 I = 0; I < D->MaterialOverrides.Num(); ++I) Mesh->SetMaterial(I, D->MaterialOverrides[I]);
					Mesh->SetCollisionEnabled(D->bLiquidVisualOnly ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
					Mesh->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
					Mesh->SetGenerateOverlapEvents(false);
					Mesh->SetCanEverAffectNavigation(false);
					Mesh->SetRelativeTransform(R->Attachment.RelativeTransform);
					Mesh->RegisterComponent();
					if (D->bLiquidVisualOnly && D->SkeletalMesh)
					{
						auto* Liquid = NewObject<USkeletalMeshComponent>(Actor);
						Piece->DeformableVisual = Liquid;
						Liquid->SetupAttachment(Piece);
						Liquid->SetSkeletalMesh(D->SkeletalMesh);
						Liquid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						Liquid->SetGenerateOverlapEvents(false);
						Liquid->SetCanEverAffectNavigation(false);
						Liquid->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
						Liquid->PrimaryComponentTick.TickInterval=.05f;
						for (int32 I=0; I<D->MaterialOverrides.Num(); ++I) Liquid->SetMaterial(I,D->MaterialOverrides[I]);
						Liquid->RegisterComponent();
						Piece->SetVisibility(false,false);
					}
					AssemblyComponents.Add(Child, Mesh);
					if(D->bLiquidVisualOnly)SpawnContentEffect(FindBottleContentVisual(R->ProductID,R->VisualBottlePID),Mesh);
					ISM->MoveInstanceToResetTransform(Child);
					if (D->bLiquidVisualOnly)
					{
						if (auto* Bottle = Cast<AColaPropActor>(Actor))
						{
							Bottle->AssemblyLiquidMesh = Mesh;
							Bottle->AssemblyLiquidSkeletalMesh = Piece->DeformableVisual;
							Bottle->UpdateDynamicMaterial(false);
						}
					}
				}
				if (D->bLiquidVisualOnly)
				{
					if (auto* Bottle = Cast<AColaPropActor>(Actor))
					{
						auto* Piece = Cast<UPropAssemblyMesh>(Mesh);
						Bottle->AssemblyLiquidMesh = Mesh;
						Bottle->AssemblyLiquidSkeletalMesh = Piece ? Piece->DeformableVisual : nullptr;
						Bottle->UpdateDynamicMaterial(false);
						Bottle->UpdateMorphTargets();
					}
				}
				if (auto* Piece = Cast<UPropAssemblyMesh>(Mesh); Piece && Piece->DeformableVisual)
				{
					Piece->SetVisibility(false,false);
					Piece->DeformableVisual->SetVisibility(true);
				}
				if (!HasAuthority() && !Mesh->GetRelativeTransform().Equals(R->Attachment.RelativeTransform)) Mesh->SetRelativeTransform(R->Attachment.RelativeTransform);
				if (!ISM->IsInstanceParkedAtResetTransform(R->ISMI)) ISM->MoveInstanceToResetTransform(Child);
			}
			else if (!Actor)
			{
				if (Mesh) { DestroyAssemblyPiece(Mesh); AssemblyComponents.Remove(Child); }
				const FTransform World = R->Attachment.RelativeTransform * GetTransform(Parent);
				R->ActiveTransform = World;
				if (!ISM->GetStaticMesh()) continue;
				FTransform Current;
				if (!ISM->GetInstanceTransform(R->ISMI, Current, true) || !Current.Equals(World))
					ISM->RestoreInstanceToGameplayTransform(Child, World);
				if (D->bLiquidVisualOnly)
				{
					const auto* U = FindUniqueData(RootID);
					ISM->UpdateInstanceCustomDataDirect(R->ISMI, D, U ? FindContentDefinition(U->EventData.ContentID) : nullptr, U, WorldElapsedSeconds, true);
				}
			}
		}
	}
	SynchronizingCapBottles.Remove(RootID);
}

void APropManager::CaptureAssembly(int32 Root)
{
	TArray<int32> Parents{Root};
	for (int32 I = 0; I < Parents.Num(); ++I)
	{
		for (int32 ID : GetChildISIDs(Parents[I]))
		{
			Parents.Add(ID);
			auto* R = FindISIDData(ID);
			if (auto* Mesh = GetAssemblyComponent(ID); R && Mesh)
			{
				R->Attachment.RelativeTransform = Mesh->GetRelativeTransform();
				R->ActiveTransform = Mesh->GetComponentTransform();
				IncrementInstanceVersion(ID);
				MarkInstanceDirty(ID, EPropDirtyFlags::Transform);
			}
		}
	}
}
