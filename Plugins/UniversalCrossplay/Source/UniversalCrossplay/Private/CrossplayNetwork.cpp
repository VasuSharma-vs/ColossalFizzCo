#include "CrossplayNetwork.h"
#include "CrossplaySubsystem.h"
#include "CrossplaySettings.h"
#include "CrossplayNetworkTestActor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "InternetAddrEOS.h"
#include "OnlineSubsystemEOSTypesPublic.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
 ACrossplayLobbyState* FindState(UWorld* World)
 {
  if (World) for (TActorIterator<ACrossplayLobbyState> It(World);It;++It) return *It;
  return nullptr;
 }
 UCrossplaySubsystem* Service(UWorld* World)
 {return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCrossplaySubsystem>() : nullptr;}
}
UCrossplayPlayerComponent::UCrossplayPlayerComponent() {SetIsReplicatedByDefault(true);PrimaryComponentTick.bCanEverTick=false;}
void UCrossplayPlayerComponent::BeginPlay()
{
 Super::BeginPlay();if (GetOwner()->HasAuthority()) if (auto* S=FindState(GetWorld())) S->Register(Cast<APlayerController>(GetOwner()));
 if (auto* PC=Cast<APlayerController>(GetOwner());PC && PC->IsLocalController()) if (const auto* S=Service(GetWorld())) ServerReportPlatform(S->Identity.Store);
}
void UCrossplayPlayerComponent::EndPlay(const EEndPlayReason::Type Reason)
{ if (GetOwner() && GetOwner()->HasAuthority()) if (auto* S=FindState(GetWorld())) S->Remove(Cast<APlayerController>(GetOwner()));Super::EndPlay(Reason); }
bool UCrossplayPlayerComponent::SendChat(const FString& Message)
{
 FString Clean;
 if (LocalSequence==MAX_uint32 || !FCrossplayChatPolicy::Validate(Message,Clean)) return false;
 FCrossplayChatPayload P;P.Text=Clean;ServerSendChat(P,++LocalSequence);return true;
}
void UCrossplayPlayerComponent::SetReady(bool Ready) {ServerSetReady(Ready);}
void UCrossplayPlayerComponent::RequestStartGame() {ServerStartGame();}
void UCrossplayPlayerComponent::ServerSendChat_Implementation(FCrossplayChatPayload P,uint32 Sequence)
{
 auto* PC=Cast<APlayerController>(GetOwner());FString Clean;
 // Controllers/components can survive seamless travel while world time resets.
 // A monotonic process clock keeps the rate budget valid across that transition.
 if (!PC || !PC->HasAuthority() || !ChatPolicy.Accept(Sequence,FPlatformTime::Seconds()) || !FCrossplayChatPolicy::Validate(P.Text,Clean)) return;
 if (auto* S=FindState(GetWorld())) S->BroadcastChat(PC,Clean);
}
void UCrossplayPlayerComponent::ServerSetReady_Implementation(bool Ready)
{const double Now=FPlatformTime::Seconds();if(Now-LastReadyChange<.1)return;LastReadyChange=Now;if (auto* S=FindState(GetWorld())) S->SetReady(Cast<APlayerController>(GetOwner()),Ready);}
void UCrossplayPlayerComponent::ServerReportPlatform_Implementation(ECrossplayStore Store)
{
 if (bPlatformReported || (Store!=ECrossplayStore::Steam && Store!=ECrossplayStore::Epic && Store!=ECrossplayStore::Development)) return;
 if (auto* S=FindState(GetWorld())) {S->ReportPlatform(Cast<APlayerController>(GetOwner()),Store);bPlatformReported=true;}
}
void UCrossplayPlayerComponent::ServerStartGame_Implementation()
{ if (auto* S=FindState(GetWorld()); S && S->CanStart(Cast<APlayerController>(GetOwner()))) if (auto* O=Service(GetWorld())) O->StartGame(); }
ACrossplayLobbyState::ACrossplayLobbyState() {bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=false;}
void ACrossplayLobbyState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(ACrossplayLobbyState,Members);DOREPLIFETIME(ACrossplayLobbyState,OwnerId);DOREPLIFETIME(ACrossplayLobbyState,bGameStarted);}
FCrossplayMember ACrossplayLobbyState::Describe(APlayerController* PC) const
{
 FCrossplayMember M;
 if (!PC || PC->GetWorld()!=GetWorld() || !PC->PlayerState) return M;
 const auto Id=PC->PlayerState->GetUniqueId();
 M.OnlineId=Id.IsValid() ? Id.ToString().Left(128) : FString::Printf(TEXT("local:%d"),PC->PlayerState->GetPlayerId());
 M.Name=PC->PlayerState->GetPlayerName().Left(64);
 // Platform label is presentation only. Never trust it for permissions.
 M.Platform=Id.IsValid() ? Id.GetType().ToString().Left(16) : TEXT("Local");
 M.bOwner=M.OnlineId==OwnerId;return M;
}
void ACrossplayLobbyState::Register(APlayerController* PC)
{
 if (!HasAuthority() || !PC || !PC->PlayerState || Connections.Contains(PC)) return;
 if(GetDefault<UCrossplaySettings>()->bEnableOnline && PC->NetConnection)
 {
  const auto Id=PC->PlayerState->GetUniqueId();if(!Id.IsValid())return;
  const auto NetId=Id.IsV1() ? Id.GetV1() : nullptr;const auto Address=PC->NetConnection->GetRemoteAddr();
  bool Verified=false;
  if(NetId && NetId->GetType()==FName("EOS") && Address)
  {
   FInternetAddrEOS Expected(static_cast<const IUniqueNetIdEOS*>(NetId.Get())->GetProductUserId());
   Verified=Expected.IsValid() && Address->GetProtocolType()==Expected.GetProtocolType() && Expected.operator==(*Address);
  }
  if(!Verified){if(auto* Mode=GetWorld()->GetAuthGameMode();Mode && Mode->GameSession)Mode->GameSession->KickPlayer(PC,FText::FromString(TEXT("Online identity does not match the EOS connection.")));return;}
 }
 const auto M=Describe(PC);if (M.OnlineId.IsEmpty() || Members.Num()>=2) return;
 if (Members.ContainsByPredicate([&](const auto& X){return X.OnlineId==M.OnlineId;})) return;
 if (OwnerId.IsEmpty()) if (const auto* S=Service(GetWorld())) OwnerId=S->Identity.OnlineId;
 Connections.Add(PC,M.OnlineId);auto Added=M;Added.bOwner=Added.OnlineId==OwnerId;Members.Add(Added);OnRepMembers();ForceNetUpdate();
}
void ACrossplayLobbyState::Remove(APlayerController* PC)
{
 if (!HasAuthority()) return;
 const auto* Id=Connections.Find(PC);if (!Id) return;
 const FString Key=*Id;Connections.Remove(PC);Members.RemoveAll([&](const auto& M){return M.OnlineId==Key;});OnRepMembers();ForceNetUpdate();
}
bool ACrossplayLobbyState::SetReady(APlayerController* PC,bool Ready)
{
 if (!HasAuthority() || bGameStarted) return false;
 Register(PC);const auto* Id=Connections.Find(PC);if (!Id) return false;
 for (auto& M:Members) if (M.OnlineId==*Id) {if (M.bReady==Ready) return true;M.bReady=Ready;OnRepMembers();ForceNetUpdate();return true;}
 return false;
}
bool ACrossplayLobbyState::CanStart(APlayerController* PC) const
{
 const auto* Id=Connections.Find(PC);
 return HasAuthority() && !bGameStarted && Id && *Id==OwnerId && !Members.IsEmpty() && !Members.ContainsByPredicate([](const auto& M){return !M.bReady;});
}
void ACrossplayLobbyState::ReportPlatform(APlayerController* PC,ECrossplayStore Store)
{
 if(!HasAuthority())return;Register(PC);const auto* Id=Connections.Find(PC);if(!Id)return;
 for(auto& M:Members)if(M.OnlineId==*Id){M.Platform=Store==ECrossplayStore::Steam ? TEXT("Steam") : Store==ECrossplayStore::Epic ? TEXT("Epic") : TEXT("Development");OnRepMembers();ForceNetUpdate();}
}
void ACrossplayLobbyState::BroadcastChat(APlayerController* PC,const FString& Text)
{
 if (!HasAuthority() || ChatSequence==MAX_int64 || !Connections.Contains(PC)) return;
 FCrossplayChatLine Line;Line.Sender=Describe(PC);Line.Message=Text;Line.Sequence=++ChatSequence;MulticastChat(Line);
}
void ACrossplayLobbyState::MulticastChat_Implementation(const FCrossplayChatLine& Line)
{if (auto* S=Service(GetWorld())) S->ReceiveChat(Line);}
void ACrossplayLobbyState::OnRepMembers()
{
 if(auto* S=Service(GetWorld()))
 {
  S->OnMembersChanged.Broadcast(Members);
  // A final replication update from the old connection must not reopen a
  // lobby after leave/kick cleanup has put the service back online/offline.
  const bool Receiving=S->Phase==ECrossplayPhase::Joining || S->Phase==ECrossplayPhase::Lobby
   || S->Phase==ECrossplayPhase::Starting || S->Phase==ECrossplayPhase::Playing || S->Phase==ECrossplayPhase::Searching;
  if(Receiving && GetNetMode()==NM_Client && !S->Identity.OnlineId.IsEmpty())
   S->SetPhase(bGameStarted ? ECrossplayPhase::Playing : ECrossplayPhase::Lobby,bGameStarted ? TEXT("Game connected") : TEXT("Lobby connected"));
 }
}
bool UCrossplayWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const {return Type==EWorldType::Game || Type==EWorldType::PIE;}
ACrossplayLobbyState* UCrossplayWorldSubsystem::GetLobbyState() const {return State ? State.Get() : FindState(GetWorld());}
void UCrossplayWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
 Super::OnWorldBeginPlay(World);const auto* S=Service(GetWorld());
 const bool OnlineWorld=S && (S->Phase==ECrossplayPhase::Lobby || S->Phase==ECrossplayPhase::Playing);
 if (GetDefault<UCrossplaySettings>()->bAutomaticPlayerComponents || OnlineWorld) EnablePlayerComponents();
 if (OnlineWorld) if (auto* Mode=World.GetAuthGameMode()) {Mode->bUseSeamlessTravel=true;if (Mode->GameSession) Mode->GameSession->MaxPlayers=2;}
 if (State && S) State->bGameStarted=S->Phase==ECrossplayPhase::Playing;
