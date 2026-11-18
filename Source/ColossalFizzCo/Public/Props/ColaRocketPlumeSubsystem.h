#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ColaRocketPlumeSubsystem.generated.h"

class AColaPropActor;
class UNiagaraComponent;
class USceneComponent;

/** Feeds all local bottle mouths into one world-space Niagara simulation. */
UCLASS()
class COLOSSALFIZZCO_API UColaRocketPlumeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void SetBottleNozzle(AColaPropActor* Bottle, USceneComponent* Parent, FName Socket, const FTransform& RelativeNozzle);
	void RemoveBottle(AColaPropActor* Bottle);
	UNiagaraComponent* GetSharedPlume() const { return SharedPlume; }
	int32 GetNozzleCount() const { return Nozzles.Num(); }
	virtual void Deinitialize() override;

private:
	struct FNozzle
	{
		TWeakObjectPtr<USceneComponent> Parent;
		FName Socket;
		FTransform Relative;
		int32 CachedContentID = INDEX_NONE;
		int32 CachedBottlePID = INDEX_NONE;
		uint32 ContentRevision = 0;
		FLinearColor SprayColor = FLinearColor::White;
	};
	struct FNozzleTick : FTickFunction
	{
		UColaRocketPlumeSubsystem* Owner = nullptr;
		virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type Thread, const FGraphEventRef& Completion) override;
		virtual FString DiagnosticMessage() override { return TEXT("Cola rocket nozzle sampling"); }
	};
	FNozzleTick NozzleTick;
	TMap<TWeakObjectPtr<AColaPropActor>, FNozzle> Nozzles;
	TArray<FVector> Positions;
	TArray<FVector> Directions;
	TArray<float> NozzleScales;
	TArray<FLinearColor> NozzleColors;
	float DrainStartSimulationAge = -1.f;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> SharedPlume;

	void UpdatePlume();
	void DestroyPlume();
};
