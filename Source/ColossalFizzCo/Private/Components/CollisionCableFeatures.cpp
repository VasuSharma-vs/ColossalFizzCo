#include "Components/CollisionCableComponent.h"
#include "Components/CableMovementConstraintComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UCollisionCableComponent::BeginPlay()
{
    Super::BeginPlay();
    if (bEnableNetworkSync && GetOwner() && GetOwner()->HasAuthority()) GetOwner()->SetReplicates(true);
    LastFeatureSignature = FeatureSignature();
    GetEndPositions(LastWakeStart, LastWakeEnd);
    RefreshWakeSources();
    RefreshMovementBindings();
    if (GetWorld()) GetWorld()->GetTimerManager().SetTimer(WakeMonitorTimer, this,
        &UCollisionCableComponent::CheckForWakeSources, FMath::Clamp(WakeMonitorInterval, .05f, 1.f), true);
    PublishNetworkSnapshot(0, true);
}

void UCollisionCableComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(WakeMonitorTimer);
    for (auto& Binding : MovementBindings) if (Binding.IsValid()) Binding->UnregisterCable(this);
    MovementBindings.Reset(); ActiveGrabs.Reset();
    Super::EndPlay(Reason);
}

void UCollisionCableComponent::OnUnregister()
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(WakeMonitorTimer);
    for (auto& Binding : MovementBindings) if (Binding.IsValid()) Binding->UnregisterCable(this);
    MovementBindings.Reset();
    for(auto& Pair:ObservedTransforms)if(Pair.Key.IsValid())Pair.Key->TransformUpdated.RemoveAll(this);
    ObservedTransforms.Reset();
    if (WakeSensor) { WakeSensor->DestroyComponent(); WakeSensor = nullptr; }
    Super::OnUnregister();
}

uint32 UCollisionCableComponent::FeatureSignature() const
{
    uint32 Hash = uint32(bEnableCollision) | uint32(bEnableSelfCollision)<<1 | uint32(bEnableSleeping)<<2 |
        uint32(bEnablePointWeight)<<3 | uint32(bEnableGrabbing)<<4 | uint32(bEnableMovementRestrictions)<<5 |
        uint32(bEnableVisualSubdivision)<<6 | uint32(bAttachStart)<<7 | uint32(bAttachEnd)<<8;
    Hash = HashCombine(Hash, GetTypeHash(PointMassKg));
    Hash = HashCombine(Hash, GetTypeHash(CableLength));
    Hash = HashCombine(Hash, GetTypeHash(NumSegments));
    Hash = HashCombine(Hash, GetTypeHash(CableForce));
    Hash = HashCombine(Hash, GetTypeHash(CableGravityScale));
    Hash = HashCombine(Hash, GetTypeHash(CableWidth));
    Hash = HashCombine(Hash, GetTypeHash(CollisionSkin));
    Hash = HashCombine(Hash, GetTypeHash(SelfCollisionThickness));
    Hash = HashCombine(Hash, GetTypeHash(SelfCollisionNeighborExclusion));
    Hash = HashCombine(Hash, GetTypeHash(SelfCollisionIterations));
    Hash = HashCombine(Hash, GetTypeHash(VisualSubdivisions));
    Hash = HashCombine(Hash, GetTypeHash(MaxVisualDeviation));
    Hash = HashCombine(Hash, GetTypeHash(VisualSmoothing));
    Hash = HashCombine(Hash, uint32(bLimitVisualDeviation) | uint32(bUseFullCollisionQueries)<<1);
    Hash = HashCombine(Hash, GetTypeHash(FullCollisionSolverInterval));
    Hash = HashCombine(Hash, GetTypeHash(SleepVelocityThreshold));
    Hash = HashCombine(Hash, GetTypeHash(SleepDelay));
    Hash = HashCombine(Hash, GetTypeHash(SleepConstraintTolerance));
    Hash = HashCombine(Hash, uint32(bStartAttachmentIsWorldAnchor) | uint32(bEndAttachmentIsWorldAnchor)<<1);
    for (float Mass : PointMassOverrides) Hash = HashCombine(Hash, GetTypeHash(Mass));
    for (int32 Index : FixedPointIndices) Hash = HashCombine(Hash, GetTypeHash(Index));
    return Hash;
}

void UCollisionCableComponent::SetCableFeatureEnabled(ECableFeature Feature, bool bEnabled)
{
    switch (Feature)
    {
    case ECableFeature::WorldCollision: bEnableCollision = bEnabled; break;
    case ECableFeature::SelfCollision: bEnableSelfCollision = bEnabled; break;
    case ECableFeature::Sleeping: bEnableSleeping = bEnabled; break;
    case ECableFeature::PointWeight: bEnablePointWeight = bEnabled; break;
    case ECableFeature::GrabbingAndPulling: bEnableGrabbing = bEnabled; break;
    case ECableFeature::MovementRestrictions: bEnableMovementRestrictions = bEnabled; break;
    case ECableFeature::VisualSubdivision: bEnableVisualSubdivision = bEnabled; break;
    }
    OnRep_CableFeatures();
}

