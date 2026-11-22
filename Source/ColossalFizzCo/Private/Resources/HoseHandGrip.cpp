#include "Resources/HoseCableActor.h"

#include "Components/CollisionCableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
    const FName RequestAttachName(TEXT("Request Attach to Component"));
}

UStaticMeshComponent* AHoseCableActor::GetConnectorMesh(int32 PointIndex) const
{
    if (PointIndex == 0) return StartEndpointMesh.Get();
    return PointIndex == Cable->NumSegments ? EndEndpointMesh.Get() : nullptr;
}

bool AHoseCableActor::UsesConnectorHandGrip() const
{
    return bCarryConnectorOnPickup || FindFunction(RequestAttachName) != nullptr;
}

bool AHoseCableActor::CanGrabConnector(int32 PointIndex) const
{
    if (!GetConnectorMesh(PointIndex) || !Cable->bEnableGrabbing || Cable->FixedPointIndices.Contains(PointIndex)) return false;
    if (Cable->GetActiveGrabs().ContainsByPredicate([PointIndex](const FCableGrabState& Grab)
        { return Grab.PointIndex == PointIndex && Grab.Settings.bApproachingConnector && IsValid(Grab.Participant); })) return false;
    return PointIndex == 0
        ? !Cable->bAttachStart && !IsValid(StartHandGrip.Component)
        : !Cable->bAttachEnd && !IsValid(EndHandGrip.Component);
}

bool AHoseCableActor::RequestConnectorHandGrip(int32 PointIndex, USceneComponent* Hand, FName Socket)
{
    if (!HasAuthority() || GetNetMode() == NM_Client || !CanGrabConnector(PointIndex) || !IsValid(Hand) || RequestedConnectorIndex != INDEX_NONE)
        return false;

    TGuardValue<int32> Selection(RequestedConnectorIndex, PointIndex);
    UFunction* Request = FindFunction(RequestAttachName);
    if (!Request) return bCarryConnectorOnPickup && AttachSelectedConnectorToHand(Hand, Socket);
    auto* ComponentParam = FindFProperty<FObjectPropertyBase>(Request, TEXT("Component"));
    auto* SocketParam = FindFProperty<FNameProperty>(Request, TEXT("Socket Name"));
    const FBoolProperty* SuccessParam = nullptr;
    for (TFieldIterator<FBoolProperty> Param(Request); Param; ++Param)
    {
        if (Param->HasAllPropertyFlags(CPF_Parm | CPF_OutParm))
        {
            SuccessParam = *Param;
            break;
        }
    }
    if (!ComponentParam || !SocketParam || !SuccessParam || !Hand->IsA(ComponentParam->PropertyClass)) return false;

    // The inherited interface takes a hand and socket; the server has already
    // identified which connector was hit. Keep that selection scoped to this call.
    FStructOnScope Parameters(Request);
    ComponentParam->SetObjectPropertyValue_InContainer(Parameters.GetStructMemory(), Hand);
    SocketParam->SetPropertyValue_InContainer(Parameters.GetStructMemory(), Socket);
    ProcessEvent(Request, Parameters.GetStructMemory());
    return SuccessParam->GetPropertyValue_InContainer(Parameters.GetStructMemory())
        && FindHandHeldConnector(Hand->GetOwner()) == PointIndex;
}

bool AHoseCableActor::AttachSelectedConnectorToHand(USceneComponent* Hand, FName Socket)
{
    auto* Connector = GetConnectorMesh(RequestedConnectorIndex);
    if (!HasAuthority() || GetNetMode() == NM_Client || !CanGrabConnector(RequestedConnectorIndex) || !IsValid(Hand) || Hand->GetWorld() != GetWorld())
        return false;

    const auto* Player = Cast<ACharacter>(Hand->GetOwner());
    if (!IsValid(Player) || Player->GetMesh() != Hand || (!Socket.IsNone() && !Hand->DoesSocketExist(Socket))
        || FindHandHeldConnector(Player) != INDEX_NONE)
        return false;

    const bool bStart = RequestedConnectorIndex == 0;
    FHoseHandGrip& Grip = bStart ? StartHandGrip : EndHandGrip;

    if (!Connector->AttachToComponent(Hand, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket)) return false;
    const FTransform& Offset = bStart ? StartMeshOffset : EndMeshOffset;
    Connector->SetRelativeLocationAndRotation(Offset.GetLocation(), Offset.GetRotation());

    Grip.Component = Hand;
    Grip.Socket = Socket;
    RefreshCableAttachments();
    ForceNetUpdate();
    return true;
}

int32 AHoseCableActor::FindHandHeldConnector(const AActor* Holder) const
{
    if (!IsValid(Holder)) return INDEX_NONE;
    if (IsValid(StartHandGrip.Component) && StartHandGrip.Component->GetOwner() == Holder) return 0;
    if (IsValid(EndHandGrip.Component) && EndHandGrip.Component->GetOwner() == Holder) return Cable->NumSegments;
    return INDEX_NONE;
}

bool AHoseCableActor::IsLooseHoseHeldBy(const AActor* Holder) const
{
    return !IsValid(StartPort) && !IsValid(EndPort) && FindHandHeldConnector(Holder) != INDEX_NONE
        && Cable->bAttachStart != Cable->bAttachEnd && Cable->FixedPointIndices.IsEmpty() && Cable->GetActiveGrabs().IsEmpty();
}

int32 AHoseCableActor::FindHeldConnector(const AActor* Holder) const
{
    if (!IsValid(Holder)) return INDEX_NONE;
    const int32 HandIndex = FindHandHeldConnector(Holder);
    if (HandIndex != INDEX_NONE) return HandIndex;

    const auto Grabs = Cable->GetActiveGrabs();
    const auto* Grab = Grabs.FindByPredicate([Holder](const auto& Item)
        { return Item.Participant == Holder && !Item.Settings.bApproachingConnector; });
    return Grab && GetConnectorMesh(Grab->PointIndex) ? Grab->PointIndex : INDEX_NONE;
}

bool AHoseCableActor::ReleaseHandHeldConnector(const AActor* Holder)
{
    if (!HasAuthority() || GetNetMode() == NM_Client) return false;
    const int32 PointIndex = FindHandHeldConnector(Holder);
    if (PointIndex == INDEX_NONE) return false;

    FHoseHandGrip& Grip = PointIndex == 0 ? StartHandGrip : EndHandGrip;
    Grip = FHoseHandGrip();
    RefreshCableAttachments();
    ForceNetUpdate();
    return true;
}
