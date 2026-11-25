#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Shop/PlayerUpgradesComponent.h"
#include "Shop/ShopProductionStationComponent.h"

bool APropManager::SetBottleAgitationPressure(int32 InISID,float Pressure,bool bPublish)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(Pressure) || Pressure<0) return false;
	auto* Unique=FindUniqueData(InISID); const auto* Record=FindISIDData(InISID);
	const auto* Def=Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Unique || !Def || (Def->BottleMaterialType!=EBottleMaterialType::Plastic
		&& (!Unique->EventData.bManualFillPressure || Def->bIsBottleCap || Def->ConsumableProfile.NumOfPortions<=0))) return false;
	const float NewPressure=FMath::Min(Pressure,100.f);
	const bool Ended=Unique->ContinuousData.SealedPressure>0 && NewPressure==0;
	Unique->ContinuousData.SealedPressure=NewPressure;
	// One reliable final record repairs a lost last unreliable pressure sample.
	if (Ended) MarkInstanceDirty(InISID,EPropDirtyFlags::RuntimeData);
	// Continuous pressure has its own timestamped channel. Do not also queue a full
	// instance record for each integration step; late joins and saves read this cache.
	// LastUpdatedTime is also the thermal integration clock. Pressure must not consume
	// elapsed heating/cooling time. Only update this channel in the active canonical cache.
	if (auto* Active=GetActiveActorFromISID(InISID)) Active->LocalUniqueData.ContinuousData.SealedPressure=NewPressure;
	if (bPublish)
	{
		FPropContinuousSyncItem Item; Item.ISID=InISID;
		Item.Temperature=Unique->ContinuousData.Temperature; Item.SealedPressure=Unique->ContinuousData.SealedPressure;
		const double SnapshotTime=FMath::Max3(WorldElapsedSeconds,Unique->ContinuousData.LastUpdatedTime,Record->LastDiscreteEventTime);
		Multicast_SyncContinuousBatch(SnapshotTime,{Item});
	}
	return true;
}

bool AColaPropActor::UsesPlasticPressure() const
{
	const auto* Def=ResolveProductDefinition();
	return Def && Def->BottleMaterialType==EBottleMaterialType::Plastic;
}

float AColaPropActor::GetBottlePressure() const
{
	if (GetContentLevel()<=UE_SMALL_NUMBER) return 0;
	const auto* Content=ResolveContentDefinition();
	return FMath::Max(0.f,LocalUniqueData.ContinuousData.SealedPressure)
		+GetContentLevel()*FMath::Max(0.f,Content ? Content->PressureStrengthMultiplier : 1.f);
}

float AColaPropActor::GetBottlePressureTolerance() const
{
	const auto* Def=ResolveProductDefinition();
	return Def ? FMath::Max(UE_SMALL_NUMBER,Def->PressureFailureThreshold*GetStrengthRatio()) : TNumericLimits<float>::Max();
}

void AColaPropActor::AddAgitationPressure(float Amount)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || IsCarriedForDrinking() || !UsesPlasticPressure()
		|| !FMath::IsFinite(Amount) || Amount<=0 || IsEmpty() || !PropManagerReference) return;
	const auto* Def=ResolveProductDefinition();
	const float Pressure=LocalUniqueData.ContinuousData.SealedPressure+Amount/FMath::Max(.01f,Def->PressureResistanceMultiplier);
	// Active handling integrates locally every tick. The existing 5 Hz pressure channel
	// publishes the accumulated change, including the final decay, to remote peers.
	if (!PropManagerReference->SetBottleAgitationPressure(ISID,Pressure,false)) return;
	InteractionPressure=LocalUniqueData.ContinuousData.SealedPressure;
	FoamPressure=FMath::Clamp(InteractionPressure*5.f,0.f,10.f);
	EvaluateManufacturingPressureFailure();
}

bool AColaPropActor::HasTemporaryManufacturingSeal() const
{
	return PropManagerReference && GetCapSealed()<1 && (LocalUniqueData.EventData.bTemporarySealArmed
		|| LocalUniqueData.EventData.TemporarySealUntil>PropManagerReference->WorldElapsedSeconds);
}