void UCollisionCableComponent::OnRep_CableFeatures()
{
    if (!bEnableGrabbing) ActiveGrabs.Reset();
    if (!bEnableCollision)
    {
        for (auto& Contact : PointLineContacts) Contact = FLineContact();
        for (auto& Contact : SegmentLineContacts) Contact = FLineContact();
    }
    WakeCable(); RefreshMovementBindings(); RefreshWakeSources();
    LastFeatureSignature = FeatureSignature();
    PublishSimulation(); PublishNetworkSnapshot(0, true);
}

float UCollisionCableComponent::GetPointMass(int32 PointIndex) const
{
    if (!bEnablePointWeight) return 1;
    const float Mass = PointMassOverrides.IsValidIndex(PointIndex) && PointMassOverrides[PointIndex] > 0 ? PointMassOverrides[PointIndex] : PointMassKg;
    return FMath::IsFinite(Mass) ? FMath::Clamp(Mass, .001f, 1000000.f) : 1;
}

double UCollisionCableComponent::InversePointMass(int32 Index) const
{
    return IsPinned(Index) ? 0. : 1. / GetPointMass(Index);
}

bool UCollisionCableComponent::SetPointMass(int32 Index, float Mass)
{
    if (!Particles.IsValidIndex(Index) || !FMath::IsFinite(Mass) || Mass <= 0) return false;
    if (PointMassOverrides.Num() <= Index) PointMassOverrides.SetNumZeroed(Index+1);
    PointMassOverrides[Index] = FMath::Clamp(Mass, .001f, 1000000.f);
    WakePoint(Index); return true;
}

bool UCollisionCableComponent::SetPointFixed(int32 Index, bool bFixed)
{
    if (!Particles.IsValidIndex(Index)) return false;
    if (bFixed) FixedPointIndices.AddUnique(Index); else FixedPointIndices.Remove(Index);
    WakePoint(Index); return true;
}

bool UCollisionCableComponent::AddImpulseToPoint(int32 Index, FVector Impulse)
{
    if (!HasSimulationAuthority() || !Particles.IsValidIndex(Index) || IsPinned(Index) || Impulse.ContainsNaN()) return false;
    WakePoint(Index);
    Particles[Index].Velocity += Impulse / GetPointMass(Index);
    return true;
}

int32 UCollisionCableComponent::FindNearestCablePoint(FVector Position, float MaxDistance) const
{
    if (Position.ContainsNaN() || !FMath::IsFinite(MaxDistance) || MaxDistance < 0) return INDEX_NONE;
    double Best = FMath::Square(MaxDistance); int32 Index = INDEX_NONE;
    for (int32 I=0; I<Particles.Num(); ++I)
    {
        const double D = FVector::DistSquared(Position, Particles[I].Position);
        if (D <= Best) { Best = D; Index = I; }
    }
    return Index;
}

bool UCollisionCableComponent::FindCablePointAlongRay(FVector Start, FVector Direction, float Reach, float Radius,
    int32& PointIndex, FVector& HitPosition, float& RayDistance) const
{
    PointIndex = INDEX_NONE; HitPosition = FVector::ZeroVector; RayDistance = Reach;
    if (!bEnableGrabbing || Start.ContainsNaN() || Direction.ContainsNaN() || !FMath::IsFinite(Reach) || Reach <= 0 || Radius < 0) return false;
    Direction = Direction.GetSafeNormal();
    if (Direction.IsNearlyZero()) return false;
    const FVector RayEnd = Start + Direction*Reach;
    double Best = Reach;
    for (int32 I=0; I+1<Particles.Num(); ++I)
    {
        FVector OnRay, OnCable;
        FMath::SegmentDistToSegmentSafe(Start, RayEnd, Particles[I].Position, Particles[I+1].Position, OnRay, OnCable);
        const double Along = FVector::DotProduct(OnRay-Start, Direction);
        if (Along >= 0 && Along <= Best && FVector::DistSquared(OnRay,OnCable) <= FMath::Square(Radius + CableWidth*.5f))
        {
            Best = Along; HitPosition = OnCable;
            PointIndex = FVector::DistSquared(OnCable,Particles[I].Position) <= FVector::DistSquared(OnCable,Particles[I+1].Position) ? I : I+1;
        }
    }
    RayDistance = float(Best);
    return PointIndex != INDEX_NONE;
}

