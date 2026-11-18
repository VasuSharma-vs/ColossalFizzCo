#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/ResourceTransferTypes.h"
#include "ResourcePortLibrary.generated.h"
class UResourceTransferEndpointComponent;

UCLASS()
class COLOSSALFIZZCO_API UResourcePortLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Use authored ports, or create a managed content adapter on a bottle/container. Never creates fuel/electric stores on props. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Resource Transfer")
    static UResourceTransferEndpointComponent* ResolveResourcePort(AActor* Actor,EResourceTransportCategory Category,int32 ResourceID,bool bAsSource);
};
