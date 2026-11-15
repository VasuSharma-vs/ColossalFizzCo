#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "CrossplaySubsystem.h"
#include "CrossplayOnlineState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "OnlineSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossplayCleanupTest,"UniversalCrossplay.Session.IdempotentDisconnectCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrossplayCleanupTest::RunTest(const FString&)
{
 auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone();auto* World=GI->GetWorld();
 if(!World)return false;
 ON_SCOPE_EXIT{GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 auto* Service=GI->GetSubsystem<UCrossplaySubsystem>();
 Service->Identity.OnlineId=TEXT("cleanup-fixture-user");
 Service->Lobbies.AddDefaulted();Service->ChatHistory.AddDefaulted();
 Service->Online->Results.AddDefaulted();Service->Online->Pending=TEXT("Leave");
 Service->SetPhase(ECrossplayPhase::Leaving,TEXT("Leaving"));
 Service->FinishLeave(false);
 TestTrue(TEXT("Missing session after reported failure completes cleanup"),Service->Phase==ECrossplayPhase::Online);
 TestTrue(TEXT("Cleanup releases operation gate"),Service->Online->Pending.IsNone());
 TestTrue(TEXT("Stale result/history caches are cleared"),Service->Lobbies.IsEmpty() && Service->ChatHistory.IsEmpty() && Service->Online->Results.IsEmpty());
 Service->FinishLeave(false);
 TestTrue(TEXT("Repeated missing-session cleanup remains complete"),Service->Phase==ECrossplayPhase::Online);
 Service->Identity=FCrossplayIdentity();Service->FinishLeave(false);
 TestTrue(TEXT("Signed-out cleanup remains offline"),Service->Phase==ECrossplayPhase::Offline);
 auto* Provider=IOnlineSubsystem::Get(FName("NULL"));
 if(!TestNotNull(TEXT("Local provider available for real named-session presence checks"),Provider))return false;
 auto Sessions=Provider->GetSessionInterface();
 if(!TestTrue(TEXT("Local session interface is available"),Sessions.IsValid()))return false;
 if(!TestNull(TEXT("Isolated fixture does not overwrite an existing session"),Sessions->GetNamedSession(NAME_GameSession)))return false;
 FOnlineSessionSettings Settings;Settings.NumPublicConnections=2;Settings.bShouldAdvertise=false;
 if(!TestTrue(TEXT("Local provider can create the retained-session fixture"),Sessions->CreateSession(0,NAME_GameSession,Settings)))return false;
 ON_SCOPE_EXIT{Sessions->RemoveNamedSession(NAME_GameSession);Service->Online->Sessions.Reset();};
 Service->Online->Sessions=Sessions;Service->Identity.OnlineId=TEXT("cleanup-fixture-user");
 Service->SetPhase(ECrossplayPhase::Leaving,TEXT("Leaving"));Service->Online->Pending=TEXT("Leave");
 Service->FinishLeave(false);
 TestTrue(TEXT("An actual cleanup failure with a retained session is reported"),!Service->LastError.IsEmpty());
 TestTrue(TEXT("Retained-session failure unlocks retry instead of remaining Leaving"),Service->Phase==ECrossplayPhase::Lobby && Service->Online->Pending.IsNone());
 Sessions->RemoveNamedSession(NAME_GameSession);Service->FinishLeave(false);
 TestTrue(TEXT("Provider NotFound with no local record completes cleanup"),Service->Phase==ECrossplayPhase::Online);
 return true;
}
#endif
