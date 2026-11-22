#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Curves/CurveFloat.h"
#include "Props/ColaPricingLibrary.h"
#include "Props/FragmentAssemblyDataAsset.h"

bool AColaPropActor::CanDemoteToISM() const
{
	const auto* Def=ResolveProductDefinition();
	return Super::CanDemoteToISM() && LiquidMotion.IsSettled() && !HasTemporaryManufacturingSeal()
		&& !(GetContentLevel()>UE_SMALL_NUMBER && GetCapSealed()<1.f
			&& (!Def || !Def->bAllowUncappedFilledISMDemotion || LocalUniqueData.EventData.AttachedCapISID>0))
		&& SmoothedPressureExpansion<=.001f
		&& (UsesPlasticPressure() ? LocalUniqueData.ContinuousData.SealedPressure : InteractionPressure) <= UE_KINDA_SMALL_NUMBER
		&& AcceptedFailureType == EContainmentFailureType::None;
}
void AColaPropActor::ResetTransientPoolState()
{
	StopRocketPlume();
	Super::ResetTransientPoolState();
	LiquidMotion.Reset(); LastLiquidLocalNormal=FVector::ZeroVector; LastLiquidFill=-1;
	SmoothedPressureExpansion=0; bMorphInitialized=false; ContentMass=0;
	RocketInitialContentLevel=RocketInitialPressure=0; RocketLaunchConfiguration=FColaRocketLaunchConfiguration();
	InteractionPressure = FoamPressure = 0;
	AcceptedFailureType = EContainmentFailureType::None;
	bManufacturingFailureDispatched=false;
	ProcessedImpactIDs.Empty(); LastImpactTime = -1;
	bDebugBottleImpacts=false;
	AssemblyLiquidMesh = nullptr;
	AssemblyLiquidSkeletalMesh = nullptr;
	bHasAgitationHistory = false;
	DynamicMaterialInstance = nullptr;
	if (CapMeshComponent) { CapMeshComponent->DestroyComponent(); CapMeshComponent = nullptr; }
	if (RocketComp) RocketComp->ResetRocketSimulation();
	CapHoldingStation.Reset(); ManualFillingStation.Reset(); PressureReleaseEpisode.Invalidate(); PressurePublishElapsed=0; LastPublishedPressure=0;
}
void AColaPropActor::OnApplyContinuousData(const FPropContinuousUpdateData& Data,float NewStrength,bool bStrengthChanged)
{
	Super::OnApplyContinuousData(Data,NewStrength,bStrengthChanged);
	// Pressure arrives through the existing continuous-data channel. Morph targets
	// interpolate locally on active actors; they do not create another network stream.
	UpdateMorphTargets();
}
void AColaPropActor::ResetForInstanceBinding()
{
	if (HasAuthority() || !RocketComp) { ResetTransientPoolState(); return; }
	// Initial component replication can precede the manager's ISID binding. Keep
	// that received snapshot while clearing cosmetic residue. Actual pool return
	// still calls ResetTransientPoolState directly and discards the old flight.
	const FRocketFlightNetState Incoming=RocketComp->GetReplicatedFlightState();
	ResetTransientPoolState();
	RocketComp->RestoreReplicatedFlightState(Incoming);
}
void AColaPropActor::OnApplyEventData(const FPropEventUpdateData& Data, uint8 DirtyMask)
{
	Super::OnApplyEventData(Data, DirtyMask);
	if (LocalUniqueData.EventData.AttachedCapISID>0 && CapMeshComponent)
	{
		CapMeshComponent->DestroyComponent(); CapMeshComponent=nullptr;
	}
	ColaLevel = GetContentLevel();
	UpdateBottleMass(); UpdateMorphTargets(); UpdateDynamicMaterial(false);
}
bool AColaPropActor::SetBottleContentType(int32 NewContentID)
{
	if (!HasAuthority() || !UColaPricingLibrary::CanReuseBottle(GetStrength(), LocalUniqueData.EventData.bClean, GetContentLevel()) || !PropManagerReference) return false;
	const FPropProductDefinition* Def = ResolveProductDefinition();
	const FPropContentDefinition* Content = PropManagerReference->FindContentDefinition(NewContentID);
	const auto* Cap=PropManagerReference->FindISIDData(LocalUniqueData.EventData.AttachedCapISID);
	if (!Def || !Content || !PropManagerReference->IsAssemblyCompatible(Def->ProductID, NewContentID, Cap ? Cap->ProductID : 0)) return false;
	LocalUniqueData.EventData.ContentID = NewContentID;
	bHasValidUniqueData = true;
	OnApplySharedDefinitions(Def, Content);
	EmitPropEventUpdate(EPropEventDirtyMask::ContentID);
	return true;
}
float AColaPropActor::FillBottleAtRate(float Rate, float DeltaSeconds)
{
	if (!HasAuthority() || !FMath::IsFinite(Rate) || !FMath::IsFinite(DeltaSeconds) || Rate <= 0 || DeltaSeconds <= 0 || GetCapSealed() > 0 || GetContentID() <= 0 || AcceptedFailureType != EContainmentFailureType::None) return 0;
	const FPropProductDefinition* Def = ResolveProductDefinition();
	if (!Def) return 0;
	const bool bDeforms = Def->BottleMaterialType == EBottleMaterialType::Plastic || Def->BottleMaterialType == EBottleMaterialType::HeavyMetal;
	const float MaxLevel = Def->MaxOverfillLevel * (bDeforms ? GetStrengthRatio() : 1.0f);
	const float Flow = Def->FillSlowdownCurve ? FMath::Max(0.0f, Def->FillSlowdownCurve->GetFloatValue(GetContentLevel())) : 1.0f;
	const float Accepted = FMath::Min(Rate * DeltaSeconds * Flow, FMath::Max(0.0f, MaxLevel - GetContentLevel()));
	if (!FMath::IsFinite(Accepted) || Accepted <= 0) return 0;
	const bool bStartsContentLifecycle = IsEmpty();
	LocalUniqueData.EventData.ContentLevel = GetContentLevel() + Accepted;
	if (bStartsContentLifecycle) LocalUniqueData.EventData.ContentCreationTime = PropManagerReference ? PropManagerReference->WorldElapsedSeconds : GetWorld()->GetTimeSeconds();
	ColaLevel = LocalUniqueData.EventData.ContentLevel;
	EmitPropEventUpdate(static_cast<uint8>(EPropEventDirtyMask::ContentLevel), bStartsContentLifecycle ? EPropExtendedEventMask::ContentCreationTime : 0);
	return Accepted * GetNumPortions();
}
void AColaPropActor::RemoveBottleContent()
{
	if (!HasAuthority()) return;
	LocalUniqueData.EventData.ContentLevel = 0; LocalUniqueData.EventData.ContentID = 0;
	ColaLevel = 0;
	EmitPropEventUpdate(EPropEventDirtyMask::ContentLevel | EPropEventDirtyMask::ContentID);
}
void AColaPropActor::ApplyBottleImpact(const FVector& Impulse, int32 EventID)
{
	if (!HasAuthority() || Impulse.ContainsNaN() || (EventID != 0 && ProcessedImpactIDs.Contains(EventID))) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastImpactTime >= 0 && Now - LastImpactTime < 0.05) return;
	LastImpactTime = Now;
	if (EventID) { if (ProcessedImpactIDs.Num() >= 128) ProcessedImpactIDs.Empty(); ProcessedImpactIDs.Add(EventID); }
	const FPropProductDefinition* Def = ResolveProductDefinition();
	if (!Def || Def->BottleMaterialType == EBottleMaterialType::Diamond) return;
	const float Severity = FMath::Max(0.0f, Impulse.Size() - DropImpactThreshold) / FMath::Max(1.0f, DropImpactThreshold);
	if (UsesPlasticPressure())
	{
		const float EquivalentSpeed = Impulse.Size()/FMath::Max(.001f,ImpactReferenceMassKg);
		const float Excitation = FMath::Max(0.f,EquivalentSpeed-ImpactAgitationSpeedThreshold)/FMath::Max(1.f,ImpactAgitationSpeedThreshold);
		AddAgitationPressure(Excitation*FMath::Max(0.f,ImpactPressureGain));
	}
	if (Severity <= 0) return;
	LocalUniqueData.EventData.Strength = FMath::Clamp(GetBaseStrength() - Severity * Def->StrengthDamageRate, 0.0f, 1.0f);
	EmitPropEventUpdate(EPropEventDirtyMask::Strength);

	if (GetStrength() <= 0 && (Def->BottleMaterialType == EBottleMaterialType::Glass || Def->BottleMaterialType == EBottleMaterialType::Concrete))
	{
		FContainmentFailureRequest Failure; Failure.FailureType = EContainmentFailureType::BottleBreakage;
		Failure.FailureLocation = GetActorLocation(); Failure.FailureDirection = Impulse.GetSafeNormal();
		SubmitFailureRequest(Failure);
	}
}
void AColaPropActor::ApplyInitialImpactFromPromotion(FVector Location, const FVector& Impulse, AActor* InstigatorActor, int32 EventID)
{
	FVector ContactMomentum = Impulse;
	if (const auto* Pawn = Cast<APawn>(InstigatorActor); Pawn && Impulse.Size() < DropImpactThreshold)
	{
		// Legacy character overlaps report a fixed kick. Limit passive contacts to measured motion.
		const float Mass = PropMesh ? PropMesh->BodyInstance.GetBodyMass() : BottleMass+ContentMass;
		const float ClosingSpeed = FMath::Max(0.f,float(FVector::DotProduct(Pawn->GetVelocity()-LinearVelocity,Impulse.GetSafeNormal())));
		ContactMomentum = Impulse.GetClampedToMaxSize(FMath::Max(.001f,Mass)*ClosingSpeed);
	}
	ApplyBottleImpact(NormalizePhysicalContactImpulse(ContactMomentum), EventID);
}
bool AColaPropActor::SubmitFailureRequest(const FContainmentFailureRequest& Request)
{
	if (!HasAuthority() || Request.FailureType == EContainmentFailureType::None || (AcceptedFailureType != EContainmentFailureType::None && static_cast<uint8>(AcceptedFailureType) <= static_cast<uint8>(Request.FailureType))) return false;
	const FPropProductDefinition* Def = ResolveProductDefinition();
	if (!Def) return false;
	if (Request.FailureType == EContainmentFailureType::BottleBreakage && PropManagerReference && (Def->FragmentAssembly || !Def->FractureFragments.IsEmpty()))
	{
		APropManager* Manager = PropManagerReference;
		const int32 SourceISID = ISID;
		if (SourceISID <= 0 || !Manager->FindISIDData(SourceISID) || Manager->GetActiveActorFromISID(SourceISID) != this) return false;
		const int32 CapISID = LocalUniqueData.EventData.AttachedCapISID;
		const FPropInstanceUniqueData* CapData = CapISID > 0 ? Manager->FindUniqueData(CapISID) : nullptr;
		if (CapISID > 0 && (!CapData || CapData->EventData.AttachedBottleISID != SourceISID)) return false;
		if (GetContentLevel() > 0 && !Request.bReleaseContent) return false;
		UFragmentAssemblyDataAsset* Assembly = Def->FragmentAssembly;
		const TArray<FPropFractureFragmentEntry> Recipe = Def->FractureFragments;
		const FVector SourceVelocity = Manager->GetLinearVelocity(SourceISID), SourceAngularVelocity = Manager->GetAngularVelocity(SourceISID);
		const FTransform CapTransform = CapISID > 0 ? Manager->GetTransform(CapISID) : FTransform::Identity;
		TArray<int32> Fragments;
		const bool bCreated = Assembly ? Manager->SpawnFragmentAssembly(Assembly, GetActorTransform(), SourceVelocity, SourceAngularVelocity, Fragments)
			: Manager->CreateFragmentDestruction(Recipe, GetActorTransform(), SourceVelocity, SourceAngularVelocity, Fragments);
		if (!bCreated) return false;
		if (!Manager->ReleaseBottleLiquid(SourceISID))
		{
			for (int32 Fragment : Fragments) Manager->ReleaseInstanceToEmpty(Fragment);
			return false;
		}
		if (CapISID > 0)
		{
			FPropEventSyncDelta Detach; Detach.ISID = CapISID; Detach.ExtendedDirtyMask = EPropExtendedEventMask::AttachedBottle;
			Detach.AttachedBottleISID = 0;
			Manager->SubmitAtomicInstanceEventUpdates({Detach});
			if (AActivePropActor* Cap = Manager->GetActiveActorFromISID(CapISID)) if (Cap->GetAttachParentActor() == this) Cap->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Manager->SetAssemblyEdge(CapISID, FPropAttachment());
			Manager->InitializeInstanceTransform(CapISID, CapTransform);
			Manager->ApplyImpulse(CapISID, SourceVelocity, true);
		}
		AcceptedFailureType = Request.FailureType;
		return Manager->ReleaseInstanceToEmpty(SourceISID);
	}
	AcceptedFailureType = Request.FailureType;
	const int32 TargetPID = Request.FailureType == EContainmentFailureType::BottleBreakage ? Def->BrokenProductID : Request.FailureType == EContainmentFailureType::ThermalFailure ? Def->MeltedProductID : Def->BurstBrokenProductID;
	if (Request.bReleaseContent) RemoveBottleContent();
	if (Request.bReleasePressure) { InteractionPressure = FoamPressure = 0; }
	if (TargetPID > 0 && TargetPID != ProductID && PropManagerReference)
		return PropManagerReference->ChangePropProductWithMode(ISID, TargetPID, EPropProductResetMode::PreserveUniqueState);
	ForceNetUpdate();
	return true;
}

FColaPriceQuote AColaPropActor::GetSaleQuote() const
{
	if (PropManagerReference && ISID > 0)
	{
		FShopProductScanData Scan;
		return PropManagerReference->GetProductScanData(ISID, Scan) ? Scan.Price : FColaPriceQuote();
	}
	const FPropContentDefinition* Content = ResolveContentDefinition();
	return Content ? UColaPricingLibrary::QuoteCola(GetContentLevel(), GetNumPortions(), Content->BasePortionPrice, GetCurrentTemperature(), Content->IdealServingTemperature, Content->TemperaturePenaltyRange, IsExpired()) : FColaPriceQuote();
}
