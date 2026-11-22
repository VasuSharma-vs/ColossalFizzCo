#include "Components/CollisionCableComponent.h"
#include "Components/CableMovementConstraintComponent.h"
#include "CollisionCableRendering.h"

#include "DynamicMeshBuilder.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "Net/UnrealNetwork.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "SceneManagement.h"

namespace CollisionCable
{
    const FName StartSocket(TEXT("CableStart"));
    const FName EndSocket(TEXT("CableEnd"));

    FCollisionShape SegmentShape(const FVector& A, const FVector& B, float Radius, FQuat& Rotation)
    {
        const FVector Delta = B - A;
        Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
        return FCollisionShape::MakeCapsule(Radius, float(Delta.Size() * .5) + Radius);
    }


}

UCollisionCableComponent::UCollisionCableComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    bTickInEditor = true;
    bWantsOnUpdateTransform = true;
    SetCollisionProfileName(TEXT("PhysicsActor"));
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetGenerateOverlapEvents(false);
    SetIsReplicatedByDefault(true);
}

void UCollisionCableComponent::OnRegister()
{
    Super::OnRegister();
    RefreshCollisionParams();
    // Registration happens during Blueprint reconstruction and before placement
    // applies the final actor transform. Matching array sizes do not mean that
    // old world-space points still belong to this location.
    if (!HasBegunPlay() || (GetWorld() && !GetWorld()->IsGameWorld()) ||
        Particles.Num() != FMath::Clamp(NumSegments, 1, 256) + 1) ResetCable();
}

void UCollisionCableComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
    Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
    if (!IsRegistered() || IsTemplate() || !GetWorld() || bResettingCable) return;
    // Editor placement is an authoring operation, not a physical movement from
    // the world origin. Reset immediately even when editor realtime is disabled.
    // During play leave points in world space so the swept anchors still work.
    if (!GetWorld()->IsGameWorld() || !HasBegunPlay()) ResetCable();
    else
    {
        // The simulated path stays in world space when a hand or mount rotates the component.
        MarkRenderDynamicDataDirty();
        if (bEnableSleeping) OnObservedTransform(this,UpdateTransformFlags,Teleport);
    }
}

#if WITH_EDITOR
void UCollisionCableComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    CollisionPointsPerFrame = FMath::Clamp(CollisionPointsPerFrame, 1, FMath::Clamp(NumSegments, 1, 256) + 1);
    if (IsRegistered() && !IsTemplate() && GetWorld() && !GetWorld()->IsGameWorld())
    {
        ResetCable();
        if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UCollisionCableComponent, NumSides)) MarkRenderStateDirty();
    }
    // The superclass can reconstruct and replace this Blueprint component.
    Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UCollisionCableComponent::GetEndPositions(FVector& Start, FVector& End) const
{
    Start = GetStartAttachmentLocation();
    const USceneComponent* EndComponent = GetAttachedComponent();
    if (!EndComponent) EndComponent = this;
    End = (AttachEndToSocketName.IsNone() ? EndComponent->GetComponentTransform() :
        EndComponent->GetSocketTransform(AttachEndToSocketName)).TransformPosition(EndLocation);
}

void UCollisionCableComponent::SetStartAttachmentOverride(USceneComponent* Component, FName SocketName)
{
    if (StartAttachmentOverride.Get()==Component && StartOverrideSocket==SocketName) return;
    StartAttachmentOverride = Component;
    StartOverrideSocket = SocketName;
    WakeCable();
    RefreshMovementBindings();
    RefreshWakeSources();
}

USceneComponent* UCollisionCableComponent::GetStartAttachmentComponent() const
{
    return StartAttachmentOverride.Get();
}

USceneComponent* UCollisionCableComponent::GetStartAttachmentSource() const
{
    if (auto* Source = StartAttachmentOverride.Get()) return Source;
    return const_cast<UCollisionCableComponent*>(this);
}

FVector UCollisionCableComponent::GetStartAttachmentLocation() const
{
    const auto* Source = StartAttachmentOverride.Get();
    return Source ? Source->GetSocketLocation(StartOverrideSocket) : GetComponentLocation();
}

void UCollisionCableComponent::SetSocketMountedEndpoints(bool bStartMounted, bool bEndMounted)
{
    bStartSocketMounted = bStartMounted;
    bEndSocketMounted = bEndMounted;
}

const AActor* UCollisionCableComponent::GetSocketMountOwner(int32 PointIndex) const
{
    const USceneComponent* Socket = nullptr;
    if (PointIndex==0 && bAttachStart && bStartSocketMounted) Socket = GetStartAttachmentComponent();
    else if (PointIndex==Particles.Num()-1 && bAttachEnd && bEndSocketMounted) Socket = GetAttachedComponent();
    return IsValid(Socket) ? Socket->GetOwner() : nullptr;
}

const FCollisionQueryParams& UCollisionCableComponent::GetSpanCollisionParams(int32 SpanIndex, TOptional<FCollisionQueryParams>& LocalParams) const
{
    const AActor* FirstMount = GetSocketMountOwner(SpanIndex);
    const AActor* LastMount = GetSocketMountOwner(SpanIndex+1);
    if (!FirstMount && !LastMount) return QueryParams;

    // A connected plug is allowed to occupy its fitting. Only its neighboring
    // span may pass through that housing. Other spans still collide with it.
    LocalParams.Emplace(QueryParams);
    if (FirstMount) LocalParams->AddIgnoredActor(FirstMount);
    if (LastMount && LastMount!=FirstMount) LocalParams->AddIgnoredActor(LastMount);
    return LocalParams.GetValue();
}

void UCollisionCableComponent::SetAttachEndToComponent(USceneComponent* Component, FName SocketName)
{
    ReplicatedEndComponent = Component;
    AttachEndTo.OtherActor = Component ? Component->GetOwner() : nullptr;
    AttachEndTo.ComponentProperty = NAME_None;
    AttachEndTo.PathToComponent.Reset();
    AttachEndTo.OverrideComponent = Component;
    AttachEndToSocketName = SocketName;
    WakeCable(); RefreshMovementBindings();
}

void UCollisionCableComponent::SetAttachEndTo(AActor* Actor, FName ComponentProperty, FName SocketName)
{
    ReplicatedEndComponent = nullptr;
    AttachEndTo.OverrideComponent.Reset();
    AttachEndTo.OtherActor = Actor;
    AttachEndTo.ComponentProperty = ComponentProperty;
    AttachEndTo.PathToComponent.Reset();
    AttachEndToSocketName = SocketName;
    ReplicatedEndComponent = Cast<USceneComponent>(AttachEndTo.GetComponent(GetOwner()));
    WakeCable(); RefreshMovementBindings();
}

AActor* UCollisionCableComponent::GetAttachedActor() const
{
    const USceneComponent* Component = GetAttachedComponent();
    return Component ? Component->GetOwner() : AttachEndTo.OtherActor.Get();
}

USceneComponent* UCollisionCableComponent::GetAttachedComponent() const
{
    if (ReplicatedEndComponent) return ReplicatedEndComponent.Get();
    // FComponentReference resolves an empty reference to the owning actor's root.
    // For a cable, "no target" means EndLocation is relative to THIS component.
    // The difference is invisible when this is the root, but breaks nested BP
    // components and causes the level and Blueprint previews to disagree.
    if (!AttachEndTo.OtherActor.IsValid() && AttachEndTo.ComponentProperty.IsNone() &&
        AttachEndTo.PathToComponent.IsEmpty() && !AttachEndTo.OverrideComponent.IsValid()) return nullptr;
    return Cast<USceneComponent>(AttachEndTo.GetComponent(GetOwner()));
}

void UCollisionCableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UCollisionCableComponent, bAttachStart);
    DOREPLIFETIME(UCollisionCableComponent, bAttachEnd);
    DOREPLIFETIME(UCollisionCableComponent, EndLocation);
    DOREPLIFETIME(UCollisionCableComponent, ReplicatedEndComponent);
    DOREPLIFETIME(UCollisionCableComponent, AttachEndToSocketName);
    DOREPLIFETIME(UCollisionCableComponent, CableLength);
    DOREPLIFETIME(UCollisionCableComponent, NumSegments);
    DOREPLIFETIME(UCollisionCableComponent, bEnableCollision);
    DOREPLIFETIME(UCollisionCableComponent, bUseFullCollisionQueries);
    DOREPLIFETIME(UCollisionCableComponent, bEnableSelfCollision);
    DOREPLIFETIME(UCollisionCableComponent, bEnableSleeping);
    DOREPLIFETIME(UCollisionCableComponent, bEnablePointWeight);
    DOREPLIFETIME(UCollisionCableComponent, bEnableGrabbing);
    DOREPLIFETIME(UCollisionCableComponent, bEnableMovementRestrictions);
    DOREPLIFETIME(UCollisionCableComponent, bEnableVisualSubdivision);
    DOREPLIFETIME(UCollisionCableComponent, PointMassKg);
    DOREPLIFETIME(UCollisionCableComponent, PointMassOverrides);
    DOREPLIFETIME(UCollisionCableComponent, FixedPointIndices);
    DOREPLIFETIME(UCollisionCableComponent, bStartAttachmentIsWorldAnchor);
    DOREPLIFETIME(UCollisionCableComponent, bEndAttachmentIsWorldAnchor);
    DOREPLIFETIME(UCollisionCableComponent, SimulationSnapshot);
}

void UCollisionCableComponent::SetComponentTickEnabled(bool bEnabled)
{
    if(bEnabled && bCableSleeping){WakeCable();return;}
    Super::SetComponentTickEnabled(bEnabled || (!bCableSleeping && (bEnableSleeping || (bKeepCollisionAwake && bEnableCollision))));
}

bool UCollisionCableComponent::IsPinned(int32 Index) const
{
    return (Index == 0 && bAttachStart) || (Index == Particles.Num() - 1 && bAttachEnd) || FixedPointIndices.Contains(Index);
}

bool UCollisionCableComponent::HasCollision() const
{
    return bEnableCollision && GetWorld() && IsQueryCollisionEnabled();
}

float UCollisionCableComponent::CollisionRadius() const { return FMath::Max(CableWidth * .5f, .05f) + FMath::Max(CollisionSkin, .01f); }

void UCollisionCableComponent::RefreshCollisionParams()
{
    QueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(CollisionCable), bTraceComplex);
    QueryParams.bFindInitialOverlaps = true;
    QueryParams.AddIgnoredComponent(this);
    if (bIgnoreOwner && GetOwner()) QueryParams.AddIgnoredActor(GetOwner());
    if (bIgnoreAttachedComponent)
    {
        if (const auto* Start = Cast<UPrimitiveComponent>(StartAttachmentOverride.Get())) QueryParams.AddIgnoredComponent(Start);
        if (const auto* End = Cast<UPrimitiveComponent>(GetAttachedComponent())) QueryParams.AddIgnoredComponent(End);
    }
    ResponseParams = FCollisionResponseParams(GetCollisionResponseToChannels());
    if (bIgnorePlayerCollision)
    {
        ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
        if (GetWorld())
            for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
                if (const APlayerController* Controller = It->Get())
                    if (const APawn* Pawn = Controller->GetPawn()) QueryParams.AddIgnoredActor(Pawn);
        // Remove old player contacts immediately when this option is enabled.
        auto ClearPlayerContact = [&](FLineContact& Contact)
        {
            const UPrimitiveComponent* Component = Contact.Component.Get();
            const APawn* Pawn = Component ? Cast<APawn>(Component->GetOwner()) : nullptr;
            if (!Component || Component->GetCollisionObjectType() == ECC_Pawn || (Pawn && Pawn->IsPlayerControlled()) ||
                (Component->GetOwner() && QueryParams.GetIgnoredSourceObjects().Contains(Component->GetOwner()->GetUniqueID())))
                Contact = FLineContact();
        };
        for (FLineContact& Contact : PointLineContacts) ClearPlayerContact(Contact);
        for (FLineContact& Contact : SegmentLineContacts) ClearPlayerContact(Contact);
    }
}

void UCollisionCableComponent::ResetCable()
{
    if (bResettingCable) return;
    TGuardValue<bool> ResetGuard(bResettingCable, true);
    NumSegments = FMath::Clamp(NumSegments, 1, 256);
    CollisionPointsPerFrame = FMath::Clamp(CollisionPointsPerFrame, 1, NumSegments + 1);
    FVector Start, End; GetEndPositions(Start, End);
    TArray<FVector> Points;
    const double ChordLength = FVector::Distance(Start, End);
    const double RestLength = FMath::Max(double(CableLength), ChordLength);
    if (NumSegments < 2 || RestLength <= ChordLength + .01)
    {
        for (int32 I = 0; I <= NumSegments; ++I) Points.Add(FMath::Lerp(Start, End, double(I) / NumSegments));
    }
    else
    {
        // A collapsed seed gives every distance constraint an arbitrary direction
        // and can expand upward before gravity has a chance to establish a sag.
        // Seed the slack below the mounts, then sample it by arc length.
        const FVector Acceleration = CableForce + FVector(0,0,GetWorld() ? GetWorld()->GetGravityZ() * CableGravityScale : -980.f);
        const FVector Down = Acceleration.GetSafeNormal(UE_SMALL_NUMBER, -FVector::UpVector);
        FVector Side = FVector::VectorPlaneProject(GetForwardVector(), Down).GetSafeNormal();
        if (Side.IsNearlyZero()) { FVector Unused; Down.FindBestAxisVectors(Side, Unused); }
        const bool bLoop = ChordLength < FMath::Max(double(CableWidth), 1.);
        const int32 Samples = FMath::Max(128, NumSegments * 8);
        auto PointAt = [&](double T, double Sag)
        {
            const FVector Chord = FMath::Lerp(Start, End, T);
            if (bLoop)
                return Chord + Sag * (Side * FMath::Sin(2 * PI * T) + Down * (1 - FMath::Cos(2 * PI * T)));
            return Chord + Down * (Sag * FMath::Sin(PI * T));
        };
        auto CurveLength = [&](double Sag)
        {
            double Length = 0;
            FVector Previous = Start;
            for (int32 I = 1; I <= Samples; ++I)
            {
                const FVector Current = PointAt(double(I) / Samples, Sag);
                Length += FVector::Distance(Previous, Current); Previous = Current;
            }
            return Length;
        };
        double Low = 0, High = RestLength;
        for (int32 Iteration = 0; Iteration < 24; ++Iteration)
        {
            const double Mid = (Low + High) * .5;
            if (CurveLength(Mid) < RestLength) Low = Mid; else High = Mid;
        }
        TArray<FVector> DensePoints;
        TArray<double> Distances;
        double TotalLength = 0;
        for (int32 I = 0; I <= Samples; ++I)
        {
            const FVector Current = PointAt(double(I) / Samples, (Low + High) * .5);
            if (I > 0) TotalLength += FVector::Distance(DensePoints.Last(), Current);
            DensePoints.Add(Current); Distances.Add(TotalLength);
        }
        int32 SampleIndex = 1;
        for (int32 I = 0; I <= NumSegments; ++I)
        {
            const double Distance = TotalLength * double(I) / NumSegments;
            while (SampleIndex < Samples && Distances[SampleIndex] < Distance) ++SampleIndex;
            const double Interval = Distances[SampleIndex] - Distances[SampleIndex - 1];
            const double Alpha = Interval > UE_SMALL_NUMBER ? (Distance - Distances[SampleIndex - 1]) / Interval : 0;
            Points.Add(FMath::Lerp(DensePoints[SampleIndex - 1], DensePoints[SampleIndex], Alpha));
        }
        Points[0] = Start; Points.Last() = End;
    }
    TArray<FLineContact> SeedContacts;
    SeedContacts.SetNum(Points.Num());
    RefreshCollisionParams();
    if (HasCollision())
    {
        // An authored slack curve may extend through the floor before its first
        // simulation step. Sweep the generated seed from its mount so it starts
        // on the accessible side of a surface; depenetrating an already embedded
        // loop can otherwise choose the underside of a thin floor. Explicitly
        // supplied particle paths are unaffected, and fixed endpoints are kept.
        for (int32 I = 1; I < Points.Num(); ++I)
        {
            if (I == Points.Num() - 1 && bAttachEnd) continue;
            FHitResult Hit;
            ++CollisionQueryCount;
            if (GetWorld()->SweepSingleByChannel(Hit, Start, Points[I], FQuat::Identity, GetCollisionObjectType(),
                FCollisionShape::MakeSphere(CollisionRadius()), QueryParams, ResponseParams) && !Hit.bStartPenetrating)
            {
                Points[I] = Hit.Location + Hit.Normal * .01f;
                SeedContacts[I].Point = Hit.ImpactPoint;
                SeedContacts[I].Normal = Hit.Normal;
                SeedContacts[I].Component = Hit.GetComponent();
            }
        }
    }
    SetCableParticleLocations(Points);
    PointLineContacts = MoveTemp(SeedContacts);
    // A deliberate reset (including pool delivery/load restoration) is not a pull
    // from the actor's previous placement. Ordinary particle updates retain history.
    if(HasBegunPlay())RefreshMovementBindings();
    for(const auto& Binding:MovementBindings)if(Binding.IsValid())Binding->ResetMovementHistory();
}

