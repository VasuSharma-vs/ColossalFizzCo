#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/ScopeExit.h"
#include "Shop/ColaTableQueueComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaTableQueueTest, "ColossalFizzCo.Shop.ColaTableQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FColaTableQueueTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	auto* Table = World->SpawnActor<AActor>();
	auto* Surface = NewObject<UStaticMeshComponent>(Table);
	Table->SetRootComponent(Surface);
	Table->AddInstanceComponent(Surface);
	Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Surface->RegisterComponent();
	auto* Queue = UColaTableQueueComponent::GetOrCreate(*Table, *Surface);
	if (!TestNotNull(TEXT("Table receives a shared queue"), Queue)) return false;
	TestTrue(TEXT("Repeated lookups reuse the table's queue"), Queue == UColaTableQueueComponent::GetOrCreate(*Table, *Surface));

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto AddCustomer = [&](const FVector& Location)
	{
		auto* Customer = World->SpawnActor<ACharacter>(Location, FRotator::ZeroRotator, Spawn);
		Customer->SetActorScale3D(FVector(6));
		return Customer;
	};
	const FVector CounterLocation(0, 0, 528);
	auto* Front = AddCustomer(CounterLocation);
	const double Radius = Front->GetSimpleCollisionRadius();
	if (!TestTrue(TEXT("The fixture uses oversized capsules like the placed customers"), Radius > 150.0)) return false;
	const double RowSpacing = 2.0 * Radius + 120.0;
	const FVector FirstWaitingStand(0, -RowSpacing, 0);
	const FVector SecondWaitingStand(1.2 * RowSpacing, -RowSpacing - 10.0, 0);
	auto* Near = AddCustomer(FirstWaitingStand + FVector(30, 0, CounterLocation.Z));
	auto* Far = AddCustomer(SecondWaitingStand + FVector(0, 0, CounterLocation.Z));

	// Flat, fully walkable ground keeps this test focused on queue geometry and reservation rules.
	Queue->BuildStands(FBox(FVector(-110, -80, -20), FVector(110, 80, 20)),
		FTransform(FRotator(0, 25, 0), FVector(250, -140, 60), FVector(6, 5, 1)), 0, Radius, 9,
		[](const FVector& Candidate, FVector& Projected) { Projected = Candidate; return true; });
	int32 CounterStands = 0;
	for (int32 Index = 0; Index < Queue->Stands.Num(); ++Index)
	{
		const auto& Stand = Queue->Stands[Index];
		CounterStands += Stand.bAtCounter;
		for (int32 Other = 0; Other < Index; ++Other)
			TestTrue(TEXT("Reserved centers give two giant capsules space to stand"),
				FVector::DistSquared2D(Stand.Position, Queue->Stands[Other].Position) > FMath::Square(2.0 * Radius));
		if (!Stand.bAtCounter) continue;
		for (int32 Other = 0; Other < Queue->Stands.Num(); ++Other)
		{
			if (Other == Index) continue;
			const FVector Position = Queue->Stands[Other].Position;
			const FVector NearestOnExit = FMath::ClosestPointOnSegment(Position, Stand.Position, Stand.Exit);
			TestTrue(TEXT("A customer can exit the counter without sweeping another occupied spot"),
				FVector::DistSquared2D(Position, NearestOnExit) > FMath::Square(2.0 * Radius));
		}
	}
	TestTrue(TEXT("The tabletop has front and waiting spots"), CounterStands > 0 && CounterStands < Queue->Stands.Num());

	// Isolate one counter lane so both waiting customers compete for the same opening.
	Queue->LayoutRadius = Radius;
	const FVector Exit(2.0 * RowSpacing, 0, 0);
	Queue->Stands = {
		{FVector::ZeroVector, Exit, true},
		{FirstWaitingStand, FirstWaitingStand, false},
		{SecondWaitingStand, SecondWaitingStand, false, 2}
	};
	Queue->Members.Add({Far});
	Queue->Members.Add({Near});
	Queue->Members.Add({Front});
	const auto Reservation = [&](const ACharacter* Customer)
	{
		const auto* Member = Queue->Members.FindByPredicate([Customer](const auto& Candidate) { return Candidate.Customer.Get() == Customer; });
		return Member ? Member->StandIndex : INDEX_NONE;
	};
	Queue->FillStands(1.0);
	TestEqual(TEXT("The closest customer takes the counter independent of registration order"), Reservation(Front), 0);
	TestEqual(TEXT("The first waiter has its own standing reservation"), Reservation(Near), 1);
	TestEqual(TEXT("The other waiter has a distinct standing reservation"), Reservation(Far), 2);

	Queue->FillStands(1.5);
	TestTrue(TEXT("A filled queue keeps existing reservations on later updates"),
		Reservation(Front) == 0 && Reservation(Near) == 1 && Reservation(Far) == 2);
	Queue->Leave(*Front);
	Front->SetActorLocation(FVector(1.5 * Radius, 0, CounterLocation.Z));
	Queue->FillStands(2.0);
	TestTrue(TEXT("The counter stays clear for the departing customer until its capsule moves away"),
		Reservation(Front) == INDEX_NONE && Reservation(Near) != 0 && Reservation(Far) != 0);

	Front->SetActorLocation(Exit + FVector(0, 0, CounterLocation.Z));
	Queue->FillStands(2.5);
	TestEqual(TEXT("The nearest waiting customer takes the vacated counter"), Reservation(Near), 0);
	TestEqual(TEXT("The next customer keeps its spot while the promoted customer is still in the waiting row"), Reservation(Far), 2);

	Near->SetActorLocation(CounterLocation);
	Queue->FillStands(2.75);
	TestEqual(TEXT("The next waiting customer moves into the newly freed waiting row"), Reservation(Far), 1);
	Queue->Leave(*Near);
	Near->SetActorLocation(Exit + FVector(0, RowSpacing, CounterLocation.Z));
	Queue->FillStands(3.0);
	TestEqual(TEXT("The next departure opens the same counter for the remaining waiter"), Reservation(Far), 0);

	auto* SecondSurface = NewObject<UStaticMeshComponent>(Table);
	SecondSurface->SetupAttachment(Surface);
	SecondSurface->SetStaticMesh(Surface->GetStaticMesh());
	Table->AddInstanceComponent(SecondSurface);
	SecondSurface->RegisterComponent();
	auto* SecondQueue = UColaTableQueueComponent::GetOrCreate(*Table, *SecondSurface);
	if (!TestNotNull(TEXT("A second mesh on the same actor has a queue"), SecondQueue)
		|| !TestTrue(TEXT("Each rack keeps its own queue"), SecondQueue != Queue)) return false;
	TestTrue(TEXT("The second mesh reuses its own queue"), SecondQueue == UColaTableQueueComponent::GetOrCreate(*Table, *SecondSurface));

	const FVector OpposingStand(0, 1.75 * Radius, 0);
	const FVector ClearStand(0, 3.5 * Radius, 0);
	const FVector CapsuleHeight(0, 0, CounterLocation.Z);
	Queue->Members.Reset();
	Queue->Departures.Reset();
	Queue->LayoutHalfHeight = CounterLocation.Z;
	Queue->Stands = {{FVector::ZeroVector, Exit, true}};
	SecondQueue->LayoutRadius = Radius;
	SecondQueue->LayoutHalfHeight = CounterLocation.Z;
	SecondQueue->Stands = {{OpposingStand, OpposingStand + Exit, true}, {ClearStand, ClearStand + Exit, true}};
	Front->SetActorLocation(CounterLocation);
	Far->SetActorLocation(OpposingStand + CapsuleHeight);
	Queue->Members.Add({Front});
	SecondQueue->Members.Add({Far});
	Queue->FillStands(4.0);
	SecondQueue->FillStands(4.0);
	TestEqual(TEXT("The first rack claims its counter"), Reservation(Front), 0);
	TestEqual(TEXT("The second rack uses the separated spot while the opposing spot is blocked"), SecondQueue->Members[0].StandIndex, 1);

	SecondQueue->Members[0].Bottles = {{OpposingStand + CapsuleHeight, Radius * .2, 1}};
	Queue->Leave(*Front);
	SecondQueue->FillStands(4.25);
	TestEqual(TEXT("The second rack waits for the departing customer to clear the shared aisle"), SecondQueue->Members[0].StandIndex, 1);
	Front->SetActorLocation(Exit + CapsuleHeight);
	SecondQueue->FillStands(4.5);
	TestEqual(TEXT("The customer moves to the newly clear stand that can reach the stock"), SecondQueue->Members[0].StandIndex, 0);
	return true;
}

#endif
