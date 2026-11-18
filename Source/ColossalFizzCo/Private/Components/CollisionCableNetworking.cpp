#include "Components/CollisionCableComponent.h"
#include "Engine/World.h"

bool UCollisionCableComponent::HasSimulationAuthority() const
{
    return !bEnableNetworkSync || GetNetMode()!=NM_Client;
}

void UCollisionCableComponent::PublishNetworkSnapshot(float Dt,bool bForce)
{
    if(!bEnableNetworkSync || !GetOwner() || !GetOwner()->HasAuthority() || !HasSimulationAuthority() || !GetWorld() || !GetWorld()->IsGameWorld())return;
    SnapshotAccumulator+=FMath::Max(0.f,Dt);
    if(!bForce && SnapshotAccumulator<1.f/FMath::Clamp(SnapshotRate,1.f,30.f))return;
    SnapshotAccumulator=0;
    bool Changed=bForce || SimulationSnapshot.Positions.Num()!=Particles.Num() || SimulationSnapshot.bSleeping!=bCableSleeping || SimulationSnapshot.Grabs.Num()!=ActiveGrabs.Num();
    if(!Changed)for(int32 I=0;I<Particles.Num();++I)
        if(!Particles[I].Position.Equals(SimulationSnapshot.Positions[I],FMath::Max(.01f,SnapshotPositionThreshold)) ||
            !SimulationSnapshot.SleepingPoints.IsValidIndex(I) || bool(SimulationSnapshot.SleepingPoints[I])!=Particles[I].bSleeping)
        {Changed=true;break;}
    if(!Changed)for(int32 I=0;I<ActiveGrabs.Num();++I)
        if(ActiveGrabs[I].Participant!=SimulationSnapshot.Grabs[I].Participant || ActiveGrabs[I].PointIndex!=SimulationSnapshot.Grabs[I].PointIndex ||
            !ActiveGrabs[I].ParticipantOffset.Equals(SimulationSnapshot.Grabs[I].ParticipantOffset,.1) ||
            !ActiveGrabs[I].TargetOffset.Equals(SimulationSnapshot.Grabs[I].TargetOffset,.1))
        {Changed=true;break;}
    if(!Changed)return;
    SimulationSnapshot.Positions.SetNum(Particles.Num());SimulationSnapshot.SleepingPoints.SetNum(Particles.Num());
    for(int32 I=0;I<Particles.Num();++I)
    {SimulationSnapshot.Positions[I]=Particles[I].Position;SimulationSnapshot.SleepingPoints[I]=Particles[I].bSleeping?1:0;}
    SimulationSnapshot.bSleeping=bCableSleeping;SimulationSnapshot.Grabs=ActiveGrabs;
    ++SimulationSnapshot.Revision;++SnapshotCount;GetOwner()->ForceNetUpdate();
}

void UCollisionCableComponent::OnRep_SimulationSnapshot()
{
    if(HasSimulationAuthority() || SimulationSnapshot.Positions.Num()<2 || SimulationSnapshot.Positions.Num()>257)return;
    const bool Resize=Particles.Num()!=SimulationSnapshot.Positions.Num();
    NumSegments=SimulationSnapshot.Positions.Num()-1;
    if(Resize)Particles.SetNum(SimulationSnapshot.Positions.Num());
    ClientSnapshotTargets.SetNum(Particles.Num());SleepingPointCount=0;
    for(int32 I=0;I<Particles.Num();++I)
    {
        ClientSnapshotTargets[I]=SimulationSnapshot.Positions[I];
        if(Resize || SimulationSnapshot.bSleeping)Particles[I].Position=ClientSnapshotTargets[I];
        Particles[I].bSleeping=SimulationSnapshot.SleepingPoints.IsValidIndex(I) && SimulationSnapshot.SleepingPoints[I]!=0;
        if(Particles[I].bSleeping)++SleepingPointCount;
    }
    if(Resize)ResetCollisionHistory();
    bCableSleeping=SimulationSnapshot.bSleeping;
    Super::SetComponentTickEnabled(!bCableSleeping);
    RefreshMovementBindings();RefreshWakeSources();PublishSimulation();
}
