#include "Shop/MachinePlacementGeometry.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "PhysicsEngine/BodySetup.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ColaFlowController.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/SocketColaFillingMachine.h"

namespace
{
	constexpr double SurfaceGap = 1.0;
	constexpr double MinimumSurfaceNormalZ = 0.55;

	void AddMeshBounds(const AActor& Actor, const FTransform& ReferencePose, FBox& Bounds)
	{
		for (const UStaticMeshComponent* Part : TInlineComponentArray<UStaticMeshComponent*>(&Actor))
		{
			const UStaticMesh* Mesh = Part->GetStaticMesh();
			const auto Collision = Part->BodyInstance.GetCollisionEnabled(false);
			if (!Mesh || (!Part->GetVisibleFlag() && Collision == ECollisionEnabled::NoCollision)) continue;
			const FTransform RelativePose = Part->GetComponentTransform().GetRelativeTransform(ReferencePose);
			Bounds += Mesh->GetBoundingBox().TransformBy(RelativePose);
			if (CollisionEnabledHasPhysics(Collision))
				if (const UBodySetup* Body = Mesh->GetBodySetup(); Body && Body->CollisionTraceFlag != CTF_UseComplexAsSimple && Body->AggGeom.GetElementCount() > 0)
					Bounds += Body->AggGeom.CalcAABB(RelativePose);
		}
	}

	FBox GetPlacementBounds(const AActor& Machine, FCollisionQueryParams& Query)
	{
		FBox Bounds(ForceInit);
		const FTransform ReferencePose = Machine.GetActorTransform();
		AddMeshBounds(Machine, ReferencePose, Bounds);
		Query.AddIgnoredActor(&Machine);

		TArray<AActor*> Contents;
		Machine.GetAttachedActors(Contents, true, true);
		if (const auto* Table = Cast<ASocketColaFillingMachine>(&Machine); Table && Table->Production && Table->PropManagerReference
			&& Table->Production->View.SeatedBottleISID > 0)
			Contents.AddUnique(Table->PropManagerReference->GetActiveActorFromISID(Table->Production->View.SeatedBottleISID));
		for (const AActor* Item : Contents)
		{
			if (!IsValid(Item)) continue;
			Query.AddIgnoredActor(Item);
			AddMeshBounds(*Item, ReferencePose, Bounds);
		}
		return Bounds;
	}

	FRotator GetPlacementRotation(const AActor& Machine, float Yaw)
	{
		const FRotator Rest = Machine.GetActorRotation();
		return FRotator(Rest.Pitch, FRotator::NormalizeAxis(Yaw), Rest.Roll);
	}

	bool IsPlacementClear(const AActor& Machine, const UWorld& World, const APawn& Player, const FBox& LocalBounds,
		const FTransform& Pose, const FCollisionQueryParams& Query)
	{
		const FVector Center = Pose.TransformPosition(LocalBounds.GetCenter());
		if (FVector::DistSquared(Center, Player.GetActorLocation()) > FMath::Square(ShopMachinePlacement::MaxPreviewDistance)) return false;

		const FVector HalfSize = (LocalBounds.GetExtent() * Pose.GetScale3D().GetAbs() - FVector(.5)).ComponentMax(FVector(.1));
		const FCollisionShape Bounds = FCollisionShape::MakeBox(HalfSize);
		const bool bObstructed = Machine.IsA<AColaFlowController>()
			? World.OverlapAnyTestByChannel(Center, Pose.GetRotation(), ECC_PhysicsBody, Bounds, Query)
			: World.OverlapBlockingTestByChannel(Center, Pose.GetRotation(), ECC_PhysicsBody, Bounds, Query);
		if (bObstructed) return false;

		FCollisionQueryParams TraceQuery = Query;
		TraceQuery.AddIgnoredActor(&Player);
		const FBox WorldBounds = LocalBounds.TransformBy(Pose);
		const FVector Bottom(Center.X, Center.Y, WorldBounds.Min.Z);
		FHitResult Surface;
		if (!World.LineTraceSingleByChannel(Surface, Bottom + FVector(0, 0, 4), Bottom - FVector(0, 0, 5), ECC_PhysicsBody, TraceQuery)
			|| Surface.ImpactNormal.Z < MinimumSurfaceNormalZ) return false;

		FVector Eye;
		FRotator View;
		Player.GetActorEyesViewPoint(Eye, View);
		return !World.LineTraceTestByChannel(Eye, Center, ECC_PhysicsBody, TraceQuery);
	}
}

