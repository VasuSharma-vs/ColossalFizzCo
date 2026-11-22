#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourcePumpComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPropActor.h"
#include "Resources/HoseCableActor.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

UResourceTransferEndpointComponent::UResourceTransferEndpointComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick=false;
}

void UResourceTransferEndpointComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bManagedContent) ClearManagedContentBinding();
	const auto Connections=ConnectedHoses;
	for (const auto& Hose:Connections) if (Hose.IsValid()) Hose->DisconnectEndpoints();
	ConnectedHoses.Empty();
	Super::EndPlay(Reason);
}

bool UResourceTransferEndpointComponent::CanTransferResource_Implementation(bool bAsSource) const
{
	if (!HasValidStore() || (bAsSource ? !bCanSupply : !bCanReceive)) return false;
	if (bManagedReceivingProxy && (bAsSource || ContentISID<=0)) return false;
	if (!bAsSource && bManagedContent)
		if (const auto* Bottle=Cast<AColaPropActor>(ContentManager->GetActiveActorFromISID(ContentISID)))
			return Bottle->CanAcceptManufacturingContent(ResourceID);
	return true;
}

void UResourceTransferEndpointComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UResourceTransferEndpointComponent,Category);
	DOREPLIFETIME(UResourceTransferEndpointComponent,ResourceID);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bCanSupply);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bCanReceive);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaxConnectedHoses);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bSupplyEnabled);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bRequestsFlow);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaxSupplyUnitsPerSecond);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaxReceiveUnitsPerSecond);
	DOREPLIFETIME(UResourceTransferEndpointComponent,TotalSuppliedUnits);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bAllowManagedContentBridge);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaximumManagedContentLevel);
	DOREPLIFETIME(UResourceTransferEndpointComponent,ContentTemperature);
	DOREPLIFETIME(UResourceTransferEndpointComponent,ContentCreationTime);
	DOREPLIFETIME(UResourceTransferEndpointComponent,ContentProductID);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bEnableIntakeOverload);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaxIntakePressure);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaxIntakeUnitsPerSecond);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bEnableOuttakeOverload);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaxOuttakePressure);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MaxOuttakeUnitsPerSecond);
	DOREPLIFETIME(UResourceTransferEndpointComponent,LastIncomingPressure);
	DOREPLIFETIME(UResourceTransferEndpointComponent,LastIncomingUnitsPerSecond);
	DOREPLIFETIME(UResourceTransferEndpointComponent,PushPressure);
	DOREPLIFETIME(UResourceTransferEndpointComponent,PullPressure);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bUseSignedPressure);
	DOREPLIFETIME(UResourceTransferEndpointComponent,OperatingPressure);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bMotorRunning);
	DOREPLIFETIME(UResourceTransferEndpointComponent,MotorPressureBoost);
	DOREPLIFETIME(UResourceTransferEndpointComponent,ConnectedMotor);
	DOREPLIFETIME(UResourceTransferEndpointComponent,CapacityUnits);
	DOREPLIFETIME(UResourceTransferEndpointComponent,StoredUnits);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bManagedContent);
	DOREPLIFETIME(UResourceTransferEndpointComponent,bManagedReceivingProxy);
	DOREPLIFETIME(UResourceTransferEndpointComponent,ContentManager);
	DOREPLIFETIME(UResourceTransferEndpointComponent,ContentISID);
}

bool UResourceTransferEndpointComponent::SetSupplyEnabled(bool bEnabled)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return false;
	bSupplyEnabled=bEnabled; GetOwner()->ForceNetUpdate(); return true;
}

bool UResourceTransferEndpointComponent::CanAcceptHose(const UHoseTransferComponent* Hose) const
{
	if (!IsValid(Hose) || Hose->GetWorld()!=GetWorld()) return false;
	if (CompatibleHoseClass && (!IsValid(Hose->GetOwner()) || !Hose->GetOwner()->IsA(CompatibleHoseClass))) return false;
	if (MaxConnectedHoses<=0) return true;
	TSet<const UHoseTransferComponent*> Connections;
	for (const auto& Existing:ConnectedHoses) if (Existing.IsValid())
	{
		if (Existing.Get()==Hose) return true;
		Connections.Add(Existing.Get());
	}
	if (GetWorld()) for (TActorIterator<AHoseCableActor> It(GetWorld());It;++It)
	{
		if ((It->StartPort==this || It->EndPort==this) && IsValid(It->Transfer))
		{
			if (It->Transfer.Get()==Hose) return true;
			Connections.Add(It->Transfer.Get());
		}
	}
	return Connections.Num()<MaxConnectedHoses;
}

