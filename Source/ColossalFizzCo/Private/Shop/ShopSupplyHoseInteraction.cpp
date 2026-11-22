#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ColaSupplyContainer.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Resources/HoseCableActor.h"
#include "Resources/PressurizedColaContainer.h"
#include "Resources/ResourcePortLibrary.h"
#include "Components/HoseTransferComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Interfaces/ResourceEndpointProvider.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "WorldInteractionCarry.h"

namespace
{
	constexpr float UnplugHoldSeconds = .5f;

	APlayerController* SocketController(AActor* Owner)
	{
		if (auto* Controller = Cast<APlayerController>(Owner)) return Controller;
		const auto* Pawn = Cast<APawn>(Owner);
		return Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	}

	void IgnoreCarriedProp(FCollisionQueryParams& Query, const APawn& Pawn)
	{
		if (const auto* Actor = Cast<AActor>(WorldInteractionCarry::GetCarryReference(Pawn, TEXT("Holding Actor")));
			Actor && Actor->GetAttachParentActor()==&Pawn) Query.AddIgnoredActor(Actor);
		if (const auto* Component = Cast<UPrimitiveComponent>(WorldInteractionCarry::GetCarryReference(Pawn, TEXT("Holding Component")));
			Component && Component->GetAttachParent() && Component->GetAttachParent()->GetOwner()==&Pawn)
			Query.AddIgnoredComponent(Component);
	}

	bool CanReachHoseSocket(const APawn& Pawn, const UBoxComponent& Volume, float MaxDistance, const AHoseCableActor& Hose,
		const AActor* CarriedHose = nullptr)
	{
		if (!FMath::IsFinite(MaxDistance) || MaxDistance<=0 || !Volume.IsRegistered() || Volume.GetWorld()!=Pawn.GetWorld()
			|| Volume.Bounds.GetBox().ComputeSquaredDistanceToPoint(Pawn.GetActorLocation())>FMath::Square(MaxDistance)) return false;
		FVector Eye;
		FRotator View;
		Pawn.GetActorEyesViewPoint(Eye, View);
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(HoseSocketReach), false, &Pawn);
		Query.AddIgnoredActor(&Hose);
		if (CarriedHose) Query.AddIgnoredActor(CarriedHose);
		IgnoreCarriedProp(Query, Pawn);
		return Pawn.GetWorld()->LineTraceSingleByChannel(Hit, Eye, Volume.Bounds.Origin, ECC_Visibility, Query)
			&& Hit.GetComponent()==&Volume;
	}

	bool HasFreeHands(const APawn& Pawn)
	{
		return !IsValid(WorldInteractionCarry::GetCarryReference(Pawn, TEXT("Holding Actor")))
			&& !IsValid(WorldInteractionCarry::GetCarryReference(Pawn, TEXT("Holding Component")));
	}
}

