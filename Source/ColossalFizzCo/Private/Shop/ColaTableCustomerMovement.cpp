#include "Shop/ColaTableCustomerComponent.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Shop/ColaCustomerSidestep.h"

DEFINE_LOG_CATEGORY_STATIC(LogColaCustomerMovement, Log, All);

namespace
{
	constexpr double CustomerBlockedSeconds = .30;
	constexpr double SidestepRetrySeconds = .24;
	constexpr double SidestepStallSeconds = .75;
	constexpr float SidestepAcceptance = 22.f;
	constexpr float WalkingTurnRate = 220.f;
	constexpr float DetourTurnRate = 135.f;
	constexpr float StandingTurnRate = 160.f;
	constexpr float MaxSidestepFacingAngle = 60.f;
	constexpr float BackstepAngle = 150.f;
	constexpr float MinimumFacingSpeed = 42.f;

	bool TurnCustomer(ACharacter& Character, float DesiredYaw, float DeltaSeconds, float TurnRate)
	{
		const float Yaw = float(Character.GetActorRotation().Yaw);
		const float Delta = FMath::FindDeltaAngleDegrees(Yaw, DesiredYaw);
		if (FMath::Abs(Delta) <= 1.f) return true;
		const float StepSeconds = FMath::Clamp(DeltaSeconds, 0.f, .05f);
		const float MaxStep = TurnRate * StepSeconds;
		const float Step = FMath::Clamp(Delta * (1.f - FMath::Exp(-9.f * StepSeconds)), -MaxStep, MaxStep);
		Character.SetActorRotation(FRotator(0.f, Yaw + Step, 0.f));
		return FMath::Abs(Delta - Step) <= 1.f;
	}

	ACharacter* FindBlockingCustomer(const ACharacter& Customer, const FVector& Direction)
	{
		const auto* Capsule = Customer.GetCapsuleComponent();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const FVector Start = Capsule->GetComponentLocation() + FVector(0, 0, 4);
		const double ProbeDistance = FMath::Clamp(Radius * .6 + 24.0, 60.0, 165.0);
		const auto Shape = FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 2.f), FMath::Max(1.f, HalfHeight - 2.f));
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(ColaCustomerBlocked), false, &Customer);
		const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
		FHitResult Hit;
		if (!Customer.GetWorld()->SweepSingleByChannel(Hit, Start, Start + Direction * ProbeDistance,
			Capsule->GetComponentQuat(), ECC_Pawn, Shape, Query, Response)) return nullptr;
		auto* Other = Cast<ACharacter>(Hit.GetActor());
		return Other && Cast<AAIController>(Other->GetController()) ? Other : nullptr;
	}
}

EPathFollowingRequestResult::Type UColaTableCustomerComponent::RequestWalk(AAIController& AI, const FVector& Goal, float Acceptance, double Now)
{
	const auto Result = AI.MoveToLocation(Goal, Acceptance, false, true, false, false, nullptr, false);
	if (Result == EPathFollowingRequestResult::RequestSuccessful)
	{
		if (!WalkTarget || !WalkTarget->Goal.Equals(Goal, 2.f))
		{
			LastWalkProgress = Customer->GetNavAgentLocation();
			LastWalkProgressAt = Now;
		}
		WalkTarget = FWalkTarget{Goal, Acceptance};
	}
	else if (Result == EPathFollowingRequestResult::AlreadyAtGoal) WalkTarget.Reset();
	return Result;
}

