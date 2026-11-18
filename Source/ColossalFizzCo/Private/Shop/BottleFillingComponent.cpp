#include "Shop/BottleFillingComponent.h"
#include "Props/InteractablePropManager.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Shop/ShopProductionStationComponent.h"
#include "GameFramework/Pawn.h"
#include "Props/ColaPropActor.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ColaSupplyContainer.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"

UBottleFillingComponent::UBottleFillingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UBottleFillingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBottleFillingComponent, bIsFilling);
	DOREPLIFETIME(UBottleFillingComponent, SourceISID);
	DOREPLIFETIME(UBottleFillingComponent, TargetISID);
	DOREPLIFETIME(UBottleFillingComponent, DeliveredPortionsPerSecond);
	DOREPLIFETIME(UBottleFillingComponent, DeliveredSupplyPressure);
}

bool UBottleFillingComponent::ConnectionsInRange() const
{
	if (!Manager.IsValid() || !GetOwner() || !FMath::IsFinite(MaxConnectionDistance) || MaxConnectionDistance <= 0 || !Manager->FindISIDData(TargetISID)) return false;
	if (bRequireLever && (!LeverOperator.IsValid() || GetWorld()->GetTimeSeconds()>=LeverExpiresAt
		|| FVector::DistSquared(LeverOperator->GetActorLocation(),GetOwner()->GetActorLocation())>FMath::Square(400.f))) return false;
	if (bRequireLever)
	{
		const auto* Pawn=Cast<APawn>(LeverOperator.Get());
		const auto* Station=GetOwner()->FindComponentByClass<UShopProductionStationComponent>();
		if (!Pawn || !Pawn->GetController() || !Station || !Station->IsLeverHoldingBottle(TargetISID) || !Station->IsBottleReadyForFilling(TargetISID)) return false;
	}
	const FVector Origin = GetOwner()->GetActorLocation();
	if (const auto* Machine=Cast<AManualColaFillingMachine>(GetOwner()); Machine && bRequireLever)
		return Machine->Production && Machine->IsSupplyInRange(Machine->Production->View.SelectedContentPort)
			&& FVector::DistSquared(Origin,Manager->GetTransform(TargetISID).GetLocation())<=FMath::Square(MaxConnectionDistance);
	if (!Manager->FindISIDData(SourceISID)) return false;
	return FVector::DistSquared(Origin, Manager->GetTransform(SourceISID).GetLocation()) <= FMath::Square(MaxConnectionDistance)
		&& FVector::DistSquared(Origin, Manager->GetTransform(TargetISID).GetLocation()) <= FMath::Square(MaxConnectionDistance);
}

