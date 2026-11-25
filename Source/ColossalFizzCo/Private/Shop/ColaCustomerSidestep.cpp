#include "Shop/ColaCustomerSidestep.h"

#include "Components/CapsuleComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

bool ColaCustomerMovement::FindSidestep(const ACharacter& Customer, const ACharacter& Blocker, const FVector& Intent,
	const FVector* BlockerSidestep, FSidestep& OutStep)
{
	UWorld* World = Customer.GetWorld();
	const auto* Capsule = Customer.GetCapsuleComponent();
	const auto* BlockerCapsule = Blocker.GetCapsuleComponent();
	if (!World || Blocker.GetWorld() != World || !Capsule || !BlockerCapsule) return false;

	const FVector Feet = Customer.GetNavAgentLocation();
	const FVector OtherFeet = Blocker.GetNavAgentLocation();
	const FVector Separation = Feet - OtherFeet;
	const FVector Away = Separation.GetSafeNormal2D(UE_SMALL_NUMBER, -Intent.GetSafeNormal2D());
	if (Away.IsNearlyZero()) return false;

	const double Radius = Capsule->GetScaledCapsuleRadius();
	const double HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const double StepDistance = FMath::Clamp(1.25 * Radius, 75.0, 285.0);
	const FVector ProjectionExtent(FMath::Clamp(.35 * Radius, 25.0, 70.0), FMath::Clamp(.35 * Radius, 25.0, 70.0), 90.0);
	const auto* Nav = UNavigationSystemV1::GetCurrent(World);
	const auto* NavData = Nav ? Nav->GetNavDataForProps(Customer.GetNavAgentPropertiesRef(), Feet) : nullptr;
	FNavLocation StartNav;
	if (!NavData || !Nav->ProjectPointToNavigation(Feet, StartNav, ProjectionExtent, NavData)) return false;

	// The same side choice sends two customers facing each other in opposite directions.
	const FVector Side(-Away.Y, Away.X, 0);
	const FVector SweepStart = Capsule->GetComponentLocation() + FVector(0, 0, 4);
	const FQuat CapsuleRotation = Capsule->GetComponentQuat();
	const auto Footprint = FCollisionShape::MakeCapsule(float(FMath::Max(1.0, Radius - 2.0)), float(FMath::Max(1.0, HalfHeight - 2.0)));
	const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
	const FCollisionQueryParams FullQuery(SCENE_QUERY_STAT(ColaCustomerSidestepRoute), false, &Customer);
	FCollisionQueryParams EscapeQuery(FullQuery);
	EscapeQuery.AddIgnoredActor(&Blocker);
	const double PeerClearance = Radius + BlockerCapsule->GetScaledCapsuleRadius() + 8.0;
	const auto CapsuleAt = [&](const FVector& NavPoint)
	{
		return FVector(NavPoint.X, NavPoint.Y, SweepStart.Z + NavPoint.Z - StartNav.Location.Z);
	};
	const auto FindLeg = [&](const FVector& Candidate, const FNavLocation& From, bool bEscaping, FNavLocation& Goal)
	{
		if (!Nav->ProjectPointToNavigation(Candidate, Goal, ProjectionExtent, NavData)
			|| FVector::DistSquared2D(Candidate, Goal.Location) > FMath::Square(ProjectionExtent.X)
			|| FMath::Abs(Goal.Location.Z - From.Location.Z) > 45.0) return false;
		// Only the outward leg can ignore an existing contact with the blocking capsule.
		if (bEscaping && FVector::DotProduct(Goal.Location - Feet, Away) < .25 * StepDistance) return false;
		if (BlockerSidestep && FMath::PointDistToSegmentSquared(*BlockerSidestep, From.Location, Goal.Location) < FMath::Square(PeerClearance)) return false;

		FVector NavHit;
		if (NavData->Raycast(From.Location, Goal.Location, NavHit, NavData->GetDefaultQueryFilter(), &Customer)) return false;
		const FVector SweepEnd = CapsuleAt(Goal.Location);
		if (World->OverlapBlockingTestByChannel(SweepEnd, CapsuleRotation, ECC_Pawn, Footprint, FullQuery, Response)) return false;
		FHitResult Hit;
		const FVector SweepFrom = bEscaping ? SweepStart : CapsuleAt(From.Location);
		return !World->SweepSingleByChannel(Hit, SweepFrom, SweepEnd, CapsuleRotation, ECC_Pawn, Footprint,
			bEscaping ? EscapeQuery : FullQuery, Response);
	};

	const double PassClearance = PeerClearance + 36.0;
	for (const double Sign : {1.0, -1.0})
	{
		const FVector BypassSide = Side * Sign;
		FNavLocation First, Past;
		if (!FindLeg(Feet + BypassSide * PassClearance + Away * (.5 * StepDistance), StartNav, true, First)
			|| FVector::DotProduct(First.Location - OtherFeet, BypassSide) < PeerClearance) continue;
		const double ForwardDistance = FVector::DotProduct(First.Location - OtherFeet, Away) + PassClearance;
		if (ForwardDistance <= 0.0 || ForwardDistance > 1100.0) continue;
		if (!FindLeg(First.Location - Away * ForwardDistance, First, false, Past)
			|| FVector::DotProduct(OtherFeet - Past.Location, Away) < PeerClearance) continue;
		OutStep.Away = First.Location;
		OutStep.PastBlocker = Past.Location;
		return true;
	}

	const FVector ShortOffsets[] = {Side + .6 * Away, -Side + .6 * Away, Away};
	for (const FVector& Offset : ShortOffsets)
	{
		FNavLocation Goal;
		if (!FindLeg(Feet + Offset * StepDistance, StartNav, true, Goal)) continue;
		OutStep.Away = Goal.Location;
		OutStep.PastBlocker.Reset();
		return true;
	}
	return false;
}
