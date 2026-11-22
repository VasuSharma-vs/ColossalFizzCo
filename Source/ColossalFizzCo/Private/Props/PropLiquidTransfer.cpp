#include "Props/InteractablePropManager.h"
#include "Props/ColaPricingLibrary.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Curves/CurveFloat.h"

namespace
{
	bool IsExpiredSupply(const FPropInstanceUniqueData& Unique, const FPropContentDefinition& Content, double Now)
	{
		return Content.ExpiryProfile.bHasExpiry && Now - Unique.EventData.ContentCreationTime >= Content.ExpiryProfile.ExpiryLifetimeMinutes * 60.0;
	}
}

FGuid APropManager::BeginLiquidTransfer(int32 SourceID, int32 TargetID)
{
	if (!HasAuthority() || SourceID == TargetID) return FGuid();
	const FISIDRuntimeIndex* SourceRecord = FindISIDData(SourceID);
	const FISIDRuntimeIndex* TargetRecord = FindISIDData(TargetID);
	const FPropInstanceUniqueData* Source = FindUniqueData(SourceID);
	const FPropInstanceUniqueData* Target = FindUniqueData(TargetID);
	const FPropProductDefinition* SourceDef = SourceRecord ? FindProductDefinition(SourceRecord->ProductID) : nullptr;
	const FPropProductDefinition* TargetDef = TargetRecord ? FindProductDefinition(TargetRecord->ProductID) : nullptr;
	const FPropContentDefinition* Content = Source ? FindContentDefinition(Source->EventData.ContentID) : nullptr;
	if (!SourceDef || !TargetDef || !Source || !Target || !Content || SourceDef->ConsumableProfile.NumOfPortions <= 0 || TargetDef->ConsumableProfile.NumOfPortions <= 0
		|| !FMath::IsFinite(Source->EventData.ContentLevel) || !FMath::IsFinite(Target->EventData.ContentLevel)
		|| !FMath::IsFinite(Source->ContinuousData.Temperature) || !FMath::IsFinite(Target->ContinuousData.Temperature)
		|| Source->EventData.ContentLevel <= 0 || Target->EventData.ContentLevel < 0
		|| (Target->EventData.ContentLevel > 0 && Target->EventData.ContentID != Content->ContentID)
		|| Target->EventData.CapSealed > 0 || Target->EventData.AttachedCapISID>0 || (!Target->EventData.bTemporarySealArmed && Target->EventData.TemporarySealUntil>WorldElapsedSeconds)
		|| !UColaPricingLibrary::CanReuseBottle(TargetDef->GetEffectiveStrength(*Target), Target->EventData.bClean, 0.f)
		|| IsExpiredSupply(*Source, *Content, WorldElapsedSeconds) || !IsAssemblyCompatible(TargetRecord->ProductID, Content->ContentID, 0)) return FGuid();
	// An interrupted same-content fill can resume without emptying the bottle or
	// resetting its liquid age. The original cleanliness/strength rules still apply.
	const bool Deforms = TargetDef->BottleMaterialType == EBottleMaterialType::Plastic || TargetDef->BottleMaterialType == EBottleMaterialType::HeavyMetal;
	const float Capacity = TargetDef->MaxOverfillLevel * (Deforms ? FMath::Clamp(TargetDef->GetEffectiveStrength(*Target), 0.f, 1.f) : 1.f);
	if (!FMath::IsFinite(Capacity) || Target->EventData.ContentLevel >= Capacity) return FGuid();
	for (const auto& Pair : FillSessions) if (Pair.Value.TargetISID == TargetID || Pair.Value.SourceISID == TargetID || Pair.Value.TargetISID == SourceID) return FGuid();
	if (ShopCheckout)
	{
		FShopStockEntry Entry;
		if ((ShopCheckout->GetStockEntry(SourceID, Entry) && (!ShopCheckout->IsAvailableShopStock(SourceID)))
			|| (ShopCheckout->GetStockEntry(TargetID, Entry) && (!ShopCheckout->IsAvailableShopStock(TargetID)))) return FGuid();
	}
	const FGuid ID = FGuid::NewGuid();
	FFillSession Session; Session.SourceISID = SourceID; Session.TargetISID = TargetID; Session.ContentID = Content->ContentID;
	FillSessions.Add(ID, Session);
	return ID;
}