bool AColaPropActor::StartPlasticPressureRelease(bool bTemporarySealFailure)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || IsCarriedForDrinking() || !UsesPlasticPressure() || IsEmpty() || !PropManagerReference || !RocketComp
		|| (!bTemporarySealFailure && (GetBottlePressure()<=GetBottlePressureTolerance() || GetCapSealed()<1))
		|| (bTemporarySealFailure && LocalUniqueData.ContinuousData.SealedPressure<=FMath::Max(0.f,SafeResidualPressure))
		|| (RocketComp->GetSimulationState()!=ERocketSimulationState::Idle && RocketComp->GetSimulationState()!=ERocketSimulationState::AtRest)
		|| !PropMesh || GetRootComponent()!=PropMesh || !PropMesh->GetStaticMesh()) return false;
	const bool WasHeld=bIsHeld;
	const float LaunchPressure=GetBottlePressure();
	if (bIsHeld && !PropManagerReference->ReleaseGrab(ISID,InteractionOwner.Get())) return false;
	const int32 AttachedCap=LocalUniqueData.EventData.AttachedCapISID;
	if (AttachedCap>0)
	{
		if (!PropManagerReference->DetachPhysicalCap(ISID,GetActorUpVector()*1500.f)) return false;
		RocketComp->IgnoreEjectedCap(PropManagerReference->GetActiveActorFromISID(AttachedCap));
	}
	else RemoveCap(1.f,ECapRemovalOperation::EjectPhysics);
	if (GetCapSealed()>0) return false;
	SetSeatedInFiller(false);
	if (LocalUniqueData.EventData.bTemporarySealArmed || LocalUniqueData.EventData.TemporarySealUntil>0)
	{
		FPropEventSyncDelta Seal; Seal.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal;
		Seal.bTemporarySealUsed=LocalUniqueData.EventData.bTemporarySealUsed; Seal.bManualFillPressure=LocalUniqueData.EventData.bManualFillPressure;
		PropManagerReference->SubmitInstanceEventUpdate(ISID,Seal);
	}
	const bool bLaunched=bUseOriginalRocketFlight ? LaunchOriginalBottleRocket(true) : RocketComp->StartPressurePhysicsFlight();
	if (!bLaunched) return false;
	UE_LOG(LogTemp,Verbose,TEXT("BOTTLE_PRESSURE_LAUNCH id=%d pressure=%.3f tolerance=%.3f held=%d original=%d level=%.3f position=%s"),ISID,LaunchPressure,GetBottlePressureTolerance(),WasHeld,bUseOriginalRocketFlight,GetContentLevel(),*GetActorLocation().ToString());
	PressureReleaseEpisode=FGuid::NewGuid();
	RocketInitialContentLevel=GetContentLevel(); RocketInitialPressure=LocalUniqueData.ContinuousData.SealedPressure;
	ForceNetUpdate(); return true;
}

void AColaPropActor::UpdateOriginalRocketDischarge(float DeltaSeconds)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !bUseOriginalRocketFlight || !PressureReleaseEpisode.IsValid()
		|| !PropManagerReference || !RocketComp || RocketComp->GetReplicatedFlightState().bPhysicsSimulating) return;
	// The planned low-fuel climb is still powered travel; retain its share of liquid until that stage completes.
	const float Progress=FMath::Clamp(RocketComp->GetTotalFlightDistanceTraveled()/FMath::Max(1.f,RocketComp->GetPoweredTravelDistance()),0.f,1.f);
	const float Target=RocketInitialContentLevel*(1-Progress);
	const double Portions=FMath::Max(0.f,GetContentLevel()-Target)*GetNumPortions();
	if (Portions>0) PropManagerReference->ReleaseBottleLiquidPortions(ISID,Portions,PressureReleaseEpisode);
	PropManagerReference->SetBottleAgitationPressure(ISID,FMath::Min(LocalUniqueData.ContinuousData.SealedPressure,RocketInitialPressure*(1-Progress)),false);
}

