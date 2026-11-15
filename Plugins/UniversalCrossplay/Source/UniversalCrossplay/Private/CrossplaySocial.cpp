#include "CrossplaySubsystem.h"
#include "CrossplayOnlineState.h"
#include "CrossplaySettings.h"
#include "SteamCrossplayBridge.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineAchievementsInterface.h"
#include "Interfaces/OnlineStatsInterface.h"
#include "IOnlineSubsystemEOS.h"
#include "OnlineSubsystemEOSTypesPublic.h"
#include "IEOSSDKManager.h"
#include "EOSSharedTypes.h"
#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_lobby.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

bool UCrossplaySubsystem::ReadFriends(ECrossplayStore Store)
{
 if (!Online || !Online->Pending.IsNone() || Identity.OnlineId.IsEmpty()) return false;
 IOnlineSubsystem* Source=Store==ECrossplayStore::Steam ? IOnlineSubsystem::Get(FName("STEAM")) : Online->Provider;
 const auto F=Source ? Source->GetFriendsInterface() : nullptr;
 if (!F || !Source->GetIdentityInterface() || Source->GetIdentityInterface()->GetLoginStatus(0)!=ELoginStatus::LoggedIn) return Fail(TEXT("Friends"),TEXT("This platform friends service is unavailable for the signed-in account."));
 Online->Pending=TEXT("Friends");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 const FString List=EFriendsLists::ToString(EFriendsLists::Default);
 const bool OK=F->ReadFriendsList(0,List,FOnReadFriendsListComplete::CreateWeakLambda(this,[this,F,Store](int32,bool Success,const FString& ListName,const FString&)
 {
  if (!Online || Online->Pending!=TEXT("Friends")) return;
  Online->Pending=NAME_None;Friends.Reset();Online->FriendIds.Reset();Online->FriendStores.Reset();
  TArray<TSharedRef<FOnlineFriend>> Values;
  if (Success && F->GetFriendsList(0,ListName,Values)) for (const auto& V:Values)
  {
   FCrossplayFriend Item;Item.Id=V->GetUserId()->ToString();Item.Name=V->GetDisplayName().Left(64);Item.Store=Store;
   Item.bOnline=V->GetPresence().bIsOnline;Item.bJoinable=V->GetPresence().bIsJoinable;
   Friends.Add(Item);Online->FriendIds.Add(V->GetUserId());Online->FriendStores.Add(Store);
  }
  OnFriends.Broadcast(Friends);OnOperation.Broadcast(TEXT("Friends"),Success);
 }));
 if (!OK) {Online->Pending=NAME_None;return Fail(TEXT("Friends"),TEXT("Friends query could not start."));}return true;
}
bool UCrossplaySubsystem::InviteFriend(int32 Index)
{
 if (!Online || !Online->Sessions || !Online->FriendIds.IsValidIndex(Index) || !Online->Sessions->GetNamedSession(NAME_GameSession)) return false;
 const auto Id=Online->FriendIds[Index];if (!Id) return false;
 if (Online->FriendStores[Index]==ECrossplayStore::Steam)
 {
  if (!Online->Steam || !Online->Pending.IsNone()) return false;
  const auto* Session=Online->Sessions->GetNamedSession(NAME_GameSession);
  if (Session->SessionSettings.bShouldAdvertise) return Online->Steam->Invite(Id->ToString(),Online->LastLobbyId);
  // Private EOS lobbies require a REAL EOS invite, not merely a Steam join URI.
  const auto Local=Online->Identity->GetUniquePlayerId(0);
  const auto Platform=static_cast<IOnlineSubsystemEOS*>(Online->Provider)->GetEOSPlatformHandle();
  if (!Local || Local->GetType()!=FName("EOS") || !Platform) return false;
  const auto User=static_cast<const IUniqueNetIdEOS*>(Local.Get())->GetProductUserId();
  const auto Connect=EOS_Platform_GetConnectInterface(*Platform);const auto Lobby=EOS_Platform_GetLobbyInterface(*Platform);
  const FString SteamId=Id->ToString(),LobbyId=Online->LastLobbyId;
  const FTCHARToUTF8 External(*SteamId);const char* Ids[]={External.Get()};
  EOS_Connect_QueryExternalAccountMappingsOptions Options={};Options.ApiVersion=EOS_CONNECT_QUERYEXTERNALACCOUNTMAPPINGS_API_LATEST;Options.LocalUserId=User;Options.AccountIdType=EOS_EExternalAccountType::EOS_EAT_STEAM;Options.ExternalAccountIds=Ids;Options.ExternalAccountIdCount=1;
  Online->Pending=TEXT("Invite");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
  using Query=TEOSCallback<EOS_Connect_OnQueryExternalAccountMappingsCallback,EOS_Connect_QueryExternalAccountMappingsCallbackInfo,FCrossplayOnlineState>;
  auto* Callback=new Query(Online.ToWeakPtr());TWeakObjectPtr<UCrossplaySubsystem> Weak=this;
  Callback->CallbackLambda=[Weak,Platform,User,Connect,Lobby,SteamId,LobbyId](const auto* Data)
  {
   auto* Self=Weak.Get();if (!Self || !Self->Online || Self->Online->Pending!=TEXT("Invite")) return;
   const FTCHARToUTF8 SteamBytes(*SteamId),LobbyBytes(*LobbyId);
   EOS_Connect_GetExternalAccountMappingsOptions Get={};Get.ApiVersion=EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST;Get.LocalUserId=User;Get.AccountIdType=EOS_EExternalAccountType::EOS_EAT_STEAM;Get.TargetExternalUserId=SteamBytes.Get();
   const auto Target=Data->ResultCode==EOS_EResult::EOS_Success ? EOS_Connect_GetExternalAccountMapping(Connect,&Get) : nullptr;
   if (!EOS_ProductUserId_IsValid(Target)) {Self->Online->Pending=NAME_None;Self->Fail(TEXT("Invite"),TEXT("Your friend must sign into this game's EOS deployment once before receiving a private Steam invitation."));return;}
   EOS_Lobby_SendInviteOptions Send={};Send.ApiVersion=EOS_LOBBY_SENDINVITE_API_LATEST;Send.LocalUserId=User;Send.TargetUserId=Target;Send.LobbyId=LobbyBytes.Get();
   using Invite=TEOSCallback<EOS_Lobby_OnSendInviteCallback,EOS_Lobby_SendInviteCallbackInfo,FCrossplayOnlineState>;
   auto* Done=new Invite(Self->Online.ToWeakPtr());Done->CallbackLambda=[Weak,Platform,SteamId,LobbyId](const auto* Result)
   {if (auto* S=Weak.Get();S && S->Online && S->Online->Pending==TEXT("Invite")) {S->Online->Pending=NAME_None;const bool OK=Result->ResultCode==EOS_EResult::EOS_Success && S->Online->Steam && S->Online->Steam->Invite(SteamId,LobbyId);S->OnOperation.Broadcast(TEXT("Invite"),OK);}};
   EOS_Lobby_SendInvite(Lobby,&Send,Done,Done->GetCallbackPtr());
  };
  EOS_Connect_QueryExternalAccountMappings(Connect,&Options,Callback,Callback->GetCallbackPtr());return true;
 }
 return Online->Sessions->SendSessionInviteToFriend(0,NAME_GameSession,*Id);
}
bool UCrossplaySubsystem::OpenFriendsOverlay(ECrossplayStore Store)
{
 if (!Online) return false;
 if (Store==ECrossplayStore::Steam) return Online->Steam && Online->Steam->ShowFriends();
 const auto UI=Online->Provider ? Online->Provider->GetExternalUIInterface() : nullptr;
 return UI && UI->ShowFriendsUI(0);
}
bool UCrossplaySubsystem::InviteProductUser(const FString& Id)
{
 if(!Online || !Online->Provider || !Online->Sessions || !Online->Pending.IsNone() || !Online->Sessions->GetNamedSession(NAME_GameSession) || Id.Len()!=32)return false;
 const auto Local=Online->Identity->GetUniquePlayerId(0);if(!Local || Local->GetType()!=FName("EOS"))return false;
 const auto Target=EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*Id));if(!EOS_ProductUserId_IsValid(Target))return false;
 const auto Platform=static_cast<IOnlineSubsystemEOS*>(Online->Provider)->GetEOSPlatformHandle();if(!Platform)return false;
 const FTCHARToUTF8 LobbyId(*Online->LastLobbyId);
 EOS_Lobby_SendInviteOptions Options={};Options.ApiVersion=EOS_LOBBY_SENDINVITE_API_LATEST;Options.LocalUserId=static_cast<const IUniqueNetIdEOS*>(Local.Get())->GetProductUserId();Options.TargetUserId=Target;Options.LobbyId=LobbyId.Get();
 using Callback=TEOSCallback<EOS_Lobby_OnSendInviteCallback,EOS_Lobby_SendInviteCallbackInfo,FCrossplayOnlineState>;
 auto* Done=new Callback(Online.ToWeakPtr());TWeakObjectPtr<UCrossplaySubsystem> Weak=this;
 Done->CallbackLambda=[Weak,Platform](const auto* Data){if(auto* S=Weak.Get();S && S->Online && S->Online->Pending==TEXT("InviteProductUser")){S->Online->Pending=NAME_None;S->OnOperation.Broadcast(TEXT("InviteProductUser"),Data->ResultCode==EOS_EResult::EOS_Success);}};
 Online->Pending=TEXT("InviteProductUser");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 EOS_Lobby_SendInvite(EOS_Platform_GetLobbyInterface(*Platform),&Options,Done,Done->GetCallbackPtr());return true;
}
bool UCrossplaySubsystem::KickMember(const FString& Id)
{
 if(!Online || !Online->Sessions || !Online->Pending.IsNone() || !GetWorld() || GetWorld()->GetNetMode()!=NM_ListenServer || Id==Identity.OnlineId)return false;
 const auto* Session=Online->Sessions->GetNamedSession(NAME_GameSession);if(!Session || !Session->bHosting)return false;
 const auto Local=Online->Identity->GetUniquePlayerId(0),TargetId=Online->Identity->CreateUniquePlayerId(Id);
 if(!Local || !TargetId || Local->GetType()!=FName("EOS") || TargetId->GetType()!=FName("EOS"))return false;
 TWeakObjectPtr<APlayerController> Player;
 for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)if(auto* PC=It->Get();PC && PC->PlayerState && PC->PlayerState->GetUniqueId().ToString()==Id)Player=PC;
 if(!Player.IsValid())return false;
 const auto Platform=static_cast<IOnlineSubsystemEOS*>(Online->Provider)->GetEOSPlatformHandle();if(!Platform)return false;
 const FTCHARToUTF8 LobbyId(*Online->LastLobbyId);
 EOS_Lobby_KickMemberOptions Options={};Options.ApiVersion=EOS_LOBBY_KICKMEMBER_API_LATEST;Options.LocalUserId=static_cast<const IUniqueNetIdEOS*>(Local.Get())->GetProductUserId();Options.TargetUserId=static_cast<const IUniqueNetIdEOS*>(TargetId.Get())->GetProductUserId();Options.LobbyId=LobbyId.Get();
 using Callback=TEOSCallback<EOS_Lobby_OnKickMemberCallback,EOS_Lobby_KickMemberCallbackInfo,FCrossplayOnlineState>;
 auto* Done=new Callback(Online.ToWeakPtr());TWeakObjectPtr<UCrossplaySubsystem> Weak=this;
 Done->CallbackLambda=[Weak,Platform,Player](const auto* Data)
 {if(auto* S=Weak.Get();S && S->Online && S->Online->Pending==TEXT("Kick")){S->Online->Pending=NAME_None;const bool OK=Data->ResultCode==EOS_EResult::EOS_Success;if(OK && Player.IsValid())if(auto* Mode=S->GetWorld()->GetAuthGameMode();Mode && Mode->GameSession)Mode->GameSession->KickPlayer(Player.Get(),FText::FromString(TEXT("Removed by lobby owner.")));S->OnOperation.Broadcast(TEXT("Kick"),OK);}};
 Online->Pending=TEXT("Kick");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;EOS_Lobby_KickMember(EOS_Platform_GetLobbyInterface(*Platform),&Options,Done,Done->GetCallbackPtr());return true;
}
bool UCrossplaySubsystem::WriteAchievement(ECrossplayStore Store,const FString& Id,float Percent)
{
 if (!Online || !Online->Pending.IsNone() || !FMath::IsFinite(Percent) || Percent<0 || Percent>100 || !GetDefault<UCrossplaySettings>()->AllowedAchievementIds.Contains(Id)) return false;
 auto* Source=Store==ECrossplayStore::Steam ? IOnlineSubsystem::Get(FName("STEAM")) : Online->Provider;
 const auto IdentityAPI=Source ? Source->GetIdentityInterface() : nullptr;const auto API=Source ? Source->GetAchievementsInterface() : nullptr;
 const auto User=IdentityAPI ? IdentityAPI->GetUniquePlayerId(0) : nullptr;
 if (!User || !API) return Fail(TEXT("Achievement"),TEXT("The requested platform achievement service is unavailable."));
 Online->Pending=TEXT("Achievement");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 API->QueryAchievements(*User,FOnQueryAchievementsCompleteDelegate::CreateWeakLambda(this,[this,API,User,Id,Percent](const FUniqueNetId&,bool OK)
 {
  if (!Online || Online->Pending!=TEXT("Achievement")) return;
  if (!OK) {Online->Pending=NAME_None;Fail(TEXT("Achievement"),TEXT("Achievement query failed."));return;}
  auto Write=MakeShared<FOnlineAchievementsWrite>();Write->SetFloatStat(*Id,Percent);
  API->WriteAchievements(*User,Write,FOnAchievementsWrittenDelegate::CreateWeakLambda(this,[this](const FUniqueNetId&,bool Success)
  {if (Online && Online->Pending==TEXT("Achievement")) {Online->Pending=NAME_None;OnOperation.Broadcast(TEXT("Achievement"),Success);}}));
 }));return true;
}
bool UCrossplaySubsystem::SubmitStat(const FString& Id,int32 Delta)
{
 if (!Online || !Online->Provider || !Online->Pending.IsNone() || !GetDefault<UCrossplaySettings>()->AllowedStatisticIds.Contains(Id) || Delta<0) return false;
 const auto API=Online->Provider->GetStatsInterface();const auto User=Online->Identity->GetUniquePlayerId(0);
 if (!API || !User) return false;
 FOnlineStatsUserUpdatedStats Entry(User.ToSharedRef());Entry.Stats.Add(Id,FOnlineStatUpdate(FOnlineStatValue(Delta),FOnlineStatUpdate::EOnlineStatModificationType::Sum));
 Online->Pending=TEXT("Stat");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 API->UpdateStats(User.ToSharedRef(),{Entry},FOnlineStatsUpdateStatsComplete::CreateWeakLambda(this,[this](const FOnlineError& Error)
 {if (Online && Online->Pending==TEXT("Stat")) {Online->Pending=NAME_None;OnOperation.Broadcast(TEXT("Stat"),Error.WasSuccessful());}}));return true;
}
