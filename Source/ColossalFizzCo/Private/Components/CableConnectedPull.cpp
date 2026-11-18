#include "Components/CableMovementConstraintComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/MovementComponent.h"

namespace CableConnectedPull
{
    constexpr int32 MaxConnectedNodes=128;
    constexpr int32 MaxConnectedCables=128;
    constexpr int32 MaxWorldCables=4096;
    TArray<TWeakObjectPtr<UCollisionCableComponent>> RegisteredCables;

    struct FPointKey
    {
        UCollisionCableComponent* Cable=nullptr;
        int32 Index=0;
        bool operator==(const FPointKey& Other) const {return Cable==Other.Cable && Index==Other.Index;}
        friend uint32 GetTypeHash(const FPointKey& Key){return HashCombine(GetTypeHash(Key.Cable),GetTypeHash(Key.Index));}
    };
    struct FPoint
    {
        FPointKey Key;
        FCableMovementAttachment Attachment;
        int32 Parent=0;
    };
    struct FBody
    {
        AActor* Actor=nullptr;
        bool Fixed=false;
        double Mass=0,Resistance=0;
        double Distance=DBL_MAX;
        int32 Depth=0;
        TArray<int32> Edges;
    };
    struct FEdge
    {
        int32 A=0,B=0;
        FVector PositionA,PositionB;
        double Radius=0;
        UCollisionCableComponent* Cable=nullptr;
    };
    struct FGraph
    {
        AActor* Driver=nullptr;
        FVector Old,Delta,Direction;
        double Dt=.016,Force=0,MaximumFraction=1;
        int32 Root=INDEX_NONE;
        bool Limited=false;
        TArray<FBody> Bodies;
        TArray<FEdge> Edges;
        TMap<UCollisionCableComponent*,double> CableDistances;
        TArray<int32> Order;

        double Slack(int32 From,const FEdge& Edge) const
        {
            const FVector Separation=From==Edge.A?Edge.PositionA-Edge.PositionB:Edge.PositionB-Edge.PositionA;
            // Preserve an authored stretched configuration without allowing further stretch.
            const double Radius=FMath::Max(Edge.Radius,Separation.Size());
            const double A=Delta.SizeSquared(),B=FVector::DotProduct(Separation,Delta);
            const double Discriminant=FMath::Max(0.,B*B+A*(Radius*Radius-Separation.SizeSquared()));
            return A>UE_SMALL_NUMBER?FMath::Max(0.,(-B+FMath::Sqrt(Discriminant))/A):DBL_MAX;
        }

        void FindLoads()
        {
            if(Root==INDEX_NONE)return;
            Bodies[Root].Distance=0;
            TSet<int32> Settled;
            // Nonnegative slack costs form a shortest-path problem. A cycle can never add weight twice
            // or amplify motion: each body is settled once and each cable is counted once.
            for(;;)
            {
                int32 Next=INDEX_NONE;double Best=1.+UE_KINDA_SMALL_NUMBER;
                for(int32 I=0;I<Bodies.Num();++I)if(!Settled.Contains(I) && Bodies[I].Distance<Best)
                {Next=I;Best=Bodies[I].Distance;}
                if(Next==INDEX_NONE)break;
                if(Settled.Num()>=MaxConnectedNodes){Limited=true;break;}
                Settled.Add(Next);Order.Add(Next);
                FBody& Body=Bodies[Next];
                if(Body.Fixed)MaximumFraction=FMath::Min(MaximumFraction,Body.Distance);
                for(int32 EdgeIndex:Body.Edges)
                {
                    const FEdge& Edge=Edges[EdgeIndex];
                    double& CableDistance=CableDistances.FindOrAdd(Edge.Cable,Body.Distance);
                    CableDistance=FMath::Min(CableDistance,Body.Distance);
                    if(CableDistances.Num()>MaxConnectedCables){Limited=true;return;}
                    const int32 Other=Edge.A==Next?Edge.B:Edge.A;
                    const double Distance=Body.Distance+Slack(Next,Edge);
                    if(Distance<Bodies[Other].Distance-UE_SMALL_NUMBER)
                    {Bodies[Other].Distance=Distance;Bodies[Other].Depth=Body.Depth+1;}
                }
            }
            Order.Sort([&](int32 A,int32 B)
            {
                if(Bodies[A].Depth!=Bodies[B].Depth)return Bodies[A].Depth>Bodies[B].Depth;
                const double DistanceA=Bodies[A].Actor?FVector::DistSquared(Bodies[A].Actor->GetActorLocation(),Old):0;
                const double DistanceB=Bodies[B].Actor?FVector::DistSquared(Bodies[B].Actor->GetActorLocation(),Old):0;
                return DistanceA>DistanceB;
            });
        }

