#include "Shop/ShopPlayerInteractionComponent.h"
#include "Resources/PressurizedColaContainer.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

bool UShopPlayerInteractionComponent::TryTurnColaContainerValveFromView(bool bOpen)
{
    auto* Pawn=Cast<APawn>(GetOwner());
    auto* Controller=Pawn?Cast<APlayerController>(Pawn->GetController()):Cast<APlayerController>(GetOwner());
    if(!Controller || !Controller->IsLocalController() || !GetWorld() || LocalValveSequence==MAX_int64) return false;
    if(!Pawn) Pawn=Controller->GetPawn();
    if(!Pawn || !FMath::IsFinite(MaxInteractionDistance) || MaxInteractionDistance<=0) return false;
    FVector Eye; FRotator Rotation; Controller->GetPlayerViewPoint(Eye,Rotation);
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(ColaContainerValve),false,Pawn);
    const float Reach=MaxInteractionDistance+FVector::Distance(Eye,Pawn->GetActorLocation());
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Rotation.Vector()*Reach,ECC_Visibility,Params)) return false;
    auto* Container=Cast<APressurizedColaContainer>(Hit.GetActor());
    if(!Container || Hit.GetComponent()!=Container->ValveWheel || !CanReach(Container)) return false;
    const int64 Sequence=++LocalValveSequence;
    if(GetOwner()->HasAuthority()) return ExecuteTurnColaContainerValve(Container,bOpen,Sequence);
    Server_TurnColaContainerValve(Container,bOpen,Sequence); return true;
}
bool UShopPlayerInteractionComponent::ExecuteTurnColaContainerValve(APressurizedColaContainer* Container,bool bOpen,int64 Sequence)
{
    if(!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !GetWorld()
        || Sequence<=0 || Sequence<=LastValveSequence) return false;
    LastValveSequence=Sequence;
    auto* Pawn=Cast<APawn>(GetOwner());
    if(!Pawn) if(auto* Controller=Cast<APlayerController>(GetOwner())) Pawn=Controller->GetPawn();
    if(!Pawn || !IsValid(Container) || Container->GetWorld()!=GetWorld() || !CanReach(Container)) return false;
    const FVector Control=Container->ValveWheel->GetComponentLocation();
    if(FVector::DistSquared(Pawn->GetActorLocation(),Control)>FMath::Square(MaxInteractionDistance)) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    if(LastValveInteractionTime>=0 && Now-LastValveInteractionTime<.08) return false;
    FVector Eye; FRotator Rotation; Pawn->GetActorEyesViewPoint(Eye,Rotation);
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(ColaContainerValveReach),false,Pawn);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Control,ECC_Visibility,Params) && Hit.GetActor()!=Container) return false;
    if(!FMath::IsFinite(Container->ValveInteractionStepDegrees) || Container->ValveInteractionStepDegrees<=0) return false;
    LastValveInteractionTime=Now;
    return Container->RotateValve((bOpen?1.f:-1.f)*Container->ValveInteractionStepDegrees);
}
void UShopPlayerInteractionComponent::Server_TurnColaContainerValve_Implementation(APressurizedColaContainer* Container,bool bOpen,int64 Sequence)
{
    ExecuteTurnColaContainerValve(Container,bOpen,Sequence);
}