bool UResourceTransferEndpointComponent::SetFlowRequested(bool bRequested)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return false;
	bRequestsFlow=bRequested; GetOwner()->ForceNetUpdate(); return true;
}

bool UResourceTransferEndpointComponent::IsSupplyRequested() const
{
	for (const auto& Hose:ConnectedHoses)
		if (Hose.IsValid() && Hose->IsRequestingContentFrom(this)) return true;
	return false;
}

bool UResourceTransferEndpointComponent::HasConnectedSupplyHose() const
{
	return ConnectedHoses.ContainsByPredicate([this](const auto& Hose)
	{
		return Hose.IsValid() && (Hose->SourceEndpoint==this || (Category==EResourceTransportCategory::Content && Hose->TargetEndpoint==this))
			&& (Hose->HasValidConnection() || Hose->bBurst);
	});
}

UHoseTransferComponent* UResourceTransferEndpointComponent::GetReceivingHose() const
{
	if (!bCanReceive) return nullptr;
	for (const auto& Hose:ConnectedHoses)
		if (Hose.IsValid() && (Hose->TargetEndpoint==this
			|| (Category==EResourceTransportCategory::Content && Hose->SourceEndpoint==this))
			&& Hose->HasValidConnection()) return Hose.Get();
	return nullptr;
}

bool UResourceTransferEndpointComponent::IsIntakeOverloaded(float Pressure,float UnitsPerSecond) const
{
	return bEnableIntakeOverload && (!FMath::IsFinite(MaxIntakePressure) || MaxIntakePressure<0
		|| !FMath::IsFinite(MaxIntakeUnitsPerSecond) || MaxIntakeUnitsPerSecond<0
		|| Pressure>MaxIntakePressure || UnitsPerSecond>MaxIntakeUnitsPerSecond);
}

float UResourceTransferEndpointComponent::GetConnectorOverload(float Pressure,float UnitsPerSecond,bool bOuttake) const
{
	if (!(bOuttake ? bEnableOuttakeOverload : bEnableIntakeOverload)) return 0.f;
	const float PressureLimit=bOuttake ? MaxOuttakePressure : MaxIntakePressure;
	const float FlowLimit=bOuttake ? MaxOuttakeUnitsPerSecond : MaxIntakeUnitsPerSecond;
	if (!FMath::IsFinite(PressureLimit) || PressureLimit<0 || !FMath::IsFinite(FlowLimit) || FlowLimit<0) return MAX_flt;
	const float PressureOver=Pressure>PressureLimit+UE_KINDA_SMALL_NUMBER ? Pressure/FMath::Max(PressureLimit,UE_KINDA_SMALL_NUMBER) : 0.f;
	const float FlowOver=UnitsPerSecond>FlowLimit+UE_KINDA_SMALL_NUMBER ? UnitsPerSecond/FMath::Max(FlowLimit,UE_KINDA_SMALL_NUMBER) : 0.f;
	return FMath::Max(PressureOver,FlowOver);
}

void UResourceTransferEndpointComponent::NotifyIntakeOverload(UHoseTransferComponent* Hose,float Pressure,float UnitsPerSecond)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	LastIncomingPressure=Pressure; LastIncomingUnitsPerSecond=UnitsPerSecond; bRequestsFlow=false;
	EjectOverloadedHose(Hose,Pressure);
	GetOwner()->ForceNetUpdate(); OnSocketOverloaded.Broadcast(this,Hose,Pressure,UnitsPerSecond);
}

void UResourceTransferEndpointComponent::NotifyOuttakeOverload(UHoseTransferComponent* Hose,float Pressure,float UnitsPerSecond)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	EjectOverloadedHose(Hose,Pressure);
	GetOwner()->ForceNetUpdate(); OnSocketOverloaded.Broadcast(this,Hose,Pressure,UnitsPerSecond);
}