void AColaPropActor::UpdatePlasticPressure(float DeltaSeconds)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || IsCarriedForDrinking() || !PropManagerReference
		|| (!UsesPlasticPressure() && !LocalUniqueData.EventData.bManualFillPressure && !HasTemporaryManufacturingSeal())
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
	if (!IsManufacturingFailureActive()) bManufacturingFailureDispatched=false;
	// Let the station end its expired lease and atomically install its stocked seal before bottle policy runs.
	if (ManualFillingStation.IsValid() && ManualFillingStation->View.bLeverHeld
		&& ManualFillingStation->View.SeatedBottleISID==ISID && !ManualFillingStation->IsLeverHoldingBottle(ISID))
	{
		ManualFillingStation->EndManualLever();
		if (IsManufacturingFailureActive() || (RocketComp && RocketComp->IsPoweredFlight())) return;
	}
	bool Filling=IsManufacturingFlowActive();
	for (const auto& Session:PropManagerReference->FillSessions) if (Session.Value.TargetISID==ISID) { Filling=true; break; }
	const bool LeverHeld=ManualFillingStation.IsValid() && ManualFillingStation->IsLeverHoldingBottle(ISID);
	// Lost station/lease and restored saves count as release; an installed seal on an empty bottle stays armed.
	if (LocalUniqueData.EventData.bTemporarySealArmed && LocalUniqueData.EventData.bManualFillPressure && !IsEmpty() && !Filling && !LeverHeld)
		NotifyManufacturingLeverReleased();
	PressurePublishElapsed+=DeltaSeconds;
	float RemainingSeconds=DeltaSeconds;
	const double SealDeadline=LocalUniqueData.EventData.TemporarySealUntil;
	const bool SealExpired=SealDeadline>0 && PropManagerReference->WorldElapsedSeconds>=SealDeadline;
	if (SealExpired)
	{
		// A hitch may straddle the deadline: settle under the seal, then test at expiry before unprotected venting.
		const float ProtectedSeconds=float(FMath::Clamp(SealDeadline-(PropManagerReference->WorldElapsedSeconds-DeltaSeconds),0.,double(DeltaSeconds)));
		const float SealDecay=Filling ? 0.f : (LocalUniqueData.EventData.bManualFillPressure
			? (LeverHeld ? ManualFillVentRate : TemporarySealVentRate) : 0.f);
		const float BoundaryPressure=IsEmpty() ? 0.f : FMath::Max(0.f,LocalUniqueData.ContinuousData.SealedPressure-FMath::Max(0.f,SealDecay)*ProtectedSeconds);
		if (!PropManagerReference->SetBottleAgitationPressure(ISID,BoundaryPressure,true)) return;
		FPropEventSyncDelta Expired; Expired.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal;
		Expired.bTemporarySealUsed=LocalUniqueData.EventData.bTemporarySealUsed;
		Expired.bManualFillPressure=LocalUniqueData.EventData.bManualFillPressure;
		if (!PropManagerReference->SubmitInstanceEventUpdate(ISID,Expired)) return;
		if (EvaluateManufacturingPressureFailure()) return;
		RemainingSeconds-=ProtectedSeconds;
	}
	float Pressure=LocalUniqueData.ContinuousData.SealedPressure;
	const bool Jet=PressureReleaseEpisode.IsValid() && RocketComp && RocketComp->IsPoweredFlight();
	const bool HeldClosed=CapHoldingStation.IsValid() && CapHoldingStation->HasSufficientCapHold(ISID);
	if (Jet)
	{
		if (bUseOriginalRocketFlight)
		{
			// Distance-driven discharge runs at each projectile movement sub-step.
			// A fixed per-second drain can stop a fast flight with half a bottle left.
			UpdateOriginalRocketDischarge(DeltaSeconds);
			Pressure=LocalUniqueData.ContinuousData.SealedPressure;
		}
		else
		{
		if (GetContentLevel()>UE_SMALL_NUMBER && Pressure>UE_KINDA_SMALL_NUMBER)
		{
			const auto* Def=ResolveProductDefinition();
			const double Portions=FMath::Max(0.f,JetLiquidLevelPerSecond)*DeltaSeconds*Def->ConsumableProfile.NumOfPortions;
			if (Portions>0 && PropManagerReference->ReleaseBottleLiquidPortions(ISID,Portions,PressureReleaseEpisode)
				&& RocketComp->IsPressureThrustActive())
				RocketComp->ApplyPressureThrust(GetBottlePressure()*FMath::Max(0.f,JetAccelerationPerPressure));
			Pressure=FMath::Max(0.f,Pressure-FMath::Max(0.f,JetPressureVentRate)*DeltaSeconds);
		}
		}
		if (GetContentLevel()<=UE_SMALL_NUMBER || Pressure<=UE_KINDA_SMALL_NUMBER) RocketComp->FinishPressureThrust();
	}
	else
	{
		const float Vent=LocalUniqueData.EventData.bManualFillPressure ? ManualFillVentRate : AtmosphereVentRate;
		// The nozzle keeps a full bottle restrained while the operator vents it.
		// A purchased temporary seal retains liquid but only bleeds pressure slowly.
		const float Decay=Filling ? 0.f : (HasTemporaryManufacturingSeal()
			? (LocalUniqueData.EventData.bManualFillPressure ? (LeverHeld ? ManualFillVentRate : TemporarySealVentRate) : 0.f)
			: (GetCapSealed()>=1 || HeldClosed ? PressureDecayRate : Vent));
		Pressure=IsEmpty() ? 0.f : FMath::Max(0.f,Pressure-FMath::Max(0.f,Decay)*RemainingSeconds);
	}
	const bool Publish=(PressurePublishElapsed>=.2f && Pressure!=LastPublishedPressure)
		|| (PressurePublishElapsed>=1.f && Pressure>0);
	if (Pressure!=LocalUniqueData.ContinuousData.SealedPressure || Publish)
		PropManagerReference->SetBottleAgitationPressure(ISID,Pressure,Publish);
	if (Publish) { PressurePublishElapsed=0; LastPublishedPressure=Pressure; }
	InteractionPressure=Pressure; FoamPressure=FMath::Clamp(Pressure*5.f,0.f,10.f);
	if (!Jet && !Filling) EvaluateManufacturingPressureFailure();
}

