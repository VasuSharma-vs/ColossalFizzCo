#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ColaSupplyContainer.h"
#include "Components/HoseTransferComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Props/InteractablePropManager.h"
#include "Resources/HoseCableActor.h"
#include "Components/CollisionCableComponent.h"

void AManualColaFillingMachine::ConfigureReceivingHosePort(UHoseSocketComponent& Port, bool bGenerated)
{
	Port.Category=EResourceTransportCategory::Content;
	Port.bCanSupply=false; Port.bCanReceive=true; Port.bSupplyEnabled=false; Port.bRequestsFlow=false;
	Port.MaxConnectedHoses=1;
	if (bGenerated)
	{
		Port.MaxIntakePressure=MaximumIntakePressure;
		Port.MaxIntakeUnitsPerSecond=MaximumIntakePortionsPerSecond;
	}
	if (bGenerated || Port.MaxReceiveUnitsPerSecond==0.f) Port.MaxReceiveUnitsPerSecond=IntakeFlowPortionsPerSecond;
	Port.OnResourceReceived.AddUniqueDynamic(this,&AManualColaFillingMachine::HandleSupplyReceived);
	Port.OnSocketOverloaded.AddUniqueDynamic(this,&AManualColaFillingMachine::HandleSupplySocketOverloaded);
}

UHoseSocketComponent* AManualColaFillingMachine::GetHoseSocket(int32 PortIndex) const
{
	return HoseSockets.IsValidIndex(PortIndex) ? HoseSockets[PortIndex].Get() : nullptr;
}

UResourceTransferEndpointComponent* AManualColaFillingMachine::GetReceivingHosePort(int32 PortIndex) const
{
	return GetHoseSocket(PortIndex);
}

bool AManualColaFillingMachine::PrepareReceivingHosePort(int32 PortIndex,int32 ContentID)
{
	return ContentID > 0 && PrepareReceivingHoseConnection(PortIndex, ContentID, nullptr);
}

int32 AManualColaFillingMachine::GetRequiredHoseContentID(int32 PortIndex) const
{
	return HosePortContentIDs.IsValidIndex(PortIndex) ? FMath::Max(0, HosePortContentIDs[PortIndex]) : 0;
}

bool AManualColaFillingMachine::PrepareReceivingHoseConnection(int32 PortIndex, int32 ContentID, const AHoseCableActor* Hose)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !bIsActive || !Production || Production->View.bLeverHeld) return false;
	auto* Port=GetHoseSocket(PortIndex); auto* Manager=APropManager::Get(this);
	if (!Port || Port->bIsBroken || (Port->ConnectedHose && Port->ConnectedHose.Get() != Hose) || !Manager || PropManagerReference != Manager
		|| (!Port->IsManagedContentEndpoint() && Port->StoredUnits > 0)
		|| ContentID < 0 || (ContentID == 0 && !Hose)) return false;
	const int32 RequiredID = GetRequiredHoseContentID(PortIndex);
	if (RequiredID > 0 && RequiredID != ContentID) return false;
	if (Port->GetReceivingHose()) return false;
	if (const auto* Equipment=Manager->FindUniqueData(ISID); Equipment && Equipment->EventData.ConnectedSupplyISIDs.IsValidIndex(PortIndex))
	{
		const int32 SourceID=Equipment->EventData.ConnectedSupplyISIDs[PortIndex];
		if (SourceID>0 && HasSupplyNozzle(PortIndex,SourceID)) return false;
	}
	Port->SetFlowRequested(false);
	return ContentID == 0 || Port->ConfigureManagedReceivingProxy(Manager, 0, ContentID);
}

UHoseTransferComponent* AManualColaFillingMachine::GetSupplyHoseForPort(int32 PortIndex) const
{
	if (const auto* Port=GetReceivingHosePort(PortIndex))
		if (auto* Hose=Port->GetReceivingHose()) return Hose->bBurst ? nullptr : Hose;
	const auto* Manager=APropManager::Get(this);
	const auto* Equipment=Manager ? Manager->FindUniqueData(ISID) : nullptr;
	const int32 SourceID=Equipment && Equipment->EventData.ConnectedSupplyISIDs.IsValidIndex(PortIndex)
		? Equipment->EventData.ConnectedSupplyISIDs[PortIndex] : 0;
	const auto* Source=Manager ? Cast<AColaSupplyContainer>(Manager->GetActiveActorFromISID(SourceID)) : nullptr;
	return Source && Source->SupplyTransfer && !Source->SupplyTransfer->bBurst && HasSupplyNozzle(PortIndex,SourceID)
		? Source->SupplyTransfer.Get() : nullptr;
}

int32 AManualColaFillingMachine::GetSupplyContentID(int32 PortIndex) const
{
	const auto* Hose=GetSupplyHoseForPort(PortIndex);
	if (!Hose) return 0;
	if (const auto* Source=Hose->GetOtherEndpoint(GetReceivingHosePort(PortIndex))) return Source->ResourceID;
	const auto* Container=Cast<AColaSupplyContainer>(Hose->GetOwner());
	return Container && Container->SupplyPort ? Container->SupplyPort->ResourceID : 0;
}

