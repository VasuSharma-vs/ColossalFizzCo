#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/ResourceTransferTypes.h"
#include "HoseTransferTestObserver.generated.h"

class UResourceTransferEndpointComponent;
class UHoseTransferComponent;

/** Receives real reflected gameplay callbacks in hose lifecycle regression tests. */
UCLASS(Transient, NotBlueprintable)
class UHoseTransferTestObserver : public UObject
{
	GENERATED_BODY()
public:
	int32 DeliveryCalls=0;
	int32 ReleaseCalls=0;
	float ReleasedUnits=0;
	UFUNCTION() void DestroyHoseOnDelivery(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float Units,float Pressure,float Rate);
	UFUNCTION() void RecordRelease(EResourceTransportCategory Category,int32 ResourceID,float Units);
};
