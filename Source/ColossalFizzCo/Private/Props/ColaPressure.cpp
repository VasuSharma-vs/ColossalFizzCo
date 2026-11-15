#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Shop/ShopProductionStationComponent.h"

bool APropManager::SetBottleAgitationPressure(int32 InISID,float Pressure,bool bPublish)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(Pressure) || Pressure<0) return false;
	auto* Unique=FindUniqueData(InISID); const auto* Record=FindISIDData(InISID);
	const auto* Def=Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Unique || !Def || Def->BottleMaterialType!=EBottleMaterialType::Plastic) return false;
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
	if (!HasAuthority() || GetNetMode()==NM_Client || !UsesPlasticPressure() || !FMath::IsFinite(Amount) || Amount<=0 || IsEmpty() || !PropManagerReference) return;
	const auto* Def=ResolveProductDefinition();
	const float Pressure=LocalUniqueData.ContinuousData.SealedPressure+Amount/FMath::Max(.01f,Def->PressureResistanceMultiplier);
	// Active handling integrates locally every tick. The existing 5 Hz pressure channel
	// publishes the accumulated change, including the final decay, to remote peers.
	if (!PropManagerReference->SetBottleAgitationPressure(ISID,Pressure,false)) return;
	InteractionPressure=LocalUniqueData.ContinuousData.SealedPressure;
	FoamPressure=FMath::Clamp(InteractionPressure*5.f,0.f,10.f);
	const bool HeldClosed=CapHoldingStation.IsValid() && CapHoldingStation->HasSufficientCapHold(ISID);
	if (GetCapSealed()>=1 && !HeldClosed && GetBottlePressure()>GetBottlePressureTolerance()) StartPlasticPressureRelease();
}

bool AColaPropActor::HasTemporaryManufacturingSeal() const
{
	return PropManagerReference && LocalUniqueData.EventData.TemporarySealUntil>PropManagerReference->WorldElapsedSeconds && GetCapSealed()<1;
}

bool AColaPropActor::StartPlasticPressureRelease(bool bTemporarySealFailure)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !UsesPlasticPressure() || IsEmpty() || !PropManagerReference || !RocketComp
		|| (!bTemporarySealFailure && (GetBottlePressure()<=GetBottlePressureTolerance() || GetCapSealed()<1))
		|| (bTemporarySealFailure && LocalUniqueData.ContinuousData.SealedPressure<=.1f)
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
	const float Progress=FMath::Clamp(RocketComp->GetTotalFlightDistanceTraveled()/FMath::Max(1.f,RocketLaunchConfiguration.Distance),0.f,1.f);
	const float Target=RocketInitialContentLevel*(1-Progress);
	const double Portions=FMath::Max(0.f,GetContentLevel()-Target)*GetNumPortions();
	if (Portions>0) PropManagerReference->ReleaseBottleLiquidPortions(ISID,Portions,PressureReleaseEpisode);
	PropManagerReference->SetBottleAgitationPressure(ISID,FMath::Min(LocalUniqueData.ContinuousData.SealedPressure,RocketInitialPressure*(1-Progress)),false);
}

void AColaPropActor::UpdatePlasticPressure(float DeltaSeconds)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !UsesPlasticPressure() || !PropManagerReference
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
	if (LocalUniqueData.EventData.TemporarySealUntil>0 && PropManagerReference->WorldElapsedSeconds>=LocalUniqueData.EventData.TemporarySealUntil)
	{
		FPropEventSyncDelta Expired; Expired.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal;
		Expired.bTemporarySealUsed=LocalUniqueData.EventData.bTemporarySealUsed;
		Expired.bManualFillPressure=LocalUniqueData.EventData.bManualFillPressure;
		PropManagerReference->SubmitInstanceEventUpdate(ISID,Expired);
		if (GetCapSealed()<1 && LocalUniqueData.ContinuousData.SealedPressure>.1f) StartPlasticPressureRelease(true);
	}
	PressurePublishElapsed+=DeltaSeconds;
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
		bool Filling=false;
		for (const auto& Session:PropManagerReference->FillSessions) if (Session.Value.TargetISID==ISID) { Filling=true; break; }
		const float Vent=LocalUniqueData.EventData.bManualFillPressure ? ManualFillVentRate : AtmosphereVentRate;
		const float Decay=HasTemporaryManufacturingSeal() || Filling ? 0.f : (GetCapSealed()>=1 || HeldClosed ? PressureDecayRate : Vent);
		Pressure=IsEmpty() ? 0.f : FMath::Max(0.f,Pressure-FMath::Max(0.f,Decay)*DeltaSeconds);
	}
	const bool Publish=(PressurePublishElapsed>=.2f && Pressure!=LastPublishedPressure)
		|| (PressurePublishElapsed>=1.f && Pressure>0);
	if (Pressure!=LocalUniqueData.ContinuousData.SealedPressure || Publish)
		PropManagerReference->SetBottleAgitationPressure(ISID,Pressure,Publish);
	if (Publish) { PressurePublishElapsed=0; LastPublishedPressure=Pressure; }
	InteractionPressure=Pressure; FoamPressure=FMath::Clamp(Pressure*5.f,0.f,10.f);
	if (!Jet && !HeldClosed && GetCapSealed()>=1 && GetBottlePressure()>GetBottlePressureTolerance()) StartPlasticPressureRelease();
}

void AColaPropActor::UpdateMotionPressure(float DeltaSeconds)
{
	if (!HasAuthority() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0 || IsEmpty() || IsSeatedInFiller()
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
	AddAgitationPressure(FMath::Min(8.f,LinearRate+RotationRate+RunningRate)*FMath::Min(DeltaSeconds,.1f));
}