void UCollisionCableComponent::SnapAttachedEndpoint(bool bStart)
{
    if (!(bStart ? bAttachStart : bAttachEnd)) return;
    if (Particles.Num()!=FMath::Clamp(NumSegments, 1, 256)+1)
    {
        ResetCable();
        return;
    }

    FVector Start, End;
    GetEndPositions(Start, End);
    const FVector& Anchor = bStart ? Start : End;
    if (Anchor.ContainsNaN()) return;
    const int32 PointIndex = bStart ? 0 : Particles.Num()-1;
    const int32 SpanIndex = bStart ? 0 : PointIndex-1;
    bSocketSnapPending |= !Particles[PointIndex].Position.Equals(Anchor,.001);
    Particles[PointIndex] = FParticle();
    Particles[PointIndex].Position = Anchor;

    // Drop contact history only where the mount replaces the old endpoint.
    if (PointLineContacts.IsValidIndex(PointIndex)) PointLineContacts[PointIndex] = FLineContact();
    if (SegmentLineContacts.IsValidIndex(SpanIndex)) SegmentLineContacts[SpanIndex] = FLineContact();
    if (LastPointTracePositions.IsValidIndex(PointIndex)) LastPointTracePositions[PointIndex] = Anchor;
    if (LastSegmentTracePositions.IsValidIndex(SpanIndex))
        LastSegmentTracePositions[SpanIndex] = (Particles[SpanIndex].Position + Particles[SpanIndex+1].Position)*.5;

    RefreshCollisionParams();
    WakePoint(PointIndex);
    PublishSimulation();
    PublishNetworkSnapshot(0, true);
}

bool UCollisionCableComponent::SetCableParticleLocations(const TArray<FVector>& Locations)
{
    if (Locations.Num() != FMath::Clamp(NumSegments, 1, 256) + 1) return false;
    for (const FVector& Location : Locations) if (Location.ContainsNaN()) return false;
    Particles.SetNum(Locations.Num());
    SelfContacts.Reset();
    for (int32 I = 0; I < Locations.Num(); ++I) { Particles[I] = FParticle(); Particles[I].Position = Locations[I]; }
    bCableSleeping=false;SleepingPointCount=0;
    if(IsRegistered())Super::SetComponentTickEnabled(true);
    LastClearPositions.Reset();
    RefreshCollisionParams();
    // Smooth mode discovers and recovers embedded points through the ordinary
    // budgeted queries. Explicit resets must not silently snap them outside.
    if (!bSmoothCollisionRecovery) RecoverContacts();
    ResetCollisionHistory();
    bHasUnresolvedCollision = !IsCableCollisionFree();
    if (!bHasUnresolvedCollision) GetCableParticleLocations(LastClearPositions);
    PublishSimulation();
    return true;
}

void UCollisionCableComponent::GetCableParticleLocations(TArray<FVector>& Locations) const
{
    Locations.Reset(Particles.Num());
    for (const FParticle& Particle : Particles) Locations.Add(Particle.Position);
}

void UCollisionCableComponent::ResetCollisionHistory()
{
    PointLineContacts.SetNum(Particles.Num());
    SegmentLineContacts.SetNum(FMath::Max(0, Particles.Num() - 1));
    for (FLineContact& Contact : PointLineContacts) Contact = FLineContact();
    for (FLineContact& Contact : SegmentLineContacts) Contact = FLineContact();
    GetCableParticleLocations(LastPointTracePositions);
    LastSegmentTracePositions.SetNum(SegmentLineContacts.Num());
    for (int32 I = 0; I < LastSegmentTracePositions.Num(); ++I)
        LastSegmentTracePositions[I] = (Particles[I].Position + Particles[I + 1].Position) * .5;
    NextLineTracePoint = 0;
}

bool UCollisionCableComponent::QueryPointMotion(const FVector& From, const FVector& To, FHitResult& Hit, const FCollisionQueryParams* ParamsOverride)
{
    if (!HasCollision()) return false;
    const FCollisionQueryParams& Params = ParamsOverride ? *ParamsOverride : QueryParams;
    if (!bUseFullCollisionQueries && CollisionMethod == ECableCollisionMethod::LineTraces)
    {
        // A short stationary probe also detects a line starting inside a solid.
        const FVector Direction = (To - From).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        ++CollisionQueryCount;
        // Extend the centreline by the visible radius to catch approaching floors
        // before the particle centre has already crossed them.
        return GetWorld()->LineTraceSingleByChannel(Hit, From, To + Direction * CollisionRadius(),
            GetCollisionObjectType(), Params, ResponseParams);
    }
    ++CollisionQueryCount;
    return GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, GetCollisionObjectType(),
        FCollisionShape::MakeSphere(CollisionRadius()), Params, ResponseParams);
}

bool UCollisionCableComponent::RefreshLineContact(const FVector& Position, FLineContact& Contact, const FCollisionQueryParams* ParamsOverride)
{
    if (Contact.Normal.IsNearlyZero() || !Contact.Component.IsValid()) return false;
    const double Distance = FVector::DotProduct(Position - Contact.Point, Contact.Normal);
    const double Reach = FMath::Max(double(CollisionRadius() * 2), CollisionRadius() - Distance + .1);
    FHitResult Hit;
    // Validate the cached surface itself even after the rope lifts away. A
    // short probe around the particle would discard a still-valid floor plane
    // and allow a fall through it before this point's next budgeted turn.
    if (QueryPointMotion(Position + Contact.Normal * Reach, Position - Contact.Normal * (Distance + CollisionRadius()), Hit, ParamsOverride) &&
        !Hit.bStartPenetrating && !Hit.ImpactNormal.IsNearlyZero())
    {
        Contact.Point = Hit.ImpactPoint; Contact.Normal = Hit.ImpactNormal; Contact.Component = Hit.GetComponent();
        return true;
    }
    Contact.Normal = FVector::ZeroVector;
    return false;
}

