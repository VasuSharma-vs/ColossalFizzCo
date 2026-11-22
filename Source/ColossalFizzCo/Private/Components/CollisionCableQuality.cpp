#include "Components/CollisionCableComponent.h"
#include "Engine/World.h"

FVector UCollisionCableComponent::ConstrainFullCollisionMotion(int32 Index,FVector Motion)
{
    Motion=ConstrainMotionToKnownContacts(Index,Motion);
    if(!bUseFullCollisionQueries || !HasCollision() || !Particles.IsValidIndex(Index) || Motion.IsNearlyZero(.000001))return Motion;
    const FVector Start=Particles[Index].Position;
    FHitResult Hit;
    if(!QueryPointMotion(Start,Start+Motion,Hit))return Motion;
    auto& Contact=PointLineContacts[Index];
    if(Hit.bStartPenetrating)
    {
        if(Contact.Component.Get()!=Hit.GetComponent() || Contact.Normal.IsNearlyZero())
            MakePenetrationContact(Start,Start,Hit,Contact);
        if(!Contact.Normal.IsNearlyZero())
        {
            const double Inward=FVector::DotProduct(Motion,Contact.Normal);
            if(Inward<0)Motion-=Contact.Normal*Inward;
        }
        return Motion;
    }
    Contact.Point=Hit.ImpactPoint;Contact.Normal=Hit.ImpactNormal.GetSafeNormal();Contact.Component=Hit.GetComponent();
    Particles[Index].ContactNormal+=Contact.Normal;
    const FVector Clear=Motion*FMath::Clamp(double(Hit.Time)-.0001,0.,1.);
    const FVector Slide=FVector::VectorPlaneProject(Motion-Clear,Contact.Normal);
    if(Slide.IsNearlyZero(.000001))return Clear;
    FHitResult Second;
    if(!QueryPointMotion(Start+Clear,Start+Clear+Slide,Second))return Clear+Slide;
    if(Second.bStartPenetrating)return Clear;
    Particles[Index].ContactNormal+=Second.ImpactNormal.GetSafeNormal();
    return Clear+Slide*FMath::Clamp(double(Second.Time)-.0001,0.,1.);
}

void UCollisionCableComponent::ConstrainRenderPath(TArray<FVector>& Locations)
{
    const int32 Subdivisions=bEnableVisualSubdivision?FMath::Clamp(VisualSubdivisions,1,8):1;
    if(Locations.Num()!=(Particles.Num()-1)*Subdivisions+1)return;
    const double Radius=FMath::Max(.05f,CableWidth*.5f)+.01;
    auto SpanClear=[&](int32 First, const FCollisionQueryParams& SpanParams)
    {
        for(int32 S=0;S<Subdivisions;++S)
        {
            const FVector A=Locations[First+S],B=Locations[First+S+1],Delta=B-A;
            const FQuat Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Delta.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector));
            ++RenderCollisionQueryCount;
            if(GetWorld()->OverlapBlockingTestByChannel((A+B)*.5,Rotation,GetCollisionObjectType(),
                FCollisionShape::MakeCapsule(float(Radius),float(Delta.Size()*.5+Radius)),SpanParams,ResponseParams))return false;
        }
        return true;
    };
    for(int32 I=0;I+1<Particles.Num();++I)
    {
        const int32 First=I*Subdivisions;
        TOptional<FCollisionQueryParams> LocalParams;
        const FCollisionQueryParams& SpanParams=GetSpanCollisionParams(I, LocalParams);
        if(SpanClear(First, SpanParams))continue;
        TArray<FVector> Curved;for(int32 S=0;S<=Subdivisions;++S)Curved.Add(Locations[First+S]);
        // Keep all simulation knots fixed. Reduce only a blocked visual span's
        // bow, eventually falling back to its physically simulated chord.
        for(int32 Attempt=1;Attempt<=7;++Attempt)
        {
            const double Blend=Attempt==7?0.:FMath::Pow(.5,Attempt);
            for(int32 S=1;S<Subdivisions;++S)
            {
                const FVector Chord=FMath::Lerp(Locations[First],Locations[First+Subdivisions],double(S)/Subdivisions);
                Locations[First+S]=FMath::Lerp(Chord,Curved[S],Blend);
            }
            if(SpanClear(First, SpanParams))break;
        }
    }
}
