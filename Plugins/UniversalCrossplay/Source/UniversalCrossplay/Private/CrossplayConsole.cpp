#include "CrossplaySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "String/LexFromString.h"

// Development access without a menu. The facade retains all validation and
// server permissions; this command supplies no alternate networking path.
#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCrossplayConsole, Log, All);
namespace
{
 void RunCrossplay(const TArray<FString>& Args, UWorld* World)
 {
  auto* Service = World && World->GetGameInstance()
   ? World->GetGameInstance()->GetSubsystem<UCrossplaySubsystem>() : nullptr;
  if (!Service || Args.IsEmpty())
  {
   UE_LOG(LogCrossplayConsole, Display, TEXT("Crossplay.Run login | host <name> | find | join <index> | ready | unready | start | chat <text> | leave | reconnect | logout | status | mute | unmute"));
   return;
  }
  const FString Action = Args[0].ToLower();
  if (Action == TEXT("status"))
  {
   UE_LOG(LogCrossplayConsole, Display, TEXT("Phase=%s Members=%d SearchResults=%d ChatLines=%d"),
    *StaticEnum<ECrossplayPhase>()->GetNameStringByValue(static_cast<int64>(Service->Phase)),
    Service->GetMembers().Num(), Service->Lobbies.Num(), Service->ChatHistory.Num());
   for (int32 Index = 0; Index < Service->Lobbies.Num(); ++Index)
   {
    const auto& Lobby = Service->Lobbies[Index];
    UE_LOG(LogCrossplayConsole, Display, TEXT("Lobby[%d] %s (%d/%d)"), Index, *Lobby.Name, Lobby.Players, Lobby.Capacity);
   }
   // Account identities, tokens, private configuration and chat are not logged.
   return;
  }
  TArray<FString> Tail = Args;
  Tail.RemoveAt(0);
  const FString Text = FString::Join(Tail, TEXT(" "));
  bool Accepted = false;
  if (Action == TEXT("login")) Accepted = Service->LoginEpicAccountPortal();
  else if (Action == TEXT("host")) Accepted = Service->HostLobby(Text, false);
  else if (Action == TEXT("find")) Accepted = Service->FindLobbies();
  else if (Action == TEXT("join"))
  {
   int32 Index = INDEX_NONE;
   Accepted = Args.Num() == 2 && LexTryParseString(Index, *Args[1]) && Service->JoinLobby(Index);
  }
  else if (Action == TEXT("ready") || Action == TEXT("unready"))
  {
   Accepted = !Service->GetMembers().IsEmpty();
   if (Accepted) Service->SetReady(Action == TEXT("ready"));
  }
  else if (Action == TEXT("start")) Accepted = Service->StartGame();
  else if (Action == TEXT("chat")) Accepted = Service->SendChat(Text);
  else if (Action == TEXT("leave")) Accepted = Service->LeaveLobby();
  else if (Action == TEXT("reconnect")) Accepted = Service->Reconnect();
  else if (Action == TEXT("logout")) Accepted = Service->Logout();
  else if (Action == TEXT("mute") || Action == TEXT("unmute")) Accepted = Service->SetMicrophoneMuted(Action == TEXT("mute"));
  else
  {
   UE_LOG(LogCrossplayConsole, Display, TEXT("Unknown action. Run Crossplay.Run without arguments for usage."));
   return;
  }
  UE_LOG(LogCrossplayConsole, Display, TEXT("Request %s: %s. Completion is asynchronous; inspect OnOperation or status."),
   *Action, Accepted ? TEXT("accepted") : TEXT("rejected"));
 }
 FAutoConsoleCommandWithWorldAndArgs CrossplayRun(
  TEXT("Crossplay.Run"), TEXT("Development-only multiplayer facade access. Run without arguments for usage."),
  FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCrossplay));
}
#endif
