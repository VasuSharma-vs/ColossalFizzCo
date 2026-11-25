#include "Shop/ColaTableQueueComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"

namespace
{
	constexpr double StandMargin = 20.0;
	constexpr double RecheckSeconds = .2;
	constexpr double RetrySeconds = 2.5;
	constexpr double NoServiceRoute = TNumericLimits<double>::Max();
	constexpr int32 MaximumStands = 80;
	constexpr int32 MaximumWaitingRows = 8;

	double RadiusOf(const ACharacter& Character)
	{
		return Character.GetCapsuleComponent()->GetScaledCapsuleRadius();
	}

	bool OccupiesStand(const ACharacter& Character, const FVector& Position, double StandRadius)
	{
		const FVector Feet = Character.GetNavAgentLocation();
		const double Clearance = RadiusOf(Character) + StandRadius + StandMargin;
		return FMath::Abs(Feet.Z - Position.Z) < Character.GetSimpleCollisionHalfHeight()
			&& FVector::DistSquared2D(Feet, Position) < FMath::Square(Clearance);
	}

	bool CanReachStock(const ACharacter& Customer, const FVector& Stand, TConstArrayView<FColaServingBottle> Bottles)
	{
		const FVector Center = Stand + FVector(0, 0, Customer.GetSimpleCollisionHalfHeight());
		for (const auto& Bottle : Bottles)
			if (Bottle.Reach <= 0 || FVector::DistSquared(Center, Bottle.Position) <= FMath::Square(Bottle.Reach)) return true;
		return Bottles.IsEmpty();
	}

	const ANavigationData* NavigationFor(UNavigationSystemV1& Nav, const ACharacter& Customer)
	{
		const auto* Data = Nav.GetNavDataForProps(Customer.GetNavAgentPropertiesRef(), Customer.GetNavAgentLocation());
		return Data ? Data : Nav.GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	}

	double WalkingDistance(UNavigationSystemV1& Nav, const ANavigationData& Data, const ACharacter& Customer, const FVector& Goal)
	{
		const FVector Feet = Customer.GetNavAgentLocation();
		if (FVector::DistSquared(Feet, Goal) <= FMath::Square(40.0)) return FVector::Dist(Feet, Goal);
		FNavLocation Start;
		if (!Nav.ProjectPointToNavigation(Feet, Start, FVector(120, 120, 180), &Data)) return NoServiceRoute;
		const double StartOffset = FVector::Dist(Feet, Start.Location);
		FVector Hit;
		if (!Data.Raycast(Start.Location, Goal, Hit, Data.GetDefaultQueryFilter(), &Customer))
			return StartOffset + FVector::Dist(Start.Location, Goal);
		FPathFindingQuery Query(&Customer, Data, Start.Location, Goal);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = Nav.FindPathSync(Query);
		return Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial()
			? StartOffset + Result.Path->GetLength() : NoServiceRoute;
	}
}

UColaTableQueueComponent* UColaTableQueueComponent::GetOrCreate(AActor& Table, UStaticMeshComponent& Surface)
{
	if (!Table.HasAuthority() || Table.GetNetMode() == NM_Client || !Table.GetWorld() || Surface.GetOwner() != &Table) return nullptr;
	for (auto* Existing : TInlineComponentArray<UColaTableQueueComponent*>(&Table))
		if (Existing->Tabletop.Get() == &Surface) return Existing;
	auto* Queue = NewObject<UColaTableQueueComponent>(&Table, NAME_None, RF_Transient);
	Queue->Tabletop = &Surface;
	Table.AddInstanceComponent(Queue);
	Queue->RegisterComponent();
	if (auto* Registry = Table.GetWorld()->GetSubsystem<UColaTableQueueSubsystem>())
	{
		Registry->Queues.RemoveAllSwap([](const auto& Entry) { return !Entry.IsValid(); });
		Registry->Queues.Add(Queue);
	}
	return Queue;
}

UColaTableQueueComponent::FMember* UColaTableQueueComponent::FindMember(const ACharacter& Customer)
{
	return Members.FindByPredicate([&Customer](const FMember& Member) { return Member.Customer.Get() == &Customer; });
}

