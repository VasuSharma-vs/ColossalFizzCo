#include "Shop/ShopPlayerInteractionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Resources/PressurizedColaContainer.h"
#include "Resources/HoseCableActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "WorldInteractionCarry.h"

namespace
{
    APawn* InteractionPawn(AActor* Owner)
    {
        if (auto* PC = Cast<APlayerController>(Owner)) return PC->GetPawn();
        return Cast<APawn>(Owner);
    }
}

UPrimitiveComponent* UShopPlayerInteractionComponent::FindColaContainerWorldControl(const APawn& Pawn) const
{
    const auto* Trace = FindFProperty<FStructProperty>(Pawn.GetClass(),TEXT("Hit Result On tick"));
    if (!Trace || Trace->Struct != FHitResult::StaticStruct()) return nullptr;
    const auto* Hit = Trace->ContainerPtrToValuePtr<FHitResult>(&Pawn);
    auto* Container = Cast<APressurizedColaContainer>(Hit->GetActor());
    auto* Control = Hit->GetComponent();
    if (!IsValid(Container) || !Container->FindFunction(FName(TEXT("On Intraction Begain")))
        || (!Container->IsHoseSocket(Control) && !Container->IsValveControl(Control))) return nullptr;
    return Control;
}

bool UShopPlayerInteractionComponent::CanUseColaContainerControl(const UPrimitiveComponent* Control,const AActor* HeldObject) const
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !IsValid(Control) || !CanReach(Control->GetOwner())) return false;
    const APawn* Pawn = InteractionPawn(GetOwner());
    if (!Pawn || Control->Bounds.GetBox().ComputeSquaredDistanceToPoint(Pawn->GetActorLocation())>FMath::Square(MaxInteractionDistance)) return false;

    FVector Eye;
    FRotator View;
    Pawn->GetActorEyesViewPoint(Eye,View);
    FHitResult Obstacle;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ColaContainerControl),true,Pawn);
    if (HeldObject) Params.AddIgnoredActor(HeldObject);
    return !GetWorld()->LineTraceSingleByChannel(Obstacle,Eye,Control->Bounds.Origin,ECC_Visibility,Params)
        || Obstacle.GetActor()==Control->GetOwner();
}

bool UShopPlayerInteractionComponent::ExecuteColaContainerInteraction(APressurizedColaContainer* Container,UPrimitiveComponent* Control)
{
    if (!IsValid(Container) || !IsValid(Control) || Control->GetOwner()!=Container) return false;
    if (Container->IsValveControl(Control)) return TurnColaContainerValve(Container,Control,true);
    if (!Container->IsHoseSocket(Control)) return false;

    APawn* Pawn = InteractionPawn(GetOwner());
    if (!Pawn) return false;
    const auto* ComponentProperty = FindFProperty<FObjectPropertyBase>(Pawn->GetClass(),TEXT("Holding Component"));
    const auto* ActorProperty = FindFProperty<FObjectPropertyBase>(Pawn->GetClass(),TEXT("Holding Actor"));
    auto* HeldComponent = ComponentProperty ? Cast<UPrimitiveComponent>(ComponentProperty->GetObjectPropertyValue_InContainer(Pawn)) : nullptr;
    auto* HeldActor = ActorProperty ? Cast<AActor>(ActorProperty->GetObjectPropertyValue_InContainer(Pawn)) : nullptr;
    auto* Cable = ServerCable.Get();
    AActor* HoseOwner = Cable ? Cable->GetOwner() : nullptr;
    if (IsValid(HeldActor)) HoseOwner=HeldActor;
    if (IsValid(HeldComponent)) HoseOwner=HeldComponent->GetOwner();
    auto* Hose = Cast<AHoseCableActor>(HoseOwner);
    if (!IsValid(Hose) || !CanUseColaContainerControl(Control,Hose)) return false;

    const int32 ComponentIndex = HeldComponent==Hose->StartEndpointMesh ? 0
        : HeldComponent==Hose->EndEndpointMesh ? Hose->Cable->NumSegments : INDEX_NONE;
    const bool bHeldComponent = ComponentIndex!=INDEX_NONE && IsValid(HeldComponent)
        && IsValid(HeldComponent->GetAttachParent()) && HeldComponent->GetAttachParent()->GetOwner()==Pawn;
    const bool bHeldActor = !IsValid(HeldComponent) && HeldActor==Hose && Hose->GetAttachParentActor()==Pawn;
    int32 HeldIndex = Cable==Hose->Cable ? Hose->FindHeldConnector(Pawn) : INDEX_NONE;
    if (bHeldComponent) HeldIndex = ComponentIndex;
    if (bHeldActor && HeldIndex==INDEX_NONE) HeldIndex = Hose->GetSupplyConnectorIndex();
    if (HeldIndex==INDEX_NONE || !Container->AttachSupplyHoseAtConnector(Hose,HeldIndex)) return false;
    if (bHeldComponent)
    {
        WorldInteractionCarry::InvokeCarryRelease(*Pawn,TEXT("UngrabComponentonServer"),HeldComponent);
        if (ComponentProperty->GetObjectPropertyValue_InContainer(Pawn)==HeldComponent)
        {
            // The connector is mounted. Clear the old hand reference on the owner as well.
            WorldInteractionCarry::InvokeCarryRelease(*Pawn,TEXT("UngrabComponentonClient"),HeldComponent,FUNC_NetClient);
            if (ComponentProperty->GetObjectPropertyValue_InContainer(Pawn)==HeldComponent)
                ComponentProperty->SetObjectPropertyValue_InContainer(Pawn,nullptr);
            Pawn->ForceNetUpdate();
        }
    }
    if (bHeldActor)
    {
        WorldInteractionCarry::InvokeCarryRelease(*Pawn,TEXT("RequestServertoDetachActortoComponent"),Hose);
        if (Hose->GetAttachParentActor()==Pawn) Hose->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        if (ActorProperty->GetObjectPropertyValue_InContainer(Pawn)==Hose)
        {
            ActorProperty->SetObjectPropertyValue_InContainer(Pawn,nullptr);
            // The saved character's zero holding state represents an empty hand.
            if (const auto* State = FindFProperty<FByteProperty>(Pawn->GetClass(),TEXT("Holding State")))
                State->SetPropertyValue_InContainer(Pawn,0);
            Pawn->ForceNetUpdate();
        }
    }
    if (Cable==Hose->Cable)
    {
        Cable->EndPointGrab(Pawn);
        ServerCable.Reset();
        Client_CableGrabResult(Cable,false);
    }
    return true;
}

