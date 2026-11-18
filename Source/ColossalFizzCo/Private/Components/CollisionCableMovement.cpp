#include "Components/CollisionCableComponent.h"
#include "Components/CableMovementConstraintComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractableProp.h"

AActor* UCollisionCableComponent::ResolveMovementDriver(USceneComponent* Component) const
{
    if(!IsValid(Component))return nullptr;
    // A nozzle component can still be owned by its original cable actor while
    // attached directly to the player's hand. Follow the component hierarchy.
    AActor* Actor=Component->GetOwner();
    TSet<const USceneComponent*> SeenComponents;
    for(USceneComponent* Current=Component;Current && SeenComponents.Num()<64 && !SeenComponents.Contains(Current);Current=Current->GetAttachParent())
    {
        SeenComponents.Add(Current);
        if(auto* Pawn=Cast<APawn>(Current->GetOwner()))return Pawn;
        // A separate hose actor can mount its cable on a tank's scene endpoint.
        // The attachment hierarchy, rather than the hose's UObject ownership, drives that end.
        if(Current->GetOwner())Actor=Current->GetOwner();
    }
    AActor* Fallback=Actor;
    TSet<const AActor*> Visited;
    for(int32 Depth=0;Actor && Depth<16 && !Visited.Contains(Actor);++Depth)
    {
        Visited.Add(Actor);
        if(auto* PC=Cast<APlayerController>(Actor))return PC->GetPawn()?PC->GetPawn():Fallback;
        if(Cast<APawn>(Actor) || Actor->FindComponentByClass<UMovementComponent>())return Actor;
        if(auto* Prop=Cast<AActivePropActor>(Actor);Prop && Prop->bIsHeld && Prop->InteractionOwner)
        {Actor=Prop->InteractionOwner;continue;}
        if(Actor->FindComponentByClass<UCablePullResponseComponent>())return Actor;
        if(auto* Body=Cast<UPrimitiveComponent>(Actor->GetRootComponent());Body && Body->IsSimulatingPhysics())return Actor;
        if(Cast<AActivePropActor>(Actor))return Actor;
        if(AActor* Parent=Actor->GetAttachParentActor()){Actor=Parent;continue;}
        if(AActor* Owner=Actor->GetOwner()){Actor=Owner;continue;}
        if(APawn* Instigator=Actor->GetInstigator())return Instigator;
        return Actor;
    }
    return Fallback;
}

bool UCollisionCableComponent::IsWorldAnchor(int32 Index) const
{
    if(FixedPointIndices.Contains(Index))return true;
    USceneComponent* Source=nullptr;bool Fixed=false;
    if(Index==0 && bAttachStart){Source=const_cast<UCollisionCableComponent*>(this);Fixed=bStartAttachmentIsWorldAnchor;}
    if(Index==Particles.Num()-1 && bAttachEnd)
    {
        Source=GetAttachedComponent();
        Fixed=bEndAttachmentIsWorldAnchor || (!Source && bAttachStart && bStartAttachmentIsWorldAnchor);
        if(!Source)Source=const_cast<UCollisionCableComponent*>(this);
    }
    if(!Source)return false;
    // An endpoint on another cable transmits its constraints through that cable;
    // the other cable actor's nonphysics root is not a new fixed world anchor.
    TSet<const USceneComponent*> Seen;
    for(const USceneComponent* Current=Source;Current && Seen.Num()<64 && !Seen.Contains(Current);Current=Current->GetAttachParent())
    {
        Seen.Add(Current);
        if(const auto* Cable=Cast<UCollisionCableComponent>(Current);Cable && Cable!=this)return false;
    }
    AActor* Driver=ResolveMovementDriver(Source);
    if(Driver)if(const auto* Response=Driver->FindComponentByClass<UCablePullResponseComponent>())return Response->bFixed;
    if(!Fixed)return false;
    if(!Driver || Cast<APawn>(Driver) || Driver->FindComponentByClass<UMovementComponent>())return false;
    if(const auto* Body=Cast<UPrimitiveComponent>(Source);Body && Body->IsSimulatingPhysics())return false;
    if(const auto* Body=Cast<UPrimitiveComponent>(Driver->GetRootComponent());Body && Body->IsSimulatingPhysics())return false;
    return true;
}