bool UCollisionCableComponent::BeginPointGrab(AActor* Participant, int32 Index, FVector Target, const FCableGrabSettings& Settings, USceneComponent* TargetComponent, FName SocketName)
{
    if (!HasSimulationAuthority() || !bEnableGrabbing || !IsValid(Participant) || Participant->GetWorld()!=GetWorld() ||
        !Particles.IsValidIndex(Index) || IsPinned(Index) || Target.ContainsNaN() ||
        !FMath::IsFinite(Settings.MovementRate) || !FMath::IsFinite(Settings.MaxForceNewtons) ||
        !FMath::IsFinite(Settings.InfluenceRadius) || !FMath::IsFinite(Settings.FalloffExponent) ||
        Settings.MovementRate < 0 || Settings.MaxForceNewtons < 0 || Settings.InfluenceRadius < 0 || Settings.FalloffExponent < .1f ||
        FVector::Distance(Participant->GetActorLocation(),Particles[Index].Position) > FMath::Max(1.f,MaxGrabDistance) ||
        FVector::Distance(Participant->GetActorLocation(),Target) > FMath::Max(1.f,MaxGrabDistance) ||
        (TargetComponent && TargetComponent->GetWorld()!=GetWorld())) return false;
    if (ActiveGrabs.ContainsByPredicate([&](const FCableGrabState& Grab)
        { return IsValid(Grab.Participant) && Grab.Participant != Participant && Grab.PointIndex == Index
            && (Grab.Settings.bApproachingConnector || Settings.bApproachingConnector); })) return false;
    ActiveGrabs.RemoveAll([&](const auto& Grab) { return !IsValid(Grab.Participant) || Grab.Participant==Participant; });
    FCableGrabState Grab;
    Grab.Participant=Participant; Grab.PointIndex=Index; Grab.Target=Target; Grab.Settings=Settings;
    Grab.ParticipantOffset=Target-Participant->GetActorLocation();
    Grab.TargetComponent=TargetComponent; Grab.SocketName=SocketName;
    if (TargetComponent)
    {
        const FTransform Transform=SocketName.IsNone()?TargetComponent->GetComponentTransform():TargetComponent->GetSocketTransform(SocketName);
        Grab.TargetOffset=Transform.InverseTransformPosition(Target);
    }
    ActiveGrabs.Add(Grab); WakePoint(Index); RefreshMovementBindings(); PublishNetworkSnapshot(0,true);
    return true;
}

bool UCollisionCableComponent::UpdatePointGrab(AActor* Participant, FVector Target)
{
    if (!HasSimulationAuthority() || !bEnableGrabbing || Target.ContainsNaN() || !IsValid(Participant) ||
        FVector::Distance(Target,Participant->GetActorLocation()) > FMath::Max(1.f,MaxGrabDistance)) return false;
    for (auto& Grab : ActiveGrabs) if (Grab.Participant==Participant)
    {
        if (!FVector(Grab.Target).Equals(Target,.01)) WakePoint(Grab.PointIndex);
        Grab.Target=Target; Grab.ParticipantOffset=Target-Participant->GetActorLocation(); return true;
    }
    return false;
}

bool UCollisionCableComponent::EndPointGrab(AActor* Participant)
{
    if (!HasSimulationAuthority()) return false;
    const int32 Removed=ActiveGrabs.RemoveAll([&](const auto& Grab) { return !IsValid(Grab.Participant) || Grab.Participant==Participant; });
    if (Removed) { WakeCable(); RefreshMovementBindings(); PublishNetworkSnapshot(0,true); }
    return Removed>0;
}

TArray<FCableGrabState> UCollisionCableComponent::GetActiveGrabs() const
{
    return HasSimulationAuthority()?ActiveGrabs:SimulationSnapshot.Grabs;
}

