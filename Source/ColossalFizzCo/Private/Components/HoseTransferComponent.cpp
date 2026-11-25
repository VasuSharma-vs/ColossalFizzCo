#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Props/InteractablePropManager.h"
#include "Resources/HoseCableActor.h"
#include "Net/UnrealNetwork.h"
#include <cmath>
#include <limits>

namespace
{
	// Prefer a transfer that both float inventories can represent exactly.
	float MoveLocalResourceUnits(float& Source,float& Target,float Capacity,float Requested)
	{
		const double Limit=FMath::Min3(double(Requested),double(Source),FMath::Max(0.,double(Capacity)-Target));
		if (!FMath::IsFinite(Limit) || Limit<=0) return 0;
		const float ProjectedTarget=float(double(Target)+Limit);
		const float NextTarget=std::nextafter(ProjectedTarget,std::numeric_limits<float>::infinity());
		const double SourceQuantum=double(Source)-std::nextafter(Source,0.f);
		const double TargetQuantum=FMath::IsFinite(NextTarget) ? double(NextTarget)-ProjectedTarget
			: double(ProjectedTarget)-std::nextafter(ProjectedTarget,0.f);
		const double Quantum=FMath::Max(SourceQuantum,TargetQuantum);
		if (Quantum<=0) return 0;
		const double Amount=std::floor(Limit/Quantum)*Quantum;
		float NewSource=float(double(Source)-Amount),NewTarget=float(double(Target)+Amount);
		double Debit=double(Source)-NewSource,Credit=double(NewTarget)-Target;
		if (Debit!=Credit || Debit>Limit || NewTarget>Capacity)
		{
			// Across a float spacing boundary there may be no shared delta.
			// Round the debit within the request and the credit within the debit.
			const double SourceLimit=double(Source)-Limit;
			NewSource=float(SourceLimit);
			if (NewSource<SourceLimit) NewSource=std::nextafter(NewSource,Source);
			Debit=double(Source)-NewSource;
			const double TargetLimit=double(Target)+Debit;
			NewTarget=float(TargetLimit);
			if (NewTarget>TargetLimit) NewTarget=std::nextafter(NewTarget,Target);
			Credit=double(NewTarget)-Target;
		}
		if (Credit<=0 || Credit>Debit || Debit>Limit || NewTarget>Capacity) return 0;
		Source=NewSource; Target=NewTarget;
		return float(Credit);
	}
}

UHoseTransferComponent::UHoseTransferComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick=true;
}

void UHoseTransferComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHoseTransferComponent,Category); DOREPLIFETIME(UHoseTransferComponent,ResourceID);
	DOREPLIFETIME(UHoseTransferComponent,MaxUnitsPerSecond); DOREPLIFETIME(UHoseTransferComponent,bTransferEnabled);
	DOREPLIFETIME(UHoseTransferComponent,bIntakeEnabled); DOREPLIFETIME(UHoseTransferComponent,bDrainEnabled);
	DOREPLIFETIME(UHoseTransferComponent,BufferCapacityUnits); DOREPLIFETIME(UHoseTransferComponent,Buffer);
	DOREPLIFETIME(UHoseTransferComponent,bRequiresMotorDrive); DOREPLIFETIME(UHoseTransferComponent,RatedPressure);
	DOREPLIFETIME(UHoseTransferComponent,FlowBoostPerPressure); DOREPLIFETIME(UHoseTransferComponent,MaxDrivenUnitsPerSecond);
	DOREPLIFETIME(UHoseTransferComponent,BurstPressureThreshold); DOREPLIFETIME(UHoseTransferComponent,BurstDelay);
	DOREPLIFETIME(UHoseTransferComponent,VacuumCollapseThreshold); DOREPLIFETIME(UHoseTransferComponent,FailureMode);
	DOREPLIFETIME(UHoseTransferComponent,BlockedPressureRisePerSecond); DOREPLIFETIME(UHoseTransferComponent,CurrentPressure);
	DOREPLIFETIME(UHoseTransferComponent,CurrentFlowRate); DOREPLIFETIME(UHoseTransferComponent,bBurst);
	DOREPLIFETIME(UHoseTransferComponent,TotalReleasedUnits); DOREPLIFETIME(UHoseTransferComponent,SourceEndpoint);
	DOREPLIFETIME(UHoseTransferComponent,Discharge);
	DOREPLIFETIME(UHoseTransferComponent,TargetEndpoint); DOREPLIFETIME(UHoseTransferComponent,LastTransferredUnits);
	DOREPLIFETIME(UHoseTransferComponent,LastIntakeUnits); DOREPLIFETIME(UHoseTransferComponent,BufferManager);
	DOREPLIFETIME(UHoseTransferComponent,LastTransferUpdateSeconds);
	DOREPLIFETIME(UHoseTransferComponent,bBufferUsesManagedContent);
	DOREPLIFETIME(UHoseTransferComponent,LastStartPortFlowUnits); DOREPLIFETIME(UHoseTransferComponent,LastEndPortFlowUnits);
}

