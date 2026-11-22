#include "Shop/ShopPlayerInteractionComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Resources/HoseCableActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "WorldInteractionCarry.h"

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
    return GrabCableFromView(false);
}

bool UShopPlayerInteractionComponent::TryGrabLooseHoseFromView(bool bAnimateConnector)
{
    return !LocalCable.IsValid() && GrabCableFromView(true, bAnimateConnector);
}

bool UShopPlayerInteractionComponent::TryGrabCableOnHoldFromView()
{
    return !LocalCable.IsValid() && GrabCableFromView(false, true);
}

bool UShopPlayerInteractionComponent::GrabCableFromView(bool bLooseHosesOnly, bool bAnimateConnector)
{
    AActor* Actor=CableInteraction::Participant(this);
    APlayerController* PC=CableInteraction::Controller(this);
    if(!Actor||!PC||!PC->IsLocalController()||!GetWorld())return false;
    FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
    UCollisionCableComponent* BestCable=nullptr;int32 BestIndex=INDEX_NONE;FVector BestHit=FVector::ZeroVector;float BestDistance=FMath::Max(1.f,MaxInteractionDistance);
    int32 ConnectorIndex = INDEX_NONE;
    const auto IsLooseHose = [](const AActor* Candidate)
    {
        const auto* Hose = Cast<AHoseCableActor>(Candidate);
        return Hose && !Hose->StartPort && !Hose->EndPort && Hose->UsesConnectorHandGrip();
    };
    // Connector art is query-only. Clicking an offset/large connector must select
    // its endpoint even when the visible mesh is outside the tube's pick radius.
    FHitResult Surface;FCollisionQueryParams Params(SCENE_QUERY_STAT(CableGrabSelection),true,Actor);
    if(GetWorld()->LineTraceSingleByChannel(Surface,Eye,Eye+Rotation.Vector()*BestDistance,ECC_Visibility,Params))
    {
        BestDistance=Surface.Distance;
        if(auto* Hose=Cast<AHoseCableActor>(Surface.GetActor());Hose && Hose->Cable->bEnableGrabbing
            && (!bLooseHosesOnly || IsLooseHose(Hose)))
        {
            if(Surface.GetComponent()==Hose->StartEndpointMesh)BestIndex=0;
            else if(Surface.GetComponent()==Hose->EndEndpointMesh)BestIndex=Hose->Cable->NumSegments;
            if(BestIndex!=INDEX_NONE)
            {
                TArray<FVector> Points;Hose->Cable->GetCableParticleLocations(Points);
                if (Points.IsValidIndex(BestIndex))
                {
                    BestCable = Hose->Cable;
                    BestHit = Surface.ImpactPoint;
                    ConnectorIndex = BestIndex;
                }
            }
        }
    }
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        if (bLooseHosesOnly && !IsLooseHose(*It)) continue;
        TInlineComponentArray<UCollisionCableComponent*> Cables;It->GetComponents(Cables);
        for(auto* Cable:Cables)
        {
            if(Cable==BestCable || Cable->GetOwner()==Actor || !Cable->IsVisible() || Cable->GetOwner()->IsHidden())continue;
            FHitResult Hit;
            if(Cable->TraceCableInteraction(Eye,Eye+Rotation.Vector()*BestDistance,CableGrabTraceRadius,Hit))
            {BestCable=Cable;BestIndex=Hit.Item;BestHit=Hit.ImpactPoint;BestDistance=Hit.Distance;ConnectorIndex=INDEX_NONE;}
        }
    }
    if(!BestCable)return false;
    TArray<FVector> Points;BestCable->GetCableParticleLocations(Points);
    return Points.IsValidIndex(BestIndex)
        && RequestCableGrabFromHit(BestCable, BestHit, Points[BestIndex], ConnectorIndex, bAnimateConnector);
}

bool UShopPlayerInteractionComponent::RequestCableGrab(UCollisionCableComponent* Cable,FVector Hit,FVector Target)
{
    return RequestCableGrabFromHit(Cable, Hit, Target, INDEX_NONE);
}