bool UShopPlayerInteractionComponent::TryTurnColaContainerValveFromView(bool bOpen)
{
    auto* Pawn=Cast<APawn>(GetOwner());
    auto* Controller=Pawn?Cast<APlayerController>(Pawn->GetController()):Cast<APlayerController>(GetOwner());
    if(!Controller || !Controller->IsLocalController() || !GetWorld() || LocalValveSequence==MAX_int64) return false;
    if(!Pawn) Pawn=Controller->GetPawn();
    if(!Pawn || !FMath::IsFinite(MaxInteractionDistance) || MaxInteractionDistance<=0) return false;
    FVector Eye; FRotator Rotation; Controller->GetPlayerViewPoint(Eye,Rotation);
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(ColaContainerValve),true,Pawn);
    const float Reach=MaxInteractionDistance+FVector::Distance(Eye,Pawn->GetActorLocation());
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Rotation.Vector()*Reach,ECC_Visibility,Params)) return false;
    auto* Container=Cast<APressurizedColaContainer>(Hit.GetActor());
    if(!Container || !Container->IsValveControl(Hit.GetComponent()) || !CanReach(Container)) return false;
    return RequestTurnColaContainerValve(Container,bOpen);
}

bool UShopPlayerInteractionComponent::RequestTurnColaContainerValve(APressurizedColaContainer* Container,bool bOpen)
{
    if (LocalValveSequence==MAX_int64) return false;
    const int64 Sequence=++LocalValveSequence;
    if(GetOwner()->HasAuthority()) return ExecuteTurnColaContainerValve(Container,bOpen,Sequence);
    Server_TurnColaContainerValve(Container,bOpen,Sequence); return true;
}
bool UShopPlayerInteractionComponent::ExecuteTurnColaContainerValve(APressurizedColaContainer* Container,bool bOpen,int64 Sequence)
{
    if(!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !GetWorld()
        || Sequence<=0 || Sequence<=LastValveSequence) return false;
    LastValveSequence=Sequence;
    return IsValid(Container) && TurnColaContainerValve(Container,Container->ValveVolume,bOpen);
}

bool UShopPlayerInteractionComponent::TurnColaContainerValve(APressurizedColaContainer* Container,const UPrimitiveComponent* Control,bool bOpen)
{
    const auto* Cable=ServerCable.Get();
    if(!Container->IsValveControl(Control) || !CanUseColaContainerControl(Control,Cable ? Cable->GetOwner() : nullptr)) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    if(LastValveInteractionTime>=0 && Now-LastValveInteractionTime<.08) return false;
    if(!FMath::IsFinite(Container->ValveInteractionStepDegrees) || Container->ValveInteractionStepDegrees<=0) return false;
    LastValveInteractionTime=Now;
    return Container->RotateValve((bOpen?1.f:-1.f)*Container->ValveInteractionStepDegrees);
}
void UShopPlayerInteractionComponent::Server_TurnColaContainerValve_Implementation(APressurizedColaContainer* Container,bool bOpen,int64 Sequence)
{
    ExecuteTurnColaContainerValve(Container,bOpen,Sequence);
}
