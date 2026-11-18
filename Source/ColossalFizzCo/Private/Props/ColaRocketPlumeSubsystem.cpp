#include "Props/ColaRocketPlumeSubsystem.h"

#include "Components/SceneComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstanceController.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"

void UColaRocketPlumeSubsystem::SetBottleNozzle(AColaPropActor* Bottle, USceneComponent* Parent,
	FName Socket, const FTransform& RelativeNozzle)
{
	check(Bottle && Parent && Parent->GetWorld() == GetWorld());
	Nozzles.Add(Bottle, FNozzle{Parent, Socket, RelativeNozzle});
	if (!NozzleTick.IsTickFunctionRegistered())
	{
		NozzleTick.Owner = this;
		NozzleTick.bCanEverTick = true;
		NozzleTick.TickGroup = TG_PostPhysics;
		NozzleTick.RegisterTickFunction(GetWorld()->PersistentLevel);
	}
	NozzleTick.SetTickFunctionEnable(true);
}

void UColaRocketPlumeSubsystem::RemoveBottle(AColaPropActor* Bottle)
{
	Nozzles.Remove(Bottle);
}

void UColaRocketPlumeSubsystem::FNozzleTick::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type Thread, const FGraphEventRef& Completion)
{
	Owner->UpdatePlume();
}

void UColaRocketPlumeSubsystem::UpdatePlume()
{
	const bool bHadPositions = !Positions.IsEmpty();
	Positions.Reset(Nozzles.Num());
	Directions.Reset(Nozzles.Num());
	NozzleScales.Reset(Nozzles.Num());
	NozzleColors.Reset(Nozzles.Num());
	FVector Center = FVector::ZeroVector;
	for (auto It = Nozzles.CreateIterator(); It; ++It)
	{
		FNozzle& Nozzle = It.Value();
		const AColaPropActor* Bottle = It.Key().Get();
		USceneComponent* Parent = Nozzle.Parent.Get();
		if (!Bottle || !Parent)
		{
			It.RemoveCurrent();
			continue;
		}
		const FTransform Transform = Nozzle.Relative * Parent->GetSocketTransform(Nozzle.Socket);
		Positions.Add(Transform.GetLocation());
		Directions.Add(Transform.GetRotation().GetAxisZ());
		const FVector WorldScale = Transform.GetScale3D();
		NozzleScales.Add(static_cast<float>(FMath::Sqrt(FMath::Abs(WorldScale.X * WorldScale.Y))));
		// Keep material lookups off the per-frame path. Late replication, refills
		// and live registration of an edited definition refresh the next spawn.
		const int32 ContentID = Bottle->GetContentID();
		const uint32 Revision = Bottle->PropManagerReference ? Bottle->PropManagerReference->GetContentDefinitionRevision() : 0;
		if (Nozzle.CachedContentID != ContentID || Nozzle.CachedBottlePID != Bottle->ProductID || Nozzle.ContentRevision != Revision)
		{
			const bool bKnown = Bottle->TryGetRocketSprayColor(Nozzle.SprayColor);
			Nozzle.CachedContentID = bKnown || ContentID == 0 ? ContentID : INDEX_NONE;
			Nozzle.CachedBottlePID = Bottle->ProductID;
			Nozzle.ContentRevision = Revision;
		}
		NozzleColors.Add(Nozzle.SprayColor);
		Center += Transform.GetLocation();
	}

	const bool bStarting = !SharedPlume;
	if (Positions.Num() > 0)
	{
		Center /= Positions.Num();
		DrainStartSimulationAge = -1.f;
		if (bStarting)
		{
			auto* System = LoadObject<UNiagaraSystem>(nullptr,
				TEXT("/Game/Niagara/ColaRocket/NS_ColaRocketThrustShared.NS_ColaRocketThrustShared"));
			if (System)
				SharedPlume = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Center, FRotator::ZeroRotator,
					FVector::OneVector, false, false, ENCPoolMethod::None, false);
			if (!SharedPlume) { NozzleTick.SetTickFunctionEnable(false); return; }
			SharedPlume->SetTickBehavior(ENiagaraTickBehavior::ForceTickLast);
		}
		else
		{
			SharedPlume->SetWorldLocation(Center);
		}
	}
	else
	{
		// Count simulated seconds so a stalled frame cannot discard living
		// bubbles. New nozzles reuse the same instance at any point in the tail.
		constexpr float MaxBubbleLifetime = 8.f;
		const auto Controller = SharedPlume ? SharedPlume->GetSystemInstanceController() : nullptr;
		if (!Controller || !Controller->IsValid() || Controller->IsComplete())
		{
			DestroyPlume();
			NozzleTick.SetTickFunctionEnable(false);
			return;
		}
		const float SimulationAge = Controller->GetAge();
		if (DrainStartSimulationAge < 0.f || SimulationAge < DrainStartSimulationAge)
		{
			DrainStartSimulationAge = SimulationAge;
		}
		if (SimulationAge - DrainStartSimulationAge > MaxBubbleLifetime + .25f)
		{
			DestroyPlume();
			NozzleTick.SetTickFunctionEnable(false);
			return;
		}
		if (!bHadPositions) return;
	}

	static const FName PositionParameter(TEXT("User.NozzlePositions"));
	static const FName DirectionParameter(TEXT("User.NozzleDirections"));
	static const FName ScaleParameter(TEXT("User.NozzleScales"));
	static const FName ColorParameter(TEXT("User.NozzleColors"));
	static const FName CountParameter(TEXT("User.NozzleCount"));
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(SharedPlume, PositionParameter, Positions);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(SharedPlume, DirectionParameter, Directions);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(SharedPlume, ScaleParameter, NozzleScales);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(SharedPlume, ColorParameter, NozzleColors);
	SharedPlume->SetVariableInt(CountParameter, Positions.Num());
	if (bStarting) SharedPlume->Activate();
}

void UColaRocketPlumeSubsystem::DestroyPlume()
{
	if (SharedPlume) SharedPlume->DestroyComponent();
	SharedPlume = nullptr;
}

void UColaRocketPlumeSubsystem::Deinitialize()
{
	NozzleTick.UnRegisterTickFunction();
	Nozzles.Reset();
	DestroyPlume();
	Super::Deinitialize();
}
