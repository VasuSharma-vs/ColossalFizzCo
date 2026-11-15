#include "CrossplaySubsystem.h"
#include "CrossplayOnlineState.h"
#include "CrossplaySettings.h"
#include "CrossplayNetwork.h"
#include "SteamCrossplayBridge.h"
#include "EOSSettings.h"
#include "IOnlineSubsystemEOS.h"
#include "OnlineSubsystemEOSTypesPublic.h"
#include "OnlineSubsystemUtils.h"
#include "IEOSSDKManager.h"
#include "VoiceChat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "eos_common.h"

void UCrossplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);Online=MakeShared<FCrossplayOnlineState>();
 Online->Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UCrossplaySubsystem::TickDeadline),.2f);
 if (GEngine) {Online->Network=GEngine->OnNetworkFailure().AddUObject(this,&UCrossplaySubsystem::NetworkFailure);Online->Travel=GEngine->OnTravelFailure().AddUObject(this,&UCrossplaySubsystem::TravelFailure);}
 FString Invite;if (FParse::Value(FCommandLine::Get(),TEXT("CrossplayLobby="),Invite)) ReceiveNativeInvite(Invite);
#if !UE_BUILD_SHIPPING
 if(FParse::Param(FCommandLine::Get(),TEXT("CrossplayNetworkTest"))) FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){FPlatformMisc::RequestExit(false);return false;}),90.f);
