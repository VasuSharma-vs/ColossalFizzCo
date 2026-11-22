#include "Components/PropGrabInputComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractablePropManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "../Shop/WorldInteractionCarry.h"

namespace
{
	constexpr float GrabTraceLength = 250.f;
	const FName HoldingSocket(TEXT("Holding object"));

	void GetGrabView(const ACharacter& Player, FVector& Eye, FRotator& Aim)
	{
		const auto* Controller = Cast<APlayerController>(Player.GetController());
		if (Controller && Controller->IsLocalController()) Controller->GetPlayerViewPoint(Eye, Aim);
		else Player.GetActorEyesViewPoint(Eye, Aim);
	}

	bool TraceGrab(const ACharacter& Player, const FVector& Start, const FVector& End, FHitResult& Hit, const AActor* Ignore = nullptr)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerPropGrab), true, &Player);
		if (Ignore) Params.AddIgnoredActor(Ignore);
		return Player.GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	}

	int32 GetHitISID(const APropManager* Manager, const FHitResult& Hit)
	{
		return Manager ? Manager->ResolveRootISID(Manager->GetISID(Hit.GetComponent(), Hit.Item)) : 0;
	}

	bool CanGrabLegacyActor(AActor& Actor, UPrimitiveComponent* HitComponent)
	{
		auto* Function = Actor.FindFunction(FName(TEXT("Can grab")));
		if (!Function || !Actor.GetRootComponent() || Actor.GetRootComponent()->Mobility != EComponentMobility::Movable
			|| !Actor.FindFunction(FName(TEXT("Request Attach to Component")))) return false;

		FStructOnScope Params(Function);
		if (const auto* Component = FindFProperty<FObjectPropertyBase>(Function, TEXT("Hitting Component"));
			Component && IsValid(HitComponent) && HitComponent->IsA(Component->PropertyClass))
			Component->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), HitComponent);
		Actor.ProcessEvent(Function, Params.GetStructMemory());
		for (TFieldIterator<FBoolProperty> It(Function); It; ++It)
			if (It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
				return It->GetPropertyValue_InContainer(Params.GetStructMemory());
		return false;
	}

	bool AttachLegacyActor(AActor& Actor, USkeletalMeshComponent& Mesh, FName Socket)
	{
		auto* Function = Actor.FindFunction(FName(TEXT("Request Attach to Component")));
		const auto* Parent = Function ? FindFProperty<FObjectPropertyBase>(Function, TEXT("Component")) : nullptr;
		if (!Parent || !Mesh.IsA(Parent->PropertyClass)) return false;

		FStructOnScope Params(Function);
		Parent->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), &Mesh);
		if (const auto* Name = FindFProperty<FNameProperty>(Function, TEXT("Socket Name")))
			Name->SetPropertyValue_InContainer(Params.GetStructMemory(), Socket);
		Actor.ProcessEvent(Function, Params.GetStructMemory());
		return Actor.GetAttachParentActor() == Mesh.GetOwner();
	}

	FVector FindPlacementPoint(const ACharacter& Player, const AActor& Actor)
	{
		FVector Eye;
		FRotator Aim;
		GetGrabView(Player, Eye, Aim);
		const FVector End = Eye + Aim.Vector() * GrabTraceLength;
		FHitResult Hit;
		if (!TraceGrab(Player, Eye, End, Hit, &Actor)) return End;

		const auto* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
		if (!Root) return Hit.Location + Hit.ImpactNormal * 2;
		const FVector Normal = Hit.ImpactNormal;
		const double Clearance = FVector::DotProduct(Normal.GetAbs(), Root->Bounds.BoxExtent) + 2;
		return Hit.Location + Normal * Clearance - (Root->Bounds.Origin - Actor.GetActorLocation());
	}
}

bool UPropGrabInputComponent::TryBeginActorTravel(EPropGrabInputPhase Phase, AActor* HeldActor)
{
	auto* Player = Cast<ACharacter>(GetOwner());
	if (!IsValid(Player) || !Player->IsLocallyControlled() || !IsValid(Player->GetMesh())) return false;
	const auto* Manager = APropManager::Get(this);
	const bool bPlacing = LastIntent == EPropGrabIntent::Place;
	FHitResult Hit;
	AActor* Actor = HeldActor;
	int32 ISID = 0;
	if (bPlacing)
	{
		if (!IsValid(Actor)) return false;
		if (const auto* Prop = Cast<AActivePropActor>(Actor)) ISID = Prop->ISID;
	}
	else
	{
		const auto* CachedTrace = FindFProperty<FStructProperty>(Player->GetClass(), TEXT("Hit Result On tick"));
		if (CachedTrace && CachedTrace->Struct == FHitResult::StaticStruct())
			Hit = *CachedTrace->ContainerPtrToValuePtr<FHitResult>(Player);
		if (!Hit.bBlockingHit || !IsValid(Hit.GetComponent()))
		{
			FVector Eye;
			FRotator Aim;
			GetGrabView(*Player, Eye, Aim);
			TraceGrab(*Player, Eye, Eye + Aim.Vector() * GrabTraceLength, Hit);
		}
		ISID = GetHitISID(Manager, Hit);
		Actor = Hit.GetActor();
		if (ISID <= 0 && (!IsValid(Actor) || !CanGrabLegacyActor(*Actor, Hit.GetComponent()))) return false;
	}

	// A hold recognized on the release frame has no time left for a transfer.
	if (Phase == EPropGrabInputPhase::Ongoing && LocalTravelSequence < MAX_int64)
	{
		bTravelKeyHeld = true;
		Server_BeginActorTravel(++LocalTravelSequence, ISID, ISID > 0 ? nullptr : Actor, Hit.ImpactPoint, bPlacing);
	}
	return true;
}