double UColaTableQueueComponent::GetServiceCost(ACharacter& Customer, TConstArrayView<FColaServingBottle> Bottles)
{
	if (!GetOwner()->HasAuthority() || GetNetMode() == NM_Client || Customer.GetWorld() != GetWorld()
		|| !Tabletop.IsValid() || Bottles.IsEmpty()) return NoServiceRoute;
	EnsureLayout(Customer, GetWorld()->GetTimeSeconds());
	auto* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	const auto* Data = Nav ? NavigationFor(*Nav, Customer) : nullptr;
	if (!Data) return NoServiceRoute;

	const auto* CustomerMember = FindMember(Customer);
	const double QueuePenalty = FMath::Min(8, Members.Num() - (CustomerMember ? 1 : 0)) * LayoutRadius * .6;
	double Best = NoServiceRoute;
	for (int32 Index = 0; Index < Stands.Num(); ++Index)
	{
		const auto& Stand = Stands[Index];
		if (!Stand.bAtCounter || !CanReachStock(Customer, Stand.Position, Bottles)) continue;
		const bool bOccupied = Members.ContainsByPredicate([&](const auto& Member)
		{
			return Member.StandIndex == Index && Member.Customer.IsValid() && Member.Customer.Get() != &Customer;
		}) || Departures.ContainsByPredicate([&](const FDeparted& Entry)
		{
			const auto* Shopper = Entry.Customer.Get();
			return Shopper && Shopper != &Customer && OccupiesStand(*Shopper, Entry.Position, LayoutRadius)
				&& OccupiesStand(*Shopper, Stand.Position, LayoutRadius);
		}) || IsStandClaimedElsewhere(Stand, Customer);
		const double PriorityCost = QueuePenalty + (bOccupied ? FMath::Max(350.0, LayoutRadius * 3.0) : 0.0);
		if (FVector::Dist(Customer.GetNavAgentLocation(), Stand.Position) + PriorityCost >= Best) continue;
		const double Travel = WalkingDistance(*Nav, *Data, Customer, Stand.Position);
		if (Travel < NoServiceRoute) Best = FMath::Min(Best, Travel + PriorityCost);
	}
	return Best;
}

bool UColaTableQueueComponent::GetStand(ACharacter& Customer, FVector& OutGoal, FVector& OutExit, bool& bOutAtCounter,
	TConstArrayView<FColaServingBottle> Bottles)
{
	if (!GetOwner()->HasAuthority() || GetNetMode() == NM_Client || Customer.GetWorld() != GetWorld() || !Tabletop.IsValid()) return false;
	const double Now = GetWorld()->GetTimeSeconds();
	auto* Member = FindMember(Customer);
	if (!Member)
	{
		if (Members.IsEmpty()) NextUpdate = Now + RecheckSeconds;
		Departures.RemoveAll([&Customer](const FDeparted& Entry) { return Entry.Customer.Get() == &Customer; });
		Member = &Members.Add_GetRef({&Customer});
	}
	Member->Bottles.Reset(Bottles.Num());
	if (!Bottles.IsEmpty()) Member->Bottles.Append(Bottles.GetData(), Bottles.Num());
	if (Now >= NextUpdate)
	{
		NextUpdate = Now + RecheckSeconds;
		UpdateStands(Now);
	}
	Member = FindMember(Customer);
	if (!Member || !Stands.IsValidIndex(Member->StandIndex)) return false;
	const auto& Stand = Stands[Member->StandIndex];
	OutGoal = Stand.Position;
	OutExit = Stand.Exit;
	bOutAtCounter = Stand.bAtCounter;
	return true;
}

void UColaTableQueueComponent::Leave(ACharacter& Customer)
{
	const auto* Member = FindMember(Customer);
	if (!Member) return;
	if (Stands.IsValidIndex(Member->StandIndex))
	{
		const FVector Position = Stands[Member->StandIndex].Position;
		if (OccupiesStand(Customer, Position, LayoutRadius)) Departures.Add({&Customer, Position});
	}
	Members.RemoveAll([&Customer](const FMember& Entry) { return Entry.Customer.Get() == &Customer; });
}