bool UShopPlayerInteractionComponent::RequestCableGrabFromHit(UCollisionCableComponent* Cable, FVector Hit, FVector Target,
    int32 ConnectorIndex, bool bAnimateConnector)
{
    if(!IsValid(Cable)||!Cable->bEnableGrabbing||Hit.ContainsNaN()||Target.ContainsNaN()||LocalCableSequence==MAX_int64)return false;
    const int64 Sequence=++LocalCableSequence;
    if(GetOwner()&&GetOwner()->HasAuthority()&&GetNetMode()!=NM_Client)
    {
        if(!ExecuteCableGrab(Cable,Hit,Target,Sequence,ConnectorIndex,bAnimateConnector))return false;
    }
    else Server_CableGrab(Cable,Hit,Target,Sequence,ConnectorIndex,bAnimateConnector);
    LocalCableGrabSequence = Sequence;
    const auto* Hose = Cast<AHoseCableActor>(Cable->GetOwner());
    if (bAnimateConnector && Hose && Hose->UsesConnectorHandGrip() && (ConnectorIndex != INDEX_NONE || Hose->bCarryConnectorOnPickup))
        LocalHosePickupPress = Sequence;
    if(auto* PC=CableInteraction::Controller(this))
    {
        FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
        LocalCableViewOffset=Rotation.UnrotateVector(Target-Eye);
    }
    LocalCable=Cable;CableTargetAccumulator=0;SetComponentTickEnabled(true);return true;
}

bool UShopPlayerInteractionComponent::ExecuteCableGrab(UCollisionCableComponent* Cable, FVector Hit, FVector Target,
    int64 Sequence, int32 ConnectorIndex, bool bAnimateConnector)
{
    if(!GetOwner()||!GetOwner()->HasAuthority()||GetNetMode()==NM_Client||Sequence<=LastCableSequence||Sequence<=0)return false;
    LastCableSequence=Sequence;
    AActor* Actor=CableInteraction::Participant(this);
    if(!Actor||!IsValid(Cable)||Cable->GetWorld()!=GetWorld()||Hit.ContainsNaN()||Target.ContainsNaN())return false;
    const double Reach=FMath::Min(FMath::Max(1.f,MaxInteractionDistance),FMath::Max(1.f,Cable->MaxGrabDistance));
    if(FVector::Distance(Actor->GetActorLocation(),Hit)>Reach||FVector::Distance(Actor->GetActorLocation(),Target)>Reach)return false;
    AHoseCableActor* Hose = Cast<AHoseCableActor>(Cable->GetOwner());
    int32 Index = ConnectorIndex;
    if (ConnectorIndex != INDEX_NONE)
    {
        const UStaticMeshComponent* Connector = Hose && Hose->Cable == Cable ? Hose->GetConnectorMesh(ConnectorIndex) : nullptr;
        const double PickTolerance = FMath::Max(3.f, CableGrabTraceRadius);
        if (!IsValid(Connector) || Connector->Bounds.GetBox().ComputeSquaredDistanceToPoint(Hit) > FMath::Square(PickTolerance)) return false;
    }
    else
    {
        Index = Cable->FindCableGrabPoint(Hit, FMath::Max(1.f, CableGrabTraceRadius) + Cable->CableWidth);
    }
    TArray<FVector> Points;Cable->GetCableParticleLocations(Points);
    if (!Points.IsValidIndex(Index) || FVector::DistSquared(Actor->GetActorLocation(), Points[Index]) > FMath::Square(Reach)) return false;
    FVector Eye;FRotator Rotation;Actor->GetActorEyesViewPoint(Eye,Rotation);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CableGrabReach),false,Actor);FHitResult Obstacle;
    if(GetWorld()->LineTraceSingleByChannel(Obstacle,Eye,Hit,ECC_Visibility,Params)&&Obstacle.GetActor()!=Cable->GetOwner()&&
        Obstacle.Distance+2<FVector::Distance(Eye,Hit))return false;
    // The enlarged aiming tube can reach in front of a wall while its actual
    // simulation point is behind it. Validate that point too on the server.
    if(GetWorld()->LineTraceSingleByChannel(Obstacle,Eye,Points[Index],ECC_Visibility,Params)&&Obstacle.GetActor()!=Cable->GetOwner()&&
        Obstacle.Distance+.1<FVector::Distance(Eye,Points[Index]))return false;

    int32 GripIndex = ConnectorIndex;
    if (Hose && Hose->bCarryConnectorOnPickup && GripIndex==INDEX_NONE)
    {
        // A body hit proves which hose the player aimed at. Find its nearest
        // free connector they can reach without lifting it through a wall.
        double BestDistance = TNumericLimits<double>::Max();
        for (const int32 EndIndex : {0, Points.Num()-1})
        {
            const FVector& End = Points[EndIndex];
            const double Distance = FVector::DistSquared(Hit,End);
            if (Distance >= BestDistance || !Hose->CanGrabConnector(EndIndex)
                || FVector::DistSquared(Actor->GetActorLocation(),End) > FMath::Square(Reach)) continue;
            if (GetWorld()->LineTraceSingleByChannel(Obstacle,Eye,End,ECC_Visibility,Params)
                && Obstacle.GetActor()!=Hose && Obstacle.Distance+.1<FVector::Distance(Eye,End)) continue;
            GripIndex = EndIndex;
            BestDistance = Distance;
        }
        if (GripIndex==INDEX_NONE) return false;
    }

    ReleaseServerCable(Actor);
    if (GripIndex != INDEX_NONE && Hose && Hose->UsesConnectorHandGrip())
    {
        if (!BeginHoseGrip(*Hose, GripIndex, Actor, Sequence, bAnimateConnector)) return false;
    }
    else if (!Cable->BeginPointGrab(Actor, Index, Target, bUseCableDefaultGrabSettings ? Cable->DefaultGrabSettings : CableGrabSettings)) return false;
    ServerCable=Cable;LastCableTargetTime=-1;return true;
}

