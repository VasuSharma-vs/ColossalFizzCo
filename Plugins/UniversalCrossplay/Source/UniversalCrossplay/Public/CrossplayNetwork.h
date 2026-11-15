#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "CrossplayTypes.h"
#include "CrossplayNetwork.generated.h"
class APlayerController;

/** Optional owned RPC endpoint; attach to a PlayerController, not to a global actor. */
UCLASS(ClassGroup=(Online),meta=(BlueprintSpawnableComponent))
class UNIVERSALCROSSPLAY_API UCrossplayPlayerComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UCrossplayPlayerComponent();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool SendChat(const FString& Message);
 UFUNCTION(BlueprintCallable,Category="Crossplay") void SetReady(bool Ready);
 UFUNCTION(BlueprintCallable,Category="Crossplay") void RequestStartGame();
 UFUNCTION(Server,Reliable) void ServerSendChat(FCrossplayChatPayload Payload,uint32 Sequence);
 UFUNCTION(Server,Reliable) void ServerSetReady(bool Ready);
 UFUNCTION(Server,Reliable) void ServerStartGame();
 UFUNCTION(Server,Reliable) void ServerReportPlatform(ECrossplayStore Store);
private:
 uint32 LocalSequence=0;
 FCrossplayChatPolicy ChatPolicy;
 bool bPlatformReported=false;
 double LastReadyChange=-1;
};

/** Small shared lobby state. Gameplay rules, inventory and economy remain in the game. */
UCLASS(NotBlueprintable)
class UNIVERSALCROSSPLAY_API ACrossplayLobbyState:public AActor
{
 GENERATED_BODY()
public:
 ACrossplayLobbyState();
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
 UPROPERTY(ReplicatedUsing=OnRepMembers,BlueprintReadOnly,Category="Crossplay") TArray<FCrossplayMember> Members;
 UPROPERTY(Replicated,BlueprintReadOnly,Category="Crossplay") FString OwnerId;
 UPROPERTY(ReplicatedUsing=OnRepMembers,BlueprintReadOnly,Category="Crossplay") bool bGameStarted=false;
 void Register(APlayerController* PC);
 void Remove(APlayerController* PC);
 bool SetReady(APlayerController* PC,bool Ready);
 void ReportPlatform(APlayerController* PC,ECrossplayStore Store);
 bool CanStart(APlayerController* PC) const;
 void BroadcastChat(APlayerController* PC,const FString& Text);
 FCrossplayMember Describe(APlayerController* PC) const;
 UFUNCTION(NetMulticast,Reliable) void MulticastChat(const FCrossplayChatLine& Line);
 UFUNCTION() void OnRepMembers();
private:
 TMap<TWeakObjectPtr<APlayerController>,FString> Connections;
 int64 ChatSequence=0;
};

UCLASS()
class UNIVERSALCROSSPLAY_API UCrossplayWorldSubsystem:public UWorldSubsystem
{
 GENERATED_BODY()
public:
 virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
 virtual void OnWorldBeginPlay(UWorld& World) override;
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintCallable,Category="Crossplay") void EnablePlayerComponents();
 UFUNCTION(BlueprintPure,Category="Crossplay") ACrossplayLobbyState* GetLobbyState() const;
private:
 UPROPERTY(Transient) TObjectPtr<ACrossplayLobbyState> State;
 FTimerHandle Poll;
 void AttachPlayers();
};