bool UShopPlayerInteractionComponent::TryConnectSupplyHoseFromView()
{
	if (!LocalCable.IsValid() || !GetWorld()) return false;
	auto* Pawn=Cast<APawn>(GetOwner()); auto* Controller=Pawn ? Cast<APlayerController>(Pawn->GetController()) : Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController()) return false;
	if (!Pawn) Pawn=Controller->GetPawn();
	FVector Eye; FRotator Direction; Controller->GetPlayerViewPoint(Eye,Direction);
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(SupplyHosePort),false,Pawn);
	// Traceable connector art on the held hose must not hide the receiving socket.
	Params.AddIgnoredActor(LocalCable->GetOwner());
	if (!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Direction.Vector()*MaxInteractionDistance,ECC_Visibility,Params)) return false;
	if (Cast<AHoseCableActor>(LocalCable->GetOwner()))
	{
		if (!Hit.GetActor() || LocalCableSequence==MAX_int64) return false;
		if (auto* Socket = UHoseSocketComponent::GetSocketFromHitComponent(Hit.GetComponent()))
		{
			const int64 Sequence = ++LocalCableSequence;
			if (!GetOwner()->HasAuthority()) Server_ConnectHoseSocket(LocalCable.Get(), Socket, Sequence);
			else if (ExecuteConnectHoseSocket(LocalCable.Get(), Socket, Sequence))
			{
				LocalCable.Reset();
				SetComponentTickEnabled(false);
			}
			return true; // A wrong or occupied socket leaves the connector in the player's hand.
		}
		if (Hit.GetActor()->IsA<APressurizedColaContainer>() || Hit.GetActor()->IsA<AManualColaFillingMachine>()) return false;
		const int64 ResourceSequence=++LocalCableSequence;
		if (GetOwner()->HasAuthority())
		{
			if (!ExecuteConnectResourceHose(LocalCable.Get(),Hit.GetActor(),ResourceSequence)) return false;
			LocalCable.Reset(); SetComponentTickEnabled(false);
		}
		else Server_ConnectResourceHose(LocalCable.Get(),Hit.GetActor(),ResourceSequence);
		return true;
	}
	if (!Cast<AColaSupplyContainer>(LocalCable->GetOwner())) return false;
	auto* Machine=Cast<AManualColaFillingMachine>(Hit.GetActor()); int32 PortIndex; FTransform Mount;
	if (!Machine || !Machine->GetHosePortFromHit(Hit,PortIndex,Mount) || LocalCableSequence==MAX_int64) return false;
	const int64 Sequence=++LocalCableSequence;
	if (GetOwner()->HasAuthority())
	{
		if (!ExecuteConnectSupplyHose(LocalCable.Get(),Machine,PortIndex,Sequence)) return false;
		LocalCable.Reset(); SetComponentTickEnabled(false);
	}
	else Server_ConnectSupplyHose(LocalCable.Get(),Machine,PortIndex,Sequence);
	return true;
}

bool UShopPlayerInteractionComponent::ExecuteConnectHoseSocket(UCollisionCableComponent* Cable, UHoseSocketComponent* Socket, int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Sequence<=0 || Sequence<=LastCableSequence) return false;
	LastCableSequence = Sequence;
	auto* Pawn = Cast<APawn>(GetOwner());
	if (auto* Controller = Cast<APlayerController>(GetOwner())) Pawn = Controller->GetPawn();
	const auto* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	auto* Hose = IsValid(Cable) ? Cast<AHoseCableActor>(Cable->GetOwner()) : nullptr;
	const auto* Volume = IsValid(Socket) ? Socket->GetTraceVolume() : nullptr;
	if (!Controller || Controller->GetPawn()!=Pawn || !Hose || ServerCable.Get()!=Cable || !IsValid(Volume)
		|| !CanReachHoseSocket(*Pawn, *Volume, MaxInteractionDistance, *Hose)) return false;
	const int32 HeldIndex = Hose->FindHeldConnector(Pawn);
	if (HeldIndex==INDEX_NONE || !Socket->AttachHose(Hose, HeldIndex)) return false;

	Cable->EndPointGrab(Pawn);
	ServerCable.Reset();
	return true;
}

void UShopPlayerInteractionComponent::Server_ConnectHoseSocket_Implementation(UCollisionCableComponent* Cable, UHoseSocketComponent* Socket, int64 Sequence)
{
	if (ExecuteConnectHoseSocket(Cable, Socket, Sequence)) Client_CableGrabResult(Cable, false);
}

UHoseSocketComponent* UShopPlayerInteractionComponent::FindHoseSocketInView() const
{
	const auto* Controller = SocketController(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn || !Controller->IsLocalController() || !GetWorld()
		|| !FMath::IsFinite(MaxInteractionDistance) || MaxInteractionDistance<=0) return nullptr;
	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(HoseSocketView), false, Pawn);
	if (auto* Cable = LocalCable.Get()) Query.AddIgnoredActor(Cable->GetOwner());
	IgnoreCarriedProp(Query, *Pawn);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye+View.Vector()*MaxInteractionDistance, ECC_Visibility, Query)) return nullptr;
	if (auto* Socket = UHoseSocketComponent::GetSocketFromHitComponent(Hit.GetComponent())) return Socket;
	if (const auto* Hose = Cast<AHoseCableActor>(Hit.GetActor()))
	{
		if (Hit.GetComponent()==Hose->GetConnectorMesh(Hose->GetSupplyConnectorIndex())) return Cast<UHoseSocketComponent>(Hose->StartPort);
		if (Hit.GetComponent()==Hose->GetConnectorMesh(Hose->GetReceivingConnectorIndex())) return Cast<UHoseSocketComponent>(Hose->EndPort);
	}
	return nullptr;
}