bool UShopPlayerInteractionComponent::BeginHoseGrip(AHoseCableActor& Hose, int32 Point, AActor* Participant, int64 Sequence, bool bAnimateConnector)
{
    auto* Character = Cast<ACharacter>(Participant);
    auto* Hand = Character ? Character->GetMesh() : nullptr;
    if (!IsValid(Hand)) return false;
    static const FName LeftGrip(TEXT("HandGrip_L"));
    const FName Socket = Hand->DoesSocketExist(LeftGrip) ? LeftGrip : NAME_None;
    if (!bAnimateConnector) return Hose.RequestConnectorHandGrip(Point, Hand, Socket);

    if (!Hose.CanGrabConnector(Point) || !FMath::IsFinite(Hose.HandPickupSpeed) || Hose.HandPickupSpeed <= 0
        || IsValid(WorldInteractionCarry::GetCarryReference(*Character, TEXT("Holding Actor")))
        || IsValid(WorldInteractionCarry::GetCarryReference(*Character, TEXT("Holding Component")))) return false;
    FCableGrabSettings Settings;
    Settings.bApproachingConnector = true;
    Settings.MovementRate = Hose.HandPickupSpeed;
    Settings.InfluenceRadius = 0.f;
    Settings.MaxForceNewtons = Hose.Cable->GetPointMass(Point) * 80.f;
    if (!Hose.Cable->BeginPointGrab(Character, Point, Hand->GetSocketLocation(Socket), Settings, Hand, Socket)) return false;
    ServerHosePickupSequence = Sequence;
    SetComponentTickEnabled(true);
    return true;
}

bool UShopPlayerInteractionComponent::FinishHosePickupPress()
{
    if (!LocalHosePickupPress) return false;
    const int64 Sequence = LocalHosePickupPress;
    LocalHosePickupPress = 0;
    Server_EndHosePickup(Sequence);
    return true;
}

void UShopPlayerInteractionComponent::Server_EndHosePickup_Implementation(int64 GrabSequence)
{
    if (GrabSequence <= 0 || ServerHosePickupSequence != GrabSequence) return;
    auto* Cable = ServerCable.Get();
    ReleaseServerCable(CableInteraction::Participant(this));
    Client_CableGrabResult(Cable, false, GrabSequence);
    if (!LocalCable.IsValid()) SetComponentTickEnabled(false);
}