bool AManualColaFillingMachine::IsSupplyInRange(int32 PortIndex) const
{
	const auto* Hose=GetSupplyHoseForPort(PortIndex);
	const auto* Receiver=GetReceivingHosePort(PortIndex);
	const auto* SupplyPort=Hose ? Hose->GetOtherEndpoint(Receiver) : nullptr;
	if (const auto* CableActor=Hose ? Cast<AHoseCableActor>(Hose->GetOwner()) : nullptr)
	{
		if (!CableActor->Cable || !SupplyPort || !Receiver) return false;
		const float Reach=CableActor->Cable->CableLength+FMath::Max(0.f,CableActor->Cable->LengthSlackTolerance);
		return FMath::IsFinite(Reach) && Reach>0 && FVector::DistSquared(SupplyPort->GetComponentLocation(),Receiver->GetComponentLocation())<=FMath::Square(Reach);
	}
	const auto* Source=SupplyPort ? SupplyPort->GetOwner() : (Hose ? Hose->GetOwner() : nullptr);
	return Source && BottleFiller && FMath::IsFinite(BottleFiller->MaxConnectionDistance)
		&& BottleFiller->MaxConnectionDistance>0 && Source->GetWorld()==GetWorld()
		&& FVector::DistSquared(GetSupplyConnectionOrigin(),Source->GetActorLocation())<=FMath::Square(BottleFiller->MaxConnectionDistance);
}

void AManualColaFillingMachine::HandleSupplyReceived(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float ActualUnits,float IncomingPressure,float ActualRate)
{
	if (!HasAuthority() || !Production || !BottleFiller || Socket!=GetReceivingHosePort(Production->View.SelectedContentPort)) return;
	BottleFiller->NotifyHoseDelivery(Hose,ActualUnits,IncomingPressure,ActualRate);
}

void AManualColaFillingMachine::HandleSupplySocketOverloaded(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float IncomingPressure,float OfferedRate)
{
	if (!HasAuthority() || !Socket || !Production) return;
	const int32 PortIndex=HoseSockets.IndexOfByKey(Cast<UHoseSocketComponent>(Socket));
	if (PortIndex==INDEX_NONE) return;
	Socket->SetFlowRequested(false);
	if (Production->View.SelectedContentPort==PortIndex)
	{
		Production->View.bFlowStopped=true;
		Production->EndManualLever();
	}
	auto* Manager=APropManager::Get(this);
	const auto* Equipment=Manager ? Manager->FindUniqueData(ISID) : nullptr;
	if (Equipment && Equipment->EventData.ConnectedSupplyISIDs.IsValidIndex(PortIndex))
	{
		const auto& EquipmentState=Equipment->EventData;
		const int32 SourceID=EquipmentState.ConnectedSupplyISIDs[PortIndex];
		FPropEventSyncDelta Delta; Delta.ISID=ISID;
		Delta.ExtendedDirtyMask=EPropExtendedEventMask::ManualEquipment|EPropExtendedEventMask::LinkedSupply;
		Delta.ConnectedSupplyISIDs=EquipmentState.ConnectedSupplyISIDs; Delta.ConnectedSupplyISIDs[PortIndex]=0;
		Delta.SelectedSupplyPort=EquipmentState.SelectedSupplyPort; Delta.TemporarySealCharges=EquipmentState.TemporarySealCharges;
		Delta.LoadedCapISIDs=EquipmentState.LoadedCapISIDs; Delta.LoadedSealISIDs=EquipmentState.LoadedSealISIDs; Delta.StoredInMachineISID=EquipmentState.StoredInMachineISID;
		Delta.LinkedSupplyISID=Delta.ConnectedSupplyISIDs.IsValidIndex(Delta.SelectedSupplyPort) ? Delta.ConnectedSupplyISIDs[Delta.SelectedSupplyPort] : 0;
		TArray<FPropEventSyncDelta> Updates; Updates.Add(Delta);
		if (const auto* SourceData=Manager->FindUniqueData(SourceID); SourceData && SourceData->EventData.StoredInMachineISID==ISID)
		{
			FPropEventSyncDelta Source; Source.ISID=SourceID; Source.ExtendedDirtyMask=EPropExtendedEventMask::ManualEquipment;
			Source.ConnectedSupplyISIDs=SourceData->EventData.ConnectedSupplyISIDs; Source.SelectedSupplyPort=SourceData->EventData.SelectedSupplyPort;
			Source.TemporarySealCharges=SourceData->EventData.TemporarySealCharges; Source.LoadedCapISIDs=SourceData->EventData.LoadedCapISIDs;
			Source.LoadedSealISIDs=SourceData->EventData.LoadedSealISIDs; Source.StoredInMachineISID=0; Updates.Add(Source);
		}
		Manager->SubmitAtomicInstanceEventUpdates(Updates);
	}
	// Removing the canonical connection prevents RestoreSupplyNozzles from remounting a popped nozzle.
	DisconnectSupplyNozzle(PortIndex);
	ForceNetUpdate();
}
