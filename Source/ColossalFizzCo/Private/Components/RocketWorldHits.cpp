#include "Components/RocketProjectileComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Props/PropAssemblyMesh.h"
#include "Interfaces/WorldInteractionHitLibrary.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"

void URocketProjectileComponent::ConfigureFlightCollision(bool bPhysicsSimulation)
{
	if (!TargetPrimitive) return;
	TargetPrimitive->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	TargetPrimitive->SetCollisionEnabled(bUseOverlapHitEvents && !bPhysicsSimulation ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
	const auto DynamicResponse=bUseOverlapHitEvents ? ECR_Overlap : ECR_Block;
	TargetPrimitive->SetCollisionResponseToChannel(ECC_Pawn,DynamicResponse);
	TargetPrimitive->SetCollisionResponseToChannel(ECC_PhysicsBody,DynamicResponse);
	TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic,DynamicResponse);
	// The project also defines object channels for permanent instances and their active representation.
	for (int32 Channel=0;Channel<int32(ECC_MAX);++Channel)
	{
		const FName Name=UCollisionProfile::Get()->ReturnChannelNameFromContainerIndex(Channel);
		if (Name==TEXT("ISM") || Name==TEXT("ActiveISM"))
			TargetPrimitive->SetCollisionResponseToChannel(ECollisionChannel(Channel),DynamicResponse);
	}
	TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
	if (bUseOverlapHitEvents)
	{
		TargetPrimitive->SetGenerateOverlapEvents(true);
		TargetPrimitive->OnComponentBeginOverlap.AddUniqueDynamic(this,&URocketProjectileComponent::HandleRocketOverlap);
	}
}

void URocketProjectileComponent::ResetWorldHitHistory()
{
	RecentWorldHitTargets.Reset();
	PreviousWorldHitCenter=TargetPrimitive ? GetWorldCenterLocation() : FVector::ZeroVector;
	bHasPreviousWorldHitCenter=TargetPrimitive!=nullptr;
}

bool URocketProjectileComponent::TraceFlightSurface(FHitResult& Hit,const FVector& Start,const FVector& End,ECollisionChannel Channel,const FCollisionQueryParams& Params) const
{
	if (!GetWorld()) return false;
	const FCollisionResponseParams Responses=bUseOverlapHitEvents && TargetPrimitive
		? FCollisionResponseParams(TargetPrimitive->GetCollisionResponseToChannels()) : FCollisionResponseParams::DefaultResponseParam;
	return GetWorld()->LineTraceSingleByChannel(Hit,Start,End,Channel,Params,Responses);
}

void URocketProjectileComponent::HandleRocketOverlap(UPrimitiveComponent* OverlappedComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,bool bFromSweep,const FHitResult& SweepResult)
{
	if (!bUseOverlapHitEvents || !IsActive() || CurrentState==ERocketSimulationState::Idle || CurrentState==ERocketSimulationState::AtRest
		|| !OtherActor || !OtherComponent) return;
	FHitResult Hit=SweepResult;
	if (!bFromSweep) Hit=FHitResult(OtherActor,OtherComponent,OtherComponent->Bounds.Origin,FVector::ZeroVector);
	Hit.Component=OtherComponent;
	Hit.Item=OtherBodyIndex!=INDEX_NONE ? OtherBodyIndex : Hit.Item;
	const FVector Velocity=TargetPrimitive && TargetPrimitive->IsSimulatingPhysics() ? TargetPrimitive->GetPhysicsLinearVelocity() : CurrentVelocity;
	ReportWorldHit(Hit,Velocity);
}