bool UHoseTransferComponent::HasValidBuffer() const
{
	return FMath::IsFinite(BufferCapacityUnits) && BufferCapacityUnits>=0 && FMath::IsFinite(Buffer.Units) && Buffer.Units>=0
		&& (Buffer.Units==0 || (Buffer.Category==Category && Buffer.ResourceID==ResourceID
			&& FMath::IsFinite(Buffer.ContentTemperature) && FMath::IsFinite(Buffer.ContentCreationTime) && Buffer.ContentCreationTime>=0
			&& ((!bBufferUsesManagedContent && !BufferManager) || (IsValid(BufferManager) && BufferManager->GetWorld()==GetWorld()))));
}

bool UHoseTransferComponent::CanConnect(const UResourceTransferEndpointComponent* Source,const UResourceTransferEndpointComponent* Target) const
{
	if (!GetWorld() || !HasValidBuffer() || bBurst || !IsValid(Source) || !IsValid(Target) || Source==Target
		|| Source->GetWorld()!=GetWorld() || Target->GetWorld()!=GetWorld()
		|| !Source->CanAcceptHose(this) || !Target->CanAcceptHose(this)
		|| !Source->SupportsResource(Category,ResourceID) || !Target->SupportsResource(Category,ResourceID)) return false;
	const bool Flexible=Category==EResourceTransportCategory::Content
		&& (Source->bUseSignedPressure || Target->bUseSignedPressure || GetOwner()->IsA<AHoseCableActor>());
	if ((Source->bUseFixedHoseDirection && !Source->bCanSupply) || (Target->bUseFixedHoseDirection && !Target->bCanReceive)) return false;
	if (Flexible ? (!Source->bCanSupply && !Source->bCanReceive) || (!Target->bCanSupply && !Target->bCanReceive)
		: !Source->bCanSupply || !Target->bCanReceive) return false;
	if (Source->bManagedContent!=Target->bManagedContent
		&& (Category!=EResourceTransportCategory::Content || (!Source->bAllowManagedContentBridge && !Target->bAllowManagedContentBridge))) return false;
	APropManager* ConnectionManager=Source->bManagedContent ? Source->ContentManager.Get() : Target->ContentManager.Get();
	if (Buffer.Units>0 && bBufferUsesManagedContent && BufferManager!=ConnectionManager) return false;
	return !Source->bManagedContent || !Target->bManagedContent
		|| (Source->ContentManager==Target->ContentManager && Source->ContentISID!=Target->ContentISID);
}

bool UHoseTransferComponent::HasValidConnection() const { return CanConnect(SourceEndpoint,TargetEndpoint); }

UResourceTransferEndpointComponent* UHoseTransferComponent::GetOtherEndpoint(const UResourceTransferEndpointComponent* Port) const
{
	if (!IsValid(Port)) return nullptr;
	auto* Other=Port==SourceEndpoint ? TargetEndpoint.Get() : Port==TargetEndpoint ? SourceEndpoint.Get() : nullptr;
	return IsValid(Other) ? Other : nullptr;
}

bool UHoseTransferComponent::IsRequestingContent() const
{
	return IsRequestingContentFrom(SourceEndpoint) || (Category==EResourceTransportCategory::Content && IsRequestingContentFrom(TargetEndpoint));
}

bool UHoseTransferComponent::IsRequestingContentFrom(const UResourceTransferEndpointComponent* Port) const
{
	const auto* Receiver=GetOtherEndpoint(Port);
	if (!bTransferEnabled || !bIntakeEnabled || !HasValidConnection() || !IsValid(Receiver) || !Receiver->bRequestsFlow) return false;
	if (Category!=EResourceTransportCategory::Content) return Port==SourceEndpoint;
	const float Pressure=Port->GetOperatingPressure();
	return bDrainEnabled && Port->bCanSupply && Receiver->bCanReceive && Pressure>=0.f && Pressure>=Receiver->GetOperatingPressure();
}