bool UShopPlayerInteractionComponent::BeginSocketUnplugFromView()
{
	auto* Socket = FindHoseSocketInView();
	if (!Socket) return false;
	if (!IsValid(Socket->ConnectedHose)) return !IsCableGrabActive() && !Cast<APressurizedColaContainer>(Socket->GetOwner());
	const auto* Controller = SocketController(GetOwner());
	if (Controller && Controller->IsInputKeyDown(EKeys::E))
	{
		PendingUnplugSocket = Socket;
		PendingUnplugHose = Socket->ConnectedHose.Get();
		GetWorld()->GetTimerManager().SetTimer(SocketUnplugTimer, this, &UShopPlayerInteractionComponent::UpdateSocketUnplugHold, .05f, true);
	}
	return true;
}

void UShopPlayerInteractionComponent::CancelSocketUnplugHold()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SocketUnplugTimer);
	PendingUnplugSocket.Reset();
	PendingUnplugHose.Reset();
}

void UShopPlayerInteractionComponent::UpdateSocketUnplugHold()
{
	auto* Socket = PendingUnplugSocket.Get();
	auto* Hose = PendingUnplugHose.Get();
	const auto* Controller = SocketController(GetOwner());
	if (!Socket || !Hose || Socket->ConnectedHose!=Hose || !Controller || !Controller->IsInputKeyDown(EKeys::E)
		|| FindHoseSocketInView()!=Socket)
	{
		CancelSocketUnplugHold();
		return;
	}
	if (Controller->GetInputKeyTimeDown(EKeys::E) >= UnplugHoldSeconds)
	{
		CancelSocketUnplugHold();
		RemoveSocketHoseFromView(Socket, Hose, false);
	}
}

bool UShopPlayerInteractionComponent::TryGrabSocketHoseFromView(bool bTakeInHand, bool bAnimateConnector)
{
	auto* Socket = FindHoseSocketInView();
	if (!Socket || !IsValid(Socket->ConnectedHose)) return false;
	CancelSocketUnplugHold();
	RemoveSocketHoseFromView(Socket, Socket->ConnectedHose, bTakeInHand, bAnimateConnector);
	return true;
}

bool UShopPlayerInteractionComponent::RemoveSocketHoseFromView(UHoseSocketComponent* Socket, AHoseCableActor* Hose, bool bGrab, bool bAnimateConnector)
{
	if (LocalCableSequence==MAX_int64) return false;
	const int64 Sequence = ++LocalCableSequence;
	if (GetOwner()->HasAuthority())
	{
		if (!ExecuteRemoveSocketHose(Socket, Hose, bGrab, Sequence, bAnimateConnector)) return false;
	}
	else Server_RemoveSocketHose(Socket, Hose, bGrab, Sequence, bAnimateConnector);
	if (bGrab)
	{
		LocalCableGrabSequence = Sequence;
		if (bAnimateConnector) LocalHosePickupPress = Sequence;
		LocalCable = Hose->Cable.Get();
		CableTargetAccumulator = 0;
		SetComponentTickEnabled(true);
	}
	return true;
}

