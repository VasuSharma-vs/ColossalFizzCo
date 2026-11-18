#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ResourceEndpointProvider.generated.h"

class UResourceTransferEndpointComponent;

UINTERFACE(BlueprintType, Blueprintable)
class COLOSSALFIZZCO_API UResourceEndpointProvider : public UInterface
{
	GENERATED_BODY()
};

/** Resolves a machine or container to its real resource store. Transfer mutations remain native and atomic. */
class COLOSSALFIZZCO_API IResourceEndpointProvider
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Resource Transfer")
	UResourceTransferEndpointComponent* GetResourceTransferEndpoint() const;
};
