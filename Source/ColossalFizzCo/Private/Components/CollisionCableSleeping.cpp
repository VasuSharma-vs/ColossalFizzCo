#include "Components/CollisionCableComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"

void UCollisionCableComponent::WakeCable()
{
    const bool WasSleeping=bCableSleeping;
    bCableSleeping=false; SleepingPointCount=0;
    for (auto& Point:Particles) { Point.bSleeping=false; Point.QuietTime=0; }
    if (IsRegistered()) Super::SetComponentTickEnabled(true);
    if (WasSleeping) PublishNetworkSnapshot(0,true);
}

void UCollisionCableComponent::WakePoint(int32 Index, bool bNeighbors)
{
    if (!Particles.IsValidIndex(Index)) return;
    const bool WasSleeping=bCableSleeping;
    bCableSleeping=false;
    for(int32 I=FMath::Max(0,Index-(bNeighbors?2:0));I<=FMath::Min(Particles.Num()-1,Index+(bNeighbors?2:0));++I)
    { Particles[I].bSleeping=false; Particles[I].QuietTime=0; }
    if(IsRegistered()) Super::SetComponentTickEnabled(true);
    if(WasSleeping) PublishNetworkSnapshot(0,true);
}

bool UCollisionCableComponent::IsPointSleeping(int32 Index) const
{
    return Particles.IsValidIndex(Index) && Particles[Index].bSleeping;
}

void UCollisionCableComponent::UpdateSleeping(float Dt,const TArray<FVector>& Before)
{
    SleepingPointCount=0;
    if(!bEnableSleeping)
    {
        bCableSleeping=false;
        for(auto& P:Particles){P.bSleeping=false;P.QuietTime=0;}
        return;
    }
    const double Rest=FMath::Max(1.f,CableLength)/FMath::Max(1,Particles.Num()-1);
    auto CaptureSleepSpans=[&](int32 Index)
    {
        auto& Point=Particles[Index];
        Point.SleepPreviousSpanLength=Index>0 ? FVector::Distance(Point.Position,Particles[Index-1].Position) : -1.;
        Point.SleepNextSpanLength=Index+1<Particles.Num() ? FVector::Distance(Point.Position,Particles[Index+1].Position) : -1.;
    };
    bool All=true;
    for(int32 I=0;I<Particles.Num();++I)
    {
        auto& P=Particles[I];
        if(IsPinned(I))
        {
            const bool WasSleeping=P.bSleeping;
            const bool Moved=Before.IsValidIndex(I)&&!P.Position.Equals(Before[I],FMath::Max(.01f,WakeDisplacementThreshold));
            P.QuietTime=Moved?0:P.QuietTime+Dt;P.bSleeping=P.QuietTime>=FMath::Max(.05f,SleepDelay);
            if(P.bSleeping && !WasSleeping) CaptureSleepSpans(I);
            if(P.bSleeping)++SleepingPointCount;else All=false;
            continue;
        }
        double Error=0;
        if(I>0) Error=FMath::Max(Error,FMath::Abs(FVector::Distance(P.Position,Particles[I-1].Position)-Rest));
        if(I+1<Particles.Num()) Error=FMath::Max(Error,FMath::Abs(FVector::Distance(P.Position,Particles[I+1].Position)-Rest));
        const bool Moved=Before.IsValidIndex(I) && FVector::Distance(P.Position,Before[I])>FMath::Max(.01f,WakeDisplacementThreshold);
        bool Recovering=false;
        if(PointLineContacts.IsValidIndex(I))
        {
            const auto& Contact=PointLineContacts[I];
            Recovering=!Contact.Normal.IsNearlyZero() && Contact.Component.IsValid() &&
                FVector::DotProduct(P.Position-Contact.Point,Contact.Normal)<CollisionRadius()-.005;
        }
        const bool Quiet=P.Velocity.Size()<=FMath::Max(0.f,SleepVelocityThreshold) && !Moved &&
            Error<=FMath::Max(.01f,SleepConstraintTolerance) && !Recovering && !bSelfContactUnresolved;
        if(Quiet) P.QuietTime+=Dt; else {P.QuietTime=0;P.bSleeping=false;}
        if(P.QuietTime>=FMath::Max(.05f,SleepDelay))
        {
            if(!P.bSleeping) CaptureSleepSpans(I);
            P.bSleeping=true;P.Velocity=FVector::ZeroVector;
        }
        if(P.bSleeping) ++SleepingPointCount; else All=false;
    }
    const bool HeldAttachment=(bAttachStart&&Cast<APawn>(ResolveMovementDriver(this))) ||
        (bAttachEnd&&GetAttachedComponent()&&Cast<APawn>(ResolveMovementDriver(GetAttachedComponent())));
    bool ConstraintsClear=true;
    if(All)for(int32 I=1;I<Particles.Num();++I)
        if(FMath::Abs(FVector::Distance(Particles[I-1].Position,Particles[I].Position)-Rest)>FMath::Max(.01f,SleepConstraintTolerance))
        {ConstraintsClear=false;break;}
    // Pinned points can be quiet while their authored separation is impossible.
    // They must not bypass the complete-cable unresolved-constraint check.
    if(All && ConstraintsClear && ActiveGrabs.IsEmpty() && !HeldAttachment && !bHasUnresolvedCollision && !bSelfContactUnresolved)
    {
        bCableSleeping=true;
        CollisionQueryCount=0; LastCollisionPointIndices.Reset(); MaxRecoveryDistanceThisFrame=0;
        RefreshWakeSources();
        // Deliberately bypass the legacy BP tick-shutdown guard. Wake sources
        // are serviced by an independent world timer and overlap events.
        Super::SetComponentTickEnabled(false);
        PublishNetworkSnapshot(0,true);
    }
}

