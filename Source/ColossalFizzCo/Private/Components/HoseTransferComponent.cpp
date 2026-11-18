#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Props/InteractablePropManager.h"
#include "Net/UnrealNetwork.h"
#include <cmath>
#include <limits>

namespace
{
	// Both stores are floats. Quantize to a delta both can represent so a tiny
	// transfer can never credit one store while rounding away the other's debit.
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
		const float NewSource=float(double(Source)-Amount),NewTarget=float(double(Target)+Amount);
		const double Debit=double(Source)-NewSource,Credit=double(NewTarget)-Target;
		if (Debit<=0 || Debit!=Credit || Debit>Limit || NewTarget>Capacity) return 0;
		Source=NewSource; Target=NewTarget;
		return float(Debit);
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
	DOREPLIFETIME(UHoseTransferComponent,BlockedPressureRisePerSecond); DOREPLIFETIME(UHoseTransferComponent,CurrentPressure);
	DOREPLIFETIME(UHoseTransferComponent,CurrentFlowRate); DOREPLIFETIME(UHoseTransferComponent,bBurst);
	DOREPLIFETIME(UHoseTransferComponent,TotalReleasedUnits); DOREPLIFETIME(UHoseTransferComponent,SourceEndpoint);
	DOREPLIFETIME(UHoseTransferComponent,TargetEndpoint); DOREPLIFETIME(UHoseTransferComponent,LastTransferredUnits);
	DOREPLIFETIME(UHoseTransferComponent,LastIntakeUnits); DOREPLIFETIME(UHoseTransferComponent,BufferManager);
	DOREPLIFETIME(UHoseTransferComponent,bBufferUsesManagedContent);
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
		|| !Source->bCanSupply || !Target->bCanReceive || !Source->CanAcceptHose(this) || !Target->CanAcceptHose(this)
		|| !Source->SupportsResource(Category,ResourceID) || !Target->SupportsResource(Category,ResourceID)) return false;
	if (Source->bManagedContent!=Target->bManagedContent
		&& (Category!=EResourceTransportCategory::Content || (!Source->bAllowManagedContentBridge && !Target->bAllowManagedContentBridge))) return false;
	APropManager* ConnectionManager=Source->bManagedContent ? Source->ContentManager.Get() : Target->ContentManager.Get();
	if (Buffer.Units>0 && (bBufferUsesManagedContent!=Source->bManagedContent || BufferManager!=ConnectionManager)) return false;
	return !Source->bManagedContent || !Target->bManagedContent
		|| (Source->ContentManager==Target->ContentManager && Source->ContentISID!=Target->ContentISID);
}

bool UHoseTransferComponent::HasValidConnection() const { return CanConnect(SourceEndpoint,TargetEndpoint); }

bool UHoseTransferComponent::IsRequestingContent() const
{
	return bTransferEnabled && bIntakeEnabled && !bBurst && HasValidConnection() && TargetEndpoint->bRequestsFlow;
}

bool UHoseTransferComponent::ConnectEndpoints(UResourceTransferEndpointComponent* Source,UResourceTransferEndpointComponent* Target)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bTransferInProgress || !CanConnect(Source,Target)
		|| !Source->GetOwner()->HasAuthority() || !Target->GetOwner()->HasAuthority()) return false;
	{
		TGuardValue<bool> Guard(bTransferInProgress,true);
		if (!Source->CanTransferResource(true) || !IsValid(Target)
			|| (!(Target->bManagedReceivingProxy && Target->ContentISID==0) && !Target->CanTransferResource(false)) || !CanConnect(Source,Target)) return false;
	}
	UnbindEndpoints(); SourceEndpoint=Source; TargetEndpoint=Target;
	Source->ConnectedHoses.AddUnique(this); Target->ConnectedHoses.AddUnique(this);
	if (Buffer.Units==0)
	{
		Buffer=FHoseResourceBuffer(); Buffer.Category=Category; Buffer.ResourceID=ResourceID;
		bBufferUsesManagedContent=Source->bManagedContent; BufferManager=Source->bManagedContent ? Source->ContentManager : Target->ContentManager;
	}
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
	UnbindEndpoints(); LastTransferredUnits=0; LastIntakeUnits=0; CurrentFlowRate=0; CurrentPressure=0; OverpressureSeconds=0;
	GetOwner()->ForceNetUpdate();
}