        double RequiredForce(double Fraction,double* TotalMass=nullptr) const
        {
            double Mass=0,Needed=0;
            const double Acceleration=Delta.Size()*.01/(Dt*Dt);
            const double Gravity=Driver && Driver->GetWorld()?FMath::Max(0.,Direction.Z*-Driver->GetWorld()->GetGravityZ()*.01):0;
            for(const auto& Pair:CableDistances)
            {
                const double Move=FMath::Max(0.,Fraction-Pair.Value);
                if(Move<=UE_SMALL_NUMBER)continue;
                const double CableMass=Pair.Key->GetCableMassKg();Mass+=CableMass;
                Needed+=CableMass*(Acceleration*Move+Gravity);
            }
            for(int32 Index:Order)
            {
                const FBody& Body=Bodies[Index];
                const double Move=FMath::Max(0.,Fraction-Body.Distance);
                if(Index==Root || !Body.Actor || Body.Fixed || Move<=UE_SMALL_NUMBER)continue;
                Mass+=Body.Mass;
                Needed+=Body.Resistance+Body.Mass*(Acceleration*Move+Gravity);
            }
            if(TotalMass)*TotalMass=Mass;
            return Needed;
        }
    };

    void DescribeBody(FBody& Body,AActor* Driver)
    {
        if(!Body.Actor)return;
        const auto* Root=Body.Actor->GetRootComponent();
        if(!Root || Root->Mobility!=EComponentMobility::Movable)Body.Fixed=true;
        if(const auto* Response=Body.Actor->FindComponentByClass<UCablePullResponseComponent>())
        {
            Body.Fixed|=Response->bFixed;
            Body.Mass=FMath::IsFinite(Response->WeightKg)?FMath::Clamp(double(Response->WeightKg),.001,1000000.):1000000.;
            Body.Resistance=FMath::IsFinite(Response->ResistanceNewtons)?FMath::Max(0.,double(Response->ResistanceNewtons)):DBL_MAX;
            return;
        }
        const auto* Primitive=Cast<UPrimitiveComponent>(Root);
        if(Primitive && Primitive->IsSimulatingPhysics())
        {
            Body.Mass=FMath::Max(.001,double(Primitive->GetMass()));
            Body.Resistance=Body.Mass*4.905;return;
        }
        if(const auto* Character=Cast<ACharacter>(Body.Actor))
        {Body.Mass=Character->GetCharacterMovement()->Mass;return;}
        if(Body.Actor==Driver || Body.Actor->FindComponentByClass<UMovementComponent>())
        {Body.Mass=10;return;}
        // Kinematic props require an explicit response component. Merely using a Movable mesh
        // does not authorize a cable to translate a wall, machine or level fixture.
        Body.Fixed=true;
    }

    bool FindCableLink(const FPoint& Point,FPointKey& Link)
    {
        USceneComponent* Component=Point.Attachment.Source.Get();
        FName Socket=Point.Attachment.SocketName;
        TSet<USceneComponent*> Seen;
        while(Component && Seen.Num()<64 && !Seen.Contains(Component))
        {
            Seen.Add(Component);
            if(auto* Cable=Cast<UCollisionCableComponent>(Component);Cable && Cable!=Point.Key.Cable)
            {
                Link.Cable=Cable;Link.Index=Socket==TEXT("CableEnd")?FMath::Clamp(Cable->NumSegments,1,256):0;return true;
            }
            Socket=Component->GetAttachSocketName();Component=Component->GetAttachParent();
        }
        return false;
    }

