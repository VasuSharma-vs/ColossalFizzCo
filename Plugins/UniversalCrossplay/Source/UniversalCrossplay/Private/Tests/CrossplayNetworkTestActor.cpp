#include "CrossplayNetworkTestActor.h"
#include "CrossplaySubsystem.h"
#include "CrossplayNetwork.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
ACrossplayNetworkTestActor::ACrossplayNetworkTestActor(){bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.1f;}
void ACrossplayNetworkTestActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(ACrossplayNetworkTestActor,Stage);}
void ACrossplayNetworkTestActor::ServerPeerAccepted_Implementation(){if(Stage==2) {Stage=3;ForceNetUpdate();UE_LOG(LogTemp,Display,TEXT("UCP_NETWORK_SERVER_PASSED chat_ready_owner_checks=1"));}}
void ACrossplayNetworkTestActor::Tick(float Dt)
{
 Super::Tick(Dt);
#if !UE_BUILD_SHIPPING
 auto* Service=GetGameInstance()->GetSubsystem<UCrossplaySubsystem>();auto* Bridge=GetWorld()->GetSubsystem<UCrossplayWorldSubsystem>();auto* State=Bridge ? Bridge->GetLobbyState() : nullptr;
 if(!State || !Service)return;
 if(HasAuthority() && Stage==0)
 {
  APlayerController* Host=nullptr;APlayerController* Remote=nullptr;
  for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)if(auto* PC=It->Get();PC && PC->PlayerState){if(PC->IsLocalController())Host=PC;else Remote=PC;}
  if(!Host || !Remote || State->Members.Num()!=2)return;
  State->OwnerId=State->Describe(Host).OnlineId;State->SetReady(Host,true);SetOwner(Remote);Stage=1;ForceNetUpdate();
 }
 if(!HasAuthority() && Stage==1 && !bSent)
 {
  auto* PC=Cast<APlayerController>(GetOwner());auto* C=PC ? PC->FindComponentByClass<UCrossplayPlayerComponent>() : nullptr;
  if(!PC || !PC->IsLocalController() || !C)return;
  if(!C->SendChat(TEXT("remote hello")))return;C->SetReady(true);C->RequestStartGame();bSent=true;
 }
 if(HasAuthority() && Stage==1 && Service->ChatHistory.ContainsByPredicate([](const auto& M){return M.Message==TEXT("remote hello");})
   && State->Members.Num()==2 && !State->Members.ContainsByPredicate([](const auto& M){return !M.bReady;}))
 {
  auto* Remote=Cast<APlayerController>(GetOwner());
  if(State->CanStart(Remote) || State->bGameStarted){UE_LOG(LogTemp,Error,TEXT("UCP_NETWORK_FAILED unauthorized start"));Stage=-1;return;}
  APlayerController* Host=nullptr;for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)if(It->Get() && It->Get()->IsLocalController())Host=It->Get();
  if(auto* C=Host ? Host->FindComponentByClass<UCrossplayPlayerComponent>() : nullptr)C->SendChat(TEXT("host ack"));else return;
  Stage=2;ForceNetUpdate();
 }
 if(!HasAuthority() && Stage==2 && Service->ChatHistory.Num()==2 && State->Members.Num()==2 && !State->Members.ContainsByPredicate([](const auto& M){return !M.bReady;}))
 {UE_LOG(LogTemp,Display,TEXT("UCP_NETWORK_CLIENT_PASSED messages=2 members=2 ready=2"));ServerPeerAccepted();}
 if(Stage==3 && !bExitScheduled)
 {
  bExitScheduled=true;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){FPlatformMisc::RequestExit(false);return false;}),HasAuthority()?4.f:2.f);
 }
#endif
}
