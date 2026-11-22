#include "Components/HoseSocketComponent.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Resources/HoseCableActor.h"
#include "Shop/ManualColaFillingMachine.h"

UHoseSocketComponent::UHoseSocketComponent()
{
	bCanSupply = false;
	bCanReceive = true;
	bUseAuthoredHoseConnectorFrame = true;
	bEnableIntakeOverload = true;
	bEnableOuttakeOverload = true;
	MaxConnectedHoses = 1;
}

void UHoseSocketComponent::OnRegister()
{
	Super::OnRegister();
	AActor* Owner = GetOwner();
	if (IsTemplate() || !IsValid(Owner) || !GetWorld()) return;

	constexpr EObjectFlags ChildFlags = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;
	if (!IsValid(TraceVolume))
	{
		TraceVolume = NewObject<UBoxComponent>(Owner, NAME_None, ChildFlags);
		TraceVolume->SetupAttachment(this);
		TraceVolume->SetCollisionObjectType(ECC_WorldDynamic);
		TraceVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
		TraceVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		TraceVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TraceVolume->SetGenerateOverlapEvents(false);
		TraceVolume->SetCanEverAffectNavigation(false);
		TraceVolume->SetHiddenInGame(true);
		TraceVolume->ShapeColor = FColor(49,213,179);
	}
	TraceVolume->SetMobility(Mobility);
	UpdateTraceVolume();
	if (!TraceVolume->IsRegistered()) TraceVolume->RegisterComponentWithWorld(GetWorld());

#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())
	{
		if (!IsValid(DirectionArrow))
		{
			DirectionArrow = NewObject<UArrowComponent>(Owner, NAME_None, ChildFlags);
			DirectionArrow->MarkAsEditorOnlySubobject();
			DirectionArrow->SetupAttachment(this);
			// The cable connector uses local +Z. The arrow draws along its own +X.
			DirectionArrow->SetRelativeRotation(FRotator(90,0,0));
			DirectionArrow->ArrowColor = FColor(238,173,76);
			DirectionArrow->ArrowSize = .55f;
			DirectionArrow->SetHiddenInGame(true);
		}
		DirectionArrow->SetMobility(Mobility);
		if (!DirectionArrow->IsRegistered()) DirectionArrow->RegisterComponentWithWorld(GetWorld());
	}
#endif
}

void UHoseSocketComponent::OnUnregister()
{
	if (IsValid(TraceVolume) && TraceVolume->IsRegistered()) TraceVolume->UnregisterComponent();
#if WITH_EDITORONLY_DATA
	if (IsValid(DirectionArrow) && DirectionArrow->IsRegistered()) DirectionArrow->UnregisterComponent();
#endif
	Super::OnUnregister();
}

void UHoseSocketComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	DetachHose();
	if (IsValid(TraceVolume)) TraceVolume->DestroyComponent();
	TraceVolume = nullptr;
#if WITH_EDITORONLY_DATA
	if (IsValid(DirectionArrow)) DirectionArrow->DestroyComponent();
	DirectionArrow = nullptr;
#endif
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UHoseSocketComponent::UpdateTraceVolume()
{
	if (!IsValid(TraceVolume)) return;
	TraceVolume->SetBoxExtent(TraceBoxExtent.ContainsNaN() ? FVector(22) : TraceBoxExtent.ComponentMax(FVector(1)), false);
	TraceVolume->SetRelativeLocation(TraceBoxOffset.ContainsNaN() ? FVector::ZeroVector : TraceBoxOffset);
}

#if WITH_EDITOR
void UHoseSocketComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	UpdateTraceVolume();
	Super::PostEditChangeProperty(Event);
}
#endif

void UHoseSocketComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHoseSocketComponent, BreakAtStoredUnits);
	DOREPLIFETIME(UHoseSocketComponent, ConnectedHose);
	DOREPLIFETIME(UHoseSocketComponent, bIsBroken);
	DOREPLIFETIME(UHoseSocketComponent, TotalReceivedUnits);
}

UBoxComponent* UHoseSocketComponent::GetTraceVolume() const { return TraceVolume.Get(); }

UHoseSocketComponent* UHoseSocketComponent::GetSocketFromHitComponent(const UPrimitiveComponent* HitComponent)
{
	if (!IsValid(HitComponent)) return nullptr;
	auto* Socket = Cast<UHoseSocketComponent>(HitComponent->GetAttachParent());
	return IsValid(Socket) && Socket->TraceVolume==HitComponent ? Socket : nullptr;
}

bool UHoseSocketComponent::CanAcceptHose(const UHoseTransferComponent* Hose) const
{
	return !bIsBroken && (!IsValid(ConnectedHose) || ConnectedHose->Transfer==Hose) && Super::CanAcceptHose(Hose);
}

AManualColaFillingMachine* UHoseSocketComponent::FindFillingMachine(int32& PortIndex) const
{
	PortIndex = INDEX_NONE;
	auto* Machine = Cast<AManualColaFillingMachine>(GetOwner());
	if (!Machine || GetAttachParent() != Machine->HoseConnections) return nullptr;
	PortIndex = Machine->HoseSockets.IndexOfByKey(this);
	return Machine;
}