void UColaTableQueueComponent::RejectStand(ACharacter& Customer)
{
	if (auto* Member = FindMember(Customer); Member && Stands.IsValidIndex(Member->StandIndex))
	{
		Member->RejectedStand = Member->StandIndex;
		Member->RejectedUntil = GetWorld()->GetTimeSeconds() + RetrySeconds;
		Member->StandIndex = INDEX_NONE;
	}
}

void UColaTableQueueComponent::IgnoreCustomers(FCollisionQueryParams& Query) const
{
	const auto IgnoreQueue = [&](const UColaTableQueueComponent& Queue)
	{
		for (const auto& Member : Queue.Members)
			if (const auto* Character = Member.Customer.Get()) Query.AddIgnoredActor(Character);
		for (const auto& Departing : Queue.Departures)
			if (const auto* Character = Departing.Customer.Get()) Query.AddIgnoredActor(Character);
	};
	IgnoreQueue(*this);
	if (const auto* Registry = GetWorld()->GetSubsystem<UColaTableQueueSubsystem>())
		for (const auto& Entry : Registry->Queues)
			if (const auto* Queue = Entry.Get(); Queue && Queue != this) IgnoreQueue(*Queue);
}

void UColaTableQueueComponent::UpdateStands(double Now)
{
	Members.RemoveAll([](const FMember& Entry) { return !Entry.Customer.IsValid(); });
	if (Members.IsEmpty()) return;
	EnsureLayout(*Members[0].Customer.Get(), Now);
	FillStands(Now);
}

void UColaTableQueueComponent::EnsureLayout(const ACharacter& Customer, double Now)
{
	const ACharacter* Reference = &Customer;
	double Radius = RadiusOf(*Reference);
	double Height = Reference->GetSimpleCollisionHalfHeight();
	for (const auto& Entry : Members)
	{
		const auto* Character = Entry.Customer.Get();
		if (!Character) continue;
		if (RadiusOf(*Character) > Radius)
		{
			Radius = RadiusOf(*Character);
			Reference = Character;
		}
		Height = FMath::Max(Height, double(Character->GetSimpleCollisionHalfHeight()));
	}

	const auto* Surface = Tabletop.Get();
	const UStaticMesh* Mesh = Surface ? Surface->GetStaticMesh() : nullptr;
	if (!Mesh) return;
	const FTransform SurfaceTransform = Surface->GetComponentTransform();
	const int32 Desired = FMath::Clamp(Members.Num() + 4, 16, MaximumStands);
	const bool bLayoutChanged = Desired > LayoutCapacity || Radius > LayoutRadius + 1.0 || Height > LayoutHalfHeight + 1.0
		|| !LayoutTransform.Equals(SurfaceTransform, .1);
	if ((!Stands.IsEmpty() && !bLayoutChanged) || (Stands.IsEmpty() && Now < NextEmptyLayoutRetry)) return;
	NextEmptyLayoutRetry = Now + RecheckSeconds;

	auto* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	const auto* NavData = Nav ? NavigationFor(*Nav, *Reference) : nullptr;
	if (!NavData) return;
	const FVector Feet = Reference->GetNavAgentLocation();
	LayoutRadius = FMath::Max(Radius, LayoutRadius);
	LayoutHalfHeight = FMath::Max(Height, LayoutHalfHeight);
	const FVector SearchExtent(80, 80, FMath::Max(180.0, LayoutHalfHeight));
	const auto Shape = FCollisionShape::MakeCapsule(float(FMath::Max(1.0, LayoutRadius - 2.0)),
		float(FMath::Max(1.0, LayoutHalfHeight - 2.0)));
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(ColaQueueStand), false, Reference);
	FCollisionResponseParams Response;
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	const FVector CapsuleOffset(0, 0, LayoutHalfHeight + 4.0);
	const auto Project = [&](const FVector& Goal, FVector& Out)
	{
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Goal, Projected, SearchExtent, NavData)
			|| FVector::DistSquared2D(Goal, Projected.Location) > FMath::Square(45.0)) return false;
		if (GetWorld()->OverlapBlockingTestByChannel(Projected.Location + CapsuleOffset,
			FQuat::Identity, ECC_Pawn, Shape, Query, Response)) return false;
		Out = Projected.Location;
		return true;
	};
	const auto ClearExit = [&](const FVector& Start, const FVector& End)
	{
		FVector NavHit;
		if (NavData->Raycast(Start, End, NavHit, NavData->GetDefaultQueryFilter(), Reference)) return false;
		return !GetWorld()->SweepTestByChannel(Start + CapsuleOffset, End + CapsuleOffset,
			FQuat::Identity, ECC_Pawn, Shape, Query, Response);
	};

	const TArray<FStand> Previous = MoveTemp(Stands);
	BuildStands(Mesh->GetBoundingBox(), SurfaceTransform, Feet.Z, LayoutRadius, Desired, Project, ClearExit);
	LayoutCapacity = Desired;
	LayoutTransform = SurfaceTransform;
	for (auto& Member : Members)
	{
		if (Previous.IsValidIndex(Member.StandIndex))
		{
			const auto& Old = Previous[Member.StandIndex];
			Member.StandIndex = Stands.IndexOfByPredicate([&Old](const FStand& Stand)
			{
				return Old.Priority() == Stand.Priority() && Old.Position.Equals(Stand.Position, 1.0);
			});
		}
		Member.RejectedStand = INDEX_NONE;
	}
}