void UCollisionCableComponent::GetDirectGrabVelocities(TArray<FVector>& Velocities) const
{
    Velocities.Init(FVector::ZeroVector,Particles.Num());
    if (!bEnableGrabbing || Particles.Num()<2) return;
    const auto Grabs=GetActiveGrabs();
    TArray<double> Arc;Arc.Init(0.,Particles.Num());
    for(int32 I=1;I<Particles.Num();++I)Arc[I]=Arc[I-1]+FVector::Distance(Particles[I-1].Position,Particles[I].Position);
    for (const auto& Grab : Grabs)
    {
        if (!IsValid(Grab.Participant) || !Particles.IsValidIndex(Grab.PointIndex)) continue;
        FVector Target=Grab.Participant->GetActorLocation()+Grab.ParticipantOffset;
        if (IsValid(Grab.TargetComponent))
        {
            const FTransform Transform=Grab.SocketName.IsNone()?Grab.TargetComponent->GetComponentTransform():Grab.TargetComponent->GetSocketTransform(Grab.SocketName);
            Target=Transform.TransformPosition(Grab.TargetOffset);
        }
        const FVector Offset = Target - Particles[Grab.PointIndex].Position;
        const FVector Direction = Offset.GetSafeNormal();
        const double Rate = Grab.Settings.bApproachingConnector
            ? FMath::Min<double>(Grab.Settings.MovementRate, Offset.Size() * 18.)
            : Grab.Settings.MovementRate;
        for (int32 I=0; I<Particles.Num(); ++I)
        {
            if (IsPinned(I)) continue;
            const double Distance=FMath::Abs(Arc[I]-Arc[Grab.PointIndex]);
            const double Influence=I==Grab.PointIndex?1.:Grab.Settings.InfluenceRadius>0?
                FMath::Pow(FMath::Clamp(1.-Distance/Grab.Settings.InfluenceRadius,0.,1.),FMath::Max(.1f,Grab.Settings.FalloffExponent)):0.;
            const double WeightScale = bEnablePointWeight && !Grab.Settings.bApproachingConnector
                ? FMath::Min(1., double(FMath::Max(.001f, GrabReferenceMassKg)) / GetPointMass(I)) : 1.;
            Velocities[I] += Direction * (Rate * Influence * WeightScale);
        }
    }
}

void UCollisionCableComponent::ApplyGrabDrive(float Dt)
{
    if (!bEnableGrabbing || ActiveGrabs.IsEmpty()) return;
    ActiveGrabs.RemoveAll([&](const auto& Grab) { return !IsValid(Grab.Participant) || !Particles.IsValidIndex(Grab.PointIndex); });
    TArray<FVector> Drive; GetDirectGrabVelocities(Drive);
    TArray<double> Forces; Forces.Init(0.,Particles.Num());
    TArray<double> Arc;Arc.Init(0.,Particles.Num());
    for(int32 I=1;I<Particles.Num();++I)Arc[I]=Arc[I-1]+FVector::Distance(Particles[I-1].Position,Particles[I].Position);
    for (auto& Grab : ActiveGrabs)
    {
        if (IsValid(Grab.TargetComponent))
        {
            const FTransform Transform=Grab.SocketName.IsNone()?Grab.TargetComponent->GetComponentTransform():Grab.TargetComponent->GetSocketTransform(Grab.SocketName);
            Grab.Target=Transform.TransformPosition(Grab.TargetOffset);
        }
        else if(Grab.Participant) Grab.Target=Grab.Participant->GetActorLocation()+Grab.ParticipantOffset;
        TArray<double> Influence;Influence.Init(0.,Particles.Num());double TotalInfluence=0;
        for (int32 I=0; I<Particles.Num(); ++I)
        {
            if(IsPinned(I))continue;
            const double D=FMath::Abs(Arc[I]-Arc[Grab.PointIndex]);
            const double W=I==Grab.PointIndex?1.:Grab.Settings.InfluenceRadius>0?FMath::Pow(FMath::Clamp(1.-D/Grab.Settings.InfluenceRadius,0.,1.),FMath::Max(.1f,Grab.Settings.FalloffExponent)):0.;
            Influence[I]=W;TotalInfluence+=W;
        }
        // This is the participant's total force budget, distributed across its
        // influence. Increasing visual/simulation resolution must not multiply
        // a participant's available Newtons by the number of affected points.
        if(TotalInfluence>UE_SMALL_NUMBER)for(int32 I=0;I<Particles.Num();++I)
            Forces[I]+=FMath::Max(0.f,Grab.Settings.MaxForceNewtons)*100.*Influence[I]/TotalInfluence;
    }
    for (int32 I=0; I<Particles.Num(); ++I)
    {
        if (IsPinned(I) || Forces[I]<=0 || Drive[I].IsNearlyZero()) continue;
        WakePoint(I,false);
        // A finite force accelerates mass towards the requested cm/s drive.
        // Only the drive direction is controlled; gravity/tangential motion remain.
        const FVector Direction=Drive[I].GetSafeNormal();
        const double Desired=Drive[I].Size();
        const double Current=FVector::DotProduct(Particles[I].Velocity,Direction);
        const double Delta=FMath::Clamp(Desired-Current,-Forces[I]/GetPointMass(I)*Dt,Forces[I]/GetPointMass(I)*Dt);
        Particles[I].Velocity+=Direction*Delta;
    }
}