bool UCollisionCableComponent::MakePenetrationContact(const FVector& From, const FVector& Position, const FHitResult& Hit, FLineContact& Contact)
{
    UPrimitiveComponent* Component = Hit.GetComponent();
    if (!Component) return false;
    if ((bSmoothCollisionRecovery && bUseBoundsCenterRecovery) || (!bUseFullCollisionQueries && CollisionMethod == ECableCollisionMethod::LineTraces))
    {
        // Start outside the hit component's bounds and trace back towards the
        // embedded point. This supplies an actual surface/depth for line mode,
        // which cannot provide a reliable sphere MTD from an inside-start ray.
        const FVector Fallback = Hit.Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        const FVector Direction = (Position - Component->Bounds.Origin).GetSafeNormal(UE_SMALL_NUMBER, Fallback);
        const FVector Outside = Component->Bounds.Origin + Direction * (Component->Bounds.BoxExtent.Size() + 2 * CollisionRadius() + 1);
        FHitResult Surface;
        ++CollisionQueryCount;
        if (Component->LineTraceComponent(Surface, Outside, Position - Direction * CollisionRadius(), QueryParams) &&
            !Surface.bStartPenetrating && !Surface.ImpactNormal.IsNearlyZero())
        {
            Contact.Point = Surface.ImpactPoint; Contact.Normal = Surface.ImpactNormal.GetSafeNormal(); Contact.Component = Component;
            return true;
        }
    }
    if ((bUseFullCollisionQueries || CollisionMethod == ECableCollisionMethod::Sweeps) && !Hit.Normal.IsNearlyZero())
    {
        Contact.Normal = Hit.Normal.GetSafeNormal(); Contact.Component = Component;
        Contact.Point = From + Contact.Normal * (Hit.PenetrationDepth - CollisionRadius());
        return true;
    }
    return false;
}

void UCollisionCableComponent::MoveContactParticle(int32 Index, const FVector& Correction, const FLineContact& Contact)
{
    if (!bSmoothCollisionRecovery)
    {
        Particles[Index].Position += Correction;
        return;
    }
    if (!RecoveryDistanceUsed.IsValidIndex(Index) || RecoveryDeltaTime <= 0) return;
    const double NormalDistance = FVector::DotProduct(Correction, Contact.Normal);
    if (NormalDistance <= 0) return;
    FVector Direction = Contact.Normal;
    if (bUseBoundsCenterRecovery && Contact.Component.IsValid())
    {
        const FVector Radial = (Particles[Index].Position - Contact.Component->Bounds.Origin).GetSafeNormal();
        // A center direction almost parallel to a broad floor would pull the
        // cable sideways. Keep an outward surface direction in that case.
        if (FVector::DotProduct(Radial, Contact.Normal) >= .25) Direction = Radial;
    }
    const double TargetDistance = NormalDistance / FMath::Max(.25, FVector::DotProduct(Direction, Contact.Normal));
    const double MaximumSpeed = FMath::Max(1.f, MaxCollisionRecoverySpeed);
    const double MinimumSpeed = FMath::Clamp(double(MinCollisionRecoverySpeed), 0., MaximumSpeed);
    const double Speed = FMath::Clamp(TargetDistance * FMath::Max(.1f, CollisionRecoveryRate), MinimumSpeed, MaximumSpeed);
    RecoveryDistanceLimit[Index] = FMath::Max(RecoveryDistanceLimit[Index], Speed * RecoveryDeltaTime);
    const double Step = FMath::Min(TargetDistance, FMath::Max(0., RecoveryDistanceLimit[Index] - RecoveryDistanceUsed[Index]));
    const FVector Move = Direction * Step;
    Particles[Index].Position += Move;
    Particles[Index].ContactCorrectionThisStep += Move;
    RecoveryDistanceUsed[Index] += Step;
    MaxRecoveryDistanceThisFrame = FMath::Max(MaxRecoveryDistanceThisFrame, float(RecoveryDistanceUsed[Index]));
}

FVector UCollisionCableComponent::ConstrainMotionToKnownContacts(int32 Index, FVector Motion) const
{
    if (!bSmoothCollisionRecovery || !HasCollision()) return Motion;
    auto LimitInward = [&](const FLineContact& Contact)
    {
        if (Contact.Normal.IsNearlyZero() || !Contact.Component.IsValid()) return;
        const double Distance = FVector::DotProduct(Particles[Index].Position - Contact.Point, Contact.Normal);
        const double Minimum = FMath::Min(Distance, double(CollisionRadius()));
        const double Predicted = Distance + FVector::DotProduct(Motion, Contact.Normal);
        // Stop new integration/length corrections from deepening a known
        // overlap while its existing penetration is being eased out.
        if (Predicted < Minimum) Motion += Contact.Normal * (Minimum - Predicted);
    };
    if (PointLineContacts.IsValidIndex(Index)) LimitInward(PointLineContacts[Index]);
    if (SegmentLineContacts.IsValidIndex(Index - 1)) LimitInward(SegmentLineContacts[Index - 1]);
    if (SegmentLineContacts.IsValidIndex(Index)) LimitInward(SegmentLineContacts[Index]);
    return Motion;
}

void UCollisionCableComponent::UpdateKnownCollisionStatus()
{
    bHasUnresolvedCollision = false;
    if (!HasCollision()) return;
    for (int32 I = 0; I < PointLineContacts.Num(); ++I)
    {
        const FLineContact& Contact = PointLineContacts[I];
        if (Contact.Component.IsValid() && !Contact.Normal.IsNearlyZero() &&
            FVector::DotProduct(Particles[I].Position - Contact.Point, Contact.Normal) < CollisionRadius() - .005)
            bHasUnresolvedCollision = true;
    }
    for (int32 I = 0; I < SegmentLineContacts.Num(); ++I)
    {
        const FLineContact& Contact = SegmentLineContacts[I];
        if (Contact.Component.IsValid() && !Contact.Normal.IsNearlyZero() &&
            FVector::DotProduct(FMath::Lerp(Particles[I].Position, Particles[I + 1].Position, Contact.Alpha) - Contact.Point, Contact.Normal) < CollisionRadius() - .005)
            bHasUnresolvedCollision = true;
    }
}

void UCollisionCableComponent::ProjectLinePoint(int32 Index, const FLineContact& Contact)
{
    if (Contact.Normal.IsNearlyZero() || !Contact.Component.IsValid() || GetSocketMountOwner(Index)
        || (IsPinned(Index) && !bSweepAttachedEndpoints && !bUseFullCollisionQueries)) return;
    const double Penetration = CollisionRadius() - FVector::DotProduct(Particles[Index].Position - Contact.Point, Contact.Normal);
    if (Penetration > 0)
    {
        MoveContactParticle(Index, Contact.Normal * (Penetration + .001), Contact);
        Particles[Index].ContactNormal += Contact.Normal;
    }
}

void UCollisionCableComponent::ProjectLineSegment(int32 Index, const FLineContact& Contact)
{
    if (Contact.Normal.IsNearlyZero() || !Contact.Component.IsValid()) return;
    if (const AActor* Surface = Contact.Component->GetOwner(); Surface &&
        (Surface==GetSocketMountOwner(Index) || Surface==GetSocketMountOwner(Index+1))) return;
    const double T = FMath::Clamp(Contact.Alpha, 0., 1.);
    const FVector Position = FMath::Lerp(Particles[Index].Position, Particles[Index + 1].Position, T);
    const double Penetration = CollisionRadius() - FVector::DotProduct(Position - Contact.Point, Contact.Normal);
    if (Penetration <= 0) return;
    const double WA = InversePointMass(Index), WB = InversePointMass(Index+1);
    const double Denominator = WA * FMath::Square(1 - T) + WB * FMath::Square(T);
    if (Denominator < UE_SMALL_NUMBER) { bHasUnresolvedCollision = true; return; }
    const FVector Correction = Contact.Normal * (Penetration + .001) / Denominator;
    if (WA > 0) { MoveContactParticle(Index, Correction * (1 - T), Contact); Particles[Index].ContactNormal += Contact.Normal; }
    if (WB > 0) { MoveContactParticle(Index + 1, Correction * T, Contact); Particles[Index + 1].ContactNormal += Contact.Normal; }
}

