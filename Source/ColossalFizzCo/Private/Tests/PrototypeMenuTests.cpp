#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "CrossplaySubsystem.h"
#include "CrossplayNetwork.h"
#include "UI/PrototypeMenuWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeMenuStateTest,"ColossalFizzCo.Frontend.MenuStateAndActions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPrototypeMenuStateTest::RunTest(const FString&)
{
 auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone();auto* World=GI->GetWorld();
 if(!World)return false;
 ON_SCOPE_EXIT{GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 auto* Online=GI->GetSubsystem<UCrossplaySubsystem>();
 auto* Widget=CreateWidget<UPrototypeMenuWidget>(GI,UPrototypeMenuWidget::StaticClass());
 if(!TestNotNull(TEXT("Native game menu can be constructed"),Widget))return false;
 TSharedPtr<SWidget> Slate=Widget->TakeWidget();
 auto* Login=Widget->FindAction(TEXT("Login"));
 if(!TestNotNull(TEXT("Epic login action is present on initial frame"),Login))return false;
 TestFalse(TEXT("Unconfigured online launch disables sign-in"),Login->GetIsEnabled());
 TestTrue(TEXT("Offline play remains accessible"),Widget->FindAction(TEXT("Offline"))->GetIsEnabled());
 Online->Identity.OnlineId=TEXT("frontend-test-host");Online->Identity.DisplayName=TEXT("Prototype test host");
 Online->SetPhase(ECrossplayPhase::Online,TEXT("Signed in"));
 auto* Search=Widget->FindAction(TEXT("Find"));
 if(!TestNotNull(TEXT("Sign-in changes the existing widget to session browsing"),Search))return false;
 TestTrue(TEXT("Browse refresh is enabled"),Search->GetIsEnabled());
 Search->OnClicked.Broadcast();
 TestTrue(TEXT("Rejected request restores the button for retry"),Search->GetIsEnabled());
 TestTrue(TEXT("Status updates preserve the existing focus target"),Search==Widget->FindAction(TEXT("Find")));
 auto* State=World->SpawnActor<ACrossplayLobbyState>();State->OwnerId=Online->Identity.OnlineId;
 FCrossplayMember Host;Host.OnlineId=Online->Identity.OnlineId;Host.Name=TEXT("Host");Host.bOwner=true;
 FCrossplayMember Guest;Guest.OnlineId=TEXT("frontend-test-guest");Guest.Name=TEXT("Guest");Guest.bReady=true;
 State->Members={Host,Guest};Online->SetPhase(ECrossplayPhase::Lobby,TEXT("Lobby connected"));
 auto* Start=Widget->FindAction(TEXT("Start"));
 if(!TestNotNull(TEXT("Lobby presents the host start control"),Start))return false;
 TestFalse(TEXT("Start is disabled while a member is not ready"),Start->GetIsEnabled());
 State->Members[0].bReady=true;Online->OnMembersChanged.Broadcast(State->Members);
 TestTrue(TEXT("Host start enables from authoritative ready data"),Start->GetIsEnabled());
 Online->Identity.OnlineId=Guest.OnlineId;Online->OnMembersChanged.Broadcast(State->Members);
 TestFalse(TEXT("Guest cannot use the host start control"),Start->GetIsEnabled());
 Online->SetPhase(ECrossplayPhase::Playing,TEXT("Loading shop"));
 TestFalse(TEXT("Readiness control locks after the game starts"),Widget->FindAction(TEXT("Ready"))->GetIsEnabled());
 TestTrue(TEXT("Mic mute state is sourced from the service"),Online->IsMicrophoneMuted());
 Online->ReceiveNativeInvite(TEXT("prototype-unit-invite"));
 TestTrue(TEXT("Pending invitation remains available to late-created presentation"),Online->HasPendingInvite());
 Online->SetPhase(ECrossplayPhase::Online,TEXT("Left lobby"));
 auto* Logout=Widget->FindAction(TEXT("Logout"));
 if(TestNotNull(TEXT("A leftover standalone roster does not hide the signed-in browser after cleanup"),Logout))
  TestTrue(TEXT("Sign-out is available after cleanup even before the old roster disappears"),Logout->GetIsEnabled());
 TestNull(TEXT("Disconnected standalone menu does not expose lobby readiness"),Widget->FindAction(TEXT("Ready")));
 Widget->RemoveFromParent();Slate.Reset();
 return true;
}
#endif