    FGraph Build(AActor* Driver,FVector Old,FVector Requested,float Dt,float Force)
    {
        FGraph Graph;Graph.Driver=Driver;Graph.Old=Old;Graph.Delta=Requested-Old;
        Graph.Direction=Graph.Delta.GetSafeNormal();Graph.Dt=FMath::Clamp(double(Dt),.001,.25);Graph.Force=FMath::Max(0.,double(Force));
        if(!Driver || !Driver->GetWorld())return Graph;
        RegisteredCables.RemoveAll([](const auto& Cable){return !Cable.IsValid();});
        TArray<FPoint> Points;TMap<FPointKey,int32> PointIndices;
        TMap<UCollisionCableComponent*,TArray<int32>> CablePoints;
        for(const auto& Weak:RegisteredCables)
        {
            auto* Cable=Weak.Get();
            if(!Cable || !Cable->IsRegistered() || Cable->GetWorld()!=Driver->GetWorld() || !Cable->bEnableMovementRestrictions)continue;
            if(CablePoints.Num()>=MaxWorldCables){Graph.Limited=true;return Graph;}
            TArray<FCableMovementAttachment> Attachments;Cable->GetMovementAttachments(Attachments);
            for(const auto& Attachment:Attachments)
            {
                FPointKey Key{Cable,Attachment.PointIndex};
                if(int32* Existing=PointIndices.Find(Key))
                {
                    Points[*Existing].Attachment.bFixed|=Attachment.bFixed;
                    if(Attachment.Driver.IsValid())
                    {
                        const bool Fixed=Points[*Existing].Attachment.bFixed;
                        Points[*Existing].Attachment=Attachment;Points[*Existing].Attachment.bFixed=Fixed;
                    }
                    continue;
                }
                const int32 Index=Points.Num();FPoint& Point=Points.AddDefaulted_GetRef();
                Point.Key=Key;Point.Attachment=Attachment;Point.Parent=Index;
                PointIndices.Add(Key,Index);CablePoints.FindOrAdd(Cable).Add(Index);
            }
        }
        auto Find=[&](int32 Index)
        {
            int32 Root=Index;while(Points[Root].Parent!=Root)Root=Points[Root].Parent;
            while(Points[Index].Parent!=Index){const int32 Next=Points[Index].Parent;Points[Index].Parent=Root;Index=Next;}
            return Root;
        };
        auto Unite=[&](int32 A,int32 B){A=Find(A);B=Find(B);if(A!=B)Points[B].Parent=A;};
        TMap<AActor*,int32> ActorPoints;
        for(int32 I=0;I<Points.Num();++I)
        {
            FPointKey Link;
            if(FindCableLink(Points[I],Link))
            {if(const int32* Linked=PointIndices.Find(Link))Unite(I,*Linked);else Points[I].Attachment.bFixed=true;continue;}
            if(AActor* Actor=Points[I].Attachment.Driver.Get())
            {if(const int32* Existing=ActorPoints.Find(Actor))Unite(I,*Existing);else ActorPoints.Add(Actor,I);}
        }
        TMap<int32,int32> Nodes;
        TArray<int32> PointNodes;PointNodes.SetNum(Points.Num());
        for(int32 I=0;I<Points.Num();++I)
        {
            const int32 Root=Find(I);
            int32* Existing=Nodes.Find(Root);
            const int32 Node=Existing?*Existing:Graph.Bodies.AddDefaulted();
            if(!Existing)Nodes.Add(Root,Node);
            PointNodes[I]=Node;FBody& Body=Graph.Bodies[Node];Body.Fixed|=Points[I].Attachment.bFixed;
            FPointKey Link;
            if(!FindCableLink(Points[I],Link))if(AActor* Actor=Points[I].Attachment.Driver.Get())
            {
                if(Body.Actor && Body.Actor!=Actor)Body.Fixed=true;
                Body.Actor=Actor;
            }
        }
        for(int32 I=0;I<Graph.Bodies.Num();++I)
        {DescribeBody(Graph.Bodies[I],Driver);if(Graph.Bodies[I].Actor==Driver)Graph.Root=I;}
        for(auto& Pair:CablePoints)
        {
            auto& Indices=Pair.Value;Indices.Sort([&](int32 A,int32 B){return Points[A].Key.Index<Points[B].Key.Index;});
            const double Rest=FMath::Max(1.f,Pair.Key->CableLength)/FMath::Clamp(Pair.Key->NumSegments,1,256);
            for(int32 I=1;I<Indices.Num();++I)
            {
                const int32 PA=Indices[I-1],PB=Indices[I],A=PointNodes[PA],B=PointNodes[PB];
                FEdge Edge;Edge.A=A;Edge.B=B;Edge.PositionA=Points[PA].Attachment.Position;Edge.PositionB=Points[PB].Attachment.Position;
                if(A==Graph.Root)Edge.PositionA-=Driver->GetActorLocation()-Old;
                if(B==Graph.Root)Edge.PositionB-=Driver->GetActorLocation()-Old;
                Edge.Radius=Rest*(Points[PB].Key.Index-Points[PA].Key.Index)+FMath::Max(0.f,Pair.Key->LengthSlackTolerance);Edge.Cable=Pair.Key;
                const int32 EdgeIndex=Graph.Edges.Add(Edge);Graph.Bodies[A].Edges.Add(EdgeIndex);if(A!=B)Graph.Bodies[B].Edges.Add(EdgeIndex);
            }
        }
        Graph.FindLoads();return Graph;
    }

