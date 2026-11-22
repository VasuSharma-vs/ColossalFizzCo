#include "Components/CollisionCableComponent.h"

namespace CableInteractionTrace
{
    bool Capsule(FVector Start,FVector Direction,double Reach,FVector A,FVector B,double Radius,double& Distance)
    {
        Distance=Reach+1;
        if(FVector::DistSquared(Start,FMath::ClosestPointOnSegment(Start,A,B))<=FMath::Square(Radius))
        { Distance=0;return true; }
        auto Sphere=[&](FVector Center)
        {
            const FVector Offset=Start-Center;
            const double Along=FVector::DotProduct(Offset,Direction);
            const double Discriminant=Along*Along-Offset.SizeSquared()+Radius*Radius;
            if(Discriminant<0)return;
            const double T=-Along-FMath::Sqrt(Discriminant);
            if(T>=0 && T<=Reach)Distance=FMath::Min(Distance,T);
        };
        Sphere(A);Sphere(B);
        const FVector Axis=B-A;const double Length=Axis.Size();
        if(Length>UE_SMALL_NUMBER)
        {
            const FVector Unit=Axis/Length,Offset=Start-A;
            const FVector D=Direction-Unit*FVector::DotProduct(Direction,Unit);
            const FVector O=Offset-Unit*FVector::DotProduct(Offset,Unit);
            const double AA=D.SizeSquared(),BB=FVector::DotProduct(O,D),CC=O.SizeSquared()-Radius*Radius;
            const double Disc=BB*BB-AA*CC;
            if(AA>UE_SMALL_NUMBER && Disc>=0)
            {
                const double T=(-BB-FMath::Sqrt(Disc))/AA;
                const double Height=FVector::DotProduct(Start+Direction*T-A,Unit);
                if(T>=0 && T<=Reach && Height>=0 && Height<=Length)Distance=FMath::Min(Distance,T);
            }
        }
        return Distance<=Reach;
    }
}

bool UCollisionCableComponent::TraceCableInteraction(FVector Start,FVector End,float Padding,FHitResult& Hit) const
{
    if(!bEnableGrabbing || Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(Padding) || Padding<0 || Particles.Num()<2)return false;
    const double Reach=FVector::Distance(Start,End);
    if(Reach<=UE_SMALL_NUMBER)return false;
    const FVector Direction=(End-Start)/Reach;
    TArray<FVector> Curve;GetCableRenderLocations(Curve);
    const double Radius=FMath::Max(.05f,CableWidth*.5f)+Padding;
    const int32 Subdivisions=FMath::Max(1,(Curve.Num()-1)/(Particles.Num()-1));
    double Best=Reach+1;int32 Index=INDEX_NONE;FVector OnCable;
    for(int32 I=0;I+1<Curve.Num();++I)
    {
        double Distance;
        if(!CableInteractionTrace::Capsule(Start,Direction,Reach,Curve[I],Curve[I+1],Radius,Distance) || Distance>=Best)continue;
        Best=Distance;OnCable=FMath::ClosestPointOnSegment(Start+Direction*Distance,Curve[I],Curve[I+1]);
        const int32 Span=FMath::Min(I/Subdivisions,Particles.Num()-2);
        Index=FVector::DistSquared(OnCable,Particles[Span].Position)<=FVector::DistSquared(OnCable,Particles[Span+1].Position)?Span:Span+1;
    }
    if(Index==INDEX_NONE)return false;
    const FVector At=Start+Direction*Best;
    Hit=FHitResult(GetOwner(),const_cast<UCollisionCableComponent*>(this),At,(At-OnCable).GetSafeNormal(UE_SMALL_NUMBER,-Direction));
    Hit.bBlockingHit=true;Hit.TraceStart=Start;Hit.TraceEnd=End;Hit.Distance=float(Best);Hit.Time=float(Best/Reach);Hit.Item=Index;
    return true;
}

int32 UCollisionCableComponent::FindCableGrabPoint(FVector Position,float Tolerance) const
{
    if(Position.ContainsNaN() || !FMath::IsFinite(Tolerance) || Tolerance<0 || Particles.Num()<2)return INDEX_NONE;
    TArray<FVector> Curve;GetCableRenderLocations(Curve);
    const int32 Subdivisions=FMath::Max(1,(Curve.Num()-1)/(Particles.Num()-1));
    double Best=FMath::Square(Tolerance);int32 Index=INDEX_NONE;
    for(int32 I=0;I+1<Curve.Num();++I)
    {
        const FVector Closest=FMath::ClosestPointOnSegment(Position,Curve[I],Curve[I+1]);
        const double Distance=FVector::DistSquared(Position,Closest);
        if(Distance>Best)continue;
        Best=Distance;const int32 Span=FMath::Min(I/Subdivisions,Particles.Num()-2);
        Index=FVector::DistSquared(Closest,Particles[Span].Position)<=FVector::DistSquared(Closest,Particles[Span+1].Position)?Span:Span+1;
    }
    return Index;
}
