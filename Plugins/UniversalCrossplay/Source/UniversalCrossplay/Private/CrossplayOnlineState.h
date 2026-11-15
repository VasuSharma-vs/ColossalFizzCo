#pragma once
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "CrossplayTypes.h"
#include "Containers/Ticker.h"
class IVoiceChatUser;
class FSteamCrossplayBridge;
struct FCrossplayOnlineState
{
 IOnlineSubsystem* Provider=nullptr;
 IOnlineIdentityPtr Identity;
 IOnlineSessionPtr Sessions;
 TSharedPtr<FOnlineSessionSearch> Search;
 TArray<FOnlineSessionSearchResult> Results;
 TArray<FUniqueNetIdPtr> FriendIds;
 TArray<ECrossplayStore> FriendStores;
 FOnlineSessionSearchResult Invite;
 FString InviteId,LastLobbyId;
 IVoiceChatUser* Voice=nullptr;
 TSharedPtr<FSteamCrossplayBridge> Steam;
 FDelegateHandle Login,LoginStatus,Logout,Create,Find,Join,Destroy,Start,Update,InviteAccepted,InviteReceived,SessionFailure,Connection,Network,Travel;
 TOptional<FOnlineSessionSettings> BeforeUpdate;
 FTSTicker::FDelegateHandle Ticker;
 FDelegateHandle VoiceJoined,VoiceExited,VoiceDevices;
 FName Pending;
 ECrossplayPhase SearchReturnPhase=ECrossplayPhase::Online;
 double Deadline=0;
 uint64 Epoch=0;
 bool bReturnOnLeave=true,bShuttingDown=false;
 bool bLogoutAfterLeave=false;
 bool bMicMuted=true;
 float Volume=1;
 FString LobbyName;
 bool bPrivate=false;
 static inline const FName SessionName=NAME_GameSession;
};