void UCollisionCableComponent::RefreshMovementBindings()
{
    UCableMovementConstraintComponent::RegisterWorldCable(this);
    TSet<AActor*> Drivers;
    if(bEnableMovementRestrictions && GetWorld() && GetWorld()->IsGameWorld())
    {
        if(bAttachStart && !IsWorldAnchor(0))if(auto* Driver=ResolveMovementDriver(this))Drivers.Add(Driver);
        if(bAttachEnd && !IsWorldAnchor(Particles.Num()-1))
            if(auto* Driver=ResolveMovementDriver(GetAttachedComponent()?GetAttachedComponent():this))Drivers.Add(Driver);
        for(const auto& Grab:GetActiveGrabs())if(Grab.Participant)
        {
            AActor* Driver=Grab.TargetComponent?ResolveMovementDriver(Grab.TargetComponent):Grab.Participant.Get();
            if(auto* PC=Cast<APlayerController>(Driver))Driver=PC->GetPawn();
            if(Driver)Drivers.Add(Driver);
        }
    }
    for(auto It=MovementBindings.CreateIterator();It;++It)
    {
        if(!It->IsValid() || !Drivers.Contains(It->Get()->GetOwner()))
        {if(It->IsValid())It->Get()->UnregisterCable(this);It.RemoveCurrent();}
    }
    for(AActor* Driver:Drivers)
    {
        bool Bound=false;for(const auto& Weak:MovementBindings)if(Weak.IsValid()&&Weak->GetOwner()==Driver)Bound=true;
        if(Bound)continue;
        auto* Adapter=Driver->FindComponentByClass<UCableMovementConstraintComponent>();
        if(!Adapter)
        {
            Adapter=NewObject<UCableMovementConstraintComponent>(Driver,NAME_None,RF_Transient);
            Driver->AddInstanceComponent(Adapter);Adapter->RegisterComponent();
        }
        Adapter->RegisterCable(this);MovementBindings.Add(Adapter);
    }
}

void UCollisionCableComponent::GetMovementAttachments(TArray<FCableMovementAttachment>& Attachments) const
{
    Attachments.Reset();
    if(Particles.Num()<2)return;
    auto Add=[&](int32 Index,USceneComponent* Source,FName Socket,AActor* Driver,bool Fixed,FVector Position)
    {
        FCableMovementAttachment Entry;Entry.PointIndex=Index;Entry.Source=Source;Entry.SocketName=Socket;
        Entry.Driver=Driver;Entry.bFixed=Fixed;Entry.Position=Position;Attachments.Add(Entry);
    };
    FVector Start,End;GetEndPositions(Start,End);
    Add(0,bAttachStart?const_cast<UCollisionCableComponent*>(this):nullptr,NAME_None,
        bAttachStart?ResolveMovementDriver(const_cast<UCollisionCableComponent*>(this)):nullptr,IsWorldAnchor(0),bAttachStart?Start:Particles[0].Position);
    USceneComponent* EndSource=GetAttachedComponent();if(!EndSource)EndSource=const_cast<UCollisionCableComponent*>(this);
    Add(Particles.Num()-1,bAttachEnd?EndSource:nullptr,AttachEndToSocketName,
        bAttachEnd?ResolveMovementDriver(EndSource):nullptr,IsWorldAnchor(Particles.Num()-1),bAttachEnd?End:Particles.Last().Position);
    for(int32 Index:FixedPointIndices)if(Index>0 && Index<Particles.Num()-1)
        Add(Index,nullptr,NAME_None,nullptr,true,Particles[Index].Position);
    if(bEnableGrabbing)for(const auto& Grab:GetActiveGrabs())if(Particles.IsValidIndex(Grab.PointIndex))
    {
        AActor* Driver=Grab.TargetComponent?ResolveMovementDriver(Grab.TargetComponent):Grab.Participant.Get();
        if(auto* PC=Cast<APlayerController>(Driver))Driver=PC->GetPawn();
        FVector Position=Grab.Target;
        if(Grab.TargetComponent)
        {
            const FTransform Target=Grab.SocketName.IsNone()?Grab.TargetComponent->GetComponentTransform():Grab.TargetComponent->GetSocketTransform(Grab.SocketName);
            Position=Target.TransformPosition(Grab.TargetOffset);
        }
        Add(Grab.PointIndex,Grab.TargetComponent,Grab.SocketName,Driver,FixedPointIndices.Contains(Grab.PointIndex),Position);
    }
}

