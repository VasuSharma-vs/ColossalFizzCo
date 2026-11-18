#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"

namespace
{
	FPropEventSyncDelta ManufacturingState(const AColaPropActor& Bottle)
	{
		const auto& State=Bottle.LocalUniqueData.EventData;
		FPropEventSyncDelta Delta; Delta.ISID=Bottle.ISID; Delta.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal;
		Delta.TemporarySealUntil=State.TemporarySealUntil; Delta.bTemporarySealUsed=State.bTemporarySealUsed;
		Delta.bTemporarySealArmed=State.bTemporarySealArmed; Delta.bManualFillPressure=State.bManualFillPressure;
		return Delta;
	}
}

bool AColaPropActor::IsManufacturingFailureActive() const
{
	const bool AtRest=!RocketComp || RocketComp->GetSimulationState()==ERocketSimulationState::Idle
		|| RocketComp->GetSimulationState()==ERocketSimulationState::AtRest;
	return bManufacturingFailureDispatched && !(IsEmpty() && AtRest);
}

bool AColaPropActor::CanAcceptManufacturingContent(int32 ContentID) const
{
	const auto* Def=ResolveProductDefinition();
	if (!PropManagerReference || !Def || !bHasValidUniqueData || Def->bIsBottleCap || Def->ConsumableProfile.NumOfPortions<=0
		|| GetStrength()<=.7f || !LocalUniqueData.EventData.bClean || GetCapSealed()>0 || LocalUniqueData.EventData.AttachedCapISID>0
		|| AcceptedFailureType!=EContainmentFailureType::None || IsManufacturingFailureActive()
		|| (RocketComp && RocketComp->IsPoweredFlight()) || (GetContentLevel()>0 && GetContentID()!=ContentID)
		|| (HasTemporaryManufacturingSeal() && !LocalUniqueData.EventData.bTemporarySealArmed)) return false;
	return PropManagerReference->FindContentDefinition(ContentID) && PropManagerReference->IsAssemblyCompatible(ProductID,ContentID,0);
}

void AColaPropActor::OnManufacturingLiquidReceived(float PreviousLevel)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !PropManagerReference || !FMath::IsFinite(PreviousLevel)
		|| PreviousLevel<0 || GetContentLevel()<=PreviousLevel || !FMath::IsFinite(ManufacturingPressurePerContentLevel)) return;
	// An emptied, landed bottle starts a new manufacturing episode without requiring a pool round-trip.
	if (PreviousLevel<=0 && (!RocketComp || RocketComp->GetSimulationState()==ERocketSimulationState::Idle
		|| RocketComp->GetSimulationState()==ERocketSimulationState::AtRest)) bManufacturingFailureDispatched=false;
	auto Delta=ManufacturingState(*this); Delta.bManualFillPressure=true;
	if (!PropManagerReference->SubmitInstanceEventUpdate(ISID,Delta)) return;
	const float Pressure=(PreviousLevel<=0 ? 0.f : LocalUniqueData.ContinuousData.SealedPressure)
		+(GetContentLevel()-PreviousLevel)*FMath::Max(0.f,ManufacturingPressurePerContentLevel);
	PropManagerReference->SetBottleAgitationPressure(ISID,Pressure);
	EvaluateManufacturingPressureFailure(true);
}

bool AColaPropActor::IsManufacturingFlowActive() const
{
	const auto* Station=ManualFillingStation.Get();
	const auto* Filler=Station ? Station->Filler.Get() : nullptr;
	return Filler && Filler->bIsFilling && Filler->TargetISID==ISID && Station->IsLeverHoldingBottle(ISID)
		&& (!Filler->IsHoseFilling() || Filler->DeliveredPortionsPerSecond>UE_SMALL_NUMBER);
}

void AColaPropActor::OnManufacturingSupplyReceived(float ActualUnits,float IncomingPressure,float ActualRate)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !PropManagerReference || IsEmpty() || IsManufacturingFailureActive()
		|| AcceptedFailureType!=EContainmentFailureType::None || (RocketComp && RocketComp->IsPoweredFlight())
		|| !FMath::IsFinite(ActualUnits) || ActualUnits<=0 || !FMath::IsFinite(IncomingPressure) || IncomingPressure<0
		|| !FMath::IsFinite(ActualRate) || ActualRate<=0) return;
	const float Limit=FMath::IsFinite(ManufacturingMaxSupplyPressure) ? FMath::Max(0.f,ManufacturingMaxSupplyPressure) : 3.f;
	const bool Overpressure=IncomingPressure>Limit;
	const float ReceivedPressure=Overpressure ? FMath::Max(IncomingPressure,FMath::Max(0.f,SafeResidualPressure)+.001f) : IncomingPressure;
	if (!PropManagerReference->SetBottleAgitationPressure(ISID,FMath::Max(LocalUniqueData.ContinuousData.SealedPressure,ReceivedPressure))) return;
	// The nozzle/temporary seal can restrain ordinary filling pressure, but cannot protect against an excessive supply.
	if (Overpressure && HandleManufacturingPressureFailure(true,false)) bManufacturingFailureDispatched=true;
}