void UCollisionCableComponent::RefreshWakeSources()
{
    if(!GetWorld() || !GetWorld()->IsGameWorld() || !GetOwner() || !IsRegistered()) return;
    TSet<USceneComponent*> Sources;
    Sources.Add(this);
    if(auto* End=GetAttachedComponent()) Sources.Add(End);
    for(const auto& C:PointLineContacts) if(C.Component.IsValid() && !C.Normal.IsNearlyZero()) Sources.Add(C.Component.Get());
    for(const auto& C:SegmentLineContacts) if(C.Component.IsValid() && !C.Normal.IsNearlyZero()) Sources.Add(C.Component.Get());
    for(const auto& Body:WakeNearbyBodies)if(Body.IsValid())Sources.Add(Body.Get());
    const bool NeedsSensor=bEnableSleeping && bEnableCollision && HasSimulationAuthority();
    if(NeedsSensor && !WakeSensor)
    {
        TGuardValue<bool> Guard(bInstallingWakeSensor,true);
        WakeSensor=NewObject<UBoxComponent>(GetOwner(),NAME_None,RF_Transient);
        WakeSensor->ComponentTags.Add(TEXT("CableWakeSensor"));
        GetOwner()->AddInstanceComponent(WakeSensor);
        WakeSensor->SetHiddenInGame(true); WakeSensor->SetVisibility(false);
        WakeSensor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        WakeSensor->SetCollisionObjectType(ECC_WorldDynamic);
        WakeSensor->SetCollisionResponseToAllChannels(ECR_Ignore);
        for(ECollisionChannel Channel:{ECC_WorldStatic,ECC_WorldDynamic,ECC_PhysicsBody}) WakeSensor->SetCollisionResponseToChannel(Channel,ECR_Overlap);
        WakeSensor->SetGenerateOverlapEvents(true);
        WakeSensor->OnComponentBeginOverlap.AddDynamic(this,&UCollisionCableComponent::OnWakeSensorOverlap);
        WakeSensor->RegisterComponent();
    }
    if(!NeedsSensor)
    {
        if(WakeSensor){WakeSensor->DestroyComponent();WakeSensor=nullptr;}
        WakeNearbyBodies.Reset();
    }
    if(WakeSensor)
    {
        FBox Box(ForceInit);for(const auto& P:Particles)Box+=P.Position;
        if(bUseFullCollisionQueries)for(const auto& Point:PublishedPoints)Box+=Point;
        if(Box.IsValid)
        {
            Box=Box.ExpandBy(FMath::Max(CableWidth,WakeDisplacementThreshold*2));
            TGuardValue<bool> Guard(bInstallingWakeSensor,true);
            if(!WakeSensor->GetComponentLocation().Equals(Box.GetCenter(),.05)) WakeSensor->SetWorldLocation(Box.GetCenter());
            if(!WakeSensor->GetUnscaledBoxExtent().Equals(Box.GetExtent(),.05)) WakeSensor->SetBoxExtent(Box.GetExtent());
        }
        TArray<UPrimitiveComponent*> Overlaps;WakeSensor->GetOverlappingComponents(Overlaps);
        for(auto* Other:Overlaps)if(IsValid(Other)&&Other->GetOwner()!=GetOwner()&&Other!=WakeSensor&&!Other->ComponentHasTag(TEXT("CableWakeSensor")))Sources.Add(Other);
    }
    for(auto It=ObservedTransforms.CreateIterator();It;++It)
    {
        if(!It.Key().IsValid() || !Sources.Contains(It.Key().Get()))
        {
            if(It.Key().IsValid())It.Key()->TransformUpdated.RemoveAll(this);
            It.RemoveCurrent();
        }
    }
    for(auto* Source:Sources)if(!ObservedTransforms.Contains(Source))
    {
        ObservedTransforms.Add(Source,Source->GetComponentTransform());
        Source->TransformUpdated.AddUObject(this,&UCollisionCableComponent::OnObservedTransform);
    }
}

