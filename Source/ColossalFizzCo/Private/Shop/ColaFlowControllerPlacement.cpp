#include "Shop/ColaFlowController.h"

#include "Components/HoseSocketComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Shop/MachinePlacementGeometry.h"

namespace
{
	bool HasPlacementController(const APawn* Player, const UWorld* World)
	{
		const auto* Controller=IsValid(Player) ? Cast<APlayerController>(Player->GetController()) : nullptr;
		return Controller && Controller->GetPawn()==Player && Player->GetWorld()==World;
	}
}

bool AColaFlowController::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	// The hidden original still supplies the actor reference and placement state to clients.
	return bMachinePlacementActive || Super::IsNetRelevantFor(RealViewer,ViewTarget,SrcLocation);
}

bool AColaFlowController::BeginMachinePlacement(APawn& Player)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !HasPlacementController(&Player,GetWorld())
		|| bMachinePlacementActive || IsHidden() || !GetActorEnableCollision()) return false;

	PlacementOperator=&Player;
	bMachinePlacementActive=true;
	ReleaseBatch();
	OnRep_MachinePlacement();
	SetActorHiddenInGame(true);
	for (UHoseSocketComponent* Socket : AllSelectorSockets) Socket->DetachHose();
	for (UHoseSocketComponent* Socket : AllFillerSockets) Socket->DetachHose();
	UpdateFlow();
	FlushNetDormancy();
	ForceNetUpdate();
	return true;
}

bool AColaFlowController::FinishMachinePlacement(APawn& Player, const FTransform& Pose)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !bMachinePlacementActive || PlacementOperator!=&Player
		|| !HasPlacementController(&Player,GetWorld()) || !ShopMachinePlacement::CanPlace(*this,Player,Pose)) return false;
	if (!GetActorTransform().Equals(Pose)
		&& !SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics)) return false;
	EndMachinePlacement();
	return true;
}

void AColaFlowController::CancelMachinePlacement(APawn& Player)
{
	if (HasAuthority() && bMachinePlacementActive && PlacementOperator==&Player) EndMachinePlacement();
}

void AColaFlowController::UpdateMachinePlacement()
{
	if (bMachinePlacementActive && !HasPlacementController(PlacementOperator.Get(),GetWorld())) EndMachinePlacement();
}

void AColaFlowController::EndMachinePlacement()
{
	PlacementOperator=nullptr;
	bMachinePlacementActive=false;
	OnRep_MachinePlacement();
	SetActorHiddenInGame(false);
	UpdateFlow();
	ForceNetUpdate();
}

void AColaFlowController::OnRep_MachinePlacement()
{
	SetActorEnableCollision(!bMachinePlacementActive);
	UpdateSocketAvailability();
}