void UResourceTransferEndpointComponent::EjectOverloadedHose(UHoseTransferComponent* Hose,float Pressure)
{
	if (!IsValid(Hose)) return;
	auto* HoseActor=Cast<AHoseCableActor>(Hose->GetOwner());
	if (Hose->Category==EResourceTransportCategory::Content && !Hose->bBurst)
	{
		const bool SourceSlot=Hose->SourceEndpoint==this || (HoseActor && HoseActor->StartPort==this);
		const bool StartConnector=HoseActor ? (SourceSlot ? HoseActor->GetSupplyConnectorIndex()==0 : HoseActor->GetReceivingConnectorIndex()==0) : SourceSlot;
		Hose->FailureMode=StartConnector ? EHoseFailureMode::StartConnectorPopped : EHoseFailureMode::EndConnectorPopped;
		Hose->BackPressure=Hose->OverpressureSeconds=0.f;
		Hose->PendingBodyFailure=EHoseFailureMode::None;
	}
	// Break inside the current delivery step, before a Blueprint callback can repair the socket.
	if (Hose->SourceEndpoint==this || Hose->TargetEndpoint==this) Hose->UnbindEndpoints();
	if (HoseActor) HoseActor->EjectConnectorFrom(this,Pressure);
}

void UResourceTransferEndpointComponent::NotifyResourceReceived(UHoseTransferComponent* Hose,float Units,float Pressure,float UnitsPerSecond)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Units<=0) return;
	LastIncomingPressure=Pressure; LastIncomingUnitsPerSecond=UnitsPerSecond;
	GetOwner()->ForceNetUpdate(); OnResourceReceived.Broadcast(this,Hose,Units,Pressure,UnitsPerSecond);
}

bool UResourceTransferEndpointComponent::IsMotorRunning() const
{
	return bMotorRunning || (IsValid(ConnectedMotor) && ConnectedMotor->GetWorld()==GetWorld() && ConnectedMotor->bRunning);
}

float UResourceTransferEndpointComponent::GetEffectivePushPressure() const
{
	return PushPressure+(bMotorRunning ? MotorPressureBoost : 0.f)
		+(IsValid(ConnectedMotor) && ConnectedMotor->GetWorld()==GetWorld() && ConnectedMotor->bRunning ? ConnectedMotor->PushPressure : 0.f);
}

float UResourceTransferEndpointComponent::GetEffectivePullPressure() const
{
	return PullPressure+(bMotorRunning ? MotorPressureBoost : 0.f)
		+(IsValid(ConnectedMotor) && ConnectedMotor->GetWorld()==GetWorld() && ConnectedMotor->bRunning ? ConnectedMotor->PullPressure : 0.f);
}

bool UResourceTransferEndpointComponent::SetOperatingPressure(float Pressure)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(Pressure)) return false;
	if (!bUseSignedPressure || OperatingPressure!=Pressure)
	{
		bUseSignedPressure=true;
		OperatingPressure=Pressure;
		GetOwner()->ForceNetUpdate();
	}
	return true;
}

float UResourceTransferEndpointComponent::GetOperatingPressure() const
{
	const float LegacyMotorBias=bMotorRunning ? (bCanSupply ? MotorPressureBoost : -MotorPressureBoost) : 0.f;
	const float Pressure=bUseSignedPressure ? OperatingPressure : GetEffectivePushPressure()-GetEffectivePullPressure()+LegacyMotorBias;
	if ((Pressure>0.f && !bSupplyEnabled) || (Pressure<0.f && !bRequestsFlow)) return 0.f;
	return Pressure;
}