void UCollisionCableComponent::OnObservedTransform(USceneComponent* Component,EUpdateTransformFlags Flags,ETeleportType Teleport)
{
    if(bInstallingWakeSensor || !Component || !HasSimulationAuthority()) return;
    const FTransform* Old=ObservedTransforms.Find(Component);if(!Old)return;
    const FTransform Current=Component->GetComponentTransform();
    if(Current.GetLocation().Equals(Old->GetLocation(),FMath::Max(.01f,WakeDisplacementThreshold)) &&
        Current.GetRotation().AngularDistance(Old->GetRotation())<.001 && Current.GetScale3D().Equals(Old->GetScale3D(),.001)) return;
    const FTransform Previous=*Old;
    ObservedTransforms[Component]=Current;
    bool Support=false;
    auto MoveSurface=[&](FLineContact& Contact)
    {
        // Preserve the actual supporting plane as its body moves. Keeping its
        // old world position allowed refreshed point/span contacts to reuse a
        // stale plane and incorrectly report a moved-floor overlap as clear.
        Contact.Point=Current.TransformPosition(Previous.InverseTransformPosition(Contact.Point));
        if(!Current.GetScale3D().Equals(Previous.GetScale3D(),.001))Contact.Normal=FVector::ZeroVector;
        else Contact.Normal=Current.TransformVectorNoScale(Previous.InverseTransformVectorNoScale(Contact.Normal)).GetSafeNormal();
    };
    for(int32 I=0;I<PointLineContacts.Num();++I)if(PointLineContacts[I].Component.Get()==Component)
    {MoveSurface(PointLineContacts[I]);WakePoint(I);Support=true;}
    for(int32 I=0;I<SegmentLineContacts.Num();++I)if(SegmentLineContacts[I].Component.Get()==Component)
    {MoveSurface(SegmentLineContacts[I]);WakePoint(I);WakePoint(I+1);Support=true;}
    if(Component==this || Component==GetAttachedComponent()) {WakeCable();return;}
    if(!Support)
    {
        const auto* Body=Cast<UPrimitiveComponent>(Component);
        const double Radius=Body?Body->Bounds.SphereRadius:0;
        const FVector Center=Body?Body->Bounds.Origin:Component->GetComponentLocation();
        for(int32 I=0;I+1<Particles.Num();++I)
            if(FVector::Distance(FMath::ClosestPointOnSegment(Center,Particles[I].Position,Particles[I+1].Position),Center)<=Radius+CableWidth+WakeDisplacementThreshold)
            {WakePoint(I);WakePoint(I+1);}
        if(bUseFullCollisionQueries)for(int32 I=0;I+1<PublishedPoints.Num();++I)
            if(FVector::Distance(FMath::ClosestPointOnSegment(Center,PublishedPoints[I],PublishedPoints[I+1]),Center)<=Radius+CableWidth+WakeDisplacementThreshold)
            {const int32 Point=FMath::Min(I/FMath::Max(1,bEnableVisualSubdivision?VisualSubdivisions:1),Particles.Num()-1);WakePoint(Point);WakePoint(Point+1);}
    }
}

void UCollisionCableComponent::OnWakeSensorOverlap(UPrimitiveComponent* Overlapped,AActor* OtherActor,UPrimitiveComponent* Other,int32 OtherBodyIndex,bool bFromSweep,const FHitResult& Hit)
{
    if(bInstallingWakeSensor || !bEnableSleeping || !HasSimulationAuthority() || !IsValid(Other) || OtherActor==GetOwner() || Other->ComponentHasTag(TEXT("CableWakeSensor")))return;
    if(bIgnorePlayerCollision)
        if(const auto* Pawn=Cast<APawn>(OtherActor);Pawn && (Pawn->IsPlayerControlled()||Other->GetCollisionObjectType()==ECC_Pawn))return;
    const bool Known=ObservedTransforms.Contains(Other);
    if(!Known)
    {
        ObservedTransforms.Add(Other,Other->GetComponentTransform());
        Other->TransformUpdated.AddUObject(this,&UCollisionCableComponent::OnObservedTransform);
    }
    // Persistent floor/support overlap is not an impact and must not restart
    // the sleep delay. A newly arriving nearby body is a meaningful wake source.
    if(Known && Other->GetComponentVelocity().Size()<FMath::Max(WakeThreshold,SleepVelocityThreshold*1.5f))return;
    const FVector Center=Other->Bounds.Origin;
    for(int32 I=0;I+1<Particles.Num();++I)
        if(FVector::Distance(FMath::ClosestPointOnSegment(Center,Particles[I].Position,Particles[I+1].Position),Center)<=Other->Bounds.SphereRadius+CableWidth)
        {WakePoint(I);WakePoint(I+1);}
    if(bUseFullCollisionQueries)for(int32 I=0;I+1<PublishedPoints.Num();++I)
        if(FVector::Distance(FMath::ClosestPointOnSegment(Center,PublishedPoints[I],PublishedPoints[I+1]),Center)<=Other->Bounds.SphereRadius+CableWidth)
        {const int32 Point=FMath::Min(I/FMath::Max(1,bEnableVisualSubdivision?VisualSubdivisions:1),Particles.Num()-1);WakePoint(Point);WakePoint(Point+1);}
}