    double SweepFraction(AActor* Actor,AActor* Driver,const FVector& Delta)
    {
        if(!Actor || Delta.IsNearlyZero())return 1;
        TArray<UPrimitiveComponent*> Primitives;Actor->GetComponents(Primitives);
        FComponentQueryParams Params(SCENE_QUERY_STAT(CableConnectedPull),Actor);Params.AddIgnoredActor(Driver);
        double Fraction=1;int32 Sweeps=0;
        for(auto* Primitive:Primitives)
        {
            if(!Primitive || !Primitive->IsRegistered() || !Primitive->IsQueryCollisionEnabled())continue;
            if(++Sweeps>64)return 0;
            TArray<FHitResult> Hits;const FVector Start=Primitive->GetComponentLocation();
            Actor->GetWorld()->ComponentSweepMulti(Hits,Primitive,Start,Start+Delta,Primitive->GetComponentQuat(),Params);
            for(const auto& Hit:Hits)if(Hit.bBlockingHit)
            {
                if(Hit.bStartPenetrating && FVector::DotProduct(Delta,Hit.Normal)>0)continue;
                Fraction=FMath::Min(Fraction,FMath::Max(0.,double(Hit.Time)-.001));
            }
        }
        return Fraction;
    }

    FCablePullResult Evaluate(FGraph& Graph,bool Apply)
    {
        FCablePullResult Result;Result.AllowedLocation=Graph.Old+Graph.Delta;
        Result.bTraversalLimitReached=Graph.Limited;Result.VisitedCableCount=Graph.CableDistances.Num();
        if(Graph.Limited){Result.AllowedLocation=Graph.Old;Result.bBlocked=true;return Result;}
        if(Graph.Root==INDEX_NONE || Graph.Delta.IsNearlyZero())return Result;
        double Mass=0;Result.RequiredForceNewtons=float(Graph.RequiredForce(1,&Mass));Result.TotalMassKg=float(Mass);
        double Fraction=FMath::Clamp(Graph.MaximumFraction,0.,1.);
        if(Graph.RequiredForce(Fraction)>Graph.Force)
        {
            double Low=0,High=Fraction;
            for(int32 Pass=0;Pass<16;++Pass)
            {const double Middle=(Low+High)*.5;if(Graph.RequiredForce(Middle)<=Graph.Force)Low=Middle;else High=Middle;}
            Fraction=Low;
        }
        // Preflight every moving body's actual collision shapes before mutating any transform.
        // This is conservative around other bodies, and never treats a wall as movable slack.
        for(int32 Pass=0;Pass<3;++Pass)
        {
            const double Before=Fraction;
            for(int32 Index:Graph.Order)
            {
                const FBody& Body=Graph.Bodies[Index];const double Move=FMath::Max(0.,Fraction-Body.Distance);
                if(Index==Graph.Root || !Body.Actor || Body.Fixed || Move<=UE_SMALL_NUMBER)continue;
                Fraction=FMath::Min(Fraction,Body.Distance+Move*SweepFraction(Body.Actor,Graph.Driver,Graph.Delta*Move));
            }
            if(FMath::IsNearlyEqual(Before,Fraction,UE_SMALL_NUMBER))break;
        }
        if(Apply)
        {
            for(int32 Index:Graph.Order)
            {
                FBody& Body=Graph.Bodies[Index];const double Move=FMath::Max(0.,Fraction-Body.Distance);
                if(Index==Graph.Root || !Body.Actor || Body.Fixed || Move<=UE_SMALL_NUMBER)continue;
                if(!Body.Actor->HasAuthority()) {Fraction=FMath::Min(Fraction,Body.Distance);continue;}
                const FVector Before=Body.Actor->GetActorLocation();FHitResult Hit;
                Body.Actor->SetActorLocation(Before+Graph.Delta*Move,true,&Hit,ETeleportType::None);
                const FVector Actual=Body.Actor->GetActorLocation()-Before;
                if(!Actual.IsNearlyZero())
                {Result.MovedActors.Add(Body.Actor);Body.Actor->ForceNetUpdate();}
                // A hit callback may introduce new geometry after preflight. Subsequent bodies
                // and the driver may use only the displacement that was actually achieved.
                const double Achieved=FMath::Max(0.,FVector::DotProduct(Actual,Graph.Delta)/Graph.Delta.SizeSquared());
                Fraction=FMath::Min(Fraction,Body.Distance+Achieved);
            }
            // Even a rejected pull has consumed this frame's attachment-force decision.
            // Letting the legacy rope load force run as well would bypass the queried resistance.
            for(const auto& Pair:Graph.CableDistances)
                if(Pair.Value<1.)Pair.Key->NotifyConnectedPullApplied();
        }
        Result.MovedActorCount=Result.MovedActors.Num();Result.AllowedLocation=Graph.Old+Graph.Delta*Fraction;
        Result.bBlocked=Fraction<1.-UE_KINDA_SMALL_NUMBER;return Result;
    }
}