bool UPropGrabInputComponent::IsActorInAnotherPlayerHand(const AActor* Actor) const
{
	for (const AActor* Parent = Actor->GetAttachParentActor(); IsValid(Parent); Parent = Parent->GetAttachParentActor())
		if (Parent->IsA<APawn>()) return Parent != GetOwner();
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const auto* Controller = It->Get();
		const auto* Pawn = Controller ? Controller->GetPawn().Get() : nullptr;
		const auto* Input = Pawn && Pawn != GetOwner() ? Pawn->FindComponentByClass<UPropGrabInputComponent>() : nullptr;
		if (Input && Input->Travel.Actor.Get() == Actor) return true;
	}
	return false;
}

void UPropGrabInputComponent::Server_BeginActorTravel_Implementation(int64 Sequence, int32 ISID, AActor* Actor,
	FVector_NetQuantize10 HitLocation, bool bPlacing)
{
	auto* Player = Cast<ACharacter>(GetOwner());
	const auto* Controller = IsValid(Player) ? Cast<APlayerController>(Player->GetController()) : nullptr;
	if (!Controller || Controller->GetPawn() != Player || !Player->HasAuthority() || GetNetMode() == NM_Client
		|| Sequence <= LastServerTravelSequence || !FMath::IsFinite(CarryTravelSpeed) || CarryTravelSpeed <= 0) return;
	LastServerTravelSequence = Sequence;
	if (Travel.Sequence || !Player->GetMesh()) return;

	auto* Manager = APropManager::Get(this);
	auto* HeldActor = Cast<AActor>(WorldInteractionCarry::GetCarryReference(*Player, TEXT("Holding Actor")));
	if (IsValid(WorldInteractionCarry::GetCarryReference(*Player, TEXT("Holding Component")))) return;
	if (bPlacing)
	{
		if (!IsValid(HeldActor) || !OwnsHeldTarget(HeldActor)) return;
		const auto* Prop = Cast<AActivePropActor>(HeldActor);
		if (ISID != (Prop ? Prop->ISID : 0)) return;
		if (ISID > 0 ? (!Manager || Manager->GetActiveActorFromISID(ISID) != HeldActor) : Actor != HeldActor) return;
		Actor = HeldActor;
	}
	else
	{
		if (IsValid(HeldActor) || HitLocation.ContainsNaN()) return;
		FVector Eye;
		FRotator Aim;
		GetGrabView(*Player, Eye, Aim);
		const FVector ToHit = HitLocation - Eye;
		if (ToHit.IsNearlyZero() || ToHit.SizeSquared() > FMath::Square(GrabTraceLength * 1.5f)) return;
		FHitResult VerifiedHit;
		if (!TraceGrab(*Player, Eye, HitLocation + ToHit.GetSafeNormal() * 12, VerifiedHit)) return;
		if (GetHitISID(Manager, VerifiedHit) != ISID) return;
		if (ISID > 0)
		{
			Actor = Manager->ReserveGrabForTravel(ISID, Player);
		}
		else if (!IsValid(Actor) || Actor->IsA<AActivePropActor>() || VerifiedHit.GetActor() != Actor || IsActorInAnotherPlayerHand(Actor)
			|| !CanGrabLegacyActor(*Actor, VerifiedHit.GetComponent())) return;
	}
	if (!IsValid(Actor) || Actor->GetWorld() != GetWorld()) return;

	Travel.Actor = Actor;
	Travel.ISID = ISID;
	Travel.Sequence = Sequence;
	Travel.bPlacing = bPlacing;
	if (auto* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		Travel.bWasSimulating = Root->IsSimulatingPhysics();
		Root->SetSimulatePhysics(false);
		Root->IgnoreActorWhenMoving(Player, true);
	}
	Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (bPlacing) WorldInteractionCarry::ClearHeldActor(*Player, *Actor);
	if (GetNetMode() != NM_Standalone)
	{
		if (!Actor->GetIsReplicated()) Actor->SetReplicates(true);
		Actor->SetReplicateMovement(true);
		Actor->FlushNetDormancy();
	}
	Actor->ForceNetUpdate();
	SetComponentTickEnabled(true);
}