float UCollisionCableComponent::GetCableMassKg() const
{
    if(!bEnablePointWeight)return 0;
    double Mass=0;for(int32 I=0;I<Particles.Num();++I)Mass+=GetPointMass(I);
    return float(Mass);
}

void UCollisionCableComponent::NotifyConnectedPullApplied()
{
    LastConnectedPullFrame=GFrameCounter;WakeCable();
}

FVector UCollisionCableComponent::ConstrainDriverLocation(AActor* Driver,const FVector& OldLocation,const FVector& ProposedLocation) const
{
    if(!bEnableMovementRestrictions || !IsValid(Driver) || Particles.Num()<2 || OldLocation.ContainsNaN() || ProposedLocation.ContainsNaN())return ProposedLocation;
    struct FTarget{int32 Index;FVector Goal;bool Grab;};TArray<FTarget> Targets;
    if(bAttachStart && !IsWorldAnchor(0) && ResolveMovementDriver(const_cast<UCollisionCableComponent*>(this))==Driver)
        Targets.Add({0,GetComponentLocation(),false});
    if(bAttachEnd && !IsWorldAnchor(Particles.Num()-1))
    {
        USceneComponent* Source=GetAttachedComponent();if(!Source)Source=const_cast<UCollisionCableComponent*>(this);
        if(ResolveMovementDriver(Source)==Driver)
        {
            FVector Start,End;GetEndPositions(Start,End);Targets.Add({Particles.Num()-1,End,false});
        }
    }
    if(bEnableGrabbing)for(const auto& Grab:GetActiveGrabs())
    {
        AActor* Participant=Grab.Participant;
        if(auto* PC=Cast<APlayerController>(Participant))Participant=PC->GetPawn();
        AActor* TargetDriver=Grab.TargetComponent?ResolveMovementDriver(Grab.TargetComponent):Participant;
        if(TargetDriver!=Driver || !Particles.IsValidIndex(Grab.PointIndex))continue;
        FVector Goal=ProposedLocation+Grab.ParticipantOffset;
        if(Grab.TargetComponent)
        {
            const FTransform Transform=Grab.SocketName.IsNone()?Grab.TargetComponent->GetComponentTransform():Grab.TargetComponent->GetSocketTransform(Grab.SocketName);
            Goal=Transform.TransformPosition(Grab.TargetOffset);
        }
        Targets.Add({Grab.PointIndex,Goal,true});
    }
    FVector Allowed=ProposedLocation;
    const double Rest=FMath::Max(1.f,CableLength)/(Particles.Num()-1);
    auto LimitGoal=[&](const FVector& OldGoal,const FVector& Pivot,double Radius)
    {
        // If authored state is already stretched, prevent worsening it while
        // permitting inward recovery. Never teleport the holder to a new shell.
        const double AllowedRadius=FMath::Max(FMath::Max(0.,Radius),FVector::Distance(OldGoal,Pivot));
        const FVector Goal=OldGoal+(Allowed-OldLocation);
        if(FVector::DistSquared(Goal,Pivot)>FMath::Square(AllowedRadius))
            Allowed+=Pivot+(Goal-Pivot).GetSafeNormal()*AllowedRadius-Goal;
    };
    for(int32 Pass=0;Pass<3;++Pass)for(const FTarget& Target:Targets)
    {
        const FVector OldGoal=Target.Goal-(ProposedLocation-OldLocation);
        for(int32 Anchor=0;Anchor<Particles.Num();++Anchor)
        {
            if(Anchor==Target.Index || !IsWorldAnchor(Anchor))continue;
            const int32 Step=Target.Index>Anchor?1:-1;
            FVector Pivot=Particles[Anchor].Position;double Used=0;
            // Free slack points can unfold. Retain only contact bends whose
            // surface separates this anchor from the requested target; using
            // every sagging point as a fixed pivot would restrict slack early.
            for(int32 I=Anchor+Step;I!=Target.Index;I+=Step)
            {
                bool Bend=false;
                auto NeedsBend=[&](const FLineContact& C)
                {
                    if(!C.Component.IsValid()||C.Normal.IsNearlyZero())return false;
                    return FVector::DotProduct(Particles[Anchor].Position-C.Point,C.Normal)*
                        FVector::DotProduct(Target.Goal-C.Point,C.Normal)<0;
                };
                if(PointLineContacts.IsValidIndex(I))Bend=NeedsBend(PointLineContacts[I]);
                if(SegmentLineContacts.IsValidIndex(I))Bend|=NeedsBend(SegmentLineContacts[I]);
                if(Bend){Used+=FVector::Distance(Pivot,Particles[I].Position);Pivot=Particles[I].Position;}
            }
            const double Available=Rest*FMath::Abs(Target.Index-Anchor)+FMath::Max(0.f,LengthSlackTolerance)-Used;
            LimitGoal(OldGoal,Pivot,Available);
        }
        if(Target.Grab)
        {
            // This radius follows the movable cable point, so two free ends do
            // not acquire an artificial world anchor. Extra pullers move the
            // point faster and therefore increase actual holder progress.
            LimitGoal(OldGoal,Particles[Target.Index].Position,FMath::Max(0.f,GrabMovementAllowance));
        }
    }
    return Allowed;
}

