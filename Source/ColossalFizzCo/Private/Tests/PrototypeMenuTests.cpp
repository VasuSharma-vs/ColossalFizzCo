#include "Misc/AutomationTest.h"
#include "Algo/AnyOf.h"
#include "Misc/ScopeExit.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "CrossplaySubsystem.h"
#include "CrossplayNetwork.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "UI/PrototypeMenuWidget.h"
#include "UI/SettingsControls.h"
#include "UserSettings/EnhancedInputUserSettings.h"

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

 auto* Pages=Cast<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("PrototypeMenuPages")));
 auto* Settings=Widget->FindAction(TEXT("Settings"));
 if(!TestNotNull(TEXT("The main menu has a page switcher"),Pages) ||
    !TestNotNull(TEXT("Settings is available before signing in"),Settings))return false;
 auto* MainPage=Pages->GetActiveWidget();
 TestTrue(TEXT("Settings is enabled before signing in"),Settings->GetIsEnabled());
 Settings->OnClicked.Broadcast();

 auto* SettingsPanel=Cast<UUserWidget>(Pages->GetActiveWidget());
 if(!TestNotNull(TEXT("Settings opens a user widget"),SettingsPanel))return false;
 TestEqual(TEXT("Settings is the active menu page"),SettingsPanel->GetFName(),FName(TEXT("PrototypeSettingsPanel")));
 auto* ControlsTab=Cast<UButton>(SettingsPanel->GetWidgetFromName(TEXT("ControlsTab")));
 auto* GraphicsTab=Cast<UButton>(SettingsPanel->GetWidgetFromName(TEXT("GraphicsTab")));
 auto* AudioTab=Cast<UButton>(SettingsPanel->GetWidgetFromName(TEXT("AudioTab")));
 auto* Back=Cast<UButton>(SettingsPanel->GetWidgetFromName(TEXT("BackFromSettings")));
 if(!TestNotNull(TEXT("Controls tab is present"),ControlsTab) ||
    !TestNotNull(TEXT("Graphics tab is present"),GraphicsTab) ||
    !TestNotNull(TEXT("Audio tab is present"),AudioTab) ||
    !TestNotNull(TEXT("Settings has a Back button"),Back))return false;

 GraphicsTab->OnClicked.Broadcast();
 TestNotNull(TEXT("Graphics tab renders the quality setting"),SettingsPanel->GetWidgetFromName(TEXT("GraphicsQuality")));
 AudioTab->OnClicked.Broadcast();
 TestNotNull(TEXT("Audio tab renders the master volume setting"),SettingsPanel->GetWidgetFromName(TEXT("MasterVolume")));
 ControlsTab->OnClicked.Broadcast();
 auto* SettingsContent=Cast<UVerticalBox>(SettingsPanel->GetWidgetFromName(TEXT("SettingsContent")));
 if(TestNotNull(TEXT("Controls tab has a content panel"),SettingsContent))
  TestTrue(TEXT("Controls tab renders content"),SettingsContent->GetChildrenCount()>0);
 Back->OnClicked.Broadcast();
 TestTrue(TEXT("Back returns to the original main menu page"),Pages->GetActiveWidget()==MainPage);
 TestTrue(TEXT("Returning from settings preserves the sign-in button"),Widget->FindAction(TEXT("Login"))==Login);

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
 auto* LobbySettings=Widget->FindAction(TEXT("Settings"));
 if(!TestNotNull(TEXT("Settings is available in the online lobby"),LobbySettings))return false;
 TestTrue(TEXT("Settings is enabled while the lobby waits for readiness"),LobbySettings->GetIsEnabled());
 LobbySettings->OnClicked.Broadcast();
 TestTrue(TEXT("The online lobby opens the same settings page"),Pages->GetActiveWidget()==SettingsPanel);
 Back->OnClicked.Broadcast();
 TestTrue(TEXT("Returning from settings preserves the lobby start button"),Widget->FindAction(TEXT("Start"))==Start);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeMenuInputProfileTest,"ColossalFizzCo.Frontend.SettingsInputProfile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPrototypeMenuInputProfileTest::RunTest(const FString&)
{
 auto* Game=NewObject<UGameInstance>(GEngine);
 Game->InitializeStandalone();
 auto* World=Game->GetWorld();
 if(!World)return false;
 ON_SCOPE_EXIT{Game->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};

 auto* Player=NewObject<ULocalPlayer>(GEngine);
 Game->AddLocalPlayer(Player,FPlatformUserId::CreateFromInternalId(0));
 auto* Controller=World->SpawnActor<APlayerController>();
 auto* PlayerInput=NewObject<UEnhancedPlayerInput>(Controller);
 Player->PlayerController=Controller;
 Controller->Player=Player;
 Controller->PlayerInput=PlayerInput;
 auto* Input=Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
 if(Input)Input->InitalizeUserSettings();
 auto* Settings=Input ? Input->GetUserSettings() : nullptr;
 if(!TestNotNull(TEXT("Enhanced Input settings are available before starting the shop"),Settings))return false;

 const auto Bindings=SettingsControls::GetBindings(nullptr);
 TestEqual(TEXT("Eight first-person controls are exposed"),Bindings.Num(),8);
 for(const auto& Binding:Bindings)
 {
  const auto* Mapping=Settings->FindCurrentMappingForSlot(Binding.Id,EPlayerMappableKeySlot::First);
  if(TestNotNull(*FString::Printf(TEXT("%s has its own profile mapping"),*Binding.Id.ToString()),Mapping))
   TestTrue(TEXT("The profile preserves the default key"),Mapping->GetDefaultKey()==Binding.Key);
 }

 const FName MoveForward(TEXT("CFC.MoveForward"));
 const auto* Forward=Settings->FindCurrentMappingForSlot(MoveForward,EPlayerMappableKeySlot::First);
 if(!Forward)return false;
 FMapPlayerKeyArgs Args;
 Args.MappingName=MoveForward;
 Args.Slot=EPlayerMappableKeySlot::First;
 Args.NewKey=EKeys::I;
 Args.HardwareDeviceId=Forward->GetHardwareDeviceId().HardwareDeviceIdentifier;
 Args.bCreateMatchingSlotIfNeeded=false;
 FGameplayTagContainer FailureReason;
 Settings->MapPlayerKey(Args,FailureReason);
 TestTrue(TEXT("Enhanced Input accepts a new key for a single movement direction"),FailureReason.IsEmpty());
 const auto Updated=SettingsControls::GetBindings(Player);
 const auto* UpdatedForward=Updated.FindByPredicate([MoveForward](const auto& Binding){return Binding.Id==MoveForward;});
 if(TestNotNull(TEXT("The menu reports the updated binding"),UpdatedForward))
  TestTrue(TEXT("Move forward displays the new key"),UpdatedForward->Key==EKeys::I);

 const auto* Context=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/Input/IMC_Default.IMC_Default"));
 if(!TestNotNull(TEXT("The first-person Blueprint's input context loads"),Context))return false;
 FModifyContextOptions Options;
 Options.bForceImmediately=true;
 Input->AddMappingContext(Context,0,Options);
 TestTrue(TEXT("The original gameplay context applies the new movement key"),
  Algo::AnyOf(PlayerInput->GetEnhancedActionMappingsView(),[](const FEnhancedActionKeyMapping& Mapping)
  {
   return Mapping.Action && Mapping.Action->GetFName()==TEXT("IA_Move") && Mapping.Key==EKeys::I;
  }));

 TArray<uint8> SavedProfile;
 if(!TestTrue(TEXT("The input settings serialize"),UGameplayStatics::SaveGameToMemory(Settings,SavedProfile)))return false;
 auto* Reloaded=Cast<UEnhancedInputUserSettings>(UGameplayStatics::LoadGameFromMemory(SavedProfile));
 if(!TestNotNull(TEXT("The input settings deserialize"),Reloaded))return false;
 Reloaded->Initialize(Player);
 const auto* ReloadedForward=Reloaded->FindCurrentMappingForSlot(MoveForward,EPlayerMappableKeySlot::First);
 if(TestNotNull(TEXT("The reloaded profile retains the movement mapping"),ReloadedForward))
  TestTrue(TEXT("The remapped key survives a save and reload"),ReloadedForward->GetCurrentKey()==EKeys::I);
 return true;
}
#endif