void UShopPlayerInteractionComponent::UpdateHosePickup()
{
    auto* Cable = ServerCable.Get();
    auto* Hose = Cable ? Cast<AHoseCableActor>(Cable->GetOwner()) : nullptr;
    auto* Player = Cast<ACharacter>(CableInteraction::Participant(this));
    const auto Grabs = Cable ? Cable->GetActiveGrabs() : TArray<FCableGrabState>();
    const auto* Grab = Grabs.FindByPredicate([Player](const FCableGrabState& Entry)
        { return Entry.Participant == Player && Entry.Settings.bApproachingConnector; });
    auto* Hand = IsValid(Player) ? Player->GetMesh() : nullptr;
    if (!IsValid(Hose) || !Cable->bEnableGrabbing || !Grab || !IsValid(Hand) || Grab->TargetComponent != Hand
        || !Player->GetController() || IsValid(WorldInteractionCarry::GetCarryReference(*Player, TEXT("Holding Actor")))
        || IsValid(WorldInteractionCarry::GetCarryReference(*Player, TEXT("Holding Component"))))
    {
        Server_EndHosePickup_Implementation(ServerHosePickupSequence);
        return;
    }
    TArray<FVector> Points;
    Cable->GetCableParticleLocations(Points);
    const int32 Point = Grab->PointIndex;
    const bool bPinned = Point == 0 ? Cable->bAttachStart : Cable->bAttachEnd;
    if (!Points.IsValidIndex(Point) || !Hose->GetConnectorMesh(Point) || bPinned || Cable->FixedPointIndices.Contains(Point)
        || FVector::DistSquared(Points[Point], Player->GetActorLocation()) > FMath::Square(Cable->MaxGrabDistance * 1.5f))
    {
        Server_EndHosePickup_Implementation(ServerHosePickupSequence);
        return;
    }
    constexpr double PickupDistance = 4.;
    if (!Points[Point].Equals(Hand->GetSocketLocation(Grab->SocketName), PickupDistance)) return;

    Cable->EndPointGrab(Player);
    if (Hose->RequestConnectorHandGrip(Point, Hand, Grab->SocketName)) ServerHosePickupSequence = 0;
    else Server_EndHosePickup_Implementation(ServerHosePickupSequence);
}

void UShopPlayerInteractionComponent::Server_CableGrab_Implementation(UCollisionCableComponent* Cable, FVector_NetQuantize10 Hit,
    FVector_NetQuantize10 Target, int64 Sequence, int32 ConnectorIndex, bool bAnimateConnector)
{
    Client_CableGrabResult(Cable, ExecuteCableGrab(Cable, Hit, Target, Sequence, ConnectorIndex, bAnimateConnector), Sequence);
}

void UShopPlayerInteractionComponent::Client_CableGrabResult_Implementation(UCollisionCableComponent* Cable, bool bAccepted, int64 GrabSequence)
{
    if (bAccepted || LocalCable.Get() != Cable || (GrabSequence && LocalCableGrabSequence != GrabSequence)) return;
    LocalCable.Reset();
    SetComponentTickEnabled(ServerHosePickupSequence != 0);
}

void UShopPlayerInteractionComponent::Server_CableTarget_Implementation(UCollisionCableComponent* Cable,FVector_NetQuantize10 Target,int64 Sequence)
{
    if (Sequence <= LastCableSequence || Sequence <= 0 || ServerCable.Get() != Cable || !IsValid(Cable) || !GetWorld()
        || ServerHosePickupSequence) return;
    AActor* Actor = CableInteraction::Participant(this);
    if (const auto* Hose = Cast<AHoseCableActor>(Cable->GetOwner()); Hose && Hose->FindHandHeldConnector(Actor) != INDEX_NONE) return;
    LastCableSequence=Sequence;
    const float Now=GetWorld()->GetTimeSeconds();
    if(LastCableTargetTime>=0&&Now-LastCableTargetTime<1.f/30.f)return;
    LastCableTargetTime=Now;
    if(!Actor||FVector::Distance(Target,Actor->GetActorLocation())>FMath::Min(MaxInteractionDistance,Cable->MaxGrabDistance)||
        !Cable->UpdatePointGrab(Actor,Target))
    {ReleaseServerCable(Actor);Client_CableGrabResult(Cable,false);}
}