void UCableMovementConstraintComponent::RegisterWorldCable(UCollisionCableComponent* Cable)
{
    if(IsValid(Cable) && Cable->GetWorld() && Cable->GetWorld()->IsGameWorld())CableConnectedPull::RegisteredCables.AddUnique(Cable);
}

FCablePullResult UCableMovementConstraintComponent::QueryConnectedPull(FVector OldLocation,FVector RequestedLocation,float DeltaSeconds,float AvailableForceNewtons) const
{
    FCablePullResult Invalid;Invalid.AllowedLocation=OldLocation;Invalid.bBlocked=true;
    if(!GetOwner() || OldLocation.ContainsNaN() || RequestedLocation.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0
        || !FMath::IsFinite(AvailableForceNewtons) || AvailableForceNewtons<0)return Invalid;
    auto Graph=CableConnectedPull::Build(GetOwner(),OldLocation,RequestedLocation,DeltaSeconds,AvailableForceNewtons);
    return CableConnectedPull::Evaluate(Graph,false);
}

FCablePullResult UCableMovementConstraintComponent::TryPullConnectedChain(FVector OldLocation,FVector RequestedLocation,float DeltaSeconds,float AvailableForceNewtons)
{
    FCablePullResult Invalid;Invalid.AllowedLocation=OldLocation;Invalid.bBlocked=true;
    if(!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || OldLocation.ContainsNaN() || RequestedLocation.ContainsNaN()
        || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0 || !FMath::IsFinite(AvailableForceNewtons) || AvailableForceNewtons<0)return Invalid;
    auto Graph=CableConnectedPull::Build(GetOwner(),OldLocation,RequestedLocation,DeltaSeconds,AvailableForceNewtons);
    auto Result=CableConnectedPull::Evaluate(Graph,true);
    for(AActor* Actor:Result.MovedActors)if(auto* Adapter=Actor->FindComponentByClass<UCableMovementConstraintComponent>())
        Adapter->PreviousLocation=Actor->GetActorLocation();
    return Result;
}