bool UColaTableCustomerComponent::UpdateCollisionRecovery(AAIController& AI, double Now)
{
	if (!WalkTarget || !Customer->GetCharacterMovement()->IsMovingOnGround()) return false;
	const FVector Feet = Customer->GetNavAgentLocation();
	const auto StartLeg = [&](const FVector& Goal)
	{
		if (AI.MoveToLocation(Goal, SidestepAcceptance, false, true, false, false, nullptr, false)
			!= EPathFollowingRequestResult::RequestSuccessful) return false;
		SidestepGoal = Goal;
		LastWalkProgress = Feet;
		LastWalkProgressAt = Now;
		const double Speed = FMath::Max(50.f, Customer->GetCharacterMovement()->MaxWalkSpeed);
		SidestepDeadline = Now + FMath::Clamp(.5 + 1.6 * FVector::Dist2D(Feet, Goal) / Speed, .8, 8.5);
		return true;
	};
	const auto ResumeWalk = [&]
	{
		LastWalkProgress = Feet;
		LastWalkProgressAt = Now;
		const FWalkTarget Target = *WalkTarget;
		RequestWalk(AI, Target.Goal, Target.Acceptance, Now);
	};
	if (FVector::DistSquared2D(Feet, LastWalkProgress) > FMath::Square(16.0))
	{
		LastWalkProgress = Feet;
		LastWalkProgressAt = Now;
	}
	if (SidestepGoal)
	{
		const bool bReached = FVector::DistSquared2D(Feet, *SidestepGoal) <= FMath::Square(SidestepAcceptance + 6.f);
		if (!bReached && Now < SidestepDeadline && Now - LastWalkProgressAt < SidestepStallSeconds
			&& AI.GetMoveStatus() != EPathFollowingStatus::Idle) return true;
		SidestepGoal.Reset();
		SidestepStartYaw.Reset();
		const TOptional<FVector> PassGoal = PassBlockerGoal;
		PassBlockerGoal.Reset();
		if (bReached && PassGoal && StartLeg(*PassGoal))
		{
			UE_LOG(LogColaCustomerMovement, Verbose, TEXT("Customer %s walking past the blocker"), *GetNameSafe(Customer.Get()));
			return true;
		}
		NextSidestepAt = Now + SidestepRetrySeconds;
		ResumeWalk();
		UE_LOG(LogColaCustomerMovement, Verbose, TEXT("Customer %s resuming its path"), *GetNameSafe(Customer.Get()));
		return false;
	}

	if (FVector::DistSquared2D(Feet, WalkTarget->Goal) <= FMath::Square(WalkTarget->Acceptance + 4.f)
		&& FMath::Abs(Feet.Z - WalkTarget->Goal.Z) <= 80.f)
	{
		WalkTarget.Reset();
		return false;
	}
	if (Now < NextSidestepAt || Now - LastWalkProgressAt < CustomerBlockedSeconds) return false;
	NextSidestepAt = Now + SidestepRetrySeconds;
	FVector Direction = (WalkTarget->Goal - Feet).GetSafeNormal2D();
	if (AI.GetMoveStatus() == EPathFollowingStatus::Moving)
		Direction = (AI.GetImmediateMoveDestination() - Feet).GetSafeNormal2D(UE_SMALL_NUMBER, Direction);
	if (Direction.IsNearlyZero()) return false;
	const auto* Blocker = FindBlockingCustomer(*Customer, Direction);
	if (!Blocker) return false;
	const auto* Peer = Blocker->FindComponentByClass<UColaTableCustomerComponent>();
	const FVector* PeerGoal = Peer && Peer->SidestepGoal ? &Peer->SidestepGoal.GetValue() : nullptr;
	ColaCustomerMovement::FSidestep Step;
	if (!ColaCustomerMovement::FindSidestep(*Customer, *Blocker, Direction, PeerGoal, Step)) return false;
	if (!StartLeg(Step.Away))
	{
		ResumeWalk();
		return false;
	}
	PassBlockerGoal = Step.PastBlocker;
	SidestepStartYaw = float(Customer->GetActorRotation().Yaw);
	UE_LOG(LogColaCustomerMovement, Verbose, TEXT("Customer %s stepping aside from %s"),
		*GetNameSafe(Customer.Get()), *GetNameSafe(Blocker));
	return true;
}

void UColaTableCustomerComponent::UpdateWalkingFacing(float DeltaSeconds) const
{
	const FVector Velocity = Customer->GetVelocity();
	const float FacingSpeed = FMath::Clamp(Customer->GetCharacterMovement()->MaxWalkSpeed * .25f, 1.f, MinimumFacingSpeed);
	if (Velocity.SizeSquared2D() < FMath::Square(FacingSpeed))
	{
		if (VisitState == EColaTableCustomerState::WaitingAtTable && !WalkTarget && Tabletop.IsValid())
			Face(Tabletop->Bounds.Origin, DeltaSeconds);
		return;
	}
	float DesiredYaw = float(Velocity.ToOrientationRotator().Yaw);
	if (SidestepStartYaw && SidestepGoal)
	{
		// Make the first leg a step to the side or back, keeping the body mostly forward.
		const FVector StepDirection = *SidestepGoal - Customer->GetNavAgentLocation();
		const float StepYaw = float(StepDirection.ToOrientationRotator().Yaw);
		const float Offset = FMath::FindDeltaAngleDegrees(*SidestepStartYaw, StepYaw);
		DesiredYaw = *SidestepStartYaw;
		if (FMath::Abs(Offset) < BackstepAngle)
			DesiredYaw += FMath::Clamp(Offset, -MaxSidestepFacingAngle, MaxSidestepFacingAngle);
	}
	TurnCustomer(*Customer, DesiredYaw, DeltaSeconds, SidestepGoal ? DetourTurnRate : WalkingTurnRate);
}

bool UColaTableCustomerComponent::Face(FVector Target, float DeltaSeconds) const
{
	const FVector Direction = Target - Customer->GetActorLocation();
	return Direction.SizeSquared2D() <= 1.f
		|| TurnCustomer(*Customer, float(Direction.ToOrientationRotator().Yaw), DeltaSeconds, StandingTurnRate);
}
