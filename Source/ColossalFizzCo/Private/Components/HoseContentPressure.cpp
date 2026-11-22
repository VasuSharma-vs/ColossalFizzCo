#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"

namespace
{
	struct FContentPort
	{
		UResourceTransferEndpointComponent* Socket=nullptr;
		float Pressure=0.f;
		float Available=0.f;
		float Room=0.f;
		float ReceiveLimit=MAX_flt;
		float PumpRate=0.f;
		float InputRate=0.f;
		float LastFlow=0.f;
	};
}

float UHoseTransferComponent::IntegrateContentStep(float DeltaSeconds)
{
	LastIntakeUnits=LastTransferredUnits=LastStartPortFlowUnits=LastEndPortFlowUnits=0.f;
	FContentPort Ports[2];
	if (!GetContentEndpoints(Ports[0].Socket,Ports[1].Socket)) return 0.f;
	const auto SameConnections = [this,&Ports]()
	{
		UResourceTransferEndpointComponent* First=nullptr;
		UResourceTransferEndpointComponent* Second=nullptr;
		return GetContentEndpoints(First,Second) && First==Ports[0].Socket && Second==Ports[1].Socket;
	};
	if (!FMath::IsFinite(RatedPressure) || RatedPressure<0 || !FMath::IsFinite(FlowBoostPerPressure) || FlowBoostPerPressure<0
		|| !FMath::IsFinite(MaxDrivenUnitsPerSecond) || MaxDrivenUnitsPerSecond<0
		|| !FMath::IsFinite(BurstPressureThreshold) || BurstPressureThreshold<0 || !FMath::IsFinite(VacuumCollapseThreshold) || VacuumCollapseThreshold<0
		|| !FMath::IsFinite(BurstDelay) || BurstDelay<0 || !FMath::IsFinite(BlockedPressureRisePerSecond) || BlockedPressureRisePerSecond<0) return 0.f;
	const EHoseDischargePoint VentPoint=FindOpenTip();
	if (bRequiresMotorDrive && !(Ports[0].Socket && Ports[0].Socket->IsMotorRunning()) && !(Ports[1].Socket && Ports[1].Socket->IsMotorRunning()))
	{
		BackPressure=OverpressureSeconds=0.f; PendingBodyFailure=EHoseFailureMode::None;
		const float Spilled=DrainTrappedContents(VentPoint,DeltaSeconds,0.f);
		CurrentPressure=TrappedDrainPressure;
		return Spilled>0.f ? DeltaSeconds : 0.f;
	}

	const auto FlowCapacity = [this](float Head)
	{
		return float(FMath::Min(double(MaxDrivenUnitsPerSecond), double(MaxUnitsPerSecond)*(1.+double(FMath::Max(0.f,Head-RatedPressure))*FlowBoostPerPressure)));
	};
	const auto LimitedSupply = [](const UResourceTransferEndpointComponent& Socket,float Rate)
	{
		return Socket.MaxSupplyUnitsPerSecond>0.f ? FMath::Min(Socket.MaxSupplyUnitsPerSecond,Rate) : Rate;
	};
	for (FContentPort& Port:Ports)
	{
		const auto* Socket=Port.Socket;
		if (!IsValid(Socket)) continue;
		const bool CanSupply=bIntakeEnabled && Socket->bCanSupply && Socket->CanTransferResource(true);
		const bool CanReceive=!bBurst && bDrainEnabled && IsValid(Socket) && Socket->bCanReceive && Socket->CanTransferResource(false);
		if (!SameConnections()) { UnbindEndpoints(); return 0.f; }
		Port.Pressure=Socket->GetOperatingPressure();
		if (!FMath::IsFinite(Port.Pressure) || !FMath::IsFinite(Socket->MaxSupplyUnitsPerSecond) || Socket->MaxSupplyUnitsPerSecond<0.f
			|| !FMath::IsFinite(Socket->MaxReceiveUnitsPerSecond) || Socket->MaxReceiveUnitsPerSecond<0.f) return 0.f;
		if (CanSupply && Socket->bSupplyEnabled && Port.Pressure>=0.f) Port.Available=Socket->GetStoredUnits();
		if (CanReceive && Socket->bRequestsFlow) Port.Room=Socket->GetFreeCapacityUnits();
		if (Socket->MaxReceiveUnitsPerSecond>0.f) Port.ReceiveLimit=Socket->MaxReceiveUnitsPerSecond;
		if (Port.Pressure>0.f && Port.Available>0.f)
		{
			const float RatedPump=Socket->MaxSupplyUnitsPerSecond>0.f ? Socket->MaxSupplyUnitsPerSecond : FlowCapacity(Port.Pressure);
			Port.PumpRate=FMath::Min(RatedPump,MaxDrivenUnitsPerSecond);
		}
	}
	if (bBurst)
	{
		const float SupplyPressure=FMath::Max3(0.f,Ports[0].Pressure,Ports[1].Pressure);
		DischargeContents(EHoseDischargePoint::Rupture,Buffer.Units,SupplyPressure);
		for (FContentPort& Port:Ports)
		{
			if (!SameConnections() || !bTransferEnabled) break;
			if (Port.PumpRate<=0.f) continue;
			const float Amount=FMath::Min3(Port.PumpRate*DeltaSeconds,Port.Available,BufferCapacityUnits-Buffer.Units);
			const float Rate=FMath::Max(0.f,Amount/DeltaSeconds);
			if (Port.Socket->GetConnectorOverload(Port.Pressure,Rate,true)>0.f)
			{
				Port.Socket->NotifyOuttakeOverload(this,Port.Pressure,Rate);
				break;
			}
			Port.LastFlow=IntakeFrom(Port.Socket,Amount);
			LastIntakeUnits+=Port.LastFlow;
			DischargeContents(EHoseDischargePoint::Rupture,Port.LastFlow,Port.Pressure);
		}
		LastStartPortFlowUnits=Ports[0].LastFlow; LastEndPortFlowUnits=Ports[1].LastFlow;
		return DeltaSeconds;
	}

	const float GaugePressure=Ports[0].Pressure+Ports[1].Pressure;
	float Head=FMath::Abs(Ports[0].Pressure-Ports[1].Pressure);
	if (!FMath::IsFinite(GaugePressure) || !FMath::IsFinite(Head)) return 0.f;
	const bool LegacyPassive=Head==0.f && GaugePressure==0.f && Ports[0].Socket && Ports[1].Socket
		&& !Ports[0].Socket->bUseSignedPressure && !Ports[1].Socket->bUseSignedPressure && Ports[1].Socket->bRequestsFlow;
	const int32 OutletIndex=Ports[1].Pressure>Ports[0].Pressure || (Head==0.f && GaugePressure<0.f && Ports[1].Room<=0.f) ? 0 : 1;
	FContentPort& Inlet=Ports[1-OutletIndex];
	FContentPort& Outlet=Ports[OutletIndex];
	const bool OpenTip=VentPoint!=EHoseDischargePoint::None && !Outlet.Socket;
	if (OpenTip) Outlet.Room=MAX_flt;
	if (Head==0.f && GaugePressure<0.f) Head=-Outlet.Pressure;
	const float LineRate=FlowCapacity(Head)*(LegacyPassive ? 1.f : FMath::Min(1.f,Head/FMath::Max(RatedPressure,UE_KINDA_SMALL_NUMBER)));
	const bool OpposingPumps=Inlet.PumpRate>0.f && Outlet.PumpRate>0.f;
	// A lower-pressure pump can only oppose its share of the inlet's drive.
	// Its flow rating cannot cancel a stronger pressure at the other end.
	const float Counterflow=OpposingPumps ? FMath::Min(Outlet.PumpRate,Inlet.PumpRate*(Outlet.Pressure/Inlet.Pressure)) : 0.f;
	const float FeedRate=Inlet.Available<=0.f ? 0.f : Inlet.Pressure>0.f
		? FMath::Min(LineRate,FMath::Max(0.f,Inlet.PumpRate-Counterflow))
		: LimitedSupply(*Inlet.Socket,LineRate);
	const float OutletRate=Outlet.Room>0.f ? FMath::Min(Outlet.ReceiveLimit,(OpposingPumps || Inlet.PumpRate>0.f ? FeedRate : LineRate)) : 0.f;
	if (OutletRate>0.f || LegacyPassive) Inlet.InputRate=FeedRate;
	else for (FContentPort& Port:Ports) Port.InputRate=Port.PumpRate;

	// A closed receiver holds one pump's static pressure. Opposing pumps can build extra pressure against each other.
	const bool Blocked=Ports[0].Pressure>0.f && Ports[1].Pressure>0.f && OutletRate<=0.f && Buffer.Units>=BufferCapacityUnits-UE_KINDA_SMALL_NUMBER
		&& Ports[0].PumpRate+Ports[1].PumpRate>0.f;
	if (!Blocked) BackPressure=0.f;
	const float Rise=Blocked ? (FMath::Max(0.f,Ports[0].Pressure)+FMath::Max(0.f,Ports[1].Pressure))*BlockedPressureRisePerSecond : 0.f;
	const float InitialPressure=GaugePressure+BackPressure;
	if (!FMath::IsFinite(Rise) || !FMath::IsFinite(InitialPressure)) return 0.f;
	const float InletStress=FMath::Max(Ports[0].Pressure,Ports[1].Pressure);
	EHoseFailureMode BodyFailure=EHoseFailureMode::None;
	float TimeToLimit=0.f;
	if (FMath::Max(InitialPressure,InletStress)>BurstPressureThreshold) BodyFailure=EHoseFailureMode::OverpressureBurst;
	else if (InitialPressure<0.f && InitialPressure<=-VacuumCollapseThreshold) BodyFailure=EHoseFailureMode::VacuumCollapse;
	else if (Rise>0.f)
	{
		TimeToLimit=FMath::Max(0.f,(BurstPressureThreshold-InitialPressure)/Rise);
		if (TimeToLimit<=DeltaSeconds) BodyFailure=EHoseFailureMode::OverpressureBurst;
	}
	const float Exposure=BodyFailure==PendingBodyFailure && TimeToLimit==0.f ? OverpressureSeconds : 0.f;
	const float FailureTime=BodyFailure==EHoseFailureMode::None ? MAX_flt : TimeToLimit+FMath::Max(0.f,BurstDelay-Exposure);
	const float SimulatedSeconds=FMath::Min(DeltaSeconds,FailureTime);
	const float Pressure=InitialPressure+Rise*SimulatedSeconds;
	if (!FMath::IsFinite(Pressure)) return 0.f;
	CurrentPressure=Pressure;
	// An accepting suction socket draws the reserve toward itself. Otherwise,
	// a loose tip vents first and a live supply can use the remaining flow budget.
	const bool SuctionOutlet=Outlet.Socket && Outlet.Pressure<0.f && OutletRate>0.f;
	const float VentPressure=FMath::Max3(0.f,Ports[0].Pressure,Ports[1].Pressure);
	const float Vented=SuctionOutlet ? 0.f : DrainTrappedContents(VentPoint,SimulatedSeconds,VentPressure);
	if (VentPoint!=EHoseDischargePoint::None && Pressure>=0.f) CurrentPressure=FMath::Max(Pressure,TrappedDrainPressure);
	if (!SameConnections()) { UnbindEndpoints(); return SimulatedSeconds; }

	// Trip the connector before committing any intake or delivery. If both are overloaded the more overloaded fitting releases first.
	float Room=FMath::Max(0.f,BufferCapacityUnits-Buffer.Units);
	float IntakeOffer[2]={0.f,0.f};
	for (int32 Index=0;Index<2;++Index)
	{
		IntakeOffer[Index]=FMath::Min3(Ports[Index].InputRate*DeltaSeconds,Ports[Index].Available,Room);
		if (OpenTip) IntakeOffer[Index]=FMath::Min(IntakeOffer[Index],FMath::Max(0.f,MaxDrivenUnitsPerSecond*DeltaSeconds-Vented));
		Room=FMath::Max(0.f,Room-IntakeOffer[Index]);
	}
	const float DrainOffer=FMath::Min3(OutletRate*DeltaSeconds,Outlet.Room,Buffer.Units+IntakeOffer[0]+IntakeOffer[1]);
	float MaxOverload=0.f,SocketPressure=0.f,SocketRate=0.f;
	int32 PoppedIndex=INDEX_NONE;
	bool OuttakeOverload=false;
	for (int32 Index=0;Index<2;++Index)
	{
		const FContentPort& Port=Ports[Index];
		if (!Port.Socket) continue;
		const float Received=Index==OutletIndex ? DrainOffer : 0.f;
		const bool Outtake=Received<=0.f && (IntakeOffer[Index]>0.f || (Port.Pressure>0.f && Port.Socket->bCanSupply));
		const float Stress=FMath::Max3(FMath::Abs(Port.Pressure),FMath::Abs(Pressure),Received>0.f ? Head : 0.f);
		const float OfferedFlow=(Outtake ? IntakeOffer[Index] : Received)/DeltaSeconds;
		const float Overload=(Stress>0.f || OfferedFlow>0.f) ? Port.Socket->GetConnectorOverload(Stress,OfferedFlow,Outtake) : 0.f;
		if (Overload>MaxOverload)
		{
			PoppedIndex=Index; MaxOverload=Overload; SocketRate=OfferedFlow; SocketPressure=Stress; OuttakeOverload=Outtake;
		}
	}
	if (PoppedIndex!=INDEX_NONE)
	{
		auto* Socket=Ports[PoppedIndex].Socket;
		if (OuttakeOverload) Socket->NotifyOuttakeOverload(this,SocketPressure,SocketRate);
		else Socket->NotifyIntakeOverload(this,SocketPressure,SocketRate);
		return 0.f;
	}

	const float TipBudget=OpenTip ? FMath::Max(0.f,MaxDrivenUnitsPerSecond*SimulatedSeconds-Vented) : MAX_flt;
	for (FContentPort& Port:Ports)
	{
		Port.LastFlow=IntakeFrom(Port.Socket,FMath::Min(Port.InputRate*SimulatedSeconds,TipBudget));
		LastIntakeUnits+=Port.LastFlow;
		if (!SameConnections()) { UnbindEndpoints(); break; }
	}
	float Outflow=0.f;
	if (OutletRate>0.f && SameConnections())
	{
		if (OpenTip)
		{
			Outflow=DischargeContents(VentPoint,FMath::Min(OutletRate*SimulatedSeconds,TipBudget),FMath::Max(0.f,Inlet.Pressure));
		}
		else if (Outlet.Socket->bRequestsFlow)
		{
			const float Drain=FMath::Min(OutletRate*SimulatedSeconds,Outlet.Socket->GetFreeCapacityUnits());
			LastTransferredUnits=DeliverTo(Outlet.Socket,Drain,FMath::Max3(0.f,Pressure,Head),SimulatedSeconds);
			Outlet.LastFlow-=LastTransferredUnits;
			Outflow=LastTransferredUnits;
		}
	}
	if (SuctionOutlet && Outflow>0.f)
		TrappedDrainPressure*=Buffer.Units/(Buffer.Units+Outflow);
	// Only the volume actually accepted relieves a single pushing inlet, weighted by its offered rate.
	if (Pressure>0.f && Inlet.Pressure>0.f && Outlet.Pressure<=0.f && FeedRate>0.f && SimulatedSeconds>0.f)
		CurrentPressure=FMath::Max(TrappedDrainPressure,Pressure*(1.f-FMath::Clamp((Outflow+Vented)/(FeedRate*SimulatedSeconds),0.f,1.f)));
	LastStartPortFlowUnits=Ports[0].LastFlow; LastEndPortFlowUnits=Ports[1].LastFlow;
	if (!SameConnections()) { UnbindEndpoints(); return SimulatedSeconds; }
	BackPressure+=Rise*SimulatedSeconds;
	PendingBodyFailure=BodyFailure;
	OverpressureSeconds=BodyFailure==EHoseFailureMode::None ? 0.f : Exposure+FMath::Max(0.f,SimulatedSeconds-TimeToLimit);
	if (FailureTime<=DeltaSeconds)
	{
		CurrentPressure=BodyFailure==EHoseFailureMode::OverpressureBurst ? FMath::Max(Pressure,InletStress) : Pressure;
		Rupture(BodyFailure);
	}
	return SimulatedSeconds;
}