void UShopPlayerInteractionComponent::RequestCableRelease()
{
    SubmitCableRelease(false);
}

void UShopPlayerInteractionComponent::RequestCableDrop()
{
    SubmitCableRelease(true);
}

void UShopPlayerInteractionComponent::SubmitCableRelease(bool bToss)
{
    if(LocalCableSequence==MAX_int64)return;
    if(GetOwner()&&GetOwner()->HasAuthority()&&GetNetMode()!=NM_Client)Server_CableRelease_Implementation(++LocalCableSequence,bToss);
    else Server_CableRelease(++LocalCableSequence,bToss);
    LocalCable.Reset();SetComponentTickEnabled(false);
}

void UShopPlayerInteractionComponent::Server_CableRelease_Implementation(int64 Sequence,bool bToss)
{
    if(Sequence<=LastCableSequence||Sequence<=0)return;
    LastCableSequence=Sequence;
    ReleaseServerCable(CableInteraction::Participant(this),bToss);
}

void UShopPlayerInteractionComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,TickType,TickFunction);
    if (ServerHosePickupSequence)
    {
        UpdateHosePickup();
        if (ServerHosePickupSequence) return;
        if (!LocalCable.IsValid())
        {
            SetComponentTickEnabled(false);
            return;
        }
    }
    if(!LocalCable.IsValid()||!LocalCable->bEnableGrabbing){RequestCableRelease();return;}
    if (LocalHosePickupPress) return;
    APlayerController* PC=CableInteraction::Controller(this);if(!PC||!PC->IsLocalController())return;
    AActor* Actor = CableInteraction::Participant(this);
    if (const auto* Hose = Cast<AHoseCableActor>(LocalCable->GetOwner()); Hose && Hose->FindHandHeldConnector(Actor) != INDEX_NONE) return;
    FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
    const FVector Target=Eye+Rotation.RotateVector(LocalCableViewOffset);
    CableTargetAccumulator+=Dt;
    if(CableTargetAccumulator<.05f||LocalCableSequence==MAX_int64)return;
    CableTargetAccumulator=0;
    if(GetOwner()->HasAuthority()&&GetNetMode()!=NM_Client)
    {
        if(!LocalCable->UpdatePointGrab(Actor,Target))RequestCableRelease();
    }
    else Server_CableTarget(LocalCable.Get(),Target,++LocalCableSequence);
}

void UShopPlayerInteractionComponent::CleanupCableGrab()
{
    ReleaseServerCable(CableInteraction::Participant(this));
    LocalHosePickupPress = 0;
    LocalCable.Reset();SetComponentTickEnabled(false);
}

void UShopPlayerInteractionComponent::ReleaseServerCable(AActor* Participant,bool bToss)
{
    ServerHosePickupSequence = 0;
    if (UCollisionCableComponent* Cable = ServerCable.Get())
    {
        auto* Hose = Cast<AHoseCableActor>(Cable->GetOwner());
        const auto* Pawn = Cast<APawn>(Participant);
        const int32 TossPoint = bToss && Pawn && Hose && Hose->Transfer->bBurst && Hose->IsLooseHoseHeldBy(Pawn)
            ? Hose->FindHandHeldConnector(Pawn) : INDEX_NONE;
        if (!Hose || !Hose->ReleaseHandHeldConnector(Participant)) Cable->EndPointGrab(Participant);
        else if (TossPoint != INDEX_NONE)
        {
            constexpr float TossSpeed = 600.f;
            const FVector LaunchVelocity = Pawn->GetBaseAimRotation().Vector()*TossSpeed + Pawn->GetVelocity().GetClampedToMaxSize(TossSpeed);
            Cable->AddImpulseToPoint(TossPoint,LaunchVelocity*Cable->GetPointMass(TossPoint));
        }
    }
    ServerCable.Reset();
}
