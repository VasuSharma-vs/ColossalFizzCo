#include "SteamCrossplayBridge.h"
#include "CrossplaySubsystem.h"
#include "Misc/Parse.h"
FSteamCrossplayBridge::FSteamCrossplayBridge(UCrossplaySubsystem* InOwner):Owner(InOwner)
{if (SteamUser() && SteamUser()->BLoggedOn()) Join.Register(this,&FSteamCrossplayBridge::JoinRequested);}
FSteamCrossplayBridge::~FSteamCrossplayBridge() {SetJoinableLobby(TEXT(""));Join.Unregister();}
void FSteamCrossplayBridge::JoinRequested(GameRichPresenceJoinRequested_t* Data)
{FString Id;if (Data && Owner.IsValid() && FParse::Value(UTF8_TO_TCHAR(Data->m_rgchConnect),TEXT("CrossplayLobby="),Id)) Owner->ReceiveNativeInvite(Id);}
bool FSteamCrossplayBridge::Invite(const FString& SteamId,const FString& LobbyId)
{
 uint64 Id=0;if (!SteamFriends() || !SteamUser() || !SteamUser()->BLoggedOn() || !LexTryParseString(Id,*SteamId) || !Id || LobbyId.IsEmpty() || LobbyId.Len()>128) return false;
 const FString Command=TEXT("+CrossplayLobby=")+LobbyId;
 return SteamFriends()->InviteUserToGame(CSteamID(Id),TCHAR_TO_UTF8(*Command));
}
void FSteamCrossplayBridge::SetJoinableLobby(const FString& Id)
{
 if (SteamFriends() && SteamUser() && SteamUser()->BLoggedOn())
 {const FString Command=TEXT("+CrossplayLobby=")+Id;SteamFriends()->SetRichPresence("connect",Id.IsEmpty() ? nullptr : TCHAR_TO_UTF8(*Command));}
}
bool FSteamCrossplayBridge::ShowFriends()
{if (!SteamFriends() || !SteamUtils() || !SteamUtils()->IsOverlayEnabled()) return false;SteamFriends()->ActivateGameOverlay("Friends");return true;}