bool UResourceTransferEndpointComponent::HasValidStore() const
{
	if (!GetWorld() || !IsValid(GetOwner()) || ResourceID<=0
		|| (Category!=EResourceTransportCategory::Content && Category!=EResourceTransportCategory::Fuel && Category!=EResourceTransportCategory::Electricity)) return false;
	if (!bManagedContent) return FMath::IsFinite(CapacityUnits) && CapacityUnits>=0 && FMath::IsFinite(StoredUnits) && StoredUnits>=0 && StoredUnits<=CapacityUnits
		&& (Category!=EResourceTransportCategory::Content || (FMath::IsFinite(ContentTemperature) && FMath::IsFinite(ContentCreationTime) && ContentCreationTime>=0));
	if (Category!=EResourceTransportCategory::Content || !IsValid(ContentManager) || ContentManager->GetWorld()!=GetWorld()) return false;
	if (bManagedReceivingProxy && (bCanSupply || !bCanReceive)) return false;
	if (const auto* Prop=Cast<AActivePropActor>(GetOwner()); Prop && (!Prop->bIsActive || (!bManagedReceivingProxy && Prop->ISID!=ContentISID) || Prop->PropManagerReference!=ContentManager)) return false;
	if (bManagedReceivingProxy && ContentISID==0) return ContentManager->FindContentDefinition(ResourceID)!=nullptr;
	const auto* Record=ContentManager->FindISIDData(ContentISID);
	const auto* Unique=ContentManager->FindUniqueData(ContentISID);
	const auto* Def=Record ? ContentManager->FindProductDefinition(Record->ProductID) : nullptr;
	return Unique && Def && Def->ConsumableProfile.NumOfPortions>0 && ContentManager->FindContentDefinition(ResourceID)
		&& FMath::IsFinite(Unique->EventData.ContentLevel) && Unique->EventData.ContentLevel>=0
		&& (Unique->EventData.ContentLevel==0 || Unique->EventData.ContentID==ResourceID);
}

bool UResourceTransferEndpointComponent::SupportsResource(EResourceTransportCategory InCategory,int32 InResourceID) const
{
	return Category==InCategory && ResourceID==InResourceID && HasValidStore();
}

bool UResourceTransferEndpointComponent::SetStoredUnits(float Units)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || bManagedContent || !FMath::IsFinite(Units)
		|| !FMath::IsFinite(CapacityUnits) || Units<0 || Units>CapacityUnits) return false;
	StoredUnits=Units; GetOwner()->ForceNetUpdate(); return true;
}

float UResourceTransferEndpointComponent::GetStoredUnits() const
{
	if (!HasValidStore()) return 0;
	if (!bManagedContent) return StoredUnits;
	if (ContentISID<=0) return 0;
	const auto* Record=ContentManager->FindISIDData(ContentISID);
	const auto* Def=ContentManager->FindProductDefinition(Record->ProductID);
	return ContentManager->FindUniqueData(ContentISID)->EventData.ContentLevel*Def->ConsumableProfile.NumOfPortions;
}

float UResourceTransferEndpointComponent::GetFreeCapacityUnits() const
{
	if (!HasValidStore()) return 0;
	if (!bManagedContent) return CapacityUnits-StoredUnits;
	if (ContentISID<=0) return 0;
	const auto* Record=ContentManager->FindISIDData(ContentISID);
	const auto* Unique=ContentManager->FindUniqueData(ContentISID);
	const auto* Def=ContentManager->FindProductDefinition(Record->ProductID);
	const bool Deforms=Def->BottleMaterialType==EBottleMaterialType::Plastic || Def->BottleMaterialType==EBottleMaterialType::HeavyMetal;
	float Capacity=Def->MaxOverfillLevel*(Deforms ? FMath::Clamp(Def->GetEffectiveStrength(*Unique),0.f,1.f) : 1.f);
	if (bManagedReceivingProxy)
	{
		if (!FMath::IsFinite(MaximumManagedContentLevel) || MaximumManagedContentLevel<0) return 0;
		if (MaximumManagedContentLevel>0) Capacity=FMath::Min(Capacity,MaximumManagedContentLevel);
	}
	return FMath::IsFinite(Capacity) ? FMath::Max(0.f,(Capacity-Unique->EventData.ContentLevel)*Def->ConsumableProfile.NumOfPortions) : 0.f;
}