void UPropGrabInputComponent::Server_EndActorTravel_Implementation(int64 Sequence)
{
	if (GetOwner()->HasAuthority() && GetNetMode() != NM_Client && Sequence > 0 && Travel.Sequence == Sequence)
		DropTravelActor();
}

void UPropGrabInputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Travel.Sequence) DropTravelActor();
	Super::EndPlay(Reason);
}

void UPropGrabInputComponent::DropTravelActor()
{
	const FActorTravel Dropped = Travel;
	Travel = {};
	SetComponentTickEnabled(false);
	auto* Actor = Dropped.Actor.Get();
	auto* Player = Cast<ACharacter>(GetOwner());
	if (!Actor || !Player || !Player->HasAuthority() || GetNetMode() == NM_Client) return;
	const auto* Prop = Cast<AActivePropActor>(Actor);
	if (Dropped.ISID > 0 && (!Prop || Prop->ISID != Dropped.ISID)) return;
	if (auto* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
		Root->IgnoreActorWhenMoving(Player, false);
	if (Dropped.ISID > 0)
	{
		if (OwnsHeldTarget(Actor))
			if (auto* Manager = APropManager::Get(this)) Manager->ReleaseGrab(Dropped.ISID, Player);
	}
	else if (!Actor->GetAttachParentActor() || Actor->GetAttachParentActor() == Player)
	{
		if (Dropped.bPlacing)
			if (auto* Function = Actor->FindFunction(FName(TEXT("Request Detach from Component"))))
			{
				FStructOnScope Params(Function);
				Actor->ProcessEvent(Function, Params.GetStructMemory());
			}
		Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (auto* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Dropped.bWasSimulating)
			Root->SetSimulatePhysics(true);
	}
	Actor->ForceNetUpdate();
}

void UPropGrabInputComponent::CompleteTravelPickup(ACharacter& Player, AActor& Actor)
{
	if (auto* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent())) Root->IgnoreActorWhenMoving(&Player, false);
	auto* Mesh = Player.GetMesh();
	const FName Socket = Mesh->DoesSocketExist(HoldingSocket) ? HoldingSocket : NAME_None;
	const bool Attached = Travel.ISID > 0
		? CastChecked<AActivePropActor>(&Actor)->OnPlayerGrab(Mesh, Socket)
		: AttachLegacyActor(Actor, *Mesh, Socket);
	if (!Attached || !WorldInteractionCarry::PublishHeldActor(Player, Actor))
	{
		DropTravelActor();
		return;
	}
	Travel = {};
	SetComponentTickEnabled(false);
	Actor.ForceNetUpdate();
}

void UPropGrabInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	auto* Player = Cast<ACharacter>(GetOwner());
	auto* Actor = Travel.Actor.Get();
	if (!Travel.Sequence) return;
	const auto* Prop = Cast<AActivePropActor>(Actor);
	if (!IsValid(Player) || !Player->GetController() || !Player->GetMesh() || !Actor
		|| (Travel.ISID > 0 && (!Prop || Prop->ISID != Travel.ISID || !OwnsHeldTarget(Actor))) || Actor->GetAttachParentActor()
		|| FVector::DistSquared(Actor->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(GrabTraceLength * 2))
	{
		DropTravelActor();
		return;
	}

	const FTransform Hand = Player->GetMesh()->GetSocketTransform(HoldingSocket);
	const FVector Goal = Travel.bPlacing ? FindPlacementPoint(*Player, *Actor) : Hand.GetLocation();
	const FVector Start = Actor->GetActorLocation();
	const FVector Next = FMath::VInterpConstantTo(Start, Goal, DeltaTime, CarryTravelSpeed);
	FQuat Rotation = Actor->GetActorQuat();
	if (!Travel.bPlacing)
	{
		const double Distance = FVector::Distance(Start, Goal);
		const double Alpha = Distance > UE_SMALL_NUMBER ? FVector::Distance(Start, Next) / Distance : 1.0;
		Rotation = FQuat::Slerp(Rotation, Hand.GetRotation(), Alpha);
	}
	if (!Next.Equals(Start, UE_KINDA_SMALL_NUMBER) || !Rotation.Equals(Actor->GetActorQuat(), UE_KINDA_SMALL_NUMBER))
	{
		Actor->SetActorLocationAndRotation(Next, Rotation, true, nullptr, ETeleportType::TeleportPhysics);
		if (auto* Manager = Travel.ISID > 0 ? APropManager::Get(this) : nullptr)
			if (auto* Record = Manager->FindISIDData(Travel.ISID); Record && Record->ActiveActor == Actor)
				Record->ActiveTransform = Actor->GetActorTransform();
	}
	if (!Travel.bPlacing && Actor->GetActorLocation().Equals(Goal, 1.0)) CompleteTravelPickup(*Player, *Actor);
}