void UColaTableQueueComponent::BuildStands(const FBox& LocalBox, const FTransform& SurfaceTransform, double FootZ, double Radius,
	int32 DesiredStands, TFunctionRef<bool(const FVector&, FVector&)> Project,
	TFunction<bool(const FVector&, const FVector&)> IsExitClear)
{
	Stands.Reset();
	FVector Center = SurfaceTransform.TransformPosition(LocalBox.GetCenter());
	Center.Z = FootZ;
	FVector X = SurfaceTransform.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal2D();
	if (X.IsNearlyZero()) X = FVector::ForwardVector;
	const FVector Y(-X.Y, X.X, 0);
	const FVector Extent = LocalBox.GetExtent();
	double HalfX = 0, HalfY = 0;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Offset = SurfaceTransform.TransformVector(Extent * FVector(Corner & 1 ? 1 : -1, Corner & 2 ? 1 : -1, Corner & 4 ? 1 : -1));
		HalfX = FMath::Max(HalfX, FMath::Abs(FVector::DotProduct(Offset, X)));
		HalfY = FMath::Max(HalfY, FMath::Abs(FVector::DotProduct(Offset, Y)));
	}

	struct FSide { FVector Out; FVector Along; double Edge; double Length; };
	const FSide Sides[] = {{X, Y, HalfX, HalfY}, {-X, Y, HalfX, HalfY}, {Y, X, HalfY, HalfX}, {-Y, X, HalfY, HalfX}};
	const double Separation = 2.0 * Radius + 2.0 * StandMargin;
	const double ColumnSpacing = 2.0 * Separation + 24.0;
	const double ExitDepth = 2.4 * Radius + 95.0;
	const auto IsInExitLane = [Separation](const FVector& Point, const FVector& Start, const FVector& Exit)
	{
		return FMath::PointDistToSegmentSquared(Point, Start, Exit) < FMath::Square(Separation);
	};
	const auto TryPosition = [&](const FVector& Goal, FVector& Out)
	{
		return Project(Goal, Out) && !Stands.ContainsByPredicate([&](const FStand& Occupied)
		{
			return FVector::DistSquared2D(Occupied.Position, Out) < FMath::Square(Separation);
		});
	};
	for (const FSide& Side : Sides)
	{
		const FVector Baseline = Center + Side.Out * (Side.Edge + Radius + StandMargin);
		const double HalfUsable = FMath::Max(0.0, Side.Length - Radius);
		const int32 Columns = FMath::Clamp(FMath::FloorToInt32(2.0 * HalfUsable / ColumnSpacing) + 1, 1, 5);
		for (int32 Index = 0; Index < Columns; ++Index)
		{
			const double Offset = (Index - (Columns - 1) * .5) * ColumnSpacing;
			const FVector Goal = Baseline + Side.Along * Offset;
			FVector Position, Exit;
			if (!TryPosition(Goal, Position)) continue;
			const FVector ExitDirections[] = {Side.Out, Side.Out + Side.Along * .65, Side.Out - Side.Along * .65,
				Side.Along, -Side.Along};
			for (const auto& Direction : ExitDirections)
			{
				const FVector ExitGoal = Goal + Direction * ExitDepth;
				if (!Project(ExitGoal, Exit) || FVector::DistSquared2D(Position, Exit) < FMath::Square(Separation)) continue;
				if (IsExitClear && !IsExitClear(Position, Exit)) continue;
				if (Stands.ContainsByPredicate([&](const FStand& Other)
				{
					return IsInExitLane(Other.Position, Position, Exit) || IsInExitLane(Position, Other.Position, Other.Exit);
				})) continue;
				Stands.Add({Position, Exit, true});
				break;
			}
		}
	}

	for (int32 Row = 1; Row <= MaximumWaitingRows && Stands.Num() < DesiredStands; ++Row)
	{
		for (const FSide& Side : Sides)
		{
			const double Depth = Side.Edge + Radius + StandMargin + ExitDepth + StandMargin + (Row - 1) * Separation;
			const FVector Baseline = Center + Side.Out * Depth;
			const int32 HalfColumns = FMath::Clamp(FMath::FloorToInt32(Side.Length / ColumnSpacing) + 1, 1, 3);
			for (int32 Index = 0; Index < HalfColumns; ++Index)
			{
				for (double Sign : {-1.0, 1.0})
				{
					const double Offset = (Index + .5) * ColumnSpacing + (Row % 2 == 0 ? StandMargin : 0.0);
					const FVector Goal = Baseline + Side.Along * (Sign * Offset);
					FVector Position;
					if (!TryPosition(Goal, Position)) continue;
					const bool bBlocksExit = Stands.ContainsByPredicate([&](const FStand& Stand)
					{
						return Stand.bAtCounter && IsInExitLane(Position, Stand.Position, Stand.Exit);
					});
					if (!bBlocksExit) Stands.Add({Position, Position, false, Row});
					if (Stands.Num() >= MaximumStands) return;
				}
			}
		}
	}
}