bool AColaPropActor::CanApplyManufacturingSeal() const
{
	const auto* Def=ResolveProductDefinition();
	return bSupportsTemporarySeal && Def && !Def->bIsBottleCap && Def->ConsumableProfile.NumOfPortions>0 && bHasValidUniqueData
		&& PropManagerReference && !LocalUniqueData.EventData.bTemporarySealUsed && GetCapSealed()<1 && GetStrength()>0
		&& AcceptedFailureType==EContainmentFailureType::None && !IsManufacturingFailureActive()
		&& !(RocketComp && RocketComp->IsPoweredFlight()) && FMath::IsFinite(TemporarySealDuration) && TemporarySealDuration>0;
}

bool AColaPropActor::BuildManufacturingSealDelta(FPropEventSyncDelta& OutDelta) const
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !CanApplyManufacturingSeal()) return false;
	OutDelta=ManufacturingState(*this);
	OutDelta.bTemporarySealUsed=true;
	OutDelta.bManualFillPressure=true;
	// An empty bottle may be prepared before the lever is touched. Once released, no subsequent lever press refreshes it.
	OutDelta.bTemporarySealArmed=IsEmpty() || (ManualFillingStation.IsValid() && ManualFillingStation->IsLeverHoldingBottle(ISID));
	OutDelta.TemporarySealUntil=OutDelta.bTemporarySealArmed ? 0.0 : PropManagerReference->WorldElapsedSeconds+TemporarySealDuration;
	return true;
}

bool AColaPropActor::AddManufacturingSeal()
{
	FPropEventSyncDelta Delta;
	return BuildManufacturingSealDelta(Delta) && PropManagerReference->SubmitInstanceEventUpdate(ISID,Delta);
}

void AColaPropActor::NotifyManufacturingLeverReleased()
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !PropManagerReference) return;
	if (LocalUniqueData.EventData.bTemporarySealArmed)
	{
		auto Delta=ManufacturingState(*this); Delta.bTemporarySealArmed=false;
		Delta.TemporarySealUntil=GetCapSealed()<1 ? PropManagerReference->WorldElapsedSeconds+FMath::Max(.1f,TemporarySealDuration) : 0;
		if (!PropManagerReference->SubmitInstanceEventUpdate(ISID,Delta)) return;
	}
	EvaluateManufacturingPressureFailure();
}

float AColaPropActor::GetManufacturingSealSecondsRemaining() const
{
	if (!HasTemporaryManufacturingSeal()) return 0;
	return LocalUniqueData.EventData.bTemporarySealArmed ? FMath::Max(0.f,TemporarySealDuration)
		: float(FMath::Max(0.,LocalUniqueData.EventData.TemporarySealUntil-PropManagerReference->WorldElapsedSeconds));
}

float AColaPropActor::GetManufacturingSealIntegrity() const
{
	return FMath::Clamp(GetManufacturingSealSecondsRemaining()/FMath::Max(.1f,TemporarySealDuration),0.f,1.f);
}

bool AColaPropActor::IsManufacturingSafeToRelease() const
{
	return IsEmpty() || GetCapSealed()>=1 || LocalUniqueData.ContinuousData.SealedPressure<=FMath::Max(0.f,SafeResidualPressure);
}

bool AColaPropActor::CanManufacturingCap() const
{
	if (!PropManagerReference || !bHasValidUniqueData || AcceptedFailureType!=EContainmentFailureType::None || IsManufacturingFailureActive()
		|| (RocketComp && RocketComp->IsPoweredFlight()) || IsManufacturingFlowActive()) return false;
	for (const auto& Session:PropManagerReference->FillSessions) if (Session.Value.TargetISID==ISID) return false;
	return GetCapSealed()<1 && (IsManufacturingSafeToRelease() || HasTemporaryManufacturingSeal()
		|| (CapHoldingStation.IsValid() && CapHoldingStation->HasSufficientCapHold(ISID)));
}

bool AColaPropActor::AttachManufacturingCap(int32 CapISID)
{
	return HasAuthority() && GetNetMode()!=NM_Client && CanManufacturingCap() && PropManagerReference->AttachPhysicalCap(ISID,CapISID);
}