void UCollisionCableComponent::ProjectLineContacts()
{
    if (!HasCollision()) return;
    // Cached contact projection is arithmetic only. No world queries here.
    for (int32 I = 0; I < PointLineContacts.Num(); ++I) ProjectLinePoint(I, PointLineContacts[I]);
    for (int32 I = 0; I < SegmentLineContacts.Num(); ++I) ProjectLineSegment(I, SegmentLineContacts[I]);
}

void UCollisionCableComponent::CheckCollisionPointBatch(const FVector& StartTarget, const FVector& EndTarget)
{
    if (!HasCollision() || Particles.IsEmpty()) return;
    CollisionPointsPerFrame = FMath::Clamp(CollisionPointsPerFrame, 1, Particles.Num());
    const int32 PointBudget=bUseFullCollisionQueries?Particles.Num():CollisionPointsPerFrame;
    TSet<int32> CheckedSegments;
    auto StoreHit = [](FLineContact& Contact, const FHitResult& Hit)
    { Contact.Point = Hit.ImpactPoint; Contact.Normal = Hit.ImpactNormal.GetSafeNormal(); Contact.Component = Hit.GetComponent(); };
    for (int32 Sample = 0; Sample < PointBudget; ++Sample)
    {
        const int32 I = NextLineTracePoint;
        NextLineTracePoint = (NextLineTracePoint + 1) % Particles.Num();
        if(bEnableSleeping && Particles[I].bSleeping && (!bUseFullCollisionQueries ||
            ((I==0||Particles[I-1].bSleeping)&&(I+1==Particles.Num()||Particles[I+1].bSleeping))))continue;
        LastCollisionPointIndices.AddUnique(I);
        FLineContact& Contact = PointLineContacts[I];
        const bool bSocketMounted = GetSocketMountOwner(I)!=nullptr;
        if (!bSocketMounted && RefreshLineContact(Particles[I].Position, Contact)) ProjectLinePoint(I, Contact);
        if (!bSocketMounted && (!IsPinned(I) || bSweepAttachedEndpoints || bUseFullCollisionQueries))
        {
            FHitResult Hit;
            const FVector From = LastPointTracePositions[I];
            if (QueryPointMotion(From, Particles[I].Position, Hit))
            {
                if (Hit.bStartPenetrating)
                {
                    // A moved collider can embed several adjacent samples. Its
                    // individual MTDs can choose opposite sides. Keep a side
                    // already established for this collider on a nearby span.
                    bool ReusedContact = false;
                    for (int32 Adjacent : {I - 1, I})
                    {
                        if (SegmentLineContacts.IsValidIndex(Adjacent) &&
                            SegmentLineContacts[Adjacent].Component.Get() == Hit.GetComponent() &&
                            !SegmentLineContacts[Adjacent].Normal.IsNearlyZero())
                        {
                            Contact = SegmentLineContacts[Adjacent];
                            ProjectLinePoint(I, Contact); ReusedContact = true; break;
                        }
                    }
                    if (!ReusedContact && MakePenetrationContact(From, Particles[I].Position, Hit, Contact))
                        ProjectLinePoint(I, Contact);
                }
                else if (!Hit.bStartPenetrating)
                { StoreHit(Contact, Hit); ProjectLinePoint(I, Contact); }
            }
        }
        const int32 Edge = FMath::Min(I, Particles.Num() - 2);
        if (Edge < 0 || CheckedSegments.Contains(Edge)) continue;
        CheckedSegments.Add(Edge);
        TOptional<FCollisionQueryParams> LocalParams;
        const FCollisionQueryParams& SpanParams = GetSpanCollisionParams(Edge, LocalParams);
        FLineContact& SegmentContact = SegmentLineContacts[Edge];
        FVector Midpoint = (Particles[Edge].Position + Particles[Edge + 1].Position) * .5;
        if (RefreshLineContact(FMath::Lerp(Particles[Edge].Position, Particles[Edge + 1].Position, SegmentContact.Alpha), SegmentContact, &SpanParams))
            ProjectLineSegment(Edge, SegmentContact);
        FHitResult MidHit;
        if (QueryPointMotion(LastSegmentTracePositions[Edge], Midpoint, MidHit, &SpanParams))
        {
            const bool Valid = !MidHit.bStartPenetrating || (bSmoothCollisionRecovery && MakePenetrationContact(LastSegmentTracePositions[Edge], Midpoint, MidHit, SegmentContact));
            if (!MidHit.bStartPenetrating) StoreHit(SegmentContact, MidHit);
            if (Valid) { SegmentContact.Alpha = .5; ProjectLineSegment(Edge, SegmentContact); }
        }
        if (!bUseFullCollisionQueries && CollisionMethod == ECableCollisionMethod::LineTraces)
        {
            FHitResult Hit;
            ++CollisionQueryCount;
            if (GetWorld()->LineTraceSingleByChannel(Hit, Particles[Edge].Position, Particles[Edge + 1].Position,
                GetCollisionObjectType(), SpanParams, ResponseParams))
            {
                if (Hit.bStartPenetrating)
                {
                    if (MakePenetrationContact(Particles[Edge].Position, (Particles[Edge].Position + Particles[Edge + 1].Position) * .5, Hit, SegmentContact))
                    { SegmentContact.Alpha = .5; ProjectLineSegment(Edge, SegmentContact); }
                }
                else
                {
                    StoreHit(SegmentContact, Hit); SegmentContact.Alpha = FMath::Clamp(double(Hit.Time), .05, .95);
                    const FVector Previous = FMath::Lerp(LastPointTracePositions[Edge], LastPointTracePositions[Edge + 1], SegmentContact.Alpha);
                    if (FVector::DotProduct(Previous - SegmentContact.Point, SegmentContact.Normal) < 0) SegmentContact.Normal *= -1;
                    ProjectLineSegment(Edge, SegmentContact);
                }
            }
        }
        else
        {
            // Only this budgeted span gets a volume query. Never run collision
            // for every distance-constraint correction or for unselected points.
            for (int32 Pass = 0; Pass < FMath::Clamp(CollisionRecoveryIterations, 1, 16); ++Pass)
            {
                const FVector A = Particles[Edge].Position, B = Particles[Edge + 1].Position;
                FQuat Rotation;
                const FCollisionShape Shape = CollisionCable::SegmentShape(A, B, CollisionRadius(), Rotation);
                const FVector Center = (A + B) * .5;
                TArray<FHitResult> Hits;
                ++CollisionQueryCount;
                GetWorld()->SweepMultiByChannel(Hits, Center, Center, Rotation, GetCollisionObjectType(), Shape, SpanParams, ResponseParams);
                bool Recovered = false;
                for (const FHitResult& Hit : Hits)
                {
                    if (!Hit.bBlockingHit || !Hit.bStartPenetrating || Hit.PenetrationDepth <= .0001f) continue;
                    const double LengthSq = FVector::DistSquared(A, B);
                    SegmentContact.Alpha = LengthSq > UE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(Hit.ImpactPoint - A, B - A) / LengthSq, .05, .95) : .5;
                    const FVector AtContact = FMath::Lerp(A, B, SegmentContact.Alpha);
                    const FLineContact* Existing = nullptr;
                    for (int32 End : {Edge, Edge + 1})
                        if (PointLineContacts[End].Component.Get() == Hit.GetComponent() && !PointLineContacts[End].Normal.IsNearlyZero())
                        { Existing = &PointLineContacts[End]; break; }
                    if (Existing)
                    {
                        SegmentContact.Point = Existing->Point; SegmentContact.Normal = Existing->Normal;
                    }
                    else
                    {
                        SegmentContact.Normal = Hit.Normal;
                        SegmentContact.Point = AtContact + Hit.Normal * (Hit.PenetrationDepth - CollisionRadius());
                    }
                    SegmentContact.Component = Hit.GetComponent();
                    ProjectLineSegment(Edge, SegmentContact);
                    Recovered = !Particles[Edge].Position.Equals(A, .00001) || !Particles[Edge + 1].Position.Equals(B, .00001);
                    break;
                }
                if (!Recovered) break;
            }
        }
    }
    ProjectLineContacts();
    UpdateKnownCollisionStatus();
    for (int32 I : LastCollisionPointIndices) LastPointTracePositions[I] = Particles[I].Position;
    for (int32 Edge : CheckedSegments) LastSegmentTracePositions[Edge] = (Particles[Edge].Position + Particles[Edge + 1].Position) * .5;
    if(!bUseFullCollisionQueries)ApplyCollisionVelocityResponse();
    bAttachmentBlocked = (bAttachStart && !Particles[0].Position.Equals(StartTarget, .1)) ||
        (bAttachEnd && !Particles.Last().Position.Equals(EndTarget, .1));
}

