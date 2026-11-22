#include "Components/PropGrabInputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

EPropGrabIntent FPropGrabGesture::Process(EPropGrabInputPhase Phase, double Now, uint64 HeldToken, double HoldSeconds, double DoubleTapSeconds)
{
	if (!FMath::IsFinite(Now) || !FMath::IsFinite(HoldSeconds) || !FMath::IsFinite(DoubleTapSeconds)
		|| HoldSeconds <= 0 || DoubleTapSeconds <= 0) return EPropGrabIntent::None;
	if (Phase == EPropGrabInputPhase::Started)
	{
		bActive = true; bPerformed = false; StartedAt = Now; StartedToken = HeldToken;
		return EPropGrabIntent::None;
	}
	if (!bActive) return EPropGrabIntent::None;
	const bool bEnding = Phase != EPropGrabInputPhase::Ongoing;
	EPropGrabIntent Intent = EPropGrabIntent::None;
	if (!bPerformed && Now >= StartedAt + HoldSeconds)
	{
		bPerformed = true;
		LastTapToken = 0;
		// A pickup during this press must not become placement of the newly held object.
		if (HeldToken == StartedToken) Intent = StartedToken ? EPropGrabIntent::Place : EPropGrabIntent::Grab;
	}
	if (bEnding)
	{
		bActive = false;
		if (!bPerformed && StartedToken && HeldToken == StartedToken)
		{
			if (LastTapToken == HeldToken && Now >= LastTapAt && Now - LastTapAt <= DoubleTapSeconds)
			{
				Intent = EPropGrabIntent::Drop;
				LastTapToken = 0;
			}
			else { LastTapToken = HeldToken; LastTapAt = Now; }
		}
	}
	return Intent;
}

UPropGrabInputComponent::UPropGrabInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UPropGrabInputComponent::BeginPlay()
{
	Super::BeginPlay();
	// CharacterMovement's default 750,000 push force overwhelms even giant props.
	// Apply on every peer through the existing player component; NPC movement is untouched.
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->InitialPushForceFactor = .02f;
			Movement->PushForceFactor = .5f;
			Movement->bPushForceScaledToMass = true;
			Movement->bTouchForceScaledToMass = true;
			Movement->TouchForceFactor = .001f;
			Movement->MinTouchForce = 0.f;
			Movement->MaxTouchForce = .005f;
			Movement->RepulsionForce = .0001f;
		}
	}
	if (!GetWorld() || !GetWorld()->GetWorldSettings()->ActorHasTag(TEXT("NormalColaPrototype"))) return;
	// The prototype has no combat attack window. Its legacy idle hand overlaps must not apply punches.
	TArray<UPrimitiveComponent*> Parts; GetOwner()->GetComponents(Parts);
	for (auto* Part:Parts) if (Part->GetName().Contains(TEXT("Hand")) && Part->GetName().Contains(TEXT("Collision"))) Part->SetGenerateOverlapEvents(false);
}