bool UHoseTransferComponent::ConnectEndpoints(UResourceTransferEndpointComponent* Source,UResourceTransferEndpointComponent* Target)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bTransferInProgress || !CanConnect(Source,Target)
		|| !Source->GetOwner()->HasAuthority() || !Target->GetOwner()->HasAuthority()) return false;
	{
		TGuardValue<bool> Guard(bTransferInProgress,true);
		const bool Flexible=Category==EResourceTransportCategory::Content
			&& (Source->bUseSignedPressure || Target->bUseSignedPressure || GetOwner()->IsA<AHoseCableActor>());
		auto CanAdmit = [Flexible](UResourceTransferEndpointComponent* Port,bool bNominalSource)
		{
			if (!IsValid(Port)) return false;
			if ((Flexible ? Port->bCanSupply : bNominalSource) && Port->CanTransferResource(true)) return true;
			return IsValid(Port) && (Flexible ? Port->bCanReceive : !bNominalSource)
				&& ((Port->bManagedReceivingProxy && Port->ContentISID==0) || Port->CanTransferResource(false));
		};
		if (!CanAdmit(Source,true) || !CanAdmit(Target,false) || !CanConnect(Source,Target)) return false;
	}
	UnbindEndpoints(); SourceEndpoint=Source; TargetEndpoint=Target;
	Source->ConnectedHoses.AddUnique(this); Target->ConnectedHoses.AddUnique(this);
	if (Buffer.Units==0)
	{
		Buffer=FHoseResourceBuffer(); Buffer.Category=Category; Buffer.ResourceID=ResourceID;
		bBufferUsesManagedContent=false; BufferManager=nullptr;
	}
	FailureMode=PendingBodyFailure=EHoseFailureMode::None;
	BackPressure=OverpressureSeconds=0.f; CurrentPressure=0.f;
	TrappedDrainPressure=0.f; LastUnpluggedTip=EHoseDischargePoint::None;
	Discharge=FHoseDischargeState(); DischargeEpisodes.Reset(); EpisodePoint=EHoseDischargePoint::None;
	GetOwner()->ForceNetUpdate(); return true;
}

void UHoseTransferComponent::UnbindEndpoints()
{
	if (IsValid(SourceEndpoint)) SourceEndpoint->ConnectedHoses.Remove(this);
	if (IsValid(TargetEndpoint)) TargetEndpoint->ConnectedHoses.Remove(this);
	SourceEndpoint=nullptr; TargetEndpoint=nullptr;
}

void UHoseTransferComponent::DisconnectEndpoints()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bTransferInProgress) return;
	UnbindEndpoints(); LastTransferredUnits=0; LastIntakeUnits=0; CurrentFlowRate=0;
	LastStartPortFlowUnits=LastEndPortFlowUnits=0;
	Discharge=FHoseDischargeState(); DischargeEpisodes.Reset(); EpisodePoint=EHoseDischargePoint::None;
	if (!bBurst) CurrentPressure=0;
	BackPressure=OverpressureSeconds=0; PendingBodyFailure=EHoseFailureMode::None;
	LastTransferUpdateSeconds=0;
	GetOwner()->ForceNetUpdate();
}

bool UHoseTransferComponent::GetContentEndpoints(UResourceTransferEndpointComponent*& First,UResourceTransferEndpointComponent*& Second) const
{
	First=nullptr; Second=nullptr;
	if (HasValidConnection())
	{
		First=SourceEndpoint; Second=TargetEndpoint;
		return First->GetOwner()->HasAuthority() && Second->GetOwner()->HasAuthority();
	}
	const auto* Hose=Cast<AHoseCableActor>(GetOwner());
	if (!Hose || !HasValidBuffer()) return false;
	if (!bBurst && (SourceEndpoint || TargetEndpoint || (IsValid(Hose->StartPort) && IsValid(Hose->EndPort)))) return false;
	const auto CanUsePort = [this](const UResourceTransferEndpointComponent* Port)
	{
		return IsValid(Port) && Port->SupportsResource(Category,ResourceID) && Port->GetOwner()->HasAuthority() && Port->CanAcceptHose(this);
	};
	if (CanUsePort(Hose->StartPort)) First=Hose->StartPort;
	if (CanUsePort(Hose->EndPort)) Second=Hose->EndPort;
	return First || Second || (Buffer.Units>0.f && (bDrainEnabled || bBurst));
}

void UHoseTransferComponent::CaptureUnpluggedPressure(EHoseDischargePoint Point)
{
	if (Category!=EResourceTransportCategory::Content || bBurst) return;
	LastUnpluggedTip=Point;
	if (Buffer.Units<=0.f) TrappedDrainPressure=0.f;
	else if (FMath::IsFinite(CurrentPressure)) TrappedDrainPressure=FMath::Max(TrappedDrainPressure,CurrentPressure);
}