FVector UCollisionCableComponent::ConstrainSelfContactDirection(int32 Index,FVector Direction) const
{
    if(!HasCollision() || !Particles.IsValidIndex(Index))return Direction;
    auto Project=[&](const FLineContact& Contact)
    {
        if(!Contact.Component.IsValid() || Contact.Normal.IsNearlyZero())return;
        const double Distance=FVector::DotProduct(Particles[Index].Position-Contact.Point,Contact.Normal);
        if(Distance>CollisionRadius()+.02)return;
        const double Inward=FVector::DotProduct(Direction,Contact.Normal);
        if(Inward<0)Direction-=Contact.Normal*Inward;
    };
    // These are constraint gradients rather than finite movements. Project only
    // against touching supports so clearance is not mistaken for a fixed point.
    for(int32 Pass=0;Pass<2;++Pass)
    {
        if(PointLineContacts.IsValidIndex(Index))Project(PointLineContacts[Index]);
        if(SegmentLineContacts.IsValidIndex(Index-1))Project(SegmentLineContacts[Index-1]);
        if(SegmentLineContacts.IsValidIndex(Index))Project(SegmentLineContacts[Index]);
    }
    return Direction;
}

void UCollisionCableComponent::SolveSelfCollision(const TArray<FVector>& Before)
{
    bSelfContactUnresolved=false;
    if (!bEnableSelfCollision || Particles.Num()<4) {SelfContacts.Reset();return;}
    const double Thickness=FMath::Max(.1f,SelfCollisionThickness>0?SelfCollisionThickness:CableWidth);
    const double Tolerance=FMath::Clamp(Thickness*.001,.005,.02);
    const int32 Exclude=FMath::Clamp(SelfCollisionNeighborExclusion,1,16);
    auto PairKey=[](int32 A,int32 B){return (uint64(uint32(A))<<32)|uint32(B);};
    TMap<uint64,FVector> PreviousNormals;
    TMap<uint64,double> NormalCorrections;
    TMap<uint64,double> RemainingInitialRepair;
    for(const auto& Contact:SelfContacts)PreviousNormals.Add(PairKey(Contact.A,Contact.B),Contact.Normal);
    SelfContacts.Reset();
    auto ContactAt=[&](int32 A,int32 B,FSelfContact& Contact)
    {
        ++SelfCollisionPairChecks;
        const FVector A0=Particles[A].Position,A1=Particles[A+1].Position,B0=Particles[B].Position,B1=Particles[B+1].Position;
        FVector PA,PB;FMath::SegmentDistToSegmentSafe(A0,A1,B0,B1,PA,PB);
        const double Distance=FVector::Distance(PA,PB);
        Contact.A=A;Contact.B=B;Contact.Normal=(PB-PA).GetSafeNormal();
        if(Distance<=Tolerance)
        {
            if(const auto* Previous=PreviousNormals.Find(PairKey(A,B)))Contact.Normal=*Previous;
            else if(Contact.Normal.IsNearlyZero())
            {
                Contact.Normal=FVector::CrossProduct(A1-A0,B1-B0).GetSafeNormal();
                if(Contact.Normal.IsNearlyZero())Contact.Normal=FVector::VectorPlaneProject(FVector::UpVector,(A1-A0).GetSafeNormal()).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
            }
        }
        Contact.TA=FMath::Clamp(FVector::DotProduct(PA-A0,A1-A0)/FMath::Max(UE_SMALL_NUMBER,FVector::DistSquared(A0,A1)),0.,1.);
        Contact.TB=FMath::Clamp(FVector::DotProduct(PB-B0,B1-B0)/FMath::Max(UE_SMALL_NUMBER,FVector::DistSquared(B0,B1)),0.,1.);
        return Distance;
    };
    for (int32 Pass=0; Pass<FMath::Clamp(SelfCollisionIterations,1,4); ++Pass)
    for (int32 A=0; A+1<Particles.Num(); ++A)
    for (int32 B=A+Exclude+1; B+1<Particles.Num(); ++B)
    {
        if (Particles[A].bSleeping && Particles[A+1].bSleeping && Particles[B].bSleeping && Particles[B+1].bSleeping) continue;
        FSelfContact Contact;const double Distance=ContactAt(A,B,Contact);
        const double Penetration=Thickness-Distance;
        if(Penetration<=Tolerance)continue;
        PreviousNormals.Add(PairKey(A,B),Contact.Normal);
        const int32 IDs[4]={A,A+1,B,B+1};const double Weights[4]={1-Contact.TA,Contact.TA,1-Contact.TB,Contact.TB};
        const FVector RelativeVelocity=FMath::Lerp(Particles[B].Velocity,Particles[B+1].Velocity,Contact.TB)
            -FMath::Lerp(Particles[A].Velocity,Particles[A+1].Velocity,Contact.TA);
        const bool Disturbed=Penetration>FMath::Max(Tolerance,double(WakeDisplacementThreshold))
            || FVector::DotProduct(RelativeVelocity,Contact.Normal)<-FMath::Max(0.f,WakeThreshold);
        double InverseMass[4],Denom=0;FVector Directions[4];
        for(int32 K=0;K<4;++K)
        {
            // Persistent resting contact is support, not an event that resets every
            // awake point's quiet timer. A real impact still wakes sleeping spans.
            if(Disturbed && Weights[K]>UE_SMALL_NUMBER && Particles[IDs[K]].bSleeping && !IsPinned(IDs[K]))WakePoint(IDs[K],false);
            InverseMass[K]=bEnableSleeping && Particles[IDs[K]].bSleeping?0.:InversePointMass(IDs[K]);
            const FVector Gradient=Contact.Normal*(K<2?-1.:1.);
            Directions[K]=ConstrainSelfContactDirection(IDs[K],Gradient);
            Denom+=InverseMass[K]*FMath::Square(Weights[K])*FMath::Max(0.,FVector::DotProduct(Directions[K],Gradient));
        }
        if(Denom<UE_SMALL_NUMBER)continue;
        const uint64 Key=PairKey(A,B);
        double* RepairBudget=RemainingInitialRepair.Find(Key);
        if(!RepairBudget)
        {
            double InitialPenetration=0;
            if(Before.IsValidIndex(A+1) && Before.IsValidIndex(B+1))
            {
                FVector PA,PB;FMath::SegmentDistToSegmentSafe(Before[A],Before[A+1],Before[B],Before[B+1],PA,PB);
                InitialPenetration=FMath::Max(0.,Thickness-FVector::Distance(PA,PB)-Tolerance);
            }
            RepairBudget=&RemainingInitialRepair.Add(Key,InitialPenetration);
        }
        double AppliedSeparation=0;FVector Moves[4]={FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector};
        for (int32 K=0;K<4;++K)
        {
            const double W=InverseMass[K]*Weights[K];if(W<=0)continue;
            const FVector Correction=Directions[K]*(Penetration*W/Denom);
            Moves[K]=ConstrainFullCollisionMotion(IDs[K],Correction);
            AppliedSeparation+=FVector::DotProduct(Moves[K],Contact.Normal*(K<2?-1.:1.))*Weights[K];
        }
        AppliedSeparation=FMath::Max(0.,AppliedSeparation);
        const double RepairFraction=AppliedSeparation>UE_SMALL_NUMBER?FMath::Clamp(*RepairBudget/AppliedSeparation,0.,1.):0.;
        *RepairBudget=FMath::Max(0.,*RepairBudget-AppliedSeparation*RepairFraction);
        for(int32 K=0;K<4;++K)
        {
            Particles[IDs[K]].Position+=Moves[K];
            // Only pre-existing overlap is repair. A normal reaction to this step's
            // gravity or movement must remain in reconstructed velocity to stop it.
            Particles[IDs[K]].SelfContactCorrectionThisStep+=Moves[K]*RepairFraction;
        }
        NormalCorrections.FindOrAdd(Key)+=AppliedSeparation/Denom;
    }
    // Check the final geometry, including corrections constrained by the world.
    // Touching contacts may sleep; a genuinely unresolved overlap may not.
    for(int32 A=0;A+1<Particles.Num();++A)
    for(int32 B=A+Exclude+1;B+1<Particles.Num();++B)
    {
        FSelfContact Contact;const double Distance=ContactAt(A,B,Contact);
        Contact.NormalCorrectionLambda=NormalCorrections.FindRef(PairKey(A,B));
        if(Distance<=Thickness+Tolerance)SelfContacts.Add(Contact);
        if(Distance<Thickness-Tolerance)bSelfContactUnresolved=true;
    }
}

