#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Interfaces/WorldInteractionHitLibrary.h"

bool APropManager::ReceiveWorldHitByICID(int32 InICID,int32 InISMI,const FWorldInteractionHit& Data)
{
	if (!HasAuthority() || GetNetMode()==NM_Client) return false;
	const auto* Component=FindIISMComponent(InICID);
	if (!Component || Data.TargetComponent!=Component || !Component->IsValidInstance(InISMI)
		|| Component->IsEmptyInstance(InISMI) || Component->IsInstanceParkedAtResetTransform(InISMI)) return false;
	const int32 TargetISID=GetISIDFromISMI(InICID,InISMI);
	if (TargetISID<=0) return false;
	FWorldInteractionHit Resolved=Data;
	Resolved.ICID=InICID; Resolved.ISMI=InISMI; Resolved.ISID=TargetISID;
	return ReceiveWorldHitByISID(TargetISID,Resolved);
}

bool APropManager::ReceiveWorldHitByISID(int32 InISID,const FWorldInteractionHit& Data)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !Data.EventID.IsValid() || !IsValid(Data.SourceActor)
		|| Data.SourceActor->GetWorld()!=GetWorld() || !Data.SourceActor->HasAuthority() || Data.Impulse.ContainsNaN()
		|| Data.Velocity.ContainsNaN() || RecentWorldHitEvents.Contains(Data.EventID)) return false;
	InISID=ResolveRootISID(InISID);
	const auto* Record=FindISIDData(InISID);
	if (!Record || InISID<=0 || Record->State==EPropState::Held) return false;
	if (const auto* Source=Cast<AActivePropActor>(Data.SourceActor))
		if (Source->ISID>0 && ResolveRootISID(Source->ISID)==InISID) return false;
	const bool NeedsPromotion=Record->Representation!=EPropRepresentation::ASM;
	if (NeedsPromotion && Data.Impulse.Size()<FMath::Max(0.f,WorldHitPromotionImpulseThreshold)) return false;
	AActivePropActor* Active=GetActiveActorFromISID(InISID);
	if (!Active)
	{
		// Promotion must not manufacture a kick or process damage before the receiver sees the original event.
		if (!SetInstanceToUnsettled(InISID,FVector::ZeroVector,Data.Hit.ImpactPoint,-1.f,false)) return false;
		Active=GetActiveActorFromISID(InISID);
	}
	if (!IsValid(Active) || Active->bIsHeld) return false;
	if (RecentWorldHitEvents.Num()>=256) RecentWorldHitEvents.Reset();
	RecentWorldHitEvents.Add(Data.EventID,GetWorld()->GetTimeSeconds());
	FWorldInteractionHit Resolved=Data;
	Resolved.ISID=InISID;
	Resolved.TargetActor=Active;
	Resolved.TargetComponent=Active->PropMesh;
	Resolved.Hit.Component=Active->PropMesh;
	Resolved.Hit.HitObjectHandle=FActorInstanceHandle(Active);
	return UWorldInteractionHitLibrary::DispatchWorldHit(Resolved);
}