#endif
}
void UCrossplaySubsystem::Deinitialize()
{
 if (Online)
 {
  Online->bShuttingDown=true;ReleaseVoice();Online->Steam.Reset();
  FTSTicker::GetCoreTicker().RemoveTicker(Online->Ticker);
  if (GEngine) {GEngine->OnNetworkFailure().Remove(Online->Network);GEngine->OnTravelFailure().Remove(Online->Travel);}
  if (Online->Identity) {Online->Identity->ClearOnLoginCompleteDelegate_Handle(0,Online->Login);Online->Identity->ClearOnLoginStatusChangedDelegate_Handle(0,Online->LoginStatus);Online->Identity->ClearOnLogoutCompleteDelegate_Handle(0,Online->Logout);}
  if (Online->Sessions)
  {
   auto& S=Online->Sessions;S->ClearOnCreateSessionCompleteDelegate_Handle(Online->Create);S->ClearOnFindSessionsCompleteDelegate_Handle(Online->Find);
   S->ClearOnJoinSessionCompleteDelegate_Handle(Online->Join);S->ClearOnDestroySessionCompleteDelegate_Handle(Online->Destroy);S->ClearOnStartSessionCompleteDelegate_Handle(Online->Start);
   S->ClearOnSessionUserInviteAcceptedDelegate_Handle(Online->InviteAccepted);
   S->ClearOnSessionInviteReceivedDelegate_Handle(Online->InviteReceived);
   S->ClearOnUpdateSessionCompleteDelegate_Handle(Online->Update);
   S->ClearOnSessionFailureDelegate_Handle(Online->SessionFailure);
   if (S->GetNamedSession(NAME_GameSession)) S->DestroySession(NAME_GameSession);
  }
  if (Online->Provider) Online->Provider->ClearOnConnectionStatusChangedDelegate_Handle(Online->Connection);
  if (Online->Identity && Online->Identity->GetLoginStatus(0)==ELoginStatus::LoggedIn) Online->Identity->Logout(0);
  Online.Reset();
 }
 Super::Deinitialize();
}
ECrossplayStore UCrossplaySubsystem::DetectStore() const
{
 FString App;
 if (FParse::Value(FCommandLine::Get(),TEXT("epicapp="),App)) return ECrossplayStore::Epic;
 if (!FPlatformMisc::GetEnvironmentVariable(TEXT("SteamAppId")).IsEmpty()) return ECrossplayStore::Steam;
 if (const auto* Steam=IOnlineSubsystem::Get(FName("STEAM")); Steam && Steam->GetIdentityInterface().IsValid() && Steam->GetIdentityInterface()->GetLoginStatus(0)==ELoginStatus::LoggedIn) return ECrossplayStore::Steam;
 return ECrossplayStore::Development;
}
TArray<FString> UCrossplaySubsystem::GetConfigurationIssues() const
{
 TArray<FString> Issues;
 const auto* S=GetDefault<UCrossplaySettings>();
 if (!S->bEnableOnline) Issues.Add(TEXT("Online mode is disabled in Universal Crossplay settings."));
 if (S->CompatibilityId.IsEmpty()) Issues.Add(TEXT("Set a shared CompatibilityId for compatible storefront builds."));
 if (S->LobbyMap.IsNull() || S->GameplayMap.IsNull() || S->ReturnMap.IsNull()) Issues.Add(TEXT("Configure and cook LobbyMap, GameplayMap and ReturnMap."));
 FEOSArtifactSettings Artifact;
 if (!UEOSSettings::GetSelectedArtifactSettings(Artifact) || Artifact.ProductId.IsEmpty() || Artifact.SandboxId.IsEmpty() || Artifact.DeploymentId.IsEmpty() || Artifact.ClientId.IsEmpty() || Artifact.ClientSecret.IsEmpty()) Issues.Add(TEXT("Configure the EOS artifact and client for this deployment."));
 const auto Settings=UEOSSettings::GetSettings();
 if (!Settings.bUseEOSConnect) Issues.Add(TEXT("Enable EOS Connect."));
 FString Default;
 GConfig->GetString(TEXT("OnlineSubsystem"),TEXT("DefaultPlatformService"),Default,GEngineIni);
 if (!Default.Equals(TEXT("EOS"),ESearchCase::IgnoreCase)) Issues.Add(TEXT("Online launches require DefaultPlatformService=EOS and NetDriverEOS; restart after configuring the launch profile."));
 if (const auto* M=IEOSSDKManager::Get(); !M || !M->GetSDKVersion().StartsWith(TEXT("1.19.2.1"))) Issues.Add(TEXT("The running EOS SDK is not the supplied 1.19.2.1; inspect the bootstrap log and restart the editor."));
 return Issues;
}
void UCrossplaySubsystem::SetPhase(ECrossplayPhase P,const FString& Message)
{Phase=P;OnStatus.Broadcast(P,Message);}
bool UCrossplaySubsystem::Fail(FName Op,const FString& Message)
{LastError=Message;OnOperation.Broadcast(Op,false);OnStatus.Broadcast(Phase,Message);return false;}
bool UCrossplaySubsystem::PrepareLogin(ECrossplayStore Store)
{
 if (!Online || !Online->Pending.IsNone()) return Fail(TEXT("Login"),TEXT("Another online operation is still running."));
 const auto Issues=GetConfigurationIssues();if (!Issues.IsEmpty()) return Fail(TEXT("Login"),Issues[0]);
 Store=Store==ECrossplayStore::Auto ? DetectStore() : Store;
 const bool EAS=UEOSSettings::GetSettings().bUseEAS;
 if ((Store==ECrossplayStore::Steam && EAS) || (Store!=ECrossplayStore::Steam && !EAS)) return Fail(TEXT("Login"),TEXT("The launch profile does not match the selected store. Steam uses Connect-only; Epic uses EAS plus Connect."));
 if (!Online->Provider)
 {
  Online->Provider=Online::GetSubsystem(GetWorld(),FName("EOS"));
  if (!Online->Provider) return Fail(TEXT("Login"),TEXT("EOS is unavailable; verify the launch profile and artifact."));
  Online->Identity=Online->Provider->GetIdentityInterface();Online->Sessions=Online->Provider->GetSessionInterface();
  if (!Online->Identity || !Online->Sessions) return Fail(TEXT("Login"),TEXT("EOS identity or lobby interface is unavailable."));
  BindServices();
 }
 if (Online->Identity->GetLoginStatus(0)==ELoginStatus::LoggedIn) return Fail(TEXT("Login"),TEXT("Already signed in; sign out before changing accounts."));
 Identity.Store=Store;Online->Pending=TEXT("Login");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 SetPhase(ECrossplayPhase::Authenticating,TEXT("Signing in"));return true;
}
bool UCrossplaySubsystem::Login(ECrossplayStore Store)
{
 if (!PrepareLogin(Store)) return false;
 if (!Online->Identity->AutoLogin(0)) {Online->Pending=NAME_None;SetPhase(ECrossplayPhase::Offline,TEXT("Sign-in could not start"));return Fail(TEXT("Login"),TEXT("Store credentials are unavailable. Launch through the configured storefront or use developer login."));}
 return true;
}
bool UCrossplaySubsystem::LoginEpicAccountPortal()
{
 if (!PrepareLogin(ECrossplayStore::Epic)) return false;
 if (!Online->Identity->Login(0,FOnlineAccountCredentials(TEXT("accountportal"),TEXT(""),TEXT("")))) {Online->Pending=NAME_None;return Fail(TEXT("Login"),TEXT("Epic account portal could not open."));}
 return true;
}
bool UCrossplaySubsystem::LoginDeveloper(const FString& Address,const FString& Name)
{
#if UE_BUILD_SHIPPING
 return Fail(TEXT("Login"),TEXT("Developer login is disabled in shipping builds."));
#else
 if (Address.IsEmpty() || Address.Len()>128 || Name.IsEmpty() || Name.Len()>64 || !PrepareLogin(ECrossplayStore::Development)) return false;
 if (!Online->Identity->Login(0,FOnlineAccountCredentials(TEXT("developer"),Address,Name))) {Online->Pending=NAME_None;return Fail(TEXT("Login"),TEXT("Developer authentication could not start."));}
 return true;
#endif
}
void UCrossplaySubsystem::HandleLoginComplete(int32 User,bool Success,const FUniqueNetId& Id,const FString&)
{
 if (!Online || User!=0) return;
 if (Online->Pending!=TEXT("Login")) {if (Success) Online->Identity->Logout(0);return;}
 Online->Pending=NAME_None;
 if (!Success || Id.GetType()!=FName("EOS")) {SetPhase(ECrossplayPhase::Offline,TEXT("Sign-in failed"));Fail(TEXT("Login"),TEXT("Authentication failed. Check account access, identity-provider settings and consent; credentials were not logged."));return;}
 const auto* EOSId=static_cast<const IUniqueNetIdEOS*>(&Id);char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH+1];int32 Size=sizeof(Buffer);
 if (!EOS_ProductUserId_IsValid(EOSId->GetProductUserId()) || EOS_ProductUserId_ToString(EOSId->GetProductUserId(),Buffer,&Size)!=EOS_EResult::EOS_Success)
 {Online->Identity->Logout(0);Fail(TEXT("Login"),TEXT("EOS Connect did not provide a valid product user identity."));return;}
 Identity.OnlineId=Id.ToString();Identity.ProductUserId=UTF8_TO_TCHAR(Buffer);Identity.DisplayName=Online->Identity->GetPlayerNickname(Id).Left(64);
 if(auto* Player=GetGameInstance()->GetFirstGamePlayer())Player->SetCachedUniqueNetId(FUniqueNetIdRepl(Id.AsShared()));
 if (Identity.Store==ECrossplayStore::Steam) Online->Steam=MakeShared<FSteamCrossplayBridge>(this);
 if (auto* PC=GetGameInstance()->GetFirstLocalPlayerController(); PC && PC->PlayerState) PC->PlayerState->SetPlayerName(Identity.DisplayName);
 BindVoice();SetPhase(ECrossplayPhase::Online,TEXT("Signed in"));OnOperation.Broadcast(TEXT("Login"),true);
}
bool UCrossplaySubsystem::Logout()
{
 if (!Online || !Online->Identity || !Online->Pending.IsNone()) return false;
 if (Online->Sessions->GetNamedSession(NAME_GameSession)) {Online->bLogoutAfterLeave=true;return LeaveLobby();}
 ReleaseVoice();Online->Steam.Reset();Online->Pending=TEXT("Logout");Online->Deadline=FPlatformTime::Seconds()+GetDefault<UCrossplaySettings>()->OperationTimeout;
 if (!Online->Identity->Logout(0)) {Online->Pending=NAME_None;return Fail(TEXT("Logout"),TEXT("Sign-out could not start."));}
 return true;
}
bool UCrossplaySubsystem::TickDeadline(float)
{
 if (!Online || Online->bShuttingDown) return false;
 if (!Online->Pending.IsNone() && FPlatformTime::Seconds()>Online->Deadline)
 {
  const FName Op=Online->Pending;Online->Pending=NAME_None;
  // A discovery/friends outage must not tear down an unrelated active match.
  // Clear Pending before cancellation so late completion cannot change state.
  if (Op==TEXT("Find") || Op==TEXT("Friends"))
  {
   if (Op==TEXT("Find"))
   {
    if (Online->Sessions) Online->Sessions->CancelFindSessions();
    SetPhase(Online->SearchReturnPhase,TEXT("Lobby search timed out"));
   }
   Fail(Op,TEXT("The query did not finish in time; the current session was preserved."));
   return true;
  }
  if (Op==TEXT("Login") && Online->Identity) Online->Identity->Logout(0);
  if (Online->Sessions && Online->Sessions->GetNamedSession(NAME_GameSession)) Online->Sessions->DestroySession(NAME_GameSession);
  SetPhase(Identity.OnlineId.IsEmpty() ? ECrossplayPhase::Offline : ECrossplayPhase::Online,TEXT("Online operation timed out"));Fail(Op,TEXT("The service did not finish in time; retry after checking connectivity."));
 }
 return true;
}
void UCrossplaySubsystem::NetworkFailure(UWorld* World,UNetDriver*,ENetworkFailure::Type,const FString&)
{
 if (World!=GetWorld() || !Online || Identity.OnlineId.IsEmpty()) return;
 Online->Pending=NAME_None;Fail(TEXT("Connection"),TEXT("Connection lost. The host may have left; reconnect is available while the lobby exists."));LeaveLobby();
}
void UCrossplaySubsystem::TravelFailure(UWorld* World,ETravelFailure::Type,const FString&)
{if (World==GetWorld() && Online) {Online->Pending=NAME_None;Fail(TEXT("Travel"),TEXT("Could not load the session map; verify that the configured maps are cooked."));LeaveLobby();}}
bool UCrossplaySubsystem::SendChat(const FString& Text)
{auto* PC=GetGameInstance()->GetFirstLocalPlayerController();auto* C=PC ? PC->FindComponentByClass<UCrossplayPlayerComponent>() : nullptr;return C && C->SendChat(Text);}
void UCrossplaySubsystem::SetReady(bool Ready)
{auto* PC=GetGameInstance()->GetFirstLocalPlayerController();if (auto* C=PC ? PC->FindComponentByClass<UCrossplayPlayerComponent>() : nullptr) C->SetReady(Ready);}
TArray<FCrossplayMember> UCrossplaySubsystem::GetMembers() const
{auto* S=GetWorld() ? GetWorld()->GetSubsystem<UCrossplayWorldSubsystem>() : nullptr;return S && S->GetLobbyState() ? S->GetLobbyState()->Members : TArray<FCrossplayMember>();}
void UCrossplaySubsystem::ReceiveChat(const FCrossplayChatLine& Line)
{if (ChatHistory.Num()>=50) ChatHistory.RemoveAt(0);ChatHistory.Add(Line);OnChat.Broadcast(Line);}
void UCrossplaySubsystem::ReceiveNativeInvite(const FString& Id)
{
 if (!Online || Id.IsEmpty() || Id.Len()>128) return;
 for (TCHAR C:Id) if (!FChar::IsAlnum(C) && C!='-' && C!='_') return;
 Online->Invite=FOnlineSessionSearchResult();Online->InviteId=Id;OnInvite.Broadcast(Id);
}