void UCollisionCableComponent::ApplyAttachmentForces(float Dt)
{
    if(!bEnablePointWeight || !GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client
        || !HasSimulationAuthority() || LastConnectedPullFrame==GFrameCounter || Particles.Num()<2 || Dt<=0)return;
    double UnsupportedMass=0;
    for(int32 I=0;I<Particles.Num();++I)
    {
        if(IsPinned(I))continue;
        const bool Supported=PointLineContacts.IsValidIndex(I) && PointLineContacts[I].Component.IsValid() &&
            FVector::DotProduct(PointLineContacts[I].Normal,FVector::UpVector)>.5 &&
            FVector::DotProduct(Particles[I].Position-PointLineContacts[I].Point,PointLineContacts[I].Normal)<=CollisionRadius()+.1;
        if(!Supported)UnsupportedMass+=GetPointMass(I);
    }
    const double Load=UnsupportedMass*(CableForce+FVector(0,0,GetWorld()->GetGravityZ()*CableGravityScale)).Size();
    const double Rest=FMath::Max(1.f,CableLength)/(Particles.Num()-1);
    for(int32 Side=0;Side<2;++Side)
    {
        const int32 I=Side?Particles.Num()-1:0,J=Side?I-1:1;
        if((Side?!bAttachEnd:!bAttachStart)||IsWorldAnchor(I))continue;
        auto* Source=Side?GetAttachedComponent():this;
        auto* Body=Cast<UPrimitiveComponent>(Source);
        if(!Body || !Body->IsSimulatingPhysics())
        {
            AActor* Driver=ResolveMovementDriver(Source);
            Body=Driver?Cast<UPrimitiveComponent>(Driver->GetRootComponent()):nullptr;
        }
        if(!Body || !Body->IsSimulatingPhysics())continue;
        const FVector Delta=Particles[J].Position-Particles[I].Position;
        const double Stretch=FMath::Max(0.,Delta.Size()-Rest);
        const double Force=FMath::Min(double(FMath::Max(0.f,MaxAttachmentForceNewtons))*100.,Load*.5+Stretch*GetPointMass(J)/(Dt*Dt));
        Body->AddForceAtLocation(Delta.GetSafeNormal()*Force,Particles[I].Position);
    }
}