bool UResourceTransferEndpointComponent::ConfigureManagedContent(APropManager* Manager,int32 InstanceID,int32 ExpectedContentID)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !IsValid(Manager) || !Manager->HasAuthority()
		|| Manager->GetWorld()!=GetWorld() || !Manager->FindContentDefinition(ExpectedContentID)) return false;
	if (const auto* Prop=Cast<AActivePropActor>(GetOwner()); Prop && (!Prop->bIsActive || Prop->ISID!=InstanceID || Prop->PropManagerReference!=Manager)) return false;
	const bool SameBinding=bManagedContent && !bManagedReceivingProxy && ContentManager==Manager && ContentISID==InstanceID
		&& Category==EResourceTransportCategory::Content && ResourceID==ExpectedContentID;
	if (!SameBinding && ((!bManagedContent && StoredUnits>0)
		|| ConnectedHoses.ContainsByPredicate([](const auto& Hose){ return Hose.IsValid(); }))) return false;
	const auto* Record=Manager->FindISIDData(InstanceID);
	const auto* Unique=Manager->FindUniqueData(InstanceID);
	const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	if (!Unique || !Def || Def->ConsumableProfile.NumOfPortions<=0 || ExpectedContentID<=0
		|| !FMath::IsFinite(Unique->EventData.ContentLevel) || Unique->EventData.ContentLevel<0
		|| (Unique->EventData.ContentLevel>0 && Unique->EventData.ContentID!=ExpectedContentID)) return false;
	bManagedContent=true; bManagedReceivingProxy=false; ContentManager=Manager; ContentISID=InstanceID;
	Category=EResourceTransportCategory::Content; ResourceID=ExpectedContentID;
	GetOwner()->ForceNetUpdate(); return true;
}

bool UResourceTransferEndpointComponent::ConfigureManagedReceivingProxy(APropManager* Manager,int32 TargetInstanceID,int32 ExpectedContentID)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !IsValid(Manager) || !Manager->HasAuthority()
		|| Manager->GetWorld()!=GetWorld() || TargetInstanceID<0 || !Manager->FindContentDefinition(ExpectedContentID)
		|| (!bManagedContent && StoredUnits>0)) return false;
	if (const auto* Prop=Cast<AActivePropActor>(GetOwner()); Prop && (!Prop->bIsActive || Prop->PropManagerReference!=Manager)) return false;
	const bool SameBinding=bManagedReceivingProxy && ContentManager==Manager && ContentISID==TargetInstanceID && ResourceID==ExpectedContentID;
	const bool SafeParking=bManagedReceivingProxy && TargetInstanceID==0 && !bRequestsFlow
		&& ContentManager==Manager && ResourceID==ExpectedContentID;
	for (const auto& Hose:ConnectedHoses) if (Hose.IsValid())
	{
		if ((Hose->bTransferInProgress && !SafeParking) || (!SameBinding && bRequestsFlow)
			|| !bManagedReceivingProxy || ContentManager!=Manager || ResourceID!=ExpectedContentID) return false;
	}
	if (TargetInstanceID>0)
	{
		const auto* Record=Manager->FindISIDData(TargetInstanceID);
		const auto* Unique=Manager->FindUniqueData(TargetInstanceID);
		const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (!Unique || !Def || Def->ConsumableProfile.NumOfPortions<=0 || !FMath::IsFinite(Unique->EventData.ContentLevel)
			|| Unique->EventData.ContentLevel<0 || (Unique->EventData.ContentLevel>0 && Unique->EventData.ContentID!=ExpectedContentID)) return false;
	}
	bManagedContent=true; bManagedReceivingProxy=true; ContentManager=Manager; ContentISID=TargetInstanceID;
	Category=EResourceTransportCategory::Content; ResourceID=ExpectedContentID; bCanSupply=false; bCanReceive=true;
	if (!SameBinding) bRequestsFlow=false;
	GetOwner()->ForceNetUpdate(); return true;
}

void UResourceTransferEndpointComponent::ClearManagedContentBinding()
{
	if (!bManagedContent || !GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	// A hose with only one end attached has no transfer connection yet, so also detach physical actor endpoints.
	if (GetWorld()) for (TActorIterator<AHoseCableActor> It(GetWorld());It;++It)
	{
		if (It->StartPort==this) It->DisconnectStart();
		if (It->EndPort==this) It->DisconnectEnd();
	}
	const auto Connections=ConnectedHoses;
	for (const auto& Hose:Connections) if (Hose.IsValid()) Hose->DisconnectEndpoints();
	ConnectedHoses.Empty(); ContentManager=nullptr; ContentISID=0;
	if (bManagedReceivingProxy) bRequestsFlow=false;
	// Remain an unbound managed endpoint; falling back to local storage would expose a different inventory.
	GetOwner()->ForceNetUpdate();
}
