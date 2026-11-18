#include "Components/WorldHitResponseComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Interfaces/WorldInteractionHitLibrary.h"

UWorldHitResponseComponent::UWorldHitResponseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldHitResponseComponent::ReceiveWorldHit_Implementation(const FWorldInteractionHit& Data)
{
	if (!GetOwner() || Data.TargetActor!=GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !Data.EventID.IsValid() || LastEventID==Data.EventID) return;
	LastEventID=Data.EventID;
	LastWorldHit=Data;
	if (bApplyHitForce)
	{
		auto* Primitive=ResponsePrimitive.Get();
		if (!Primitive && Data.TargetComponent && Data.TargetComponent->GetOwner()==GetOwner()) Primitive=Data.TargetComponent;
		if (!Primitive) Primitive=Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
		UWorldInteractionHitLibrary::ApplyBoundedWorldHitImpulse(Primitive,Data,ImpulseScale,MaxSpeedChange);
	}
	OnWorldHitReceived.Broadcast(Data);
}