void UCollisionCableComponent::CheckForWakeSources()
{
    if(!IsRegistered() || !GetWorld())return;
    const uint32 Signature=FeatureSignature();
    if(Signature!=LastFeatureSignature){LastFeatureSignature=Signature;OnRep_CableFeatures();}
    if(!HasSimulationAuthority())return;
    CheckSleepingBodyProximity();
    FVector Start,End;GetEndPositions(Start,End);
    if(!Start.Equals(LastWakeStart,FMath::Max(.01f,WakeDisplacementThreshold)) || !End.Equals(LastWakeEnd,FMath::Max(.01f,WakeDisplacementThreshold)))
    {LastWakeStart=Start;LastWakeEnd=End;WakeCable();RefreshMovementBindings();}
    TArray<TWeakObjectPtr<USceneComponent>> Sources;ObservedTransforms.GetKeys(Sources);
    for(auto& Weak:Sources)
    {
        if(auto* Source=Weak.Get())OnObservedTransform(Source,EUpdateTransformFlags::None,ETeleportType::None);
        else
        {
            for(int32 I=0;I<PointLineContacts.Num();++I)if(!PointLineContacts[I].Component.IsValid()&&!PointLineContacts[I].Normal.IsNearlyZero())
            {PointLineContacts[I]=FLineContact();WakePoint(I);}
            for(int32 I=0;I<SegmentLineContacts.Num();++I)if(!SegmentLineContacts[I].Component.IsValid()&&!SegmentLineContacts[I].Normal.IsNearlyZero())
            {SegmentLineContacts[I]=FLineContact();WakePoint(I);WakePoint(I+1);}
            ObservedTransforms.Remove(Weak);
        }
    }
    RefreshWakeSources(); RefreshMovementBindings();
}

void UCollisionCableComponent::CheckSleepingBodyProximity()
{
    if(!bEnableSleeping || !HasCollision() || !GetWorld() || (!bCableSleeping && SleepingPointCount==0))return;
    FBox Box(ForceInit);for(const auto& Point:Particles)Box+=Point.Position;
    if(bUseFullCollisionQueries)for(const auto& Point:PublishedPoints)Box+=Point;
    if(!Box.IsValid)return;
    Box=Box.ExpandBy(FMath::Max(CableWidth,WakeDisplacementThreshold*2));
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CableWakeMonitor),false,GetOwner());
    TArray<FOverlapResult> Overlaps;
    ++WakeQueryCount;
    // Engine overlap delegates require both objects to enable overlap events.
    // A single low-rate broad-phase query also finds ordinary blocking meshes
    // that have those events disabled; it does not retrace resting rope points.
    GetWorld()->OverlapMultiByObjectType(Overlaps,Box.GetCenter(),FQuat::Identity,
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
        FCollisionShape::MakeBox(Box.GetExtent().ComponentMax(FVector(.1))),Params);
    WakeNearbyBodies.Reset();
    for(const auto& Overlap:Overlaps)
    {
        auto* Body=Overlap.GetComponent();
        if(!IsValid(Body)||Body->GetOwner()==GetOwner()||Body->ComponentHasTag(TEXT("CableWakeSensor")) ||
            Body->GetCollisionResponseToChannel(GetCollisionObjectType())!=ECR_Block ||
            GetCollisionResponseToChannel(Body->GetCollisionObjectType())!=ECR_Block)continue;
        if(bIgnorePlayerCollision)
            if(const auto* Pawn=Cast<APawn>(Body->GetOwner());Pawn&&(Pawn->IsPlayerControlled()||Body->GetCollisionObjectType()==ECC_Pawn))continue;
        WakeNearbyBodies.AddUnique(Body);
        if(!ObservedTransforms.Contains(Body))
            OnWakeSensorOverlap(WakeSensor,Body->GetOwner(),Body,0,false,FHitResult());
    }
}