void UCollisionCableComponent::ApplySelfContactFriction(const TArray<FVector>& Before)
{
    if(!bEnableSelfCollision || CollisionFriction<=0)return;
    TArray<double> Budgets;Budgets.Reserve(SelfContacts.Num());
    for(const auto& Contact:SelfContacts)Budgets.Add(Contact.NormalCorrectionLambda*FMath::Clamp(CollisionFriction,0.f,1.f));
    auto PhysicalMove=[&](int32 Index)
    {
        const auto& Particle=Particles[Index];
        const FVector Repair=Particle.SelfContactCorrectionThisStep
            +(bSmoothCollisionRecovery?Particle.ContactCorrectionThisStep:FVector::ZeroVector);
        return Particle.Position-Before[Index]-Repair;
    };
    for(int32 Pass=0;Pass<2;++Pass)for(int32 ContactIndex=0;ContactIndex<SelfContacts.Num();++ContactIndex)
    {
        const auto& Contact=SelfContacts[ContactIndex];
        if(Budgets[ContactIndex]<=UE_SMALL_NUMBER || !Before.IsValidIndex(Contact.A+1) || !Before.IsValidIndex(Contact.B+1))continue;
        const int32 IDs[4]={Contact.A,Contact.A+1,Contact.B,Contact.B+1};
        const double Weights[4]={1-Contact.TA,Contact.TA,1-Contact.TB,Contact.TB};
        const FVector RelativePhysical=FMath::Lerp(PhysicalMove(Contact.B),PhysicalMove(Contact.B+1),Contact.TB)
            -FMath::Lerp(PhysicalMove(Contact.A),PhysicalMove(Contact.A+1),Contact.TA);
        const FVector Relative=FMath::Lerp(Particles[Contact.B].Position-Before[Contact.B],Particles[Contact.B+1].Position-Before[Contact.B+1],Contact.TB)
            -FMath::Lerp(Particles[Contact.A].Position-Before[Contact.A],Particles[Contact.A+1].Position-Before[Contact.A+1],Contact.TA);
        const FVector Tangent=FVector::VectorPlaneProject(Relative,Contact.Normal);
        const double Distance=Tangent.Size();if(Distance<=UE_SMALL_NUMBER)continue;
        const FVector TangentDirection=Tangent/Distance;
        double InverseMass[4],Denom=0;FVector Directions[4];
        for(int32 K=0;K<4;++K)
        {
            InverseMass[K]=bEnableSleeping && Particles[IDs[K]].bSleeping?0.:InversePointMass(IDs[K]);
            const FVector Gradient=TangentDirection*(K<2?1.:-1.);
            Directions[K]=Gradient;
            const FVector Outward=Contact.Normal*(K<2?-1.:1.);
            // Preserve both world support and the self-contact separation while
            // finding a feasible opposing friction direction at each point.
            for(int32 Projection=0;Projection<2;++Projection)
            {
                Directions[K]=ConstrainSelfContactDirection(IDs[K],Directions[K]);
                const double Inward=FVector::DotProduct(Directions[K],Outward);
                if(Inward<0)Directions[K]-=Outward*Inward;
            }
            Denom+=InverseMass[K]*FMath::Square(Weights[K])*FMath::Max(0.,FVector::DotProduct(Directions[K],Gradient));
        }
        if(Denom<=UE_SMALL_NUMBER)continue;
        const double Lambda=FMath::Min(Distance/Denom,Budgets[ContactIndex]);
        double RemovedTangent=0;FVector Moves[4]={FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector};
        for(int32 K=0;K<4;++K)if(InverseMass[K]>0)
        {
            const FVector Correction=Directions[K]*(Lambda*InverseMass[K]*Weights[K]);
            Moves[K]=ConstrainFullCollisionMotion(IDs[K],Correction);
            RemovedTangent+=FVector::DotProduct(Moves[K],TangentDirection*(K<2?1.:-1.))*Weights[K];
        }
        if(RemovedTangent<=UE_SMALL_NUMBER)continue;
        const double PhysicalFraction=FMath::Clamp(FVector::DotProduct(RelativePhysical,TangentDirection)/RemovedTangent,0.,1.);
        for(int32 K=0;K<4;++K)
        {
            Particles[IDs[K]].Position+=Moves[K];
            // Friction opposes all displacement, including another contact's repair.
            // Transfer only the opposed physical relative motion into momentum;
            // removing a repair must not manufacture reverse relative velocity.
            Particles[IDs[K]].SelfContactCorrectionThisStep+=Moves[K]*(1.-PhysicalFraction);
        }
        // Both passes share the same finite contact-load budget.
        Budgets[ContactIndex]=FMath::Max(0.,Budgets[ContactIndex]-FMath::Max(0.,RemovedTangent)/Denom);
    }
}

