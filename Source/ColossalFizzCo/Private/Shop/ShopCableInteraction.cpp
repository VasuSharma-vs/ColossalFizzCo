#include "Shop/ShopPlayerInteractionComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace CableInteraction
{
    AActor* Participant(const UActorComponent* Component)
    {
        AActor* Owner=Component?Component->GetOwner():nullptr;
        if(auto* PC=Cast<APlayerController>(Owner))return PC->GetPawn();
        return Owner;
    }
    APlayerController* Controller(const UActorComponent* Component)
    {
        if(auto* PC=Cast<APlayerController>(Component->GetOwner()))return PC;
        if(auto* Pawn=Cast<APawn>(Component->GetOwner()))return Cast<APlayerController>(Pawn->GetController());
        return nullptr;
    }
}

bool UShopPlayerInteractionComponent::IsCableGrabActive() const{return LocalCable.IsValid();}

bool UShopPlayerInteractionComponent::TryToggleCableGrabFromView()
{
    if(LocalCable.IsValid()){RequestCableRelease();return true;}
    AActor* Actor=CableInteraction::Participant(this);
    APlayerController* PC=CableInteraction::Controller(this);
    if(!Actor||!PC||!PC->IsLocalController()||!GetWorld())return false;
    FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
    UCollisionCableComponent* BestCable=nullptr;int32 BestIndex=INDEX_NONE;FVector BestHit=FVector::ZeroVector;float BestDistance=FMath::Max(1.f,MaxInteractionDistance);
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        TInlineComponentArray<UCollisionCableComponent*> Cables;It->GetComponents(Cables);
        for(auto* Cable:Cables)
        {
            int32 Index;FVector Hit;float Distance;
            if(Cable->FindCablePointAlongRay(Eye,Rotation.Vector(),BestDistance,CableGrabTraceRadius,Index,Hit,Distance))
            {BestCable=Cable;BestIndex=Index;BestHit=Hit;BestDistance=Distance;}
        }
    }
    if(!BestCable)return false;
    TArray<FVector> Points;BestCable->GetCableParticleLocations(Points);
    return Points.IsValidIndex(BestIndex)&&RequestCableGrab(BestCable,BestHit,Points[BestIndex]);
}

bool UShopPlayerInteractionComponent::RequestCableGrab(UCollisionCableComponent* Cable,FVector Hit,FVector Target)
{
    if(!IsValid(Cable)||!Cable->bEnableGrabbing||Hit.ContainsNaN()||Target.ContainsNaN()||LocalCableSequence==MAX_int64)return false;
    const int64 Sequence=++LocalCableSequence;
    if(GetOwner()&&GetOwner()->HasAuthority()&&GetNetMode()!=NM_Client)
    {
        if(!ExecuteCableGrab(Cable,Hit,Target,Sequence))return false;
    }
    else Server_CableGrab(Cable,Hit,Target,Sequence);
    if(auto* PC=CableInteraction::Controller(this))
    {
        FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
        LocalCableViewOffset=Rotation.UnrotateVector(Target-Eye);
    }
    LocalCable=Cable;CableTargetAccumulator=0;SetComponentTickEnabled(true);return true;
}

bool UShopPlayerInteractionComponent::ExecuteCableGrab(UCollisionCableComponent* Cable,FVector Hit,FVector Target,int64 Sequence)
{
    if(!GetOwner()||!GetOwner()->HasAuthority()||GetNetMode()==NM_Client||Sequence<=LastCableSequence||Sequence<=0)return false;
    LastCableSequence=Sequence;
    AActor* Actor=CableInteraction::Participant(this);
    if(!Actor||!IsValid(Cable)||Cable->GetWorld()!=GetWorld()||Hit.ContainsNaN()||Target.ContainsNaN())return false;
    const double Reach=FMath::Min(FMath::Max(1.f,MaxInteractionDistance),FMath::Max(1.f,Cable->MaxGrabDistance));
    if(FVector::Distance(Actor->GetActorLocation(),Hit)>Reach||FVector::Distance(Actor->GetActorLocation(),Target)>Reach)return false;
    const int32 Index=Cable->FindNearestCablePoint(Hit,FMath::Max(1.f,CableGrabTraceRadius)+Cable->CableWidth);
    if(Index==INDEX_NONE)return false;
    FVector Eye;FRotator Rotation;Actor->GetActorEyesViewPoint(Eye,Rotation);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CableGrabReach),false,Actor);FHitResult Obstacle;
    if(GetWorld()->LineTraceSingleByChannel(Obstacle,Eye,Hit,ECC_Visibility,Params)&&Obstacle.GetActor()!=Cable->GetOwner()&&
        Obstacle.Distance+2<FVector::Distance(Eye,Hit))return false;
    if(ServerCable.IsValid())ServerCable->EndPointGrab(Actor);
    if(!Cable->BeginPointGrab(Actor,Index,Target,bUseCableDefaultGrabSettings?Cable->DefaultGrabSettings:CableGrabSettings))return false;
    ServerCable=Cable;LastCableTargetTime=-1;return true;
}