void UCollisionCableComponent::ApplyCollisionVelocityResponse()
{
    for (FParticle& Particle : Particles)
    {
        const FVector Normal = Particle.ContactNormal.GetSafeNormal();
        const double Inward = FVector::DotProduct(Particle.Velocity, Normal);
        if (Inward < 0) Particle.Velocity -= Normal * Inward;
        // Tangential friction is solved against persistent contacts per substep,
        // rather than damping once per rendered frame only on penetrating points.
    }
}

bool UCollisionCableComponent::RecoverContacts()
{
    if (!HasCollision()) return false;
    bool bAnyContact = false;
    for (int32 Pass = 0; Pass < FMath::Clamp(CollisionRecoveryIterations, 1, 16); ++Pass)
    {
        bool bMoved = false;
        for (int32 I = 0; I + 1 < Particles.Num(); ++I)
        {
            FQuat Rotation;
            const FCollisionShape Shape = CollisionCable::SegmentShape(Particles[I].Position, Particles[I + 1].Position, CollisionRadius(), Rotation);
            const FVector Center = (Particles[I].Position + Particles[I + 1].Position) * .5;
            TArray<FHitResult> Hits;
            ++CollisionQueryCount;
            TOptional<FCollisionQueryParams> LocalParams;
            GetWorld()->SweepMultiByChannel(Hits, Center, Center, Rotation, GetCollisionObjectType(), Shape, GetSpanCollisionParams(I, LocalParams), ResponseParams);
            for (const FHitResult& Hit : Hits)
            {
                if (!Hit.bBlockingHit || !Hit.bStartPenetrating || Hit.PenetrationDepth <= .0001f) continue;
                bAnyContact = true;
                const double WA = InversePointMass(I), WB = InversePointMass(I+1);
                if (WA + WB == 0) continue;
                // Apply at the capsule's contact point. A contact near a fixed end
                // must move the other end further; a midpoint translation is insufficient.
                const FVector A = Particles[I].Position, B = Particles[I + 1].Position;
                const double LengthSq = FVector::DistSquared(A, B);
                const double T = LengthSq > UE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(Hit.ImpactPoint - A, B - A) / LengthSq, .05, .95) : .5;
                const double Denominator = WA * FMath::Square(1 - T) + WB * FMath::Square(T);
                const FVector Correction = Hit.Normal * (Hit.PenetrationDepth + .01f);
                for (int32 End = 0; End < 2; ++End)
                {
                    const int32 Index = I + End;
                    const double Weight = End == 0 ? WA * (1 - T) : WB * T;
                    if (Weight == 0) continue;
                    Particles[Index].Position += Correction * (Weight / Denominator);
                    Particles[Index].ContactNormal += Hit.Normal;
                    const double Inward = FVector::DotProduct(Particles[Index].Velocity, Hit.Normal);
                    if (Inward < 0) Particles[Index].Velocity -= Hit.Normal * Inward;
                }
                bMoved = true;
                break; // Re-query this capsule after changing it; other hits are now stale.
            }
        }
        if (!bMoved) break;
    }
    return bAnyContact;
}

bool UCollisionCableComponent::PathIsClear(const TArray<FVector>& Positions) const
{
    if (!HasCollision()) return true;
    for (int32 I = 0; I + 1 < Positions.Num(); ++I)
    {
        FQuat Rotation;
        const FCollisionShape Shape = CollisionCable::SegmentShape(Positions[I], Positions[I + 1], CollisionRadius() - .005f, Rotation);
        TOptional<FCollisionQueryParams> LocalParams;
        if (GetWorld()->OverlapBlockingTestByChannel((Positions[I] + Positions[I + 1]) * .5, Rotation, GetCollisionObjectType(),
            Shape, GetSpanCollisionParams(I, LocalParams), ResponseParams)) return false;
    }
    return true;
}

bool UCollisionCableComponent::IsCableCollisionFree() const
{
    TArray<FVector> Points; GetCableParticleLocations(Points); return PathIsClear(Points);
}

void UCollisionCableComponent::SolveDistance(int32 A, int32 B, float RestLength, float Strength)
{
    const FVector Delta = Particles[B].Position - Particles[A].Position;
    const double Length = Delta.Size();
    if(bEnableSleeping && (Particles[A].bSleeping || Particles[B].bSleeping))
    {
        const double Error=FMath::Abs(Length-RestLength);
        // A fast neighbor transfers motion even while the distance solver is
        // keeping this span close to rest length. Requiring accumulated stretch
        // here would let sleeping points absorb a pull like fixed anchors.
        auto WakeSpeed=[&](int32 Index)
        {
            FVector Velocity=Particles[Index].Velocity;
            auto ClipBlockedGravity=[&](const FLineContact& Contact)
            {
                if(!Contact.Component.IsValid()||Contact.Normal.IsNearlyZero() ||
                    FVector::DotProduct(Particles[Index].Position-Contact.Point,Contact.Normal)>CollisionRadius()+FMath::Max(.01f,WakeDisplacementThreshold))return;
                const double Inward=FVector::DotProduct(Velocity,Contact.Normal);
                if(Inward<0)Velocity-=Contact.Normal*Inward;
            };
            // In immediate-recovery mode gravity is integrated before the
            // contact projection. That temporary inward velocity is not a real
            // disturbance and must not repeatedly wake settled neighbors.
            if(PointLineContacts.IsValidIndex(Index))ClipBlockedGravity(PointLineContacts[Index]);
            if(SegmentLineContacts.IsValidIndex(Index-1))ClipBlockedGravity(SegmentLineContacts[Index-1]);
            if(SegmentLineContacts.IsValidIndex(Index))ClipBlockedGravity(SegmentLineContacts[Index]);
            return Velocity.Size();
        };
        const double SleepTolerance=FMath::Max(.01f,SleepConstraintTolerance);
        const double WakeStrain=FMath::Max(.005,FMath::Min(double(WakeDisplacementThreshold),SleepTolerance*.1));
        double StrainChange=0;
        if(B==A+1)
        {
            if(Particles[A].bSleeping && Particles[A].SleepNextSpanLength>=0)
                StrainChange=FMath::Max(StrainChange,FMath::Abs(Length-Particles[A].SleepNextSpanLength));
            if(Particles[B].bSleeping && Particles[B].SleepPreviousSpanLength>=0)
                StrainChange=FMath::Max(StrainChange,FMath::Abs(Length-Particles[B].SleepPreviousSpanLength));
        }
        // A supported span may sleep with a small accepted residual error. Wake
        // on a new strain change, not the same error it already had at rest.
        if(Error>SleepTolerance || StrainChange>WakeStrain ||
             FMath::Max(WakeSpeed(A),WakeSpeed(B))>FMath::Max(WakeThreshold,SleepVelocityThreshold*1.5f))
        {if(Particles[A].bSleeping)WakePoint(A,false);if(Particles[B].bSleeping)WakePoint(B,false);}
    }
    const double WA = bEnableSleeping && Particles[A].bSleeping ? 0. : InversePointMass(A);
    const double WB = bEnableSleeping && Particles[B].bSleeping ? 0. : InversePointMass(B);
    if (WA + WB == 0) return;
    const FVector Correction = Delta.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector) * ((Length - RestLength) * Strength / (WA + WB));
    if (WA > 0) Particles[A].Position += ConstrainFullCollisionMotion(A, Correction * WA);
    if (WB > 0) Particles[B].Position += ConstrainFullCollisionMotion(B, -Correction * WB);
}

