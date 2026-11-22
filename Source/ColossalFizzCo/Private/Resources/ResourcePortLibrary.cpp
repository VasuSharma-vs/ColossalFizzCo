#include "Resources/ResourcePortLibrary.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Interfaces/ResourceEndpointProvider.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

UResourceTransferEndpointComponent* UResourcePortLibrary::ResolveResourcePort(AActor* Actor,EResourceTransportCategory Category,int32 ResourceID,bool bAsSource)
{
    if (!IsValid(Actor) || !Actor->HasAuthority() || Actor->GetNetMode()==NM_Client || ResourceID<=0) return nullptr;
    UResourceTransferEndpointComponent* Port=nullptr;
    if (Actor->GetClass()->ImplementsInterface(UResourceEndpointProvider::StaticClass()))
        Port=IResourceEndpointProvider::Execute_GetResourceTransferEndpoint(Actor);
    const auto Matches=[&](const UResourceTransferEndpointComponent* Candidate)
    {
        return IsValid(Candidate) && Candidate->GetOwner()==Actor
            && !Candidate->IsA<UHoseSocketComponent>()
            && Candidate->SupportsResource(Category,ResourceID) && (bAsSource ? Candidate->bCanSupply : Candidate->bCanReceive);
    };
    if (Matches(Port) && !Port->ComponentHasTag(TEXT("ManagedHosePort"))) return Port;
    TInlineComponentArray<UResourceTransferEndpointComponent*> Ports(Actor);
    Port=nullptr;
    bool bHasAuthoredPort=false;
    for (auto* Candidate:Ports)
    {
        if (Candidate->ComponentHasTag(TEXT("ManagedHosePort"))) { Port=Candidate; continue; }
        bHasAuthoredPort=true;
        if (Matches(Candidate)) return Candidate;
    }
    // Explicit sockets define the actor's contract; do not invent a bypass around their restrictions.
    if (bHasAuthoredPort) return nullptr;
    auto* Prop=Cast<AActivePropActor>(Actor);
    auto* Manager=Prop ? Prop->PropManagerReference.Get() : nullptr;
    const auto* Record=Manager ? Manager->FindISIDData(Prop->ISID) : nullptr;
    const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
    if (Category!=EResourceTransportCategory::Content || !Product || Product->ConsumableProfile.NumOfPortions<=0
        || Product->bIsBottleCap || !Manager->IsAssemblyCompatible(Record->ProductID,ResourceID,0)) return nullptr;
    if (!Port)
    {
        Port=NewObject<UResourceTransferEndpointComponent>(Prop,TEXT("ManagedHosePort"));
        Prop->AddInstanceComponent(Port); Port->ComponentTags.Add(TEXT("ManagedHosePort"));
        Port->SetupAttachment(Prop->GetRootComponent());
        if (Prop->PropMesh && Prop->PropMesh->GetStaticMesh())
            Port->SetRelativeLocation(FVector(0,0,Prop->PropMesh->GetStaticMesh()->GetBoundingBox().Max.Z));
        Port->RegisterComponent();
    }
    return Port->ConfigureManagedContent(Manager,Prop->ISID,ResourceID) ? Port : nullptr;
}
