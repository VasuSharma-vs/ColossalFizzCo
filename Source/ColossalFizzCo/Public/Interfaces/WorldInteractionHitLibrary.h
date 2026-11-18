#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Interfaces/WorldInteractionHitInterface.h"
#include "WorldInteractionHitLibrary.generated.h"

UCLASS()
class COLOSSALFIZZCO_API UWorldInteractionHitLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Server-only message delivery. Never directly moves the recipient. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Interaction|Hit")
	static bool DispatchWorldHit(const FWorldInteractionHit& Data);
	/** Native receiver first, otherwise the existing BPI_World_Intractions_System Hit Actor message. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Interaction|Hit")
	static bool SendWorldHitToObject(UObject* Receiver, const FWorldInteractionHit& Data);
	/** Optional receiver-owned physical response, capped by target mass and maximum speed change. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Interaction|Hit")
	static bool ApplyBoundedWorldHitImpulse(UPrimitiveComponent* Target, const FWorldInteractionHit& Data, float Scale=1.f, float MaxSpeedChange=250.f);
};