bool UBottleFillingComponent::StartFilling(int32 Source, int32 Target)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bIsFilling || !FMath::IsFinite(PortionsPerSecond) || PortionsPerSecond <= 0
		|| !FMath::IsFinite(TargetContentLevel) || TargetContentLevel < 0) return false;
	Manager = APropManager::Get(this); SourceISID = Source; TargetISID = Target;
	if (!ConnectionsInRange()) { SourceISID = TargetISID = 0; return false; }
	const auto* TargetData = Manager->FindUniqueData(Target);
	if (bRequireLever)
	{
		const auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Target));
		const auto* SourceData=Manager->FindUniqueData(Source);
		const auto* Machine=Cast<AManualColaFillingMachine>(GetOwner());
		const int32 ContentID=Machine && Machine->Production ? Machine->GetSupplyContentID(Machine->Production->View.SelectedContentPort)
			: (SourceData ? SourceData->EventData.ContentID : 0);
		if (!Bottle || !Bottle->CanAcceptManufacturingContent(ContentID)) { SourceISID=TargetISID=0; return false; }
	}
	if (TargetContentLevel > 0 && (!TargetData || TargetData->EventData.ContentLevel >= TargetContentLevel))
	{ SourceISID = TargetISID = 0; return false; }
	if (auto* Machine=Cast<AManualColaFillingMachine>(GetOwner()); Machine && bRequireLever && Machine->Production)
	{
		const int32 PortIndex=Machine->Production->View.SelectedContentPort;
		auto* Hose=Machine->GetSupplyHoseForPort(PortIndex); auto* Port=Machine->GetReceivingHosePort(PortIndex);
		const int32 ContentID=Machine->GetSupplyContentID(PortIndex);
		if (!Hose || !Port) { SourceISID=TargetISID=0; return false; }
		if (Machine->HosePortContentIDs.IsValidIndex(PortIndex) && Machine->HosePortContentIDs[PortIndex]>0
			&& Machine->HosePortContentIDs[PortIndex]!=ContentID) { SourceISID=TargetISID=0; return false; }
		Port->SetFlowRequested(false);
		if (!Port->ConfigureManagedReceivingProxy(Manager.Get(),Target,ContentID)) { SourceISID=TargetISID=0; return false; }
		Port->MaximumManagedContentLevel=TargetContentLevel;
		auto* SourcePort=Hose->SourceEndpoint.Get();
		if (!SourcePort) if (auto* Container=Cast<AColaSupplyContainer>(Hose->GetOwner())) SourcePort=Container->SupplyPort;
		Port->SetFlowRequested(true);
		if (!Hose->HasValidConnection())
		{
			Hose->Category=EResourceTransportCategory::Content; Hose->ResourceID=ContentID;
			if (!Hose->ConnectEndpoints(SourcePort,Port)) { Port->SetFlowRequested(false); SourceISID=TargetISID=0; return false; }
		}
		if (Hose->TargetEndpoint!=Port) { Port->SetFlowRequested(false); SourceISID=TargetISID=0; return false; }
		ActiveHose=Hose; ActiveReceivingPort=Port; bRestoreHoseTick=Hose->IsComponentTickEnabled();
		Hose->SetComponentTickEnabled(false);
		bIsFilling=true; AccumulatedSeconds=0; DeliveredPortionsPerSecond=DeliveredSupplyPressure=0;
		SetComponentTickEnabled(true); GetOwner()->ForceNetUpdate(); return true;
	}
	SessionID = Manager->BeginLiquidTransfer(Source, Target);
	if (!SessionID.IsValid()) { SourceISID = TargetISID = 0; return false; }
	bIsFilling = true; AccumulatedSeconds = 0; SetComponentTickEnabled(true); GetOwner()->ForceNetUpdate(); return true;
}

void UBottleFillingComponent::StopFilling()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (auto* Port=ActiveReceivingPort.Get())
	{
		Port->SetFlowRequested(false);
		Port->MaximumManagedContentLevel=0.f;
		if (Manager.IsValid() && Port->IsManagedReceivingProxy()) Port->ConfigureManagedReceivingProxy(Manager.Get(),0,Port->ResourceID);
	}
	if (auto* Hose=ActiveHose.Get()) Hose->SetComponentTickEnabled(bRestoreHoseTick);
	ActiveHose.Reset(); ActiveReceivingPort.Reset(); bRestoreHoseTick=false;
	if (Manager.IsValid()) Manager->EndLiquidTransfer(SessionID);
	SessionID.Invalidate(); bIsFilling = false; AccumulatedSeconds = 0;
	DeliveredPortionsPerSecond=DeliveredSupplyPressure=0;
	SetComponentTickEnabled(false); GetOwner()->ForceNetUpdate();
}

void UBottleFillingComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bIsFilling) return;
	if (!ConnectionsInRange() || !FMath::IsFinite(Dt) || Dt < 0 || !FMath::IsFinite(PortionsPerSecond) || PortionsPerSecond <= 0) { StopFilling(); return; }
	AccumulatedSeconds += Dt;
	// Fixed steps retain elapsed time across frames and bound work during a frame hitch.
	constexpr float Step = 0.05f;
	for (int32 I = 0; bIsFilling && I < 8 && AccumulatedSeconds >= Step; ++I)
	{
		AccumulatedSeconds -= Step;
		if (!FMath::IsFinite(TargetContentLevel) || TargetContentLevel<0) { StopFilling(); break; }
		if (ActiveReceivingPort.IsValid() && !ActiveHose.IsValid()) { StopFilling(); break; }
		if (auto* Hose=ActiveHose.Get())
		{
			auto* Port=ActiveReceivingPort.Get();
			if (!Port || Hose->TargetEndpoint!=Port || !Hose->HasValidConnection() || !ConnectionsInRange()) { StopFilling(); break; }
			const auto* Data=Manager->FindUniqueData(TargetISID);
			if (!Data || (TargetContentLevel>0 && Data->EventData.ContentLevel>=TargetContentLevel)) { StopFilling(); break; }
			Port->MaximumManagedContentLevel=TargetContentLevel;
			DeliveredPortionsPerSecond=DeliveredSupplyPressure=0;
			const float Delivered=Hose->TransferForDuration(Step);
			// Delivery callbacks may launch the bottle or pop this socket. Never resume that fill in the same frame.
			if (!bIsFilling || ActiveHose.Get()!=Hose) break;
			if (Hose->bBurst || !Hose->HasValidConnection()) { StopFilling(); break; }
			DeliveredPortionsPerSecond=Delivered/Step;
			if (TargetContentLevel>0)
				if (const auto* Updated=Manager->FindUniqueData(TargetISID); Updated && Updated->EventData.ContentLevel>=TargetContentLevel)
				{ StopFilling(); break; }
			if (Delivered<=0)
			{
				// A closed source valve is a paused request, not an overfill. Only the bottle decides capacity failure.
				if (Port->GetFreeCapacityUnits()<=UE_SMALL_NUMBER)
				{
					auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(TargetISID));
					const bool Failed=Bottle && Bottle->EvaluateManufacturingPressureFailure(true);
					StopFilling();
					if (Failed) if (auto* Station=GetOwner()->FindComponentByClass<UShopProductionStationComponent>()) Station->HandleManualOverfill(TargetISID);
					break;
				}
			}
			continue;
		}
		float Requested = PortionsPerSecond * Step;
		if (TargetContentLevel > 0)
		{
			const auto* Record = Manager->FindISIDData(TargetISID);
			const auto* Data = Manager->FindUniqueData(TargetISID);
			const auto* Product = Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
			if (!Data || !Product) { StopFilling(); break; }
			Requested = FMath::Min(Requested, FMath::Max(0.f, TargetContentLevel - Data->EventData.ContentLevel) * Product->ConsumableProfile.NumOfPortions);
		}
		const auto* Before=Manager->FindUniqueData(TargetISID);
		const float PreviousLevel=Before ? Before->EventData.ContentLevel : 0;
		if (Requested <= 0 || Manager->TransferLiquid(SessionID, Requested) <= 0)
		{
			StopFilling();
			if (bRequireLever)
				if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(TargetISID)))
					if (Bottle->EvaluateManufacturingPressureFailure(true))
						if (auto* Station=GetOwner()->FindComponentByClass<UShopProductionStationComponent>()) Station->HandleManualOverfill(TargetISID);
			break;
		}
		if (bRequireLever)
			if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(TargetISID)))
			{
				Bottle->OnManufacturingLiquidReceived(PreviousLevel);
				if (Bottle->bManufacturingFailureDispatched || Bottle->AcceptedFailureType!=EContainmentFailureType::None
					|| !Bottle->IsSeatedInFiller() || Bottle->bIsInMotion)
				{
					StopFilling();
					if (auto* Station=GetOwner()->FindComponentByClass<UShopProductionStationComponent>()) Station->HandleManualOverfill(TargetISID);
				}
			}

	}
}

void UBottleFillingComponent::NotifyHoseDelivery(UHoseTransferComponent* Hose,float ActualUnits,float IncomingPressure,float ActualRate)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bIsFilling || ActiveHose.Get()!=Hose || !Manager.IsValid()
		|| !FMath::IsFinite(ActualUnits) || ActualUnits<=0 || !FMath::IsFinite(IncomingPressure) || IncomingPressure<0
		|| !FMath::IsFinite(ActualRate) || ActualRate<0) return;
	DeliveredPortionsPerSecond=ActualRate; DeliveredSupplyPressure=IncomingPressure;
	if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(TargetISID)))
	{
		Bottle->OnManufacturingSupplyReceived(ActualUnits,IncomingPressure,ActualRate);
		if (Bottle->bManufacturingFailureDispatched || Bottle->AcceptedFailureType!=EContainmentFailureType::None
			|| !Bottle->IsSeatedInFiller() || Bottle->bIsInMotion)
		{
			StopFilling();
			if (auto* Station=GetOwner()->FindComponentByClass<UShopProductionStationComponent>()) Station->HandleManualOverfill(TargetISID);
		}
	}
}

void UBottleFillingComponent::EndPlay(const EEndPlayReason::Type Reason) { StopFilling(); Super::EndPlay(Reason); }
