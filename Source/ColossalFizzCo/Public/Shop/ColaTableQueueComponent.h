#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "ColaTableQueueComponent.generated.h"

class ACharacter;
class UColaTableQueueComponent;
class UStaticMeshComponent;
struct FCollisionQueryParams;

struct FColaServingBottle
{
	FVector Position = FVector::ZeroVector;
	double Reach = 0.0;
	int32 ISID = 0;
};

/** Tracks shelf queues in a world so adjacent shelves can share aisle space. */
UCLASS()
class COLOSSALFIZZCO_API UColaTableQueueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

private:
	friend class UColaTableQueueComponent;
	TArray<TWeakObjectPtr<UColaTableQueueComponent>> Queues;
};

/** Server-owned reservations shared by all customers visiting one shelf surface. */
UCLASS()
class COLOSSALFIZZCO_API UColaTableQueueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static UColaTableQueueComponent* GetOrCreate(AActor& Table, UStaticMeshComponent& Surface);
	double GetServiceCost(ACharacter& Customer, TConstArrayView<FColaServingBottle> Bottles);
	bool GetStand(ACharacter& Customer, FVector& OutGoal, FVector& OutExit, bool& bOutAtCounter,
		TConstArrayView<FColaServingBottle> Bottles = {});
	void Leave(ACharacter& Customer);
	void RejectStand(ACharacter& Customer);
	void IgnoreCustomers(FCollisionQueryParams& Query) const;

private:
	friend class FColaTableQueueTest;

	struct FStand
	{
		FVector Position;
		FVector Exit;
		bool bAtCounter = false;
		int32 WaitingRow = 1;

		int32 Priority() const { return bAtCounter ? 0 : WaitingRow; }
	};
	struct FMember
	{
		TWeakObjectPtr<ACharacter> Customer;
		int32 StandIndex = INDEX_NONE;
		int32 RejectedStand = INDEX_NONE;
		double RejectedUntil = 0;
		TArray<FColaServingBottle> Bottles;
	};
	struct FDeparted
	{
		TWeakObjectPtr<ACharacter> Customer;
		FVector Position;
	};

	TWeakObjectPtr<UStaticMeshComponent> Tabletop;
	TArray<FStand> Stands;
	TArray<FMember> Members;
	TArray<FDeparted> Departures;
	FTransform LayoutTransform;
	double LayoutRadius = 0;
	double LayoutHalfHeight = 0;
	double NextUpdate = 0;
	double NextEmptyLayoutRetry = 0;
	int32 LayoutCapacity = 0;

	FMember* FindMember(const ACharacter& Customer);
	void UpdateStands(double Now);
	void EnsureLayout(const ACharacter& Customer, double Now);
	void BuildStands(const FBox& LocalBox, const FTransform& SurfaceTransform, double FootZ, double Radius,
		int32 DesiredStands, TFunctionRef<bool(const FVector&, FVector&)> Project,
		TFunction<bool(const FVector&, const FVector&)> IsExitClear = {});
	bool IsStandClaimedElsewhere(const FStand& Stand, const ACharacter& Customer) const;
	void FillStands(double Now);
};