bool UHoseSocketComponent::AttachHose(AHoseCableActor* Hose, int32 ConnectorIndex)
{
	if (!IsValid(Hose) || !GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !CanAcceptHose(Hose->Transfer)) return false;
	const bool bAlreadySeated = IsValid(Hose->StartPort)!=IsValid(Hose->EndPort);
	const bool bContent = Hose->Transfer->Category==EResourceTransportCategory::Content;
	const bool bSupply = bContent && bAlreadySeated ? !Hose->StartPort
		: bCanSupply && (!bCanReceive || (bContent && bUseSignedPressure) || ConnectorIndex==INDEX_NONE || ConnectorIndex==Hose->GetSupplyConnectorIndex());
	if (ConnectorIndex==INDEX_NONE)
		ConnectorIndex = bSupply ? Hose->GetSupplyConnectorIndex() : Hose->GetReceivingConnectorIndex();

	if (bSupply ? IsValid(Hose->StartPort) : IsValid(Hose->EndPort)) return false;
	int32 PortIndex = INDEX_NONE;
	if (auto* Machine = FindFillingMachine(PortIndex))
	{
		const bool bEmptyUnconnectedHose = !Hose->StartPort && !Hose->EndPort
			&& Hose->bMatchSourceResourceWhenEmpty && Hose->Transfer->Buffer.Units <= 0;
		const int32 ContentID = bEmptyUnconnectedHose ? Machine->GetRequiredHoseContentID(PortIndex) : Hose->Transfer->ResourceID;
		const bool bWaitForSource = bEmptyUnconnectedHose && ContentID == 0;
		if (!bContent || (ContentID <= 0 && !bWaitForSource)
			|| !Machine->PrepareReceivingHoseConnection(PortIndex, ContentID, Hose)) return false;
		if (bWaitForSource) return Hose->ConnectPortAtConnector(this, ConnectorIndex, bSupply, false, true);
	}
	return bSupply ? Hose->ConnectSupplyAtConnector(this, ConnectorIndex) : Hose->ConnectReceivingAtConnector(this, ConnectorIndex);
}

bool UHoseSocketComponent::DetachHose()
{
	if (!IsValid(ConnectedHose) || !GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return false;
	if (ConnectedHose->StartPort==this) ConnectedHose->DisconnectStart();
	else if (ConnectedHose->EndPort==this) ConnectedHose->DisconnectEnd();
	else return false;
	return true;
}

void UHoseSocketComponent::OnPhysicalHoseAttachmentChanged(AHoseCableActor* Hose, bool bAttached)
{
	if (!bAttached && ConnectedHose!=Hose) return;
	ConnectedHose = bAttached ? Hose : nullptr;
	if (GetOwner()) GetOwner()->ForceNetUpdate();
}

bool UHoseSocketComponent::HasStorageBreakLimit() const
{
	return !IsManagedContentEndpoint() && FMath::IsFinite(BreakAtStoredUnits) && BreakAtStoredUnits>0.f && BreakAtStoredUnits<=CapacityUnits;
}

bool UHoseSocketComponent::HasReachedStorageBreakLimit() const
{
	return HasStorageBreakLimit() && GetStoredUnits()>=BreakAtStoredUnits;
}

float UHoseSocketComponent::GetFreeCapacityUnits() const
{
	if (bIsBroken) return 0.f;
	const float Room = Super::GetFreeCapacityUnits();
	return HasStorageBreakLimit() ? FMath::Min(Room, FMath::Max(0.f, BreakAtStoredUnits-GetStoredUnits())) : Room;
}

void UHoseSocketComponent::NotifyResourceReceived(UHoseTransferComponent* Hose, float Units, float Pressure, float UnitsPerSecond)
{
	TotalReceivedUnits += Units;
	const bool bReachedLimit = !bIsBroken && HasReachedStorageBreakLimit();
	if (bReachedLimit)
	{
		bIsBroken = true;
		bRequestsFlow = false;
		EjectOverloadedHose(Hose,Pressure);
	}
	Super::NotifyResourceReceived(Hose, Units, Pressure, UnitsPerSecond);
	if (bReachedLimit) OnSocketBroken.Broadcast(this, EHoseSocketBreakReason::StorageLimit, Hose);
}

void UHoseSocketComponent::NotifyIntakeOverload(UHoseTransferComponent* Hose, float Pressure, float UnitsPerSecond)
{
	if (bIsBroken) return;
	bIsBroken = true;
	Super::NotifyIntakeOverload(Hose, Pressure, UnitsPerSecond);
	OnSocketBroken.Broadcast(this, EHoseSocketBreakReason::IntakeOverload, Hose);
}

void UHoseSocketComponent::NotifyOuttakeOverload(UHoseTransferComponent* Hose, float Pressure, float UnitsPerSecond)
{
	if (bIsBroken) return;
	bIsBroken = true;
	Super::NotifyOuttakeOverload(Hose, Pressure, UnitsPerSecond);
	OnSocketBroken.Broadcast(this, EHoseSocketBreakReason::OuttakeOverload, Hose);
}

bool UHoseSocketComponent::RepairSocket()
{
	if (!bIsBroken || !GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || HasReachedStorageBreakLimit()) return false;
	bIsBroken = false;
	SetFlowRequested(bCanReceive && !IsManagedReceivingProxy());
	return true;
}

float UHoseSocketComponent::GetLastSignedFlowUnits() const
{
	const auto* Transfer = IsValid(ConnectedHose) ? ConnectedHose->Transfer.Get() : nullptr;
	return !bIsBroken && IsValid(Transfer) && Transfer->bTransferEnabled
		? Transfer->GetLastPortFlowUnits(this) : 0.f;
}

float UHoseSocketComponent::GetLastFlowUnits() const
{
	return FMath::Abs(GetLastSignedFlowUnits());
}

float UHoseSocketComponent::GetCurrentFlowUnitsPerSecond() const
{
	return FMath::Abs(GetSignedFlowUnitsPerSecond());
}

float UHoseSocketComponent::GetSignedFlowUnitsPerSecond() const
{
	const float Amount = GetLastSignedFlowUnits();
	return Amount!=0.f && ConnectedHose->Transfer->LastTransferUpdateSeconds>0.f
		? Amount/ConnectedHose->Transfer->LastTransferUpdateSeconds : 0.f;
}