#if !UE_BUILD_SHIPPING
 if(FParse::Param(FCommandLine::Get(),TEXT("CrossplayNetworkTest")))
 {EnablePlayerComponents();if(GetWorld()->GetNetMode()!=NM_Client)GetWorld()->SpawnActor<ACrossplayNetworkTestActor>();}
#endif
}
void UCrossplayWorldSubsystem::EnablePlayerComponents()
{
 if (GetWorld()->GetNetMode()==NM_Client) return;
 if (!State) State=GetWorld()->SpawnActor<ACrossplayLobbyState>();
 AttachPlayers();if (!GetWorld()->GetTimerManager().IsTimerActive(Poll)) GetWorld()->GetTimerManager().SetTimer(Poll,this,&UCrossplayWorldSubsystem::AttachPlayers,.5f,true);
}
void UCrossplayWorldSubsystem::AttachPlayers()
{
 for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It) if (auto* PC=It->Get(); PC && PC->PlayerState)
 {
  if (!PC->FindComponentByClass<UCrossplayPlayerComponent>()) {auto* C=NewObject<UCrossplayPlayerComponent>(PC);PC->AddInstanceComponent(C);C->RegisterComponent();}
  if (State) State->Register(PC);
 }
}
void UCrossplayWorldSubsystem::Deinitialize()
{GetWorld()->GetTimerManager().ClearTimer(Poll);Super::Deinitialize();}