EHoseDischargePoint UHoseTransferComponent::FindOpenTip() const
{
	const auto* Hose=Cast<AHoseCableActor>(GetOwner());
	if (!Hose || !bDrainEnabled || bBurst || (IsValid(SourceEndpoint) && IsValid(TargetEndpoint))) return EHoseDischargePoint::None;
	const bool SupplyMounted=IsValid(Hose->StartPort);
	const bool ReceiverMounted=IsValid(Hose->EndPort);
	if (SupplyMounted && ReceiverMounted) return EHoseDischargePoint::None;
	if (SupplyMounted || ReceiverMounted)
	{
		const int32 LooseConnector=SupplyMounted ? Hose->GetReceivingConnectorIndex() : Hose->GetSupplyConnectorIndex();
		return LooseConnector==0 ? EHoseDischargePoint::StartTip : EHoseDischargePoint::EndTip;
	}
	if (LastUnpluggedTip!=EHoseDischargePoint::None) return LastUnpluggedTip;
	FTransform Start,End;
	const bool StartLower=Hose->GetDischargeTransform(EHoseDischargePoint::StartTip,Start)
		&& Hose->GetDischargeTransform(EHoseDischargePoint::EndTip,End) && Start.GetLocation().Z<=End.GetLocation().Z;
	return StartLower ? EHoseDischargePoint::StartTip : EHoseDischargePoint::EndTip;
}

float UHoseTransferComponent::GetLastPortFlowUnits(const UResourceTransferEndpointComponent* Port) const
{
	if (!IsValid(Port)) return 0.f;
	if (Category!=EResourceTransportCategory::Content)
		return Port==SourceEndpoint ? LastIntakeUnits : Port==TargetEndpoint ? -LastTransferredUnits : 0.f;
	if (const auto* Hose=Cast<AHoseCableActor>(GetOwner()))
	{
		if (Hose->StartPort==Port) return LastStartPortFlowUnits;
		if (Hose->EndPort==Port) return LastEndPortFlowUnits;
	}
	return Port==SourceEndpoint ? LastStartPortFlowUnits : Port==TargetEndpoint ? LastEndPortFlowUnits : 0.f;
}

float UHoseTransferComponent::TransferForDuration(float DeltaSeconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bTransferInProgress) return 0;
	LastTransferredUnits=0; LastIntakeUnits=0; CurrentFlowRate=0;
	LastStartPortFlowUnits=LastEndPortFlowUnits=0;
	LastTransferUpdateSeconds=0;
	Discharge=FHoseDischargeState(); DischargedDuringUpdate=0.f;
	const bool bContent=Category==EResourceTransportCategory::Content;
	if (bBurst && (!bContent || !GetOwner()->IsA<AHoseCableActor>())) return 0;
	auto CanAdvance = [this,bContent]()
	{
		if (!bContent) return HasValidConnection() && SourceEndpoint->GetOwner()->HasAuthority() && TargetEndpoint->GetOwner()->HasAuthority();
		UResourceTransferEndpointComponent* First=nullptr;
		UResourceTransferEndpointComponent* Second=nullptr;
		return GetContentEndpoints(First,Second);
	};
	if (!CanAdvance()) { DisconnectEndpoints(); return 0; }
	if (!bTransferEnabled || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0 || !FMath::IsFinite(MaxUnitsPerSecond) || MaxUnitsPerSecond<=0) return 0;
	TGuardValue<bool> Guard(bTransferInProgress,true);
	if (!FMath::IsFinite(MaxTransferStepSeconds) || MaxTransferStepSeconds<=0) return 0;
	float StepLimit=FMath::Clamp(MaxTransferStepSeconds,.001f,.1f);
	if (BufferCapacityUnits>0 && FMath::IsFinite(MaxDrivenUnitsPerSecond) && MaxDrivenUnitsPerSecond>0)
		StepLimit=FMath::Min(StepLimit,FMath::Max(UE_SMALL_NUMBER,BufferCapacityUnits/MaxDrivenUnitsPerSecond));
	double Remaining=DeltaSeconds;
	float TotalDrained=0,TotalIntake=0,TotalStartFlow=0,TotalEndFlow=0;
	const int32 StepBudget=FMath::Clamp(MaxTransferSubsteps,1,1024);
	int32 Steps=0;
	for (;Steps<StepBudget && Remaining>UE_SMALL_NUMBER && bTransferEnabled && CanAdvance();++Steps)
	{
		const float Step=float(FMath::Min(Remaining,double(StepLimit)));
		const float Consumed=bContent ? IntegrateContentStep(Step) : IntegrateTransferStep(Step);
		TotalDrained+=LastTransferredUnits; TotalIntake+=LastIntakeUnits;
		TotalStartFlow+=LastStartPortFlowUnits; TotalEndFlow+=LastEndPortFlowUnits;
		if (Consumed<=0) break;
		Remaining=FMath::Max(0.,Remaining-Consumed);
	}
	if (Steps>=StepBudget && Remaining>0 && bTransferEnabled && CanAdvance()) DroppedTransferTime+=Remaining;
	LastTransferredUnits=TotalDrained; LastIntakeUnits=TotalIntake; LastTransferUpdateSeconds=DeltaSeconds;
	LastStartPortFlowUnits=TotalStartFlow; LastEndPortFlowUnits=TotalEndFlow;
	if (bContent) CurrentFlowRate=TotalDrained/DeltaSeconds;
	Discharge.UnitsPerSecond=DischargedDuringUpdate/DeltaSeconds;
	if (DischargedDuringUpdate<=0.f) { DischargeEpisodes.Reset(); EpisodePoint=EHoseDischargePoint::None; }
	if (IsValid(GetOwner())) GetOwner()->ForceNetUpdate();
	return TotalDrained;
}