bool UShopPlayerInteractionComponent::ExecuteRemoveSocketHose(UHoseSocketComponent* Socket, AHoseCableActor* ExpectedHose,
	bool bGrab, int64 Sequence, bool bAnimateConnector)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Sequence<=0 || Sequence<=LastCableSequence) return false;
	LastCableSequence = Sequence;
	const auto* Controller = SocketController(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!IsValid(Pawn) || (bGrab && (ServerCable.IsValid() || !HasFreeHands(*Pawn))) || !IsValid(Socket) || !IsValid(ExpectedHose)
		|| Socket->ConnectedHose!=ExpectedHose) return false;
	const bool bSupply = ExpectedHose->StartPort==Socket;
	if (!bSupply && ExpectedHose->EndPort!=Socket) return false;
	const auto* Machine = Cast<AManualColaFillingMachine>(Socket->GetOwner());
	const auto* Volume = Socket->GetTraceVolume();
	const auto* CarriedCable = ServerCable.Get();
	if ((Machine && (!Machine->HoseSockets.Contains(Socket) || Machine->Production->View.bLeverHeld))
		|| !IsValid(Volume) || !CanReachHoseSocket(*Pawn, *Volume, MaxInteractionDistance, *ExpectedHose,
			CarriedCable ? CarriedCable->GetOwner() : nullptr)) return false;

	const int32 Connector = bSupply ? ExpectedHose->GetSupplyConnectorIndex() : ExpectedHose->GetReceivingConnectorIndex();
	const auto* Character = Cast<ACharacter>(Pawn);
	auto* Hand = Character ? Character->GetMesh() : nullptr;
	auto* Cable = ExpectedHose->Cable.Get();
	if (bGrab && (!Hand || !ExpectedHose->UsesConnectorHandGrip() || !Cable->bEnableGrabbing
		|| Cable->FixedPointIndices.Contains(Connector))) return false;
	if (!Socket->DetachHose()) return false;
	if (bGrab)
	{
		if (!BeginHoseGrip(*ExpectedHose, Connector, Pawn, Sequence, bAnimateConnector))
		{
			if (bSupply) ExpectedHose->ConnectSupplyAtConnector(Socket, Connector);
			else ExpectedHose->ConnectReceivingAtConnector(Socket, Connector);
			return false;
		}
		ServerCable = Cable;
		LastCableTargetTime = -1;
	}
	if (Machine) Socket->SetFlowRequested(false);
	return true;
}

void UShopPlayerInteractionComponent::Server_RemoveSocketHose_Implementation(UHoseSocketComponent* Socket, AHoseCableActor* ExpectedHose,
	bool bGrab, int64 Sequence, bool bAnimateConnector)
{
	const bool bRemoved = ExecuteRemoveSocketHose(Socket, ExpectedHose, bGrab, Sequence, bAnimateConnector);
	if (bGrab) Client_CableGrabResult(IsValid(ExpectedHose) ? ExpectedHose->Cable.Get() : nullptr, bRemoved, Sequence);
}

bool UShopPlayerInteractionComponent::ExecuteConnectResourceHose(UCollisionCableComponent* Cable,AActor* SocketActor,int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Sequence<=0 || Sequence<=LastCableSequence) return false;
	LastCableSequence=Sequence;
	auto* Participant=Cast<APawn>(GetOwner());
	if (!Participant) if (auto* Controller=Cast<APlayerController>(GetOwner())) Participant=Controller->GetPawn();
	auto* Hose=IsValid(Cable) ? Cast<AHoseCableActor>(Cable->GetOwner()) : nullptr;
	if (!Participant || !Hose || ServerCable.Get()!=Cable || !IsValid(SocketActor) || SocketActor->GetWorld()!=GetWorld()
		|| SocketActor->IsA<AManualColaFillingMachine>() || SocketActor->IsA<APressurizedColaContainer>() || !CanReach(SocketActor)) return false;
	const int32 HeldPoint = Hose->FindHeldConnector(Participant);
	if (HeldPoint == INDEX_NONE) return false;
	const bool bSource = HeldPoint == Hose->GetSupplyConnectorIndex();
	int32 ResourceID=Hose->Transfer->ResourceID;
	if (bSource && Hose->bMatchSourceResourceWhenEmpty && Hose->Transfer->Buffer.Units<=0)
	{
		if (const auto* Prop=Cast<AActivePropActor>(SocketActor); Prop && Prop->LocalUniqueData.EventData.ContentID>0) ResourceID=Prop->LocalUniqueData.EventData.ContentID;
		else
		{
			TInlineComponentArray<UResourceTransferEndpointComponent*> Ports(SocketActor);
			for (const auto* Authored:Ports)
				if (Authored->Category==Hose->Transfer->Category && Authored->bCanSupply
					&& Authored->SupportsResource(Authored->Category,Authored->ResourceID))
				{ ResourceID=Authored->ResourceID; break; }
		}
	}
	auto* Port = UResourcePortLibrary::ResolveResourcePort(SocketActor,Hose->Transfer->Category,ResourceID,bSource);
	if (!IsValid(Port) || Port->GetWorld()!=GetWorld() || Port->GetOwner()!=SocketActor
		|| FVector::DistSquared(Participant->GetActorLocation(),Port->GetComponentLocation())>FMath::Square(MaxInteractionDistance)) return false;
	FVector Eye; FRotator Rotation; Participant->GetActorEyesViewPoint(Eye,Rotation);
	FHitResult Blocker; FCollisionQueryParams Params(SCENE_QUERY_STAT(ResourceHoseReach),false,Participant);
	if (GetWorld()->LineTraceSingleByChannel(Blocker,Eye,Port->GetComponentLocation(),ECC_Visibility,Params)
		&& Blocker.GetActor()!=SocketActor && Blocker.GetActor()!=Hose) return false;
	const bool Connected = bSource ? Hose->ConnectStart(Port) : Hose->ConnectEnd(Port);
	if (!Connected) return false;
	Cable->EndPointGrab(Participant); ServerCable.Reset(); return true;
}