void UCollisionCableComponent::Simulate(float Dt, const FVector& StartTarget, const FVector& EndTarget)
{
    for (FParticle& Particle : Particles)
    { Particle.ContactNormal = FVector::ZeroVector; Particle.ContactCorrectionThisStep = FVector::ZeroVector; Particle.SelfContactCorrectionThisStep = FVector::ZeroVector; }
    auto MoveAnchor = [&](int32 Index, const FVector& Target)
    {
        if (!((Index==0 && bAttachStart)||(Index==Particles.Num()-1 && bAttachEnd))) return;
        if(!Particles[Index].Position.Equals(Target,FMath::Max(.01f,WakeDisplacementThreshold)))WakePoint(Index);
        const bool bSweep = !GetSocketMountOwner(Index) && (bSweepAttachedEndpoints || bUseFullCollisionQueries);
        Particles[Index].Position += bSweep ? ConstrainFullCollisionMotion(Index, Target - Particles[Index].Position) : Target - Particles[Index].Position;
        Particles[Index].Velocity = FVector::ZeroVector;
    };
    MoveAnchor(0, StartTarget); MoveAnchor(Particles.Num() - 1, EndTarget);
    TArray<FVector> Before; GetCableParticleLocations(Before);
    const FVector Acceleration = CableForce + FVector(0, 0, GetWorld()->GetGravityZ() * CableGravityScale);
    const float Drag = FMath::Exp(-FMath::Max(Damping, 0.f) * Dt);
    for (int32 I = 0; I < Particles.Num(); ++I)
    {
        if (IsPinned(I) || (bEnableSleeping && Particles[I].bSleeping)) continue;
        Particles[I].Velocity = Particles[I].Velocity * Drag + Acceleration * Dt;
    }
    ApplyGrabDrive(Dt);
    for(int32 I=0;I<Particles.Num();++I)
    {
        if(IsPinned(I)||(bEnableSleeping && Particles[I].bSleeping))continue;
        Particles[I].Velocity = ConstrainFullCollisionMotion(I, Particles[I].Velocity * Dt) / Dt;
        Particles[I].Position += Particles[I].Velocity * Dt;
    }
    ProjectLineContacts();
    const float RestLength = FMath::Max(CableLength, 1.f) / (Particles.Num() - 1);
    for (int32 Iteration = 0; Iteration < FMath::Clamp(SolverIterations, 1, 32); ++Iteration)
    {
        // Alternate traversal direction to avoid favouring either attachment.
        for (int32 J = 0; J < Particles.Num() - 1; ++J)
        {
            const int32 I = Iteration % 2 ? Particles.Num() - 2 - J : J;
            SolveDistance(I, I + 1, RestLength, 1);
        }
        if (bEnableStiffness)
            for (int32 I = 0; I + 2 < Particles.Num(); ++I) SolveDistance(I, I + 2, 2 * RestLength, FMath::Clamp(Stiffness, 0.f, 1.f));
        ProjectLineContacts();
        if(bUseFullCollisionQueries && ((Iteration+1)%FMath::Clamp(FullCollisionSolverInterval,1,32)==0 || Iteration+1==FMath::Clamp(SolverIterations,1,32)))
            CheckCollisionPointBatch(StartTarget,EndTarget);
    }
    SolveSelfCollision(Before);
    ProjectLineContacts();
    if(bUseFullCollisionQueries)CheckCollisionPointBatch(StartTarget,EndTarget);
    TArray<FSurfaceFrictionBudget> FrictionBudgets;
    ApplyContactFriction(Dt,Before,FrictionBudgets);
    ApplySelfContactFriction(Before);
    ApplyContactFriction(Dt,Before,FrictionBudgets);
    ProjectLineContacts();
    for (int32 I = 0; I < Particles.Num(); ++I)
    {
        if (IsPinned(I) || (bEnableSleeping && Particles[I].bSleeping)) continue;
        FParticle& Particle = Particles[I];
        const FVector IntegratedVelocity = Particle.Velocity;
        const FVector RecoveryMove = (bSmoothCollisionRecovery ? Particle.ContactCorrectionThisStep : FVector::ZeroVector) + Particle.SelfContactCorrectionThisStep;
        // Resolve the new shape without sending the position correction down the hose as a velocity spike.
        Particle.Velocity = bSocketSnapPending ? IntegratedVelocity : (Particle.Position - Before[I] - RecoveryMove) / Dt;
        const FVector Normal = Particle.ContactNormal.GetSafeNormal();
        if (!Normal.IsNearlyZero())
        {
            const double Inward = FVector::DotProduct(Particle.Velocity, Normal);
            if (Inward < 0) Particle.Velocity -= Normal * Inward;
            if (bSmoothCollisionRecovery)
            {
                // Neighbor length corrections can transmit a recovery move to
                // this point too. Do not turn that positional repair into an
                // outward launch velocity; retain actual integrated motion.
                const double Outward = FVector::DotProduct(Particle.Velocity, Normal);
                const double PhysicalOutward = FMath::Max(0., FVector::DotProduct(IntegratedVelocity, Normal));
                if (Outward > PhysicalOutward) Particle.Velocity -= Normal * (Outward - PhysicalOutward);
            }
        }
    }
    ApplySelfCollisionVelocityResponse();
}

void UCollisionCableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!GetWorld() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0) return;
    const double StartTime = FPlatformTime::Seconds();
    if(!HasSimulationAuthority())
    {
        if(ClientSnapshotTargets.Num()==Particles.Num())
        {
            const double Alpha=1-FMath::Exp(-FMath::Max(1.f,SnapshotInterpolationRate)*DeltaTime);
            for(int32 I=0;I<Particles.Num();++I)Particles[I].Position=FMath::Lerp(Particles[I].Position,ClientSnapshotTargets[I],Alpha);
            PublishSimulation();
        }
        CollisionQueryCount=0;LastCollisionPointIndices.Reset();return;
    }
    if(FeatureSignature()!=LastFeatureSignature)OnRep_CableFeatures();
    if(bCableSleeping && bEnableSleeping){CollisionQueryCount=0;RenderCollisionQueryCount=0;LastCollisionPointIndices.Reset();return;}
    TArray<FVector> BeforeTick;GetCableParticleLocations(BeforeTick);
    RefreshCollisionParams();
    if (Particles.Num() != FMath::Clamp(NumSegments, 1, 256) + 1) ResetCable();
    // Both query methods use the same planes and motion history. Preserve them
    // on a method switch so unchecked resting points retain their floor support.
    CollisionPointsPerFrame = FMath::Clamp(CollisionPointsPerFrame, 1, Particles.Num());
    CollisionQueryCount = 0; LastCollisionPointIndices.Reset();
    bAttachmentBlocked = false; bHasUnresolvedCollision = false;
    SelfCollisionPairChecks=0;
    FVector Start, End; GetEndPositions(Start, End);
    const FVector PreviousStart = Particles[0].Position, PreviousEnd = Particles.Last().Position;
    const float MaxDt = FMath::Clamp(SubstepTime, .001f, 1.f / 30.f);
    const float SimTime = FMath::Min(DeltaTime, MaxDt * FMath::Clamp(MaxSubsteps, 1, 32));
    RecoveryDeltaTime = SimTime;
    RecoveryDistanceUsed.Init(0., Particles.Num());
    RecoveryDistanceLimit.Init(0., Particles.Num());
    MaxRecoveryDistanceThisFrame = 0;
    DroppedSimulationTime += DeltaTime - SimTime;
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(SimTime / MaxDt));
    for (int32 Step = 0; Step < Steps; ++Step)
    {
        const double Alpha = double(Step + 1) / Steps;
        Simulate(SimTime / Steps, FMath::Lerp(PreviousStart, Start, Alpha), FMath::Lerp(PreviousEnd, End, Alpha));
    }
    bSocketSnapPending = false;
    // Budgeted mode issues one rotating batch per component tick. Full mode
    // checks all awake points/spans inside substeps and solver passes instead.
    if(!bUseFullCollisionQueries)CheckCollisionPointBatch(Start, End);
    else ApplyCollisionVelocityResponse();
    ApplyAttachmentForces(SimTime);
    UpdateSleeping(SimTime,BeforeTick);
    PublishSimulation();
    PublishNetworkSnapshot(SimTime);
    LastSimulationTimeMs = float((FPlatformTime::Seconds() - StartTime) * 1000.);
}