bool ShopMachinePlacement::TracePlacement(const AActor& Machine, const APawn& Player,
	const FVector& Eye, const FVector& Direction, float Yaw, FTransform& OutPose)
{
	OutPose = Machine.GetActorTransform();
	const auto* World = Machine.GetWorld();
	if (!World || Player.GetWorld() != World || Eye.ContainsNaN() || Direction.ContainsNaN() || !FMath::IsFinite(Yaw)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ShopMachinePlacement), false);
	const FBox LocalBounds = GetPlacementBounds(Machine, Query);
	if (!LocalBounds.IsValid) return false;

	OutPose = FTransform(GetPlacementRotation(Machine, Yaw), FVector::ZeroVector, Machine.GetActorScale3D());
	const FBox RotatedBounds = LocalBounds.TransformBy(OutPose);
	const FVector Position = Player.GetActorLocation();
	const FVector FlatDirection = Direction.GetSafeNormal2D();
	const double MinDistance = FMath::Min(MaxPreviewDistance * .65,
		RotatedBounds.GetExtent().Size2D() + Player.GetSimpleCollisionRadius() + 20.0);
	const FVector Forward = FlatDirection.IsNearlyZero() ? Player.GetActorForwardVector().GetSafeNormal2D() : FlatDirection;

	FCollisionQueryParams TraceQuery = Query;
	TraceQuery.AddIgnoredActor(&Player);
	FHitResult Aim;
	const bool bHit = World->LineTraceSingleByChannel(Aim, Eye, Eye + Direction.GetSafeNormal() * MaxPreviewDistance, ECC_PhysicsBody, TraceQuery);
	bool bSupported = bHit && Aim.ImpactNormal.Z >= MinimumSurfaceNormalZ
		&& FVector::DistSquared2D(Aim.ImpactPoint, Position) >= FMath::Square(MinDistance);
	FVector SurfacePoint;
	if (bSupported)
	{
		SurfacePoint = Aim.ImpactPoint;
	}
	else
	{
		// A nearby hit would put the machine in the player's space. Look for a surface farther along the aim direction.
		const double Distance = bHit ? FMath::Max(MinDistance, FVector::Dist2D(Aim.ImpactPoint, Position)) : MinDistance + 160.0;
		const FVector Ahead = Position + Forward * Distance;
		const double StartHeight = bHit ? FMath::Max(Eye.Z + 5.0, Aim.ImpactPoint.Z + 5.0) : Eye.Z + 5.0;
		FHitResult Surface;
		bSupported = World->LineTraceSingleByChannel(Surface, FVector(Ahead.X, Ahead.Y, StartHeight),
			FVector(Ahead.X, Ahead.Y, StartHeight - 600.0), ECC_PhysicsBody, TraceQuery)
			&& Surface.ImpactNormal.Z >= MinimumSurfaceNormalZ;
		SurfacePoint = bSupported ? Surface.ImpactPoint : FVector(Ahead.X, Ahead.Y, Position.Z - Player.GetSimpleCollisionHalfHeight());
	}

	const FVector OriginToBottom(RotatedBounds.GetCenter().X, RotatedBounds.GetCenter().Y, RotatedBounds.Min.Z - SurfaceGap);
	OutPose.SetLocation(SurfacePoint - OriginToBottom);
	return bSupported && IsPlacementClear(Machine, *World, Player, LocalBounds, OutPose, Query);
}

bool ShopMachinePlacement::CanPlace(const AActor& Machine, const APawn& Player, const FTransform& Pose)
{
	const auto* World = Machine.GetWorld();
	if (!World || Player.GetWorld() != World || !Pose.IsValid() || !Pose.GetScale3D().Equals(Machine.GetActorScale3D())) return false;
	const FRotator Rotation = Pose.Rotator();
	if (!Rotation.Equals(GetPlacementRotation(Machine, Rotation.Yaw), .1f)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ShopMachinePlacement), false);
	const FBox LocalBounds = GetPlacementBounds(Machine, Query);
	return LocalBounds.IsValid && IsPlacementClear(Machine, *World, Player, LocalBounds, Pose, Query);
}