void UCollisionCableComponent::ApplySelfCollisionVelocityResponse()
{
    if(!bEnableSelfCollision)return;
    for(int32 Pass=0;Pass<2;++Pass)for(const auto& Contact:SelfContacts)
    {
        if(!Particles.IsValidIndex(Contact.A+1) || !Particles.IsValidIndex(Contact.B+1))continue;
        const int32 IDs[4]={Contact.A,Contact.A+1,Contact.B,Contact.B+1};
        const double Weights[4]={1-Contact.TA,Contact.TA,1-Contact.TB,Contact.TB};
        double InverseMass[4],Denom=0;FVector Directions[4];
        for(int32 K=0;K<4;++K)
        {
            InverseMass[K]=bEnableSleeping && Particles[IDs[K]].bSleeping?0.:InversePointMass(IDs[K]);
            const FVector Gradient=Contact.Normal*(K<2?-1.:1.);
            Directions[K]=ConstrainSelfContactDirection(IDs[K],Gradient);
            Denom+=InverseMass[K]*FMath::Square(Weights[K])*FMath::Max(0.,FVector::DotProduct(Directions[K],Gradient));
        }
        if(Denom<UE_SMALL_NUMBER)continue;
        const FVector Relative=FMath::Lerp(Particles[Contact.B].Velocity,Particles[Contact.B+1].Velocity,Contact.TB)
            -FMath::Lerp(Particles[Contact.A].Velocity,Particles[Contact.A+1].Velocity,Contact.TA);
        const double Closing=FVector::DotProduct(Relative,Contact.Normal);
        if(Closing>=0)continue;
        const double NormalImpulse=-Closing/Denom;
        for(int32 K=0;K<4;++K)if(InverseMass[K]>0)
            Particles[IDs[K]].Velocity+=Directions[K]*(NormalImpulse*InverseMass[K]*Weights[K]);
    }
    bSelfContactUnresolved=false;
    const double Thickness=FMath::Max(.1f,SelfCollisionThickness>0?SelfCollisionThickness:CableWidth);
    const double Tolerance=FMath::Clamp(Thickness*.001,.005,.02);
    const int32 Exclude=FMath::Clamp(SelfCollisionNeighborExclusion,1,16);
    // Friction for one contact can affect another contact sharing its points.
    // Validate the final positions before allowing the whole cable to sleep.
    for(int32 A=0;A+1<Particles.Num();++A)
    for(int32 B=A+Exclude+1;B+1<Particles.Num();++B)
    {
        ++SelfCollisionPairChecks;
        FVector PA,PB;
        FMath::SegmentDistToSegmentSafe(Particles[A].Position,Particles[A+1].Position,Particles[B].Position,Particles[B+1].Position,PA,PB);
        if(FVector::DistSquared(PA,PB)<FMath::Square(Thickness-Tolerance))
        {bSelfContactUnresolved=true;return;}
    }
}