bool UColaTableQueueComponent::IsStandClaimedElsewhere(const FStand& Stand, const ACharacter& Customer) const
{
	const auto* Registry = GetWorld()->GetSubsystem<UColaTableQueueSubsystem>();
	if (!Registry) return false;
	for (const auto& Entry : Registry->Queues)
	{
		const auto* Queue = Entry.Get();
		if (!Queue || Queue == this || !Queue->Tabletop.IsValid()) continue;
		const double ClearanceSq = FMath::Square(LayoutRadius + Queue->LayoutRadius + StandMargin);
		const double FloorTolerance = FMath::Max3(1.0, LayoutHalfHeight, Queue->LayoutHalfHeight);
		for (const auto& Peer : Queue->Members)
		{
			const auto* Shopper = Peer.Customer.Get();
			if (!Shopper || Shopper == &Customer || !Queue->Stands.IsValidIndex(Peer.StandIndex)) continue;
			const auto& Other = Queue->Stands[Peer.StandIndex];
			if (FMath::Abs(Stand.Position.Z - Other.Position.Z) >= FloorTolerance) continue;
			if (FVector::DistSquared2D(Stand.Position, Other.Position) < ClearanceSq
				|| (Other.bAtCounter && FMath::PointDistToSegmentSquared(Stand.Position, Other.Position, Other.Exit) < ClearanceSq)
				|| (Stand.bAtCounter && FMath::PointDistToSegmentSquared(Other.Position, Stand.Position, Stand.Exit) < ClearanceSq)) return true;
		}
		for (const auto& Departing : Queue->Departures)
		{
			const auto* Shopper = Departing.Customer.Get();
			if (Shopper && Shopper != &Customer && OccupiesStand(*Shopper, Departing.Position, Queue->LayoutRadius)
				&& OccupiesStand(*Shopper, Stand.Position, LayoutRadius)) return true;
		}
	}
	return false;
}

