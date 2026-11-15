#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "CrossplayTypes.h"
#include "CrossplaySubsystem.generated.h"
struct FCrossplayOnlineState;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCrossplayResult,FName,Operation,bool,bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCrossplayStatus,ECrossplayPhase,Phase,const FString&,Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossplayLobbies,const TArray<FCrossplayLobby>&,Lobbies);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossplayMembers,const TArray<FCrossplayMember>&,Members);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossplayFriends,const TArray<FCrossplayFriend>&,Friends);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossplayChat,const FCrossplayChatLine&,Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCrossplayInvite,const FString&,LobbyId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCrossplayVoice,bool,bConnected,const FString&,Message);

/** Single public facade. Authentication/lobbies live across travel; gameplay stays server-authoritative. */
UCLASS()
class UNIVERSALCROSSPLAY_API UCrossplaySubsystem:public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") ECrossplayPhase Phase=ECrossplayPhase::Offline;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FCrossplayIdentity Identity;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") FString LastError;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") TArray<FCrossplayLobby> Lobbies;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") TArray<FCrossplayFriend> Friends;
 UPROPERTY(BlueprintReadOnly,Category="Crossplay") TArray<FCrossplayChatLine> ChatHistory;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayStatus OnStatus;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayResult OnOperation;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayLobbies OnLobbies;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayMembers OnMembersChanged;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayFriends OnFriends;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayChat OnChat;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayInvite OnInvite;
 UPROPERTY(BlueprintAssignable,Category="Crossplay") FCrossplayVoice OnVoice;
 UFUNCTION(BlueprintPure,Category="Crossplay") ECrossplayStore DetectStore() const;
 UFUNCTION(BlueprintPure,Category="Crossplay") TArray<FString> GetConfigurationIssues() const;
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool Login(ECrossplayStore Store=ECrossplayStore::Auto);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool LoginEpicAccountPortal();
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool LoginDeveloper(const FString& ToolAddress,const FString& CredentialName);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool Logout();
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool HostLobby(const FString& Name,bool bPrivate=false);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool FindLobbies();
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool JoinLobby(int32 Index);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool JoinLobbyById(const FString& Id);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool AcceptInvite();
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool LeaveLobby();
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool Reconnect();
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool StartGame();
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool ConfigureLobby(const FString& Name,bool bPrivate);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool ReadFriends(ECrossplayStore Store);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool InviteFriend(int32 FriendIndex);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool InviteProductUser(const FString& ProductUserId);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool KickMember(const FString& OnlineId);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool OpenFriendsOverlay(ECrossplayStore Store);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool SetMicrophoneMuted(bool Muted);
 UFUNCTION(BlueprintPure,Category="Crossplay") bool IsMicrophoneMuted() const;
 UFUNCTION(BlueprintPure,Category="Crossplay") bool HasPendingInvite() const;
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool SetVoiceVolume(float Volume);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool SetPlayerVoiceMuted(const FString& ProductUserId,bool Muted);
 UFUNCTION(BlueprintCallable,Category="Crossplay") TArray<FCrossplayDevice> GetVoiceDevices(bool Input) const;
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool SetVoiceDevice(const FString& Id,bool Input);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool WriteAchievement(ECrossplayStore Store,const FString& Id,float Percent);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool SubmitStat(const FString& Id,int32 Delta);
 UFUNCTION(BlueprintCallable,Category="Crossplay") bool SendChat(const FString& Text);
 UFUNCTION(BlueprintCallable,Category="Crossplay") void SetReady(bool Ready);
 UFUNCTION(BlueprintPure,Category="Crossplay") TArray<FCrossplayMember> GetMembers() const;
 // Internal notification entry points do not accept gameplay mutations.
 void ReceiveChat(const FCrossplayChatLine& Line);
 void ReceiveNativeInvite(const FString& LobbyId);
 void SetPhase(ECrossplayPhase NewPhase,const FString& Message);
 bool Fail(FName Operation,const FString& Message);
private:
 friend class FCrossplayCleanupTest;
 TSharedPtr<FCrossplayOnlineState> Online;
 bool PrepareLogin(ECrossplayStore Store);
 void BindServices();
 void BindVoice();
 void ReleaseVoice();
 bool JoinResult(int32 Index);
 void FinishLeave(bool Success);
 void HandleLoginComplete(int32 User,bool Success,const class FUniqueNetId& Id,const FString& Error);
 void HandleCreate(FName Name,bool Success);
 void HandleFind(bool Success);
 void HandleJoin(FName Name,EOnJoinSessionCompleteResult::Type Result);
 bool TickDeadline(float Dt);
 void NetworkFailure(class UWorld* World,class UNetDriver* Driver,enum ENetworkFailure::Type Type,const FString& Error);
 void TravelFailure(class UWorld* World,enum ETravelFailure::Type Type,const FString& Error);
};