void UCollisionCableComponent::GetCableRenderLocations(TArray<FVector>& Locations) const
{
    if(!PublishedPoints.IsEmpty() && PublishedRenderSignature==FeatureSignature())
    {Locations=PublishedPoints;return;}
    BuildCableRenderLocations(Locations);
}

void UCollisionCableComponent::BuildCableRenderLocations(TArray<FVector>& Locations) const
{
    Locations.Reset();
    const int32 Subdivisions=bEnableVisualSubdivision?FMath::Clamp(VisualSubdivisions,1,8):1;
    const double Smoothing=FMath::Clamp(VisualSmoothing,0.f,1.f);
    for(int32 I=0;I+1<Particles.Num();++I)
    {
        const FVector P1=Particles[I].Position,P2=Particles[I+1].Position;
        const FVector P0=I>0?Particles[I-1].Position:2*P1-P2;
        const FVector P3=I+2<Particles.Num()?Particles[I+2].Position:2*P2-P1;
        for(int32 S=0;S<Subdivisions;++S)
        {
            const double T=double(S)/Subdivisions;
            const FVector Chord=FMath::Lerp(P1,P2,T);
            const FVector Curve=.5*((2*P1)+(-P0+P2)*T+(2*P0-5*P1+4*P2-P3)*T*T+(-P0+3*P1-3*P2+P3)*T*T*T);
            FVector Offset=(Curve-Chord)*Smoothing;
            if(bLimitVisualDeviation)Offset=Offset.GetClampedToMaxSize(FMath::Max(0.f,MaxVisualDeviation));
            FVector Point=Chord+Offset;
            // A sub-centimeter clamp flattened every curved span into an offset
            // straight line. Use real cubic interpolation, with inexpensive
            // cached surface planes guarding floors/walls in budgeted mode.
            if(S>0 && bEnableCollision)
            {
                auto KeepOutside=[&](const FLineContact& Contact)
                {
                    if(!Contact.Component.IsValid()||Contact.Normal.IsNearlyZero())return;
                    if(!Contact.Component->Bounds.GetBox().ExpandBy(CollisionRadius()*2).IsInsideOrOn(Point))return;
                    const double Minimum=FMath::Min(FVector::DotProduct(Chord-Contact.Point,Contact.Normal),double(CableWidth*.5f+.01f));
                    const double Distance=FVector::DotProduct(Point-Contact.Point,Contact.Normal);
                    if(Distance<Minimum)Point+=Contact.Normal*(Minimum-Distance);
                };
                if(PointLineContacts.IsValidIndex(I))KeepOutside(PointLineContacts[I]);
                if(PointLineContacts.IsValidIndex(I+1))KeepOutside(PointLineContacts[I+1]);
                if(SegmentLineContacts.IsValidIndex(I))KeepOutside(SegmentLineContacts[I]);
            }
            Locations.Add(Point);
        }
    }
    if (!Particles.IsEmpty()) Locations.Add(Particles.Last().Position);
    FollowReplicatedHoseAttachments(Locations);
}

FMatrix UCollisionCableComponent::GetRenderMatrix() const
{
    return FTranslationMatrix(GetComponentLocation());
}