float UHoseTransferComponent::IntegrateTransferStep(float DeltaSeconds)
{
	LastTransferredUnits=0; LastIntakeUnits=0;
	if (!TargetEndpoint->bRequestsFlow) { CurrentPressure=0; CurrentFlowRate=0; OverpressureSeconds=0; return 0; }
	const bool SupplyAllowed=SourceEndpoint->CanTransferResource(true);
	const bool ReceiveAllowed=IsValid(TargetEndpoint) && TargetEndpoint->CanTransferResource(false);
	if (!HasValidConnection() || !GetOwner()->HasAuthority() || !SourceEndpoint->GetOwner()->HasAuthority()
		|| !TargetEndpoint->GetOwner()->HasAuthority()) { UnbindEndpoints(); return 0; }
	if (!TargetEndpoint->bRequestsFlow) { CurrentPressure=0; CurrentFlowRate=0; OverpressureSeconds=0; return 0; }
	if (bRequiresMotorDrive && !SourceEndpoint->IsMotorRunning() && !TargetEndpoint->IsMotorRunning()) { CurrentPressure=0; OverpressureSeconds=0; return 0; }
	const bool SupplyEnabled=bIntakeEnabled && SupplyAllowed && SourceEndpoint->bSupplyEnabled;
	const float SourcePressure=SourceEndpoint->bSupplyEnabled ? SourceEndpoint->GetEffectivePushPressure() : 0.f;
	const float TargetPressure=TargetEndpoint->GetEffectivePullPressure();
	if (!FMath::IsFinite(SourcePressure) || SourcePressure<0 || !FMath::IsFinite(TargetPressure) || TargetPressure<0
		|| !FMath::IsFinite(RatedPressure) || RatedPressure<0 || !FMath::IsFinite(FlowBoostPerPressure) || FlowBoostPerPressure<0
		|| !FMath::IsFinite(MaxDrivenUnitsPerSecond) || MaxDrivenUnitsPerSecond<0 || !FMath::IsFinite(BurstPressureThreshold) || BurstPressureThreshold<0
		|| !FMath::IsFinite(BurstDelay) || BurstDelay<0 || !FMath::IsFinite(BlockedPressureRisePerSecond) || BlockedPressureRisePerSecond<0
		|| !FMath::IsFinite(SourceEndpoint->MaxSupplyUnitsPerSecond) || SourceEndpoint->MaxSupplyUnitsPerSecond<0) return 0;
	const float DrivePressure=SourcePressure+TargetPressure;
	const double Rate=FMath::Min(double(MaxDrivenUnitsPerSecond),double(MaxUnitsPerSecond)*(1.+FMath::Max(0.f,DrivePressure-RatedPressure)*FlowBoostPerPressure));
	const double SupplyRate=SourceEndpoint->MaxSupplyUnitsPerSecond>0 ? FMath::Min(Rate,double(SourceEndpoint->MaxSupplyUnitsPerSecond)) : Rate;
	const bool BlockedAtStart=Buffer.Units>=BufferCapacityUnits && (!bDrainEnabled || !ReceiveAllowed || TargetEndpoint->GetFreeCapacityUnits()<=0);
	const bool PushingAtStart=SupplyEnabled && SourcePressure>0 && SourceEndpoint->GetStoredUnits()>0;
	const float PressureRise=BlockedAtStart && PushingAtStart ? SourcePressure*BlockedPressureRisePerSecond : 0.f;
	const float InitialPressure=PressureRise>0 ? FMath::Max(CurrentPressure,DrivePressure) : DrivePressure;
	// Stop at an overload deadline; no remaining frame time may pump through the ruptured hose.
	float TimeUntilBurst=MAX_flt;
	if (InitialPressure>BurstPressureThreshold) TimeUntilBurst=FMath::Max(0.f,BurstDelay-OverpressureSeconds);
	else if (PressureRise>0) TimeUntilBurst=(BurstPressureThreshold-InitialPressure)/PressureRise+BurstDelay;
	const bool ReachesBurst=TimeUntilBurst<=DeltaSeconds;
	DeltaSeconds=FMath::Min(DeltaSeconds,TimeUntilBurst);
	const double Budget=Rate*DeltaSeconds;
	if (!FMath::IsFinite(Budget) || Budget<0 || (!ReachesBurst && Budget<=0)) return 0;
	CurrentFlowRate=float(Rate);
	const float InitialRoom=FMath::Max(0.f,BufferCapacityUnits-Buffer.Units);
	const float PotentialIntake=SupplyEnabled ? float(FMath::Min3(SupplyRate*DeltaSeconds,double(SourceEndpoint->GetStoredUnits()),double(InitialRoom))) : 0.f;
	const float PotentialDrain=bDrainEnabled && ReceiveAllowed ? float(FMath::Min3(Budget,double(Buffer.Units+PotentialIntake),double(TargetEndpoint->GetFreeCapacityUnits()))) : 0.f;
	const float OfferedRate=DeltaSeconds>0 ? PotentialDrain/DeltaSeconds : 0.f;
	if ((Buffer.Units>0 || PotentialIntake>0) && TargetEndpoint->IsIntakeOverloaded(InitialPressure,OfferedRate))
	{
		UResourceTransferEndpointComponent* PoppedSocket=TargetEndpoint;
		UnbindEndpoints(); CurrentPressure=InitialPressure; CurrentFlowRate=0; OverpressureSeconds=0;
		PoppedSocket->NotifyIntakeOverload(this,InitialPressure,OfferedRate);
		return 0;
	}
	LastIntakeUnits=IntakeFrom(SourceEndpoint,PotentialIntake);
	// Canonical managed updates may invoke gameplay events; never use a connection invalidated by those events.
	if (!HasValidConnection()) { UnbindEndpoints(); return 0; }
	if (bDrainEnabled && ReceiveAllowed && IsValid(TargetEndpoint))
	{
		const float Drain=float(FMath::Min3(Budget,double(Buffer.Units),double(TargetEndpoint->GetFreeCapacityUnits())));
		LastTransferredUnits=DeliverTo(TargetEndpoint,Drain,InitialPressure,DeltaSeconds);
	}
	if (!HasValidConnection()) { UnbindEndpoints(); return DeltaSeconds; }
	const bool Blocked=Buffer.Units>=BufferCapacityUnits && (!bDrainEnabled || !ReceiveAllowed || LastTransferredUnits<=0);
	const bool Pushing=SupplyEnabled && SourcePressure>0 && IsValid(SourceEndpoint) && SourceEndpoint->GetStoredUnits()>0;
	const float BlockedSeconds=BlockedAtStart ? DeltaSeconds : FMath::Max(0.f,DeltaSeconds-float(InitialRoom/FMath::Max(SupplyRate,double(UE_SMALL_NUMBER))));
	CurrentPressure=Blocked && Pushing ? InitialPressure+SourcePressure*BlockedPressureRisePerSecond*BlockedSeconds : DrivePressure;
	if (!FMath::IsFinite(CurrentPressure)) CurrentPressure=MAX_flt;
	if (InitialPressure>BurstPressureThreshold) OverpressureSeconds+=DeltaSeconds;
	else if (CurrentPressure>BurstPressureThreshold && SourcePressure*BlockedPressureRisePerSecond>0)
		OverpressureSeconds=FMath::Min(DeltaSeconds,(CurrentPressure-BurstPressureThreshold)/(SourcePressure*BlockedPressureRisePerSecond));
	else OverpressureSeconds=0;
	if (ReachesBurst || (CurrentPressure>BurstPressureThreshold && OverpressureSeconds>=BurstDelay))
		Rupture(EHoseFailureMode::OverpressureBurst);
	return DeltaSeconds;
}

