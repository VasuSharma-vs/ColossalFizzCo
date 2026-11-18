#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ColaSupplyContainer.h"
#include "Components/CollisionCableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Resources/HoseCableActor.h"
#include "Resources/ResourcePortLibrary.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Interfaces/ResourceEndpointProvider.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

bool UShopPlayerInteractionComponent::TryConnectSupplyHoseFromView()
{
	if (!LocalCable.IsValid() || !GetWorld()) return false;
	auto* Pawn=Cast<APawn>(GetOwner()); auto* Controller=Pawn ? Cast<APlayerController>(Pawn->GetController()) : Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController()) return false;
	if (!Pawn) Pawn=Controller->GetPawn();
	FVector Eye; FRotator Direction; Controller->GetPlayerViewPoint(Eye,Direction);
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(SupplyHosePort),false,Pawn);
	if (!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Direction.Vector()*MaxInteractionDistance,ECC_Visibility,Params)) return false;
	if (Cast<AHoseCableActor>(LocalCable->GetOwner()))
	{
        if (!Hit.GetActor() || LocalCableSequence==MAX_int64) return false;
		int32 ResourcePortIndex=INDEX_NONE; FTransform PortTransform;
		if (auto* Machine=Cast<AManualColaFillingMachine>(Hit.GetActor()))
			if (!Machine->GetHosePortFromHit(Hit,ResourcePortIndex,PortTransform)) return false;
		const int64 ResourceSequence=++LocalCableSequence;
		if (GetOwner()->HasAuthority())
		{
			if (!ExecuteConnectResourceHose(LocalCable.Get(),Hit.GetActor(),ResourceSequence,ResourcePortIndex)) return false;
			LocalCable.Reset(); SetComponentTickEnabled(false);
		}
		else Server_ConnectResourceHose(LocalCable.Get(),Hit.GetActor(),ResourceSequence,ResourcePortIndex);
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

bool UShopPlayerInteractionComponent::ExecuteConnectResourceHose(UCollisionCableComponent* Cable,AActor* SocketActor,int64 Sequence,int32 PortIndex)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Sequence<=0 || Sequence<=LastCableSequence) return false;
	LastCableSequence=Sequence;
	auto* Participant=Cast<APawn>(GetOwner());
	if (!Participant) if (auto* Controller=Cast<APlayerController>(GetOwner())) Participant=Controller->GetPawn();
	auto* Hose=IsValid(Cable) ? Cast<AHoseCableActor>(Cable->GetOwner()) : nullptr;
	if (!Participant || !Hose || ServerCable.Get()!=Cable || !IsValid(SocketActor) || SocketActor->GetWorld()!=GetWorld()
        || !CanReach(SocketActor)) return false;
	const auto Grabs=Cable->GetActiveGrabs();
	const auto* Grab=Grabs.FindByPredicate([&](const auto& Item){return Item.Participant==Participant;});
	if (!Grab || (Grab->PointIndex!=0 && Grab->PointIndex!=Cable->NumSegments)) return false;
	const bool bSource=Grab->PointIndex==0;
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
	UResourceTransferEndpointComponent* Port=nullptr;
	if (auto* Machine=Cast<AManualColaFillingMachine>(SocketActor))
	{
		if (bSource || Hose->Transfer->Category!=EResourceTransportCategory::Content || !Machine->HosePorts.IsValidIndex(PortIndex)) return false;
		// Check the physical control before changing its typed receiver binding.
		const FVector Mount=Machine->GetHosePortTransform(PortIndex).GetLocation();
		if (FVector::DistSquared(Participant->GetActorLocation(),Mount)>FMath::Square(MaxInteractionDistance)) return false;
		FVector Eye; FRotator View; Participant->GetActorEyesViewPoint(Eye,View);
		FHitResult Blocker; FCollisionQueryParams Params(SCENE_QUERY_STAT(ResourceHoseMachinePort),false,Participant);
		if (GetWorld()->LineTraceSingleByChannel(Blocker,Eye,Mount,ECC_Visibility,Params) && Blocker.GetActor()!=Machine && Blocker.GetActor()!=Hose) return false;
		if (!Machine->PrepareReceivingHosePort(PortIndex,ResourceID)) return false;
		Port=Machine->GetReceivingHosePort(PortIndex);
	}
	else Port=UResourcePortLibrary::ResolveResourcePort(SocketActor,Hose->Transfer->Category,ResourceID,bSource);
	if (!IsValid(Port) || Port->GetWorld()!=GetWorld() || Port->GetOwner()!=SocketActor
		|| FVector::DistSquared(Participant->GetActorLocation(),Port->GetComponentLocation())>FMath::Square(MaxInteractionDistance)) return false;
	FVector Eye; FRotator Rotation; Participant->GetActorEyesViewPoint(Eye,Rotation);
	FHitResult Blocker; FCollisionQueryParams Params(SCENE_QUERY_STAT(ResourceHoseReach),false,Participant);
	if (GetWorld()->LineTraceSingleByChannel(Blocker,Eye,Port->GetComponentLocation(),ECC_Visibility,Params)
		&& Blocker.GetActor()!=SocketActor && Blocker.GetActor()!=Hose) return false;
	const bool Connected=Grab->PointIndex==0 ? Hose->ConnectStart(Port) : Hose->ConnectEnd(Port);
	if (!Connected) return false;
	Cable->EndPointGrab(Participant); ServerCable.Reset(); return true;
}

void UShopPlayerInteractionComponent::Server_ConnectResourceHose_Implementation(UCollisionCableComponent* Cable,AActor* SocketActor,int64 Sequence,int32 PortIndex)
{
	if (ExecuteConnectResourceHose(Cable,SocketActor,Sequence,PortIndex)) Client_CableGrabResult(Cable,false);
}

bool UShopPlayerInteractionComponent::ExecuteConnectSupplyHose(UCollisionCableComponent* Cable,AManualColaFillingMachine* Machine,int32 PortIndex,int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Sequence<=LastCableSequence || Sequence<=0) return false;
	LastCableSequence=Sequence;
	auto* Participant=Cast<APawn>(GetOwner()); if (!Participant) if (auto* Controller=Cast<APlayerController>(GetOwner())) Participant=Controller->GetPawn();
	auto* Container=IsValid(Cable) ? Cast<AColaSupplyContainer>(Cable->GetOwner()) : nullptr;
	if (!Participant || !Container || ServerCable.Get()!=Cable || !IsValid(Machine) || !Machine->HosePorts.IsValidIndex(PortIndex)
		|| Machine->GetWorld()!=GetWorld() || !CanReach(Machine)) return false;
	const auto Grabs=Cable->GetActiveGrabs();
	if (!Grabs.ContainsByPredicate([&](const auto& Grab){return Grab.Participant==Participant && Grab.PointIndex==Cable->NumSegments;})) return false;
	const FVector Port=Machine->GetHosePortTransform(PortIndex).GetLocation();
	FVector Eye; FRotator ViewRotation; Participant->GetActorEyesViewPoint(Eye,ViewRotation);
	FHitResult Blocker; FCollisionQueryParams Params(SCENE_QUERY_STAT(SupplyHoseReach),false,Participant);
	if (GetWorld()->LineTraceSingleByChannel(Blocker,Eye,Port,ECC_Visibility,Params) && Blocker.GetActor()!=Machine) return false;
	FHitResult Hit(Machine,Machine->HosePorts[PortIndex],Machine->GetHosePortTransform(PortIndex).GetLocation(),FVector::UpVector);
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
