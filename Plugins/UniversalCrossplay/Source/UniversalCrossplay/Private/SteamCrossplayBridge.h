#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
class UCrossplaySubsystem;
/** Steam social adapter only. EOS remains the sole lobby/session provider. */
class FSteamCrossplayBridge
{
 TWeakObjectPtr<UCrossplaySubsystem> Owner;
 CCallbackManual<FSteamCrossplayBridge,GameRichPresenceJoinRequested_t> Join;
 void JoinRequested(GameRichPresenceJoinRequested_t* Data);
public:
 explicit FSteamCrossplayBridge(UCrossplaySubsystem* InOwner);
 ~FSteamCrossplayBridge();
 bool Invite(const FString& SteamId,const FString& LobbyId);
 void SetJoinableLobby(const FString& LobbyId);
 bool ShowFriends();
};
