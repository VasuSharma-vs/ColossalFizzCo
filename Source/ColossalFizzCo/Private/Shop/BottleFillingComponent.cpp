#include "Shop/BottleFillingComponent.h"
#include "Props/InteractablePropManager.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Shop/ShopProductionStationComponent.h"
#include "GameFramework/Pawn.h"

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
}

bool UBottleFillingComponent::ConnectionsInRange() const
{
	if (!Manager.IsValid() || !GetOwner() || !FMath::IsFinite(MaxConnectionDistance) || MaxConnectionDistance <= 0 || !Manager->FindISIDData(SourceISID) || !Manager->FindISIDData(TargetISID)) return false;
	if (bRequireLever && (!LeverOperator.IsValid() || GetWorld()->GetTimeSeconds()>=LeverExpiresAt
		|| FVector::DistSquared(LeverOperator->GetActorLocation(),GetOwner()->GetActorLocation())>FMath::Square(400.f))) return false;
	if (bRequireLever)
	{
		const auto* Pawn=Cast<APawn>(LeverOperator.Get());
		const auto* Station=GetOwner()->FindComponentByClass<UShopProductionStationComponent>();
		if (!Pawn || !Pawn->GetController() || !Station || !Station->IsBottleAligned(TargetISID)) return false;
	}
	const FVector Origin = GetOwner()->GetActorLocation();
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
	if (TargetContentLevel > 0 && (!TargetData || TargetData->EventData.ContentLevel >= TargetContentLevel))
	{ SourceISID = TargetISID = 0; return false; }
	SessionID = Manager->BeginLiquidTransfer(Source, Target);
	if (!SessionID.IsValid()) { SourceISID = TargetISID = 0; return false; }
	bIsFilling = true; AccumulatedSeconds = 0; SetComponentTickEnabled(true); GetOwner()->ForceNetUpdate(); return true;
}

void UBottleFillingComponent::StopFilling()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (Manager.IsValid()) Manager->EndLiquidTransfer(SessionID);
	SessionID.Invalidate(); bIsFilling = false; AccumulatedSeconds = 0;
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
		float Requested = PortionsPerSecond * Step;
		if (!FMath::IsFinite(TargetContentLevel) || TargetContentLevel < 0) { StopFilling(); break; }
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
		if (Requested <= 0 || Manager->TransferLiquid(SessionID, Requested) <= 0) { StopFilling(); break; }
		if (PressurePerContentLevel>0)
		{
			const auto* After=Manager->FindUniqueData(TargetISID);
			if (After)
			{
				if (PreviousLevel<=0)
				{
					FPropEventSyncDelta D; D.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal; D.bManualFillPressure=true;
					Manager->SubmitInstanceEventUpdate(TargetISID,D);
				}
				After=Manager->FindUniqueData(TargetISID);
				Manager->SetBottleAgitationPressure(TargetISID,(PreviousLevel<=0 ? 0.f : After->ContinuousData.SealedPressure)+(After->EventData.ContentLevel-PreviousLevel)*PressurePerContentLevel);
			}
		}
	}
}

void UBottleFillingComponent::EndPlay(const EEndPlayReason::Type Reason) { StopFilling(); Super::EndPlay(Reason); }