void UShopPlayerInteractionComponent::Server_ConnectResourceHose_Implementation(UCollisionCableComponent* Cable,AActor* SocketActor,int64 Sequence)
{
	if (ExecuteConnectResourceHose(Cable,SocketActor,Sequence)) Client_CableGrabResult(Cable,false);
}

bool UShopPlayerInteractionComponent::ExecuteConnectSupplyHose(UCollisionCableComponent* Cable,AManualColaFillingMachine* Machine,int32 PortIndex,int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Sequence<=LastCableSequence || Sequence<=0) return false;
	LastCableSequence=Sequence;
	auto* Participant=Cast<APawn>(GetOwner()); if (!Participant) if (auto* Controller=Cast<APlayerController>(GetOwner())) Participant=Controller->GetPawn();
	auto* Container=IsValid(Cable) ? Cast<AColaSupplyContainer>(Cable->GetOwner()) : nullptr;
	const auto* Socket = IsValid(Machine) ? Machine->GetHoseSocket(PortIndex) : nullptr;
	if (!Participant || !Container || ServerCable.Get()!=Cable || !Socket || !Socket->GetTraceVolume()
		|| Machine->GetWorld()!=GetWorld() || !CanReach(Machine)) return false;
	const auto Grabs=Cable->GetActiveGrabs();
	if (!Grabs.ContainsByPredicate([&](const auto& Grab){return Grab.Participant==Participant && Grab.PointIndex==Cable->NumSegments;})) return false;
	const FVector Port=Machine->GetHosePortTransform(PortIndex).GetLocation();
	FVector Eye; FRotator ViewRotation; Participant->GetActorEyesViewPoint(Eye,ViewRotation);
	FHitResult Blocker; FCollisionQueryParams Params(SCENE_QUERY_STAT(SupplyHoseReach),false,Participant);
	if (GetWorld()->LineTraceSingleByChannel(Blocker,Eye,Port,ECC_Visibility,Params) && Blocker.GetActor()!=Machine) return false;
	FHitResult Hit(Machine,Socket->GetTraceVolume(),Port,FVector::UpVector);
	if (!Machine->AttachSupplyNozzle(Hit,Container->SupplyNozzle,Container->ISID,Participant)) return false;
	Cable->EndPointGrab(Participant); ServerCable.Reset(); return true;
}
void UShopPlayerInteractionComponent::Server_ConnectSupplyHose_Implementation(UCollisionCableComponent* Cable,AManualColaFillingMachine* Machine,int32 PortIndex,int64 Sequence)
{
	const bool Connected=ExecuteConnectSupplyHose(Cable,Machine,PortIndex,Sequence);
	if (!Connected && ServerCable.Get()==Cable)
	{
		auto* Participant=Cast<APawn>(GetOwner());
		if (!Participant) if (auto* Controller=Cast<APlayerController>(GetOwner())) Participant=Controller->GetPawn();
		Cable->EndPointGrab(Participant); ServerCable.Reset();
	}
	Client_CableGrabResult(Cable,false); // Release the local grab; an accepted connection retains the pinned hose.
}
