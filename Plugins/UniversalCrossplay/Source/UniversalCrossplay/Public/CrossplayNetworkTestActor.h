#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrossplayNetworkTestActor.generated.h"
/** Explicit opt-in development fixture; never spawned in normal or shipping play. */
UCLASS(NotBlueprintable)
class UNIVERSALCROSSPLAY_API ACrossplayNetworkTestActor:public AActor
{
 GENERATED_BODY()
public:
 ACrossplayNetworkTestActor();
 virtual void Tick(float Dt) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
 UPROPERTY(Replicated) int32 Stage=0;
 UFUNCTION(Server,Reliable) void ServerPeerAccepted();
private:
 bool bSent=false,bExitScheduled=false;
};
