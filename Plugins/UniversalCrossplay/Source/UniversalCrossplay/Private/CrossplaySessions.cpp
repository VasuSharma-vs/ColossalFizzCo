#include "CrossplaySubsystem.h"
#include "CrossplayOnlineState.h"
#include "CrossplaySettings.h"
#include "CrossplayNetwork.h"
#include "SteamCrossplayBridge.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

namespace
{
 const FName Compatibility(TEXT("UCP_COMPAT")),Title(TEXT("UCP_TITLE"));
 bool ValidMap(const FSoftObjectPath& Path) {return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName());}
 bool Begin(UCrossplaySubsystem* Self,FCrossplayOnlineState* S,FName Op,ECrossplayPhase P)
 {
  if (!S || !S->Identity || S->Identity->GetLoginStatus(0)!=ELoginStatus::LoggedIn) return Self->Fail(Op,TEXT("Sign in first."));
  if (!S->Pending.IsNone()) return Self->Fail(Op,TEXT("Another online operation is running."));
  S->Pending=Op;S->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;Self->SetPhase(P,TEXT("Working"));return true;
 }
}
void UCrossplaySubsystem::BindServices()
{
 Online->Login=Online->Identity->AddOnLoginCompleteDelegate_Handle(0,FOnLoginCompleteDelegate::CreateUObject(this,&UCrossplaySubsystem::HandleLoginComplete));
 Online->LoginStatus=Online->Identity->AddOnLoginStatusChangedDelegate_Handle(0,FOnLoginStatusChangedDelegate::CreateWeakLambda(this,[this](int32,ELoginStatus::Type,ELoginStatus::Type Status,const FUniqueNetId&)
 {
  if (!Online || Status==ELoginStatus::LoggedIn) return;
  const bool LoggingOut=Online->Pending==TEXT("Logout");ReleaseVoice();Online->Steam.Reset();Identity=FCrossplayIdentity();
  Friends.Reset();Online->FriendIds.Reset();Online->FriendStores.Reset();
  Online->Invite=FOnlineSessionSearchResult();Online->InviteId.Reset();Online->LastLobbyId.Reset();
  if(auto* Player=GetGameInstance()->GetFirstGamePlayer())Player->SetCachedUniqueNetId(FUniqueNetIdRepl());
  if (!LoggingOut && Online->Sessions->GetNamedSession(NAME_GameSession)) {Online->Pending=NAME_None;LeaveLobby();}
  else {Online->Pending=NAME_None;SetPhase(ECrossplayPhase::Offline,TEXT("Signed out"));OnOperation.Broadcast(TEXT("Logout"),true);}
 }));
 Online->Logout=Online->Identity->AddOnLogoutCompleteDelegate_Handle(0,FOnLogoutCompleteDelegate::CreateWeakLambda(this,[this](int32,bool OK)
 {if(Online && Online->Pending==TEXT("Logout")){Online->Pending=NAME_None;if(OK){Identity=FCrossplayIdentity();SetPhase(ECrossplayPhase::Offline,TEXT("Signed out"));}OnOperation.Broadcast(TEXT("Logout"),OK);}}));
 auto& S=Online->Sessions;
 Online->SessionFailure=S->AddOnSessionFailureDelegate_Handle(FOnSessionFailureDelegate::CreateWeakLambda(this,[this](const FUniqueNetId&,ESessionFailure::Type)
 {if(Online){Online->Pending=NAME_None;Fail(TEXT("Session"),TEXT("The online lobby was interrupted; cleaning up the session."));LeaveLobby();}}));
 Online->Update=S->AddOnUpdateSessionCompleteDelegate_Handle(FOnUpdateSessionCompleteDelegate::CreateWeakLambda(this,[this](FName Name,bool OK)
 {
  if(!Online || Name!=NAME_GameSession || Online->Pending!=TEXT("Update"))return;
  Online->Pending=NAME_None;
  if(OK && Online->Steam)Online->Steam->SetJoinableLobby(Online->bPrivate ? FString() : Online->LastLobbyId);
  if(!OK && Online->BeforeUpdate.IsSet())if(auto* Session=Online->Sessions->GetNamedSession(Name))Session->SessionSettings=Online->BeforeUpdate.GetValue();
  Online->BeforeUpdate.Reset();OnOperation.Broadcast(TEXT("Update"),OK);
 }));
 Online->Create=S->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this,&UCrossplaySubsystem::HandleCreate));
 Online->Find=S->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this,&UCrossplaySubsystem::HandleFind));
 Online->Join=S->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this,&UCrossplaySubsystem::HandleJoin));
 Online->Destroy=S->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateWeakLambda(this,[this](FName Name,bool OK){if (Online && Name==NAME_GameSession && Online->Pending==TEXT("Leave")) FinishLeave(OK);}));
 Online->Start=S->AddOnStartSessionCompleteDelegate_Handle(FOnStartSessionCompleteDelegate::CreateWeakLambda(this,[this](FName Name,bool OK)
 {
  if (!Online || Name!=NAME_GameSession || Online->Pending!=TEXT("Start")) return;
  Online->Pending=NAME_None;
  if (!OK) {SetPhase(ECrossplayPhase::Lobby,TEXT("Could not start"));Fail(TEXT("Start"),TEXT("The session could not start."));return;}
  SetPhase(ECrossplayPhase::Playing,TEXT("Loading game"));
  if (auto* W=GetWorld()->GetSubsystem<UCrossplayWorldSubsystem>();W && W->GetLobbyState()) W->GetLobbyState()->bGameStarted=true;
  const bool Travelled=GetWorld()->ServerTravel(GetDefault<UCrossplaySettings>()->GameplayMap.GetLongPackageName(),true);
  OnOperation.Broadcast(TEXT("Start"),Travelled);if (!Travelled) Fail(TEXT("Start"),TEXT("Server travel was rejected."));
 }));
 Online->InviteAccepted=S->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateWeakLambda(this,[this](bool OK,int32 User,FUniqueNetIdPtr,const FOnlineSessionSearchResult& Result)
 {if (Online && OK && User==0 && Result.IsValid()) {Online->Invite=Result;Online->InviteId=Result.GetSessionIdStr();OnInvite.Broadcast(Online->InviteId);}}));
 Online->InviteReceived=S->AddOnSessionInviteReceivedDelegate_Handle(FOnSessionInviteReceivedDelegate::CreateWeakLambda(this,[this](const FUniqueNetId& User,const FUniqueNetId&,const FString&,const FOnlineSessionSearchResult& Result)
 {
  if(!Online || !Online->Identity || !Result.IsValid())return;
  const auto Local=Online->Identity->GetUniquePlayerId(0);if(!Local || !(*Local==User))return;
  const auto* Current=Online->Sessions->GetNamedSession(NAME_GameSession);
  if(Current && Current->GetSessionIdStr()==Result.GetSessionIdStr())return;
  Online->Invite=Result;Online->InviteId=Result.GetSessionIdStr();OnInvite.Broadcast(Online->InviteId);
 }));
 Online->Connection=Online->Provider->AddOnConnectionStatusChangedDelegate_Handle(FOnConnectionStatusChangedDelegate::CreateWeakLambda(this,[this](const FString&,EOnlineServerConnectionStatus::Type,EOnlineServerConnectionStatus::Type Current)
 {if (Online && Current!=EOnlineServerConnectionStatus::Connected && Current!=EOnlineServerConnectionStatus::Normal) OnStatus.Broadcast(Phase,TEXT("Online service connection interrupted; active gameplay may continue."));}));
}
bool UCrossplaySubsystem::HostLobby(const FString& Name,bool Private)
{
 FString Clean;if (!FCrossplayChatPolicy::Validate(Name,Clean) || Clean.Len()>64) return Fail(TEXT("Host"),TEXT("Lobby name must contain 1 to 64 printable characters."));
 if (!ValidMap(GetDefault<UCrossplaySettings>()->LobbyMap)) return Fail(TEXT("Host"),TEXT("Configure and cook a valid lobby map."));
 if (Online && Online->Sessions && Online->Sessions->GetNamedSession(NAME_GameSession)) return Fail(TEXT("Host"),TEXT("Leave the existing session first."));
 if (!Begin(this,Online.Get(),TEXT("Host"),ECrossplayPhase::Creating)) return false;
 Online->LobbyName=Clean;Online->bPrivate=Private;
 FOnlineSessionSettings S;S.NumPublicConnections=Private ? 0 : 2;S.NumPrivateConnections=Private ? 2 : 0;
 S.bIsLANMatch=false;S.bIsDedicated=false;S.bShouldAdvertise=!Private;S.bAllowInvites=true;
 S.bAllowJoinInProgress=true;S.bUsesPresence=false;S.bUseLobbiesIfAvailable=true;S.bUseLobbiesVoiceChatIfAvailable=GetDefault<UCrossplaySettings>()->bEnableVoice;
 S.Set(Compatibility,GetDefault<UCrossplaySettings>()->CompatibilityId,EOnlineDataAdvertisementType::ViaOnlineService);
 S.Set(Title,Clean,EOnlineDataAdvertisementType::ViaOnlineService);
 if (!Online->Sessions->CreateSession(0,NAME_GameSession,S)) {Online->Pending=NAME_None;SetPhase(ECrossplayPhase::Online,TEXT("Could not host"));return Fail(TEXT("Host"),TEXT("Lobby creation was rejected."));}
 return true;
}
void UCrossplaySubsystem::HandleCreate(FName Name,bool OK)
{
 if (!Online || Name!=NAME_GameSession) return;
 if (Online->Pending!=TEXT("Host")) {if (OK) Online->Sessions->DestroySession(Name);return;}
 Online->Pending=NAME_None;
 if (!OK) {SetPhase(ECrossplayPhase::Online,TEXT("Could not host"));Fail(TEXT("Host"),TEXT("EOS could not create the lobby; check deployment policy and connectivity."));return;}
 if (const auto* Session=Online->Sessions->GetNamedSession(Name)) Online->LastLobbyId=Session->GetSessionIdStr();
 if (Online->Steam) Online->Steam->SetJoinableLobby(Online->bPrivate ? FString() : Online->LastLobbyId);
 SetPhase(ECrossplayPhase::Lobby,TEXT("Lobby created"));OnOperation.Broadcast(TEXT("Host"),true);
 UGameplayStatics::OpenLevel(this,FName(*GetDefault<UCrossplaySettings>()->LobbyMap.GetLongPackageName()),true,TEXT("listen"));
}
bool UCrossplaySubsystem::FindLobbies()
{
 const auto ReturnPhase=(Phase==ECrossplayPhase::Lobby || Phase==ECrossplayPhase::Playing) ? Phase : ECrossplayPhase::Online;
 if (!Begin(this,Online.Get(),TEXT("Find"),ECrossplayPhase::Searching)) return false;
 Online->SearchReturnPhase=ReturnPhase;
 Online->Search=MakeShared<FOnlineSessionSearch>();Online->Search->MaxSearchResults=30;Online->Search->bIsLanQuery=false;
 Online->Search->QuerySettings.Set(FName(TEXT("LOBBYSEARCH")),true,EOnlineComparisonOp::Equals);
 Online->Search->QuerySettings.Set(Compatibility,GetDefault<UCrossplaySettings>()->CompatibilityId,EOnlineComparisonOp::Equals);
 if (!Online->Sessions->FindSessions(0,Online->Search.ToSharedRef())) {Online->Pending=NAME_None;SetPhase(ReturnPhase,TEXT("Search could not start"));return Fail(TEXT("Find"),TEXT("Lobby search could not start."));}
 return true;
}
void UCrossplaySubsystem::HandleFind(bool OK)
{
 if (!Online || Online->Pending!=TEXT("Find")) return;
 Online->Pending=NAME_None;Lobbies.Reset();Online->Results.Reset();
 if (OK && Online->Search) for (const auto& R:Online->Search->SearchResults)
 {
  FString Compatible;R.Session.SessionSettings.Get(Compatibility,Compatible);
  if (!R.IsValid() || Compatible!=GetDefault<UCrossplaySettings>()->CompatibilityId) continue;
  FCrossplayLobby L;L.Id=R.GetSessionIdStr();R.Session.SessionSettings.Get(Title,L.Name);L.OwnerName=R.Session.OwningUserName.Left(64);
  L.Capacity=R.Session.SessionSettings.NumPublicConnections+R.Session.SessionSettings.NumPrivateConnections;
  L.Players=L.Capacity-R.Session.NumOpenPublicConnections-R.Session.NumOpenPrivateConnections;L.bPrivate=!R.Session.SessionSettings.bShouldAdvertise;
  Online->Results.Add(R);Lobbies.Add(L);
 }
 SetPhase(Online->SearchReturnPhase,OK ? TEXT("Search complete") : TEXT("Search failed"));OnLobbies.Broadcast(Lobbies);OnOperation.Broadcast(TEXT("Find"),OK);
}
bool UCrossplaySubsystem::JoinLobby(int32 Index) {return JoinResult(Index);}
bool UCrossplaySubsystem::JoinResult(int32 Index)
{
 if (!Online || !Online->Results.IsValidIndex(Index)) return Fail(TEXT("Join"),TEXT("Select a current lobby result."));
 const auto& R=Online->Results[Index];FString Compatible;R.Session.SessionSettings.Get(Compatibility,Compatible);
 if (Compatible!=GetDefault<UCrossplaySettings>()->CompatibilityId) return Fail(TEXT("Join"),TEXT("This lobby uses an incompatible build."));
 if (Online->Sessions->GetNamedSession(NAME_GameSession)) return Fail(TEXT("Join"),TEXT("Leave the current session first."));
 if (!Begin(this,Online.Get(),TEXT("Join"),ECrossplayPhase::Joining)) return false;
 Online->LastLobbyId=R.GetSessionIdStr();
 if (!Online->Sessions->JoinSession(0,NAME_GameSession,R)) {Online->Pending=NAME_None;return Fail(TEXT("Join"),TEXT("Joining could not start."));}
 return true;
}
void UCrossplaySubsystem::HandleJoin(FName Name,EOnJoinSessionCompleteResult::Type Result)
{
 if (!Online || Name!=NAME_GameSession || Online->Pending!=TEXT("Join")) return;
 Online->Pending=NAME_None;
 FString Address;auto* PC=GetGameInstance()->GetFirstLocalPlayerController();
 if (Result!=EOnJoinSessionCompleteResult::Success || !PC || !Online->Sessions->GetResolvedConnectString(Name,Address) || Address.IsEmpty())
 {SetPhase(ECrossplayPhase::Online,TEXT("Could not join"));Fail(TEXT("Join"),TEXT("The lobby is full, unavailable or has no reachable host."));if (Online->Sessions->GetNamedSession(Name)) Online->Sessions->DestroySession(Name);return;}
 Online->Invite=FOnlineSessionSearchResult();Online->InviteId.Reset();
 SetPhase(ECrossplayPhase::Lobby,TEXT("Connecting to host"));OnOperation.Broadcast(TEXT("Join"),true);PC->ClientTravel(Address,TRAVEL_Absolute);
}
bool UCrossplaySubsystem::JoinLobbyById(const FString& Id)
{
 if (Id.IsEmpty() || Id.Len()>128) return false;
 for (TCHAR C:Id) if (!FChar::IsAlnum(C) && C!='-' && C!='_') return false;
 if (!Begin(this,Online.Get(),TEXT("Resolve"),ECrossplayPhase::Searching)) return false;
 const auto User=Online->Identity->GetUniquePlayerId(0);const auto SessionId=Online->Sessions->CreateSessionIdFromString(Id);
 if (!User || !SessionId) {Online->Pending=NAME_None;return Fail(TEXT("Join"),TEXT("The invitation identity is invalid."));}
 const bool Started=Online->Sessions->FindSessionById(*User,*SessionId,*User,FOnSingleSessionResultCompleteDelegate::CreateWeakLambda(this,[this](int32,bool OK,const FOnlineSessionSearchResult& R)
 {if (!Online || Online->Pending!=TEXT("Resolve")) return;Online->Pending=NAME_None;if (!OK || !R.IsValid()) {Fail(TEXT("Join"),TEXT("The invited lobby is no longer available."));SetPhase(ECrossplayPhase::Online,TEXT("Invitation expired"));return;}Online->Results.Add(R);JoinResult(Online->Results.Num()-1);}));
 if (!Started) {Online->Pending=NAME_None;return Fail(TEXT("Join"),TEXT("Invitation lookup could not start."));}return true;
}
bool UCrossplaySubsystem::AcceptInvite()
{
 if (!Online) return false;
 if (Online->Invite.IsValid()) {Online->Results.Add(Online->Invite);return JoinResult(Online->Results.Num()-1);}
 return JoinLobbyById(Online->InviteId);
}
bool UCrossplaySubsystem::LeaveLobby()
{
 if (!Online || !Online->Pending.IsNone()) return false;
 if (Online->Steam) Online->Steam->SetJoinableLobby(TEXT(""));
 if (!Online->Sessions || !Online->Sessions->GetNamedSession(NAME_GameSession)) {FinishLeave(true);return true;}
 Online->Pending=TEXT("Leave");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 SetPhase(ECrossplayPhase::Leaving,TEXT("Leaving lobby"));
 if (!Online->Sessions->DestroySession(NAME_GameSession)) {Online->Pending=NAME_None;return Fail(TEXT("Leave"),TEXT("Session cleanup could not start; retry before hosting again."));}return true;
}
void UCrossplaySubsystem::FinishLeave(bool OK)
{
 if (!Online) return;
 Online->Pending=NAME_None;
 // EOS may report NotFound when a kick already removed both remote membership
 // and the local named session. Absence is a completed, idempotent cleanup.
 if (!OK && Online->Sessions && Online->Sessions->GetNamedSession(NAME_GameSession))
 {SetPhase(ECrossplayPhase::Lobby,TEXT("Cleanup failed; retry leaving the lobby"));Fail(TEXT("Leave"),TEXT("Session cleanup failed; retry before hosting again."));return;}
 Lobbies.Reset();Online->Results.Reset();ChatHistory.Reset();
 SetPhase(Identity.OnlineId.IsEmpty() ? ECrossplayPhase::Offline : ECrossplayPhase::Online,TEXT("Left lobby"));OnOperation.Broadcast(TEXT("Leave"),true);
 if (Online->bReturnOnLeave && GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone && ValidMap(GetDefault<UCrossplaySettings>()->ReturnMap)) UGameplayStatics::OpenLevel(this,FName(*GetDefault<UCrossplaySettings>()->ReturnMap.GetLongPackageName()));
 if (Online->bLogoutAfterLeave) {Online->bLogoutAfterLeave=false;Logout();}
}
bool UCrossplaySubsystem::Reconnect()
{return Online && !Online->LastLobbyId.IsEmpty() && JoinLobbyById(Online->LastLobbyId);}
bool UCrossplaySubsystem::ConfigureLobby(const FString& Name,bool Private)
{
 if (!Online || !Online->Sessions || !Online->Pending.IsNone() || Phase!=ECrossplayPhase::Lobby) return false;
 auto* S=Online->Sessions->GetNamedSession(NAME_GameSession);FString Clean;
 if (!S || !S->bHosting || !FCrossplayChatPolicy::Validate(Name,Clean) || Clean.Len()>64) return false;
 auto Settings=S->SessionSettings;Settings.bShouldAdvertise=!Private;Settings.NumPublicConnections=Private ? 0 : 2;Settings.NumPrivateConnections=Private ? 2 : 0;Settings.Set(Title,Clean,EOnlineDataAdvertisementType::ViaOnlineService);
 Online->LobbyName=Clean;Online->bPrivate=Private;Online->BeforeUpdate=S->SessionSettings;Online->Pending=TEXT("Update");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 const bool OK=Online->Sessions->UpdateSession(NAME_GameSession,Settings,true);if(!OK){Online->Pending=NAME_None;Online->BeforeUpdate.Reset();}return OK;
}
bool UCrossplaySubsystem::StartGame()
{
 if (!Online || !Online->Sessions || Phase!=ECrossplayPhase::Lobby || !Online->Pending.IsNone() || !GetWorld() || GetWorld()->GetNetMode()!=NM_ListenServer) return false;
 const auto* W=GetWorld()->GetSubsystem<UCrossplayWorldSubsystem>();const auto* State=W ? W->GetLobbyState() : nullptr;
 if (!State || State->OwnerId!=Identity.OnlineId || State->Members.IsEmpty() || State->Members.ContainsByPredicate([](const auto& M){return !M.bReady;}) || !ValidMap(GetDefault<UCrossplaySettings>()->GameplayMap)) return Fail(TEXT("Start"),TEXT("Only the lobby owner can start, after all connected players are ready and a valid game map is configured."));
 if (!Begin(this,Online.Get(),TEXT("Start"),ECrossplayPhase::Starting)) return false;
 if (!Online->Sessions->StartSession(NAME_GameSession)) {Online->Pending=NAME_None;SetPhase(ECrossplayPhase::Lobby,TEXT("Could not start"));return false;}return true;
}