float UHoseTransferComponent::IntakeFrom(UResourceTransferEndpointComponent* Port,float Units)
{
	if (!IsValid(Port) || !FMath::IsFinite(Units) || Units<=0) return 0.f;
	if (Buffer.Units<=0.f)
	{
		Buffer=FHoseResourceBuffer(); Buffer.Category=Category; Buffer.ResourceID=ResourceID;
		bBufferUsesManagedContent=Port->bManagedContent;
		BufferManager=Port->bManagedContent ? Port->ContentManager : nullptr;
	}
	// Preserve the managed inventory reference while liquid from that source remains in the line.
	if (Port->bManagedContent!=bBufferUsesManagedContent || (Port->bManagedContent && Port->ContentManager!=BufferManager)) return 0.f;
	float Accepted=0.f;
	if (Port->bManagedContent)
		Accepted=Port->ContentManager->IntakeHoseContent(Port->ContentISID,Buffer,BufferCapacityUnits,Units);
	else
	{
		auto* Store=Port->GetLocalContentSupply();
		if (!Store) Store=Port;
		const float Previous=Buffer.Units;
		Accepted=MoveLocalResourceUnits(Store->StoredUnits,Buffer.Units,BufferCapacityUnits,Units);
		if (Accepted>0.f && Category==EResourceTransportCategory::Content)
		{
			if (Previous<=0.f) Buffer.SourceProductID=Store->ContentProductID>0 ? Store->ContentProductID : ResourceID;
			Buffer.ContentTemperature=float((double(Previous)*Buffer.ContentTemperature+double(Accepted)*Store->ContentTemperature)/Buffer.Units);
			Buffer.ContentCreationTime=Previous>0 ? FMath::Min(Buffer.ContentCreationTime,Store->ContentCreationTime) : Store->ContentCreationTime;
		}
	}
	if (Accepted>0.f && IsValid(Port) && IsValid(Port->GetOwner()))
	{
		Port->TotalSuppliedUnits+=Accepted;
		Port->GetOwner()->ForceNetUpdate();
	}
	return Accepted;
}