void UColaTableQueueComponent::FillStands(double Now)
{
	Departures.RemoveAll([this](const FDeparted& Entry)
	{
		const auto* Character = Entry.Customer.Get();
		return !Character || !OccupiesStand(*Character, Entry.Position, LayoutRadius);
	});
	TBitArray<> Reserved(false, Stands.Num());
	for (const auto& Member : Members)
		if (Stands.IsValidIndex(Member.StandIndex)) Reserved[Member.StandIndex] = true;

	// A vacant reservation can still be physically occupied while its old customer is leaving.
	constexpr int32 Blocked = -2;
	TArray<int32> Occupants;
	Occupants.Init(INDEX_NONE, Stands.Num());
	for (int32 Slot = 0; Slot < Stands.Num(); ++Slot)
	{
		const auto& Stand = Stands[Slot];
		if (Departures.ContainsByPredicate([&](const FDeparted& Entry)
		{
			return OccupiesStand(*Entry.Customer.Get(), Stand.Position, LayoutRadius);
		}))
		{
			Occupants[Slot] = Blocked;
			continue;
		}
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const auto* Character = Members[Index].Customer.Get();
			if (!Character || !OccupiesStand(*Character, Stand.Position, LayoutRadius)) continue;
			if (Occupants[Slot] != INDEX_NONE)
			{
				Occupants[Slot] = Blocked;
				break;
			}
			Occupants[Slot] = Index;
		}
	}

	// Match the closest eligible customer to a vacancy before considering a farther row.
	auto* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	TSet<FIntPoint> FailedRoutes;
	for (int32 Priority = 0; Priority <= MaximumWaitingRows; ++Priority)
	{
		for (;;)
		{
			FMember* Nearest = nullptr;
			int32 BestSlot = INDEX_NONE;
			int32 BestMember = INDEX_NONE;
			double NearestDistance = TNumericLimits<double>::Max();
			for (int32 Slot = 0; Slot < Stands.Num(); ++Slot)
			{
				const auto& Stand = Stands[Slot];
				if (Reserved[Slot] || Stand.Priority() != Priority || Occupants[Slot] == Blocked) continue;
				for (int32 Index = 0; Index < Members.Num(); ++Index)
				{
					auto& Member = Members[Index];
					const auto* Character = Member.Customer.Get();
					if (!Character || (Occupants[Slot] != INDEX_NONE && Occupants[Slot] != Index)
						|| (Member.RejectedStand == Slot && Now < Member.RejectedUntil) || FailedRoutes.Contains(FIntPoint(Index, Slot))) continue;
					if (Stand.bAtCounter && !CanReachStock(*Character, Stand.Position, Member.Bottles)) continue;
					if (Stands.IsValidIndex(Member.StandIndex))
					{
						const auto& Previous = Stands[Member.StandIndex];
						if (Previous.Priority() <= Priority
							&& (!Stand.bAtCounter || !Previous.bAtCounter || CanReachStock(*Character, Previous.Position, Member.Bottles))) continue;
					}
					const double Distance = FVector::DistSquared2D(Character->GetActorLocation(), Stand.Position);
					if (Distance >= NearestDistance || IsStandClaimedElsewhere(Stand, *Character)) continue;
					NearestDistance = Distance;
					Nearest = &Member;
					BestSlot = Slot;
					BestMember = Index;
				}
			}
			if (!Nearest) break;
			const auto* Character = Nearest->Customer.Get();
			const auto* NavData = Nav ? NavigationFor(*Nav, *Character) : nullptr;
			if (NavData && WalkingDistance(*Nav, *NavData, *Character, Stands[BestSlot].Position) >= NoServiceRoute)
			{
				FailedRoutes.Add(FIntPoint(BestMember, BestSlot));
				continue;
			}
			if (Stands.IsValidIndex(Nearest->StandIndex)) Reserved[Nearest->StandIndex] = false;
			Nearest->StandIndex = BestSlot;
			Reserved[BestSlot] = true;
		}
	}
}