float UHoseTransferComponent::TransferForDuration(float DeltaSeconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bTransferInProgress) return 0;
	LastTransferredUnits=0; LastIntakeUnits=0; CurrentFlowRate=0;
	if (!HasValidConnection() || !SourceEndpoint->GetOwner()->HasAuthority() || !TargetEndpoint->GetOwner()->HasAuthority()) { DisconnectEndpoints(); return 0; }
	if (!bTransferEnabled || bBurst || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0 || !FMath::IsFinite(MaxUnitsPerSecond) || MaxUnitsPerSecond<=0) return 0;
	TGuardValue<bool> Guard(bTransferInProgress,true);
	if (!FMath::IsFinite(MaxTransferStepSeconds) || MaxTransferStepSeconds<=0) return 0;
	float StepLimit=FMath::Clamp(MaxTransferStepSeconds,.001f,.1f);
	if (BufferCapacityUnits>0 && FMath::IsFinite(MaxDrivenUnitsPerSecond) && MaxDrivenUnitsPerSecond>0)
		StepLimit=FMath::Min(StepLimit,FMath::Max(UE_SMALL_NUMBER,BufferCapacityUnits/MaxDrivenUnitsPerSecond));
	double Remaining=DeltaSeconds;
	float TotalDrained=0,TotalIntake=0;
	const int32 StepBudget=FMath::Clamp(MaxTransferSubsteps,1,1024);
	int32 Steps=0;
	for (;Steps<StepBudget && Remaining>UE_SMALL_NUMBER && bTransferEnabled && !bBurst && HasValidConnection();++Steps)
	{
		const float Consumed=IntegrateTransferStep(float(FMath::Min(Remaining,double(StepLimit))));
		TotalDrained+=LastTransferredUnits; TotalIntake+=LastIntakeUnits;
		if (Consumed<=0) break;
		Remaining=FMath::Max(0.,Remaining-Consumed);
	}
	if (Steps>=StepBudget && Remaining>0 && bTransferEnabled && !bBurst && HasValidConnection()) DroppedTransferTime+=Remaining;
	LastTransferredUnits=TotalDrained; LastIntakeUnits=TotalIntake;
	GetOwner()->ForceNetUpdate();
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
	if (SupplyEnabled)
	{
		const float Intake=PotentialIntake;
		if (FMath::IsFinite(Intake) && Intake>0)
		{
			UResourceTransferEndpointComponent* SupplyingSocket=SourceEndpoint;
			if (Buffer.Units==0) { Buffer=FHoseResourceBuffer(); Buffer.Category=Category; Buffer.ResourceID=ResourceID; }
			if (SourceEndpoint->bManagedContent)
				LastIntakeUnits=SourceEndpoint->ContentManager->IntakeHoseContent(SourceEndpoint->ContentISID,Buffer,BufferCapacityUnits,Intake);
			else
			{
				const float PreviousUnits=Buffer.Units;
				LastIntakeUnits=MoveLocalResourceUnits(SourceEndpoint->StoredUnits,Buffer.Units,BufferCapacityUnits,Intake);
				if (LastIntakeUnits>0 && Category==EResourceTransportCategory::Content)
				{
					Buffer.ContentTemperature=float((double(PreviousUnits)*Buffer.ContentTemperature+double(LastIntakeUnits)*SourceEndpoint->ContentTemperature)/Buffer.Units);
					Buffer.ContentCreationTime=PreviousUnits>0 ? FMath::Min(Buffer.ContentCreationTime,SourceEndpoint->ContentCreationTime) : SourceEndpoint->ContentCreationTime;
				}
			}
			if (LastIntakeUnits>0 && IsValid(SupplyingSocket))
			{
				SupplyingSocket->TotalSuppliedUnits+=LastIntakeUnits;
				SupplyingSocket->GetOwner()->ForceNetUpdate();
			}
		}
	}
	// Canonical managed updates may invoke gameplay events; never use a connection invalidated by those events.
	if (!HasValidConnection()) { UnbindEndpoints(); return 0; }
	if (bDrainEnabled && ReceiveAllowed && IsValid(TargetEndpoint))
	{
		const float Drain=float(FMath::Min3(Budget,double(Buffer.Units),double(TargetEndpoint->GetFreeCapacityUnits())));
		if (FMath::IsFinite(Drain) && Drain>0)
		{
			UResourceTransferEndpointComponent* ReceivingSocket=TargetEndpoint;
			if (ReceivingSocket->bManagedContent) LastTransferredUnits=ReceivingSocket->ContentManager->DeliverHoseContent(Buffer,ReceivingSocket->ContentISID,Drain);
			else
			{
				const float PreviousUnits=ReceivingSocket->StoredUnits;
				LastTransferredUnits=MoveLocalResourceUnits(Buffer.Units,ReceivingSocket->StoredUnits,ReceivingSocket->CapacityUnits,Drain);
				if (LastTransferredUnits>0 && Category==EResourceTransportCategory::Content)
				{
					ReceivingSocket->ContentTemperature=float((double(PreviousUnits)*ReceivingSocket->ContentTemperature+double(LastTransferredUnits)*Buffer.ContentTemperature)/ReceivingSocket->StoredUnits);
					ReceivingSocket->ContentCreationTime=PreviousUnits>0 ? FMath::Min(ReceivingSocket->ContentCreationTime,Buffer.ContentCreationTime) : Buffer.ContentCreationTime;
				}
			}
			if (LastTransferredUnits>0 && IsValid(ReceivingSocket))
				ReceivingSocket->NotifyResourceReceived(this,LastTransferredUnits,InitialPressure,LastTransferredUnits/FMath::Max(DeltaSeconds,UE_SMALL_NUMBER));
		}
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
	{
		bBurst=true; bTransferEnabled=false;
		const float Released=ReleaseBufferedContents();
		UnbindEndpoints(); OnBurst.Broadcast(CurrentPressure,Released);
	}
	return DeltaSeconds;
}

float UHoseTransferComponent::ReleaseBufferedContents()
{
	if (!FMath::IsFinite(Buffer.Units) || Buffer.Units<=0) return 0;
	const EResourceTransportCategory ReleasedCategory=Buffer.Category; const int32 ReleasedID=Buffer.ResourceID;
	float Released=0;
	if (bBufferUsesManagedContent)
	{
		FVector ReleaseLocation=GetOwner()->GetActorLocation();
		if (const auto* Cable=GetOwner()->FindComponentByClass<UCollisionCableComponent>())
		{
			TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
			if (!Points.IsEmpty() && !Points[Points.Num()/2].ContainsNaN()) ReleaseLocation=Points[Points.Num()/2];
		}
		if (IsValid(BufferManager)) Released=BufferManager->ReleaseHoseContent(Buffer,FGuid::NewGuid(),ReleaseLocation);
	}
	else { Released=Buffer.Units; Buffer.Units=0; }
	if (Released>0) { TotalReleasedUnits+=Released; OnContentsReleased.Broadcast(ReleasedCategory,ReleasedID,Released); }
	return Released;
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
	bBurst=false; OverpressureSeconds=0; CurrentPressure=0; bTransferEnabled=true; GetOwner()->ForceNetUpdate(); return true;
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