float APropManager::TransferLiquid(FGuid SessionID, float RequestedPortions)
{
	if (!HasAuthority() || !FMath::IsFinite(RequestedPortions) || RequestedPortions <= 0) return 0;
	const FFillSession* Found = FillSessions.Find(SessionID);
	if (!Found) return 0;
	const FFillSession Session = *Found;
	FISIDRuntimeIndex* SourceRecord = FindISIDData(Session.SourceISID);
	FISIDRuntimeIndex* TargetRecord = FindISIDData(Session.TargetISID);
	const FPropInstanceUniqueData* Source = FindUniqueData(Session.SourceISID);
	const FPropInstanceUniqueData* Target = FindUniqueData(Session.TargetISID);
	const FPropProductDefinition* SourceDef = SourceRecord ? FindProductDefinition(SourceRecord->ProductID) : nullptr;
	const FPropProductDefinition* TargetDef = TargetRecord ? FindProductDefinition(TargetRecord->ProductID) : nullptr;
	const FPropContentDefinition* Content = FindContentDefinition(Session.ContentID);
	if (!Source || !Target || !SourceDef || !TargetDef || !Content || !IsAssemblyCompatible(TargetRecord->ProductID,Session.ContentID,0) || Source->EventData.ContentID != Session.ContentID
		|| (Target->EventData.ContentLevel > 0 && Target->EventData.ContentID != Session.ContentID) || Target->EventData.CapSealed > 0 || Target->EventData.AttachedCapISID>0 || (!Target->EventData.bTemporarySealArmed && Target->EventData.TemporarySealUntil>WorldElapsedSeconds)
		|| !FMath::IsFinite(Source->EventData.ContentLevel) || !FMath::IsFinite(Target->EventData.ContentLevel)
		|| !FMath::IsFinite(Source->ContinuousData.Temperature) || !FMath::IsFinite(Target->ContinuousData.Temperature)
		|| TargetDef->GetEffectiveStrength(*Target) <= 0.70f || !Target->EventData.bClean || IsExpiredSupply(*Source, *Content, WorldElapsedSeconds))
	{
		EndLiquidTransfer(SessionID); return 0;
	}
	if (ShopCheckout)
	{
		FShopStockEntry Entry;
		if ((ShopCheckout->GetStockEntry(Session.SourceISID, Entry) && !ShopCheckout->IsAvailableShopStock(Session.SourceISID)) || (ShopCheckout->GetStockEntry(Session.TargetISID, Entry) && !ShopCheckout->IsAvailableShopStock(Session.TargetISID))) { EndLiquidTransfer(SessionID); return 0; }
	}
	const int32 SourcePortions = SourceDef->ConsumableProfile.NumOfPortions, TargetPortions = TargetDef->ConsumableProfile.NumOfPortions;
	if (SourcePortions <= 0 || TargetPortions <= 0) { EndLiquidTransfer(SessionID); return 0; }
	const bool Deforms = TargetDef->BottleMaterialType == EBottleMaterialType::Plastic || TargetDef->BottleMaterialType == EBottleMaterialType::HeavyMetal;
	const float Capacity = TargetDef->MaxOverfillLevel * (Deforms ? FMath::Clamp(TargetDef->GetEffectiveStrength(*Target), 0.0f, 1.0f) : 1.0f);
	const float Flow = TargetDef->FillSlowdownCurve ? TargetDef->FillSlowdownCurve->GetFloatValue(Target->EventData.ContentLevel) : 1.0f;
	if (!FMath::IsFinite(Flow) || Flow <= 0) return 0;
	const double Available = static_cast<double>(Source->EventData.ContentLevel) * SourcePortions;
	const double Room = FMath::Max(0.0, static_cast<double>(Capacity - Target->EventData.ContentLevel) * TargetPortions);
	const double Accepted = FMath::Min3(static_cast<double>(RequestedPortions) * Flow, Available, Room);
	if (Accepted <= 0 || !FMath::IsFinite(Accepted)) return 0;
	const bool StartsContent = Target->EventData.ContentLevel == 0;
	// Filling changes the content child and amount, never the physical bottle PID.
	TargetRecord = FindISIDData(Session.TargetISID);
	if (!TargetRecord) return 0;
	if (TargetRecord->Representation == EPropRepresentation::ISM)
	{
		AActivePropActor* Actor = nullptr;
		if (!PromoteInstanceToASM(Session.TargetISID, EPropActivationReason::Interaction, true, Actor)) return 0;
	}
	// Pool growth/promotion can reallocate maps. Resolve canonical pointers again before committing.
	Source = FindUniqueData(Session.SourceISID); Target = FindUniqueData(Session.TargetISID);
	if (!Source || !Target) return 0;
	FPropEventSyncDelta SourceDelta, TargetDelta;
	SourceDelta.ISID = Session.SourceISID; SourceDelta.SetFlag(EPropEventDirtyMask::ContentLevel);
	SourceDelta.ContentLevel = Accepted >= Available ? 0.0f : FMath::Max(0.0f, Source->EventData.ContentLevel - static_cast<float>(Accepted / SourcePortions));
	TargetDelta.ISID = Session.TargetISID; TargetDelta.SetFlag(EPropEventDirtyMask::ContentLevel); TargetDelta.SetFlag(EPropEventDirtyMask::ContentID);
	TargetDelta.ContentLevel = FMath::Min(Capacity, Target->EventData.ContentLevel + static_cast<float>(Accepted / TargetPortions));
	TargetDelta.ContentID = Session.ContentID;
	if (StartsContent) { TargetDelta.ExtendedDirtyMask = EPropExtendedEventMask::ContentCreationTime; TargetDelta.ContentCreationTime = WorldElapsedSeconds; }
	const double ExistingPortions = double(Target->EventData.ContentLevel) * TargetPortions;
	const float MixedTemperature = float((ExistingPortions * Target->ContinuousData.Temperature
		+ Accepted * Source->ContinuousData.Temperature) / (ExistingPortions + Accepted));
	if (!SubmitAtomicInstanceEventUpdates({SourceDelta, TargetDelta})) return 0;
	// Use the existing canonical temperature path so active mass/visuals and resting
	// instance data remain derived from the same state. An empty bottle takes the
	// supply temperature; a resumed fill mixes the retained and incoming cola.
	SetTemperature(Session.TargetISID, MixedTemperature);
	if (const auto* Updated = FindUniqueData(Session.TargetISID))
	{
		FPropContinuousSyncItem Item; Item.ISID = Session.TargetISID;
		Item.Temperature = Updated->ContinuousData.Temperature;
		Item.SealedPressure = Updated->ContinuousData.SealedPressure;
		Multicast_SyncContinuousBatch(WorldElapsedSeconds, {Item});
	}
	return static_cast<float>(Accepted);
}

void APropManager::EndLiquidTransfer(FGuid SessionID) { if (HasAuthority()) FillSessions.Remove(SessionID); }
void APropManager::CancelLiquidTransfers() { if (HasAuthority()) FillSessions.Empty(); }