void UCollisionCableComponent::PublishSimulation()
{
    TArray<FVector> Points;BuildCableRenderLocations(Points);
    const uint32 Signature=FeatureSignature();
    bool InputChanged=Signature!=PublishedRenderSignature || Points.Num()!=RenderInputPoints.Num();
    if(!InputChanged)for(int32 I=0;I<Points.Num();++I)if(!Points[I].Equals(RenderInputPoints[I],.0001)){InputChanged=true;break;}
    if(InputChanged)RenderInputPoints=Points;
    RenderCollisionQueryCount=0;
    if(bUseFullCollisionQueries && HasCollision() && (InputChanged || !bCableSleeping))ConstrainRenderPath(Points);
    else if(bUseFullCollisionQueries && !InputChanged && !PublishedPoints.IsEmpty())Points=PublishedPoints;
    bool Changed=Points.Num()!=PublishedPoints.Num() || !PublishedRenderOrigin.Equals(GetComponentLocation(),.0001) || PublishedWidth!=CableWidth || PublishedTiling!=TileMaterial ||
        PublishedSubdivisions!=(bEnableVisualSubdivision?VisualSubdivisions:1);
    if(!Changed)for(int32 I=0;I<Points.Num();++I)if(!Points[I].Equals(PublishedPoints[I],.0001)){Changed=true;break;}
    PublishedRenderSignature=Signature;
    if(!Changed)return;
    PublishedPoints=MoveTemp(Points);PublishedRenderOrigin=GetComponentLocation();PublishedWidth=CableWidth;PublishedTiling=TileMaterial;
    PublishedSubdivisions=bEnableVisualSubdivision?VisualSubdivisions:1;++RenderGeometryRevision;
    UpdateBounds(); MarkRenderTransformDirty(); MarkRenderDynamicDataDirty();
    if(HasBegunPlay())RefreshWakeSources();
}

FPrimitiveSceneProxy* UCollisionCableComponent::CreateSceneProxy() { return CollisionCableRendering::CreateProxy(this); }

void UCollisionCableComponent::SetHoseVisualState(float DiameterScale,bool bCenterRuptured)
{
    if (!FMath::IsFinite(DiameterScale)) return;
    DiameterScale=FMath::Clamp(DiameterScale,.1f,3.f);
    if (FMath::IsNearlyEqual(VisualDiameterScale,DiameterScale,.005f) && bVisualCenterRuptured==bCenterRuptured) return;
    VisualDiameterScale=DiameterScale;
    bVisualCenterRuptured=bCenterRuptured;
    ++RenderGeometryRevision;
    UpdateBounds(); MarkRenderTransformDirty(); MarkRenderDynamicDataDirty();
}

void UCollisionCableComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
    Super::CreateRenderState_Concurrent(Context); SendRenderDynamicData_Concurrent();
}

void UCollisionCableComponent::SendRenderDynamicData_Concurrent()
{
    Super::SendRenderDynamicData_Concurrent();
    if (!SceneProxy) return;
    TArray<FVector> Points;
    CollisionCableRendering::GetLocalRenderLocations(*this, Points);
    auto* Proxy = SceneProxy;
    const float Width = GetRenderingDiameter(), Tiling = TileMaterial;
    const bool CenterRuptured = bVisualCenterRuptured;
    ENQUEUE_RENDER_COMMAND(UpdateCollisionCable)([Proxy, Points = MoveTemp(Points), Width, Tiling, CenterRuptured](FRHICommandListImmediate& Commands) mutable
    { CollisionCableRendering::UpdateProxy(Commands,Proxy,MoveTemp(Points),Width,Tiling,CenterRuptured); });
}

FBoxSphereBounds UCollisionCableComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    const FTransform& CableToWorld = GetComponentTransform();
    FBox WorldBox(ForceInit);
    for (const FParticle& Particle : Particles) WorldBox += Particle.Position;
    for (const FVector& Point : PublishedPoints) WorldBox += Point;
    if (!WorldBox.IsValid) WorldBox += CableToWorld.GetLocation();

    const double TubeRadius = 0.5 * GetRenderingDiameter() * CableToWorld.GetScale3D().GetAbsMax();
    WorldBox = WorldBox.ExpandBy(FMath::Max<double>(CollisionRadius(), TubeRadius));
    if (LocalToWorld.Equals(CableToWorld)) return FBoxSphereBounds(WorldBox);

    // The simulation stores world positions. The renderer also requests bounds with
    // the identity transform, which must return a box in the cable's local space.
    const FBox LocalBox = WorldBox.InverseTransformBy(CableToWorld);
    return FBoxSphereBounds(LocalBox.TransformBy(LocalToWorld));
}

void UCollisionCableComponent::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
    Super::ApplyWorldOffset(InOffset, bWorldShift);
    for (FParticle& Particle : Particles) Particle.Position += InOffset;
    for (FVector& Position : LastClearPositions) Position += InOffset;
    for (FVector& Position : LastPointTracePositions) Position += InOffset;
    for (FVector& Position : LastSegmentTracePositions) Position += InOffset;
    for (FLineContact& Contact : PointLineContacts) Contact.Point += InOffset;
    for (FLineContact& Contact : SegmentLineContacts) Contact.Point += InOffset;
    for (FVector& Target : ClientSnapshotTargets) Target += InOffset;
    for (auto& Position : SimulationSnapshot.Positions) Position = FVector(Position) + InOffset;
    for (auto& Grab : ActiveGrabs) Grab.Target = FVector(Grab.Target) + InOffset;
    for (auto& Grab : SimulationSnapshot.Grabs) Grab.Target = FVector(Grab.Target) + InOffset;
    LastWakeStart += InOffset; LastWakeEnd += InOffset;
    for (auto& Pair : ObservedTransforms) Pair.Value.AddToTranslation(InOffset);
    PublishSimulation();
}

bool UCollisionCableComponent::DoesSocketExist(FName SocketName) const
{
    return SocketName == CollisionCable::StartSocket || SocketName == CollisionCable::EndSocket;
}

void UCollisionCableComponent::QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const
{
    OutSockets.Add(FComponentSocketDescription(CollisionCable::StartSocket, EComponentSocketType::Socket));
    OutSockets.Add(FComponentSocketDescription(CollisionCable::EndSocket, EComponentSocketType::Socket));
}

FTransform UCollisionCableComponent::GetSocketTransform(FName SocketName, ERelativeTransformSpace Space) const
{
    if (!DoesSocketExist(SocketName) || Particles.IsEmpty()) return Super::GetSocketTransform(SocketName, Space);
    FTransform Result(GetComponentQuat(), SocketName == CollisionCable::StartSocket ? Particles[0].Position : Particles.Last().Position);
    if (Space == RTS_Component) Result = Result.GetRelativeTransform(GetComponentTransform());
    else if (Space == RTS_Actor && GetOwner()) Result = Result.GetRelativeTransform(GetOwner()->GetActorTransform());
    return Result;
}