void UShopPlayerInteractionComponent::Server_CableGrab_Implementation(UCollisionCableComponent* Cable,FVector_NetQuantize10 Hit,FVector_NetQuantize10 Target,int64 Sequence)
{Client_CableGrabResult(Cable,ExecuteCableGrab(Cable,Hit,Target,Sequence));}

void UShopPlayerInteractionComponent::Client_CableGrabResult_Implementation(UCollisionCableComponent* Cable,bool bAccepted)
{if(!bAccepted&&LocalCable.Get()==Cable){LocalCable.Reset();SetComponentTickEnabled(false);}}

void UShopPlayerInteractionComponent::Server_CableTarget_Implementation(UCollisionCableComponent* Cable,FVector_NetQuantize10 Target,int64 Sequence)
{
    if(Sequence<=LastCableSequence||Sequence<=0)return;
    LastCableSequence=Sequence;
    if(ServerCable.Get()!=Cable||!IsValid(Cable)||!GetWorld())return;
    const float Now=GetWorld()->GetTimeSeconds();
    if(LastCableTargetTime>=0&&Now-LastCableTargetTime<1.f/30.f)return;
    LastCableTargetTime=Now;
    AActor* Actor=CableInteraction::Participant(this);
    if(!Actor||FVector::Distance(Target,Actor->GetActorLocation())>FMath::Min(MaxInteractionDistance,Cable->MaxGrabDistance)||
        !Cable->UpdatePointGrab(Actor,Target))
    {if(Actor)Cable->EndPointGrab(Actor);ServerCable.Reset();Client_CableGrabResult(Cable,false);}
}

void UShopPlayerInteractionComponent::RequestCableRelease()
{
    if(LocalCableSequence==MAX_int64)return;
    if(GetOwner()&&GetOwner()->HasAuthority()&&GetNetMode()!=NM_Client)Server_CableRelease_Implementation(++LocalCableSequence);
    else Server_CableRelease(++LocalCableSequence);
    LocalCable.Reset();SetComponentTickEnabled(false);
}

void UShopPlayerInteractionComponent::Server_CableRelease_Implementation(int64 Sequence)
{
    if(Sequence<=LastCableSequence||Sequence<=0)return;
    LastCableSequence=Sequence;
    if(ServerCable.IsValid())ServerCable->EndPointGrab(CableInteraction::Participant(this));
    ServerCable.Reset();
}

void UShopPlayerInteractionComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,TickType,TickFunction);
    if(!LocalCable.IsValid()||!LocalCable->bEnableGrabbing){RequestCableRelease();return;}
    APlayerController* PC=CableInteraction::Controller(this);if(!PC||!PC->IsLocalController())return;
    FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
    const FVector Target=Eye+Rotation.RotateVector(LocalCableViewOffset);
    CableTargetAccumulator+=Dt;
    if(CableTargetAccumulator<.05f||LocalCableSequence==MAX_int64)return;
    CableTargetAccumulator=0;
    if(GetOwner()->HasAuthority()&&GetNetMode()!=NM_Client)
    {
        if(!LocalCable->UpdatePointGrab(CableInteraction::Participant(this),Target))RequestCableRelease();
    }
    else Server_CableTarget(LocalCable.Get(),Target,++LocalCableSequence);
}

void UShopPlayerInteractionComponent::CleanupCableGrab()
{
    if(ServerCable.IsValid())ServerCable->EndPointGrab(CableInteraction::Participant(this));
    ServerCable.Reset();LocalCable.Reset();SetComponentTickEnabled(false);
}