float UHoseTransferComponent::DeliverTo(UResourceTransferEndpointComponent* Port,float Units,float Pressure,float DeltaSeconds)
{
	if (!IsValid(Port) || !FMath::IsFinite(Units) || Units<=0) return 0.f;
	float Delivered=0.f;
	if (Port->bManagedContent) Delivered=Port->ContentManager->DeliverHoseContent(Buffer,Port->ContentISID,Units);
	else
	{
		const float Previous=Port->StoredUnits;
		Delivered=MoveLocalResourceUnits(Buffer.Units,Port->StoredUnits,Port->CapacityUnits,Units);
		if (Delivered>0.f && Category==EResourceTransportCategory::Content)
		{
			if (Previous<=0.f && Port->bAcceptAnyContentWhenEmpty)
			{
				Port->ResourceID=Buffer.ResourceID;
				Port->ContentProductID=Buffer.SourceProductID;
			}
			Port->ContentTemperature=float((double(Previous)*Port->ContentTemperature+double(Delivered)*Buffer.ContentTemperature)/Port->StoredUnits);
			Port->ContentCreationTime=Previous>0 ? FMath::Min(Port->ContentCreationTime,Buffer.ContentCreationTime) : Buffer.ContentCreationTime;
		}
	}
	if (Delivered>0.f && IsValid(Port))
		Port->NotifyResourceReceived(this,Delivered,Pressure,Delivered/FMath::Max(DeltaSeconds,UE_SMALL_NUMBER));
	return Delivered;
}

void UHoseTransferComponent::Rupture(EHoseFailureMode Mode)
{
	bBurst=true; FailureMode=Mode;
	TrappedDrainPressure=0.f;
	const bool PhysicalContentHose=Category==EResourceTransportCategory::Content && GetOwner()->IsA<AHoseCableActor>();
	if (!PhysicalContentHose) bTransferEnabled=false;
	const float Released=PhysicalContentHose ? DischargeContents(EHoseDischargePoint::Rupture,Buffer.Units,FMath::Max(0.f,CurrentPressure)) : ReleaseBufferedContents();
	if (Category!=EResourceTransportCategory::Content) UnbindEndpoints();
	OnBurst.Broadcast(CurrentPressure,Released);
}

float UHoseTransferComponent::DischargeContents(EHoseDischargePoint Point,float Units,float Pressure)
{
	if (Units<=0.f || Buffer.Units<=0.f || !IsValid(GetOwner())) return 0.f;
	const auto* Hose=Cast<AHoseCableActor>(GetOwner());
	if (!Hose) return 0.f;
	FTransform Outlet;
	const FVector Location=Hose->GetDischargeTransform(Point,Outlet) ? Outlet.GetLocation() : Hose->GetActorLocation();
	if (EpisodePoint!=Point) { DischargeEpisodes.Reset(); EpisodePoint=Point; }
	const int32 ProductID=Buffer.SourceProductID>0 ? Buffer.SourceProductID : Buffer.ResourceID;
	const FIntVector SourceKey(Buffer.SourceISID,ProductID,Buffer.ResourceID);
	FGuid& Episode=DischargeEpisodes.FindOrAdd(SourceKey);
	if (!Episode.IsValid()) Episode=FGuid::NewGuid();
	const float Released=ReleaseContents(Units,Episode,Location,true);
	if (Released>0.f)
	{
		if (Discharge.Point!=Point)
		{
			Discharge.Point=Point;
			DischargedDuringUpdate=0.f;
			Discharge.Pressure=0.f;
		}
		DischargedDuringUpdate+=Released;
		Discharge.Pressure=FMath::Max(Discharge.Pressure,Pressure);
	}
	return Released;
}