bool URocketProjectileComponent::ReportWorldHit(const FHitResult& Hit,const FVector& ImpactVelocity)
{
	AActor* Owner=GetOwner();
	UPrimitiveComponent* Component=Hit.GetComponent();
	AActor* Target=Hit.GetActor();
	if (!Owner || !Owner->HasAuthority() || GetNetMode()==NM_Client || !GetWorld() || !IsValid(Component) || !IsValid(Target)
		|| Target==Owner || (Target==EjectedCap.Get() && EjectedCap->ISID==EjectedCapISID && EjectedCap->bIsActive) || ImpactVelocity.ContainsNaN()) return false;
	FWorldInteractionHit Data;
	Data.SourceActor=Owner; Data.SourceComponent=TargetPrimitive;
	Data.TargetActor=Target; Data.TargetComponent=Component; Data.Hit=Hit;
	Data.Velocity=ImpactVelocity;
	Data.RelativeVelocity=ImpactVelocity-Component->GetComponentVelocity();
	Data.MassKg=TargetPrimitive ? FMath::Max(.001f,TargetPrimitive->GetMass()) : 1.f;
	Data.bApplyHitForce=bApplyHitForceOnTargets;
	APropManager* Manager=APropManager::Get(Owner);
	if (const auto* ISM=Cast<UInteractableISMComponent>(Component))
	{
		Data.ICID=ISM->IISMComponentID;
		Data.ISMI=ISM->ResolveHitISMI(Hit,Owner,TargetPrimitive);
		Data.ISID=Manager ? Manager->GetISIDFromISMI(Data.ICID,Data.ISMI) : 0;
		if (Data.ISID<=0) return false; // Never promote an arbitrary neighbour when an overlap has no valid body index.
	}
	else if (const auto* Piece=Cast<UPropAssemblyMesh>(Component)) Data.ISID=Piece->ISID;
	else if (const auto* Active=Cast<AActivePropActor>(Target)) Data.ISID=Active->ISID;
	if (Manager && Data.ISID>0) Data.ISID=Manager->ResolveRootISID(Data.ISID);
	if (const auto* Source=Cast<AActivePropActor>(Owner))
		if (Manager && Source->ISID>0 && Data.ISID>0 && Manager->ResolveRootISID(Source->ISID)==Data.ISID) return false;
	const uint64 Key=Data.ISID>0 ? (uint64(1)<<63)|uint64(Data.ISID) : uint64(Target->GetUniqueID());
	const double Now=GetWorld()->GetTimeSeconds();
	if (const double* Last=RecentWorldHitTargets.Find(Key); Last && Now-*Last<FMath::Max(.01f,WorldHitRepeatDelay)) return false;
	if (RecentWorldHitTargets.Num()>=256) RecentWorldHitTargets.Reset();
	RecentWorldHitTargets.Add(Key,Now);
	Data.EventID=FGuid::NewGuid();
	const FVector Direction=Data.RelativeVelocity.GetSafeNormal();
	if (Data.Hit.ImpactNormal.IsNearlyZero()) Data.Hit.ImpactNormal=-Direction;
	Data.Impulse=Direction*FMath::Max(0.f,float(-FVector::DotProduct(Data.RelativeVelocity,Data.Hit.ImpactNormal)))*Data.MassKg;
	const bool Delivered=UWorldInteractionHitLibrary::DispatchWorldHit(Data);
	// Unrelated trigger volumes are not physical impacts on the bottle.
	if (Delivered || Hit.bBlockingHit) OnRocketHitTarget.Broadcast(Target,Data.Hit);
	return Delivered || Hit.bBlockingHit;
}

bool URocketProjectileComponent::SweepWorldHitContacts(const FVector& Start,const FVector& End,const FQuat& Rotation,FHitResult& OutBlockingHit)
{
	if (!GetWorld() || !TargetPrimitive) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RocketWorldHits),false,GetOwner());
	IgnoreLaunchDebris(Params);
	Params.bFindInitialOverlaps=true;
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByChannel(Hits,Start,End,Rotation,CollisionChannel,FCollisionShape::MakeSphere(SweepRadius),Params,
		FCollisionResponseParams(TargetPrimitive->GetCollisionResponseToChannels()));
	const FVector Velocity=TargetPrimitive->IsSimulatingPhysics() ? TargetPrimitive->GetPhysicsLinearVelocity() : CurrentVelocity;
	bool Blocked=false;
	for (const auto& Hit:Hits)
	{
		ReportWorldHit(Hit,Velocity);
		if (Hit.bBlockingHit && (!Blocked || Hit.Time<OutBlockingHit.Time)) { OutBlockingHit=Hit; Blocked=true; }
	}
	return Blocked;
}

void URocketProjectileComponent::TickPhysicsWorldHits()
{
	if (!bUseOverlapHitEvents || !TargetPrimitive || !GetOwner() || !GetOwner()->HasAuthority()) return;
	const FVector Center=GetWorldCenterLocation();
	FHitResult Ignored;
	if (bHasPreviousWorldHitCenter) SweepWorldHitContacts(PreviousWorldHitCenter,Center,TargetPrimitive->GetComponentQuat(),Ignored);
	PreviousWorldHitCenter=Center;
	bHasPreviousWorldHitCenter=true;
}