void UPropGrabInputComponent::ProcessGrabInput(EPropGrabInputPhase Phase, AActor* HeldActor, UPrimitiveComponent* HeldComponent)
{
	auto* CableActions=GetOwner()?GetOwner()->FindComponentByClass<UShopPlayerInteractionComponent>():nullptr;
	const bool bHoldingProp=IsValid(HeldComponent)||IsValid(HeldActor);
	const bool bHoldingCable=!bHoldingProp && CableActions && CableActions->IsCableGrabActive();
	uint64 Token = 0;
	if (IsValid(HeldComponent)) Token = (uint64(HeldComponent->GetUniqueID()) << 32) | 1;
	else if (IsValid(HeldActor))
	{
		const auto* Prop = Cast<AActivePropActor>(HeldActor);
		Token = (uint64(HeldActor->GetUniqueID()) << 32) | uint32(Prop ? Prop->ISID : 1);
	}
	else if(bHoldingCable) Token=(uint64(CableActions->GetGrabbedCable()->GetUniqueID())<<32)|2;
	LastIntent = GetWorld() ? Gesture.Process(Phase, GetWorld()->GetTimeSeconds(), Token, HoldSeconds, DoubleTapSeconds) : EPropGrabIntent::None;
	const bool bEnding = Phase != EPropGrabInputPhase::Ongoing && Phase != EPropGrabInputPhase::Started;
	if (bTravelKeyHeld)
	{
		if (bEnding)
		{
			bTravelKeyHeld = false;
			Server_EndActorTravel(LocalTravelSequence);
		}
		LastIntent = EPropGrabIntent::None;
		return;
	}
	if (CableActions && bEnding && CableActions->FinishHosePickupPress())
	{
		LastIntent = EPropGrabIntent::None;
		return;
	}
	if (CableActions && Phase==EPropGrabInputPhase::Started && !bHoldingProp && !bHoldingCable
		&& CableActions->TryGrabLooseHoseFromView(true)) return;
	if (CableActions && (LastIntent==EPropGrabIntent::Grab || LastIntent==EPropGrabIntent::Place)
		&& CableActions->TryGrabSocketHoseFromView(LastIntent==EPropGrabIntent::Grab, true))
	{
		if (bEnding) CableActions->FinishHosePickupPress();
		LastIntent=EPropGrabIntent::None;
		return;
	}
	if(bHoldingCable)
	{
		if (LastIntent==EPropGrabIntent::Drop) CableActions->RequestCableDrop();
		else if (LastIntent==EPropGrabIntent::Place) CableActions->RequestCableRelease();
		// Cable grabs never become legacy actor/component attachments or placement.
		LastIntent=EPropGrabIntent::None;
	}
	else if(!bHoldingProp && LastIntent==EPropGrabIntent::Grab && CableActions && CableActions->TryGrabCableOnHoldFromView())
	{
		if (bEnding) CableActions->FinishHosePickupPress();
		LastIntent = EPropGrabIntent::None;
	}
	else if ((LastIntent == EPropGrabIntent::Grab || LastIntent == EPropGrabIntent::Place) && !IsValid(HeldComponent)
		&& TryBeginActorTravel(Phase, HeldActor)) LastIntent = EPropGrabIntent::None;
}

bool UPropGrabInputComponent::CanGrabHitComponent(const UPrimitiveComponent* HittingComponent) const
{
	if (!IsValid(HittingComponent) || HittingComponent->GetWorld()!=GetWorld() || !IsValid(HittingComponent->GetOwner())) return false;
	const auto* Prop=Cast<AActivePropActor>(HittingComponent->GetOwner());
	return !Prop || Prop->AllowsPlayerGrab();
}

bool UPropGrabInputComponent::CanGrabTarget(AActor* Target) const
{
	if (!IsValid(Target) || Target->GetWorld() != GetWorld() || !IsValid(GetOwner())) return false;
	if (const auto* Prop = Cast<AActivePropActor>(Target))
	{
		const auto* Manager = APropManager::Get(this);
		return Manager && Prop->ISID > 0 && Manager->GetActiveActorFromISID(Prop->ISID) == Prop && Manager->ValidateGrab(Prop->ISID, GetOwner());
	}
	return !IsActorInAnotherPlayerHand(Target); // Other actor types still use their interface validation.
}

bool UPropGrabInputComponent::OwnsHeldTarget(AActor* Target) const
{
	if (!IsValid(Target) || Target->GetWorld() != GetWorld() || !IsValid(GetOwner())) return false;
	if (const auto* Prop = Cast<AActivePropActor>(Target))
	{
		const auto* Manager = APropManager::Get(this);
		const auto* Record = Manager ? Manager->FindISIDData(Prop->ISID) : nullptr;
		return Record && Record->ActiveActor == Prop && Record->State == EPropState::Held && Record->RuntimeData.InteractionOwner.Get() == GetOwner();
	}
	for (AActor* Parent = Target->GetAttachParentActor(); IsValid(Parent); Parent = Parent->GetAttachParentActor())
		if (Parent == GetOwner()) return true;
	return false;
}