float UHoseTransferComponent::DrainTrappedContents(EHoseDischargePoint Point,float DeltaSeconds,float SupplyPressure)
{
	if (Point==EHoseDischargePoint::None || Buffer.Units<=0.f || DeltaSeconds<=0.f) return 0.f;
	const float Pressure=FMath::Max(TrappedDrainPressure,SupplyPressure);
	const float GravityRate=FMath::IsFinite(UnpressurizedDrainUnitsPerSecond) ? FMath::Max(.01f,UnpressurizedDrainUnitsPerSecond) : 1.f;
	const double PressureRate=.65*MaxUnitsPerSecond*FMath::Sqrt(double(Pressure)/FMath::Max(RatedPressure,.1f));
	const double Rate=FMath::Min(double(MaxDrivenUnitsPerSecond),GravityRate+PressureRate);
	const float Before=Buffer.Units;
	const float Released=DischargeContents(Point,float(FMath::Min(double(Before),Rate*DeltaSeconds)),Pressure);
	// Bleed the captured head in proportion to the liquid that actually left.
	TrappedDrainPressure*=FMath::Clamp(Buffer.Units/Before,0.f,1.f);
	return Released;
}

float UHoseTransferComponent::ReleaseContents(float Units,FGuid Episode,const FVector& Location,bool bContinueEpisode)
{
	if (!FMath::IsFinite(Buffer.Units) || Buffer.Units<=0.f || !FMath::IsFinite(Units) || Units<=0.f) return 0.f;
	const EResourceTransportCategory ReleasedCategory=Buffer.Category; const int32 ReleasedID=Buffer.ResourceID;
	float Released=0;
	if (ReleasedCategory==EResourceTransportCategory::Content)
	{
		auto* Manager=bBufferUsesManagedContent ? BufferManager.Get() : APropManager::Get(this);
		if (!bBufferUsesManagedContent && Buffer.SourceProductID<=0) Buffer.SourceProductID=Buffer.ResourceID;
		if (IsValid(Manager)) Released=Manager->ReleaseHoseContent(Buffer,Episode,Location,Units,bContinueEpisode);
	}
	if (Released<=0 && !bBufferUsesManagedContent)
	{
		float Remaining=FMath::Max(0.f,Buffer.Units-Units);
		if (Buffer.Units-Remaining>Units) Remaining=std::nextafter(Remaining,Buffer.Units);
		Released=Buffer.Units-Remaining;
		Buffer.Units=Remaining;
	}
	if (Buffer.Units<=0.f) TrappedDrainPressure=0.f;
	if (Released>0) { TotalReleasedUnits+=Released; OnContentsReleased.Broadcast(ReleasedCategory,ReleasedID,Released); }
	return Released;
}

float UHoseTransferComponent::ReleaseBufferedContents()
{
	if (Buffer.Units<=0.f) return 0.f;
	FVector Location=GetOwner()->GetActorLocation();
	if (const auto* Cable=GetOwner()->FindComponentByClass<UCollisionCableComponent>())
	{
		TArray<FVector> Points; Cable->GetCableRenderLocations(Points);
		const int32 Middle=(Points.Num()-2)/2;
		if (Points.Num()>=2) Location=(Points[Middle]+Points[Middle+1])*.5;
	}
	return ReleaseContents(Buffer.Units,FGuid::NewGuid(),Location,false);
}

float UHoseTransferComponent::PurgeContents()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bTransferInProgress) return 0;
	TGuardValue<bool> Guard(bTransferInProgress,true);
	const float Released=ReleaseBufferedContents(); GetOwner()->ForceNetUpdate(); return Released;
}

bool UHoseTransferComponent::RepairHose()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bTransferInProgress || Buffer.Units>0) return false;
	bBurst=false; bTransferEnabled=true; FailureMode=PendingBodyFailure=EHoseFailureMode::None;
	BackPressure=OverpressureSeconds=CurrentPressure=0.f;
	TrappedDrainPressure=0.f; LastUnpluggedTip=EHoseDischargePoint::None;
	Discharge=FHoseDischargeState(); DischargeEpisodes.Reset(); EpisodePoint=EHoseDischargePoint::None;
	GetOwner()->ForceNetUpdate(); return true;
}

void UHoseTransferComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction); TransferForDuration(DeltaTime);
}

void UHoseTransferComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Reason==EEndPlayReason::Destroyed && GetOwner() && GetOwner()->HasAuthority() && GetNetMode()!=NM_Client)
	{
		// Destruction can originate in OnResourceReceived. Public purge rejects
		// reentrancy, but lifecycle cleanup must still account for trapped contents.
		TGuardValue<bool> Guard(bTransferInProgress,true);
		ReleaseBufferedContents();
	}
	UnbindEndpoints(); Super::EndPlay(Reason);
}