bool AColaPropActor::AdvanceManufacturingCap(float ProgressDelta,bool bSufficientHold)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(ProgressDelta) || ProgressDelta<=0 || !PropManagerReference
		|| !LocalUniqueData.EventData.AttachedCapISID || GetCapSealed()>=1 || IsManufacturingFailureActive()
		|| IsManufacturingFlowActive()
		|| (!IsManufacturingSafeToRelease() && !HasTemporaryManufacturingSeal() && !bSufficientHold)) return false;
	const auto* Def=ResolveProductDefinition();
	const auto* Cap=PropManagerReference->FindISIDData(LocalUniqueData.EventData.AttachedCapISID);
	if (!Def || !Cap || !PropManagerReference->IsAssemblyCompatible(ProductID,GetContentID(),Cap->ProductID)) return false;
	for (const auto& Session:PropManagerReference->FillSessions) if (Session.Value.TargetISID==ISID) return false;
	const float Progress=FMath::Clamp(GetCapSealed()+ProgressDelta,0.f,1.f);
	if (Progress>=1) return CloseManufacturingCap(FMath::Lerp(Def->TorqueSweetSpotMin,Def->TorqueSweetSpotMax,.5f));
	FPropEventSyncDelta Delta; Delta.SetFlag(EPropEventDirtyMask::CapSealed); Delta.CapSealed=Progress;
	return PropManagerReference->SubmitInstanceEventUpdate(ISID,Delta);
}

bool AColaPropActor::CloseManufacturingCap(float Torque)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !CanManufacturingCap() || !PropManagerReference->ClosePhysicalCap(ISID,Torque)) return false;
	if (GetCapSealed()>=1 && (LocalUniqueData.EventData.bTemporarySealArmed || LocalUniqueData.EventData.TemporarySealUntil>0))
	{
		auto Delta=ManufacturingState(*this); Delta.bTemporarySealArmed=false; Delta.TemporarySealUntil=0;
		PropManagerReference->SubmitInstanceEventUpdate(ISID,Delta);
	}
	return true;
}

void AColaPropActor::UpdateManufacturingCapLeakage(float DeltaSeconds,bool bSufficientHold,FGuid ReleaseEpisode)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !PropManagerReference || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0
		|| !FMath::IsFinite(ManufacturingLeakageLevelPerSecond) || ManufacturingLeakageLevelPerSecond<=0 || !ReleaseEpisode.IsValid()
		|| !LocalUniqueData.EventData.AttachedCapISID || GetCapSealed()>=1 || IsEmpty() || bSufficientHold
		|| (LocalUniqueData.EventData.bManualFillPressure && IsManufacturingSafeToRelease())
		|| HasTemporaryManufacturingSeal() || IsManufacturingFailureActive()) return;
	PropManagerReference->ReleaseBottleLiquidPortions(ISID,double(ManufacturingLeakageLevelPerSecond)*DeltaSeconds*GetNumPortions(),ReleaseEpisode);
}

bool AColaPropActor::EvaluateManufacturingPressureFailure(bool bOverfill)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !PropManagerReference || IsEmpty() || IsManufacturingFailureActive()
		|| AcceptedFailureType!=EContainmentFailureType::None || (RocketComp && RocketComp->IsPoweredFlight())) return false;
	const bool Overfilled=bOverfill && GetContentLevel()>=FMath::Max(1.01f,ManufacturingOverfillFailureLevel);
	const bool LeverHeld=ManualFillingStation.IsValid() && ManualFillingStation->IsLeverHoldingBottle(ISID);
	const bool CapHeld=CapHoldingStation.IsValid() && CapHoldingStation->HasSufficientCapHold(ISID);
	const bool OpenFailure=LocalUniqueData.EventData.bManualFillPressure && !IsManufacturingSafeToRelease()
		&& !LeverHeld && !CapHeld && !HasTemporaryManufacturingSeal();
	const bool ClosedFailure=GetCapSealed()>=1 && !CapHeld && GetBottlePressure()>GetBottlePressureTolerance();
	if (!Overfilled && !OpenFailure && !ClosedFailure) return false;
	if (HandleManufacturingPressureFailure(Overfilled || OpenFailure,Overfilled)) { bManufacturingFailureDispatched=true; return true; }
	return false;
}

bool AColaPropActor::HandleManufacturingPressureFailure_Implementation(bool bTemporarySealFailure,bool bOverfill)
{
	if (!HasAuthority() || GetNetMode()==NM_Client) return false;
	if (UsesPlasticPressure()) return StartPlasticPressureRelease(bTemporarySealFailure);
	FContainmentFailureRequest Request; Request.FailureType=EContainmentFailureType::OverpressureBurst;
	Request.FailureLocation=GetActorLocation(); Request.FailureDirection=GetActorUpVector();
	return SubmitFailureRequest(Request);
}