float AColaPropActor::GetHeldMovementShakeMultiplier() const
{
	const AActor* AttachedTo = GetAttachParentActor();
	// A grab reserves the held state before the bottle reaches the hand.
	if ((!bIsHeld && State != EPropState::Held) || !IsValid(AttachedTo)) return 1.f;
	const AActor* Holder = IsValid(InteractionOwner.Get()) ? InteractionOwner.Get() : AttachedTo;
	return UPlayerUpgradesComponent::GetCarryShakeMultiplierFor(Holder);
}

void AColaPropActor::UpdateMotionPressure(float DeltaSeconds)
{
	if (!HasAuthority() || IsCarriedForDrinking() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0 || IsEmpty() || IsSeatedInFiller()
		|| (RocketComp && RocketComp->IsPoweredFlight()) || LinearVelocity.ContainsNaN() || AngularVelocity.ContainsNaN())
	{
		bHasAgitationHistory = false;
		return;
	}
	if (!bHasAgitationHistory || DeltaSeconds > .25f)
	{
		PreviousAgitationVelocity = LinearVelocity;
		FilteredAgitationAcceleration = FVector::ZeroVector;
		bHasAgitationHistory = true;
		return;
	}
	const FVector Acceleration = ((LinearVelocity-PreviousAgitationVelocity)/DeltaSeconds).GetClampedToMaxSize(5000.f);
	FilteredAgitationAcceleration = FMath::Lerp(FilteredAgitationAcceleration,Acceleration,1.f-FMath::Exp(-DeltaSeconds/.08f));
	PreviousAgitationVelocity = LinearVelocity;
	const float LinearRate = FMath::Max(0.f,float(FilteredAgitationAcceleration.Size())-AccelerationAgitationThreshold)/980.f * FMath::Max(0.f,AccelerationPressureGain);
	const float RotationRate = FMath::Max(0.f,float(AngularVelocity.Size())-AngularAgitationThreshold)/FMath::Max(1.f,AngularAgitationThreshold)*FMath::Max(0.f,AngularPressureRate);
	const float RunningRate = bIsHeld ? FMath::Max(0.f,float(LinearVelocity.Size())-RunningSpeedThreshold)/FMath::Max(1.f,RunningSpeedThreshold)*FMath::Max(0.f,RunningPressureRate) : 0.f;
	const float MovementShakeRate = FMath::Min(8.f,LinearRate+RotationRate+RunningRate);
	const float EffectiveShakeRate = MovementShakeRate * GetHeldMovementShakeMultiplier();
	AddAgitationPressure(EffectiveShakeRate * FMath::Min(DeltaSeconds,.1f));
}
