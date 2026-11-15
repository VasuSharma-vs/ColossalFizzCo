#include "UI/PrototypeMenuSubsystem.h"
#include "UI/PrototypeMenuWidget.h"
#include "UI/PrototypeMenuGameMode.h"
#include "CrossplaySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"

bool UPrototypeMenuSubsystem::ShouldCreateSubsystem(UObject*) const
{
 return !IsRunningDedicatedServer() && !IsRunningCommandlet()
  && !FParse::Param(FCommandLine::Get(),TEXT("NoPrototypeMenu"))
  && !FParse::Param(FCommandLine::Get(),TEXT("nullrhi"));
}
void UPrototypeMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 Online=GetLocalPlayer()->GetGameInstance()->GetSubsystem<UCrossplaySubsystem>();
 if(Online)
 {
  Online->OnInvite.AddDynamic(this,&UPrototypeMenuSubsystem::InvitationReceived);
  Online->OnOperation.AddDynamic(this,&UPrototypeMenuSubsystem::OperationFinished);
 }
 // Controllers may persist through seamless travel; their world can still change.
 Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UPrototypeMenuSubsystem::Tick),.25f);
}
void UPrototypeMenuSubsystem::Deinitialize()
{
 FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
 if(Online){Online->OnInvite.RemoveAll(this);Online->OnOperation.RemoveAll(this);}
 CloseMenu();DetachController();Super::Deinitialize();
}
bool UPrototypeMenuSubsystem::IsMenuWorld() const
{
 const auto* PC=GetLocalPlayer()->GetPlayerController(GetLocalPlayer()->GetGameInstance()->GetWorld());
 return PC && PC->GetWorld() && UWorld::RemovePIEPrefix(PC->GetWorld()->GetOutermost()->GetName())==APrototypeMenuGameMode::MenuMap();
}
void UPrototypeMenuSubsystem::DetachController()
{
 if(auto* PC=BoundController.Get();PC && MenuInput)PC->PopInputComponent(MenuInput);
 if(MenuInput)MenuInput->DestroyComponent();MenuInput=nullptr;BoundController.Reset();BoundWorld.Reset();
}
bool UPrototypeMenuSubsystem::Tick(float)
{
 auto* PC=GetLocalPlayer()->GetPlayerController(GetLocalPlayer()->GetGameInstance()->GetWorld());auto* World=PC ? PC->GetWorld() : nullptr;
 if(PC!=BoundController.Get() || World!=BoundWorld.Get())
 {
  CloseMenu();DetachController();BoundController=PC;BoundWorld=World;
  if(PC && PC->IsLocalController())
  {
   MenuInput=NewObject<UInputComponent>(PC,TEXT("PrototypeSessionMenuInput"));
   MenuInput->Priority=100;MenuInput->bBlockInput=false;PC->AddInstanceComponent(MenuInput);MenuInput->RegisterComponent();
   MenuInput->BindKey(EKeys::Escape,IE_Pressed,this,&UPrototypeMenuSubsystem::ToggleMenu);
   PC->PushInputComponent(MenuInput);
   PC->SetInputMode(FInputModeGameOnly());PC->bShowMouseCursor=false;
  }
 }
 if(PC && World && World->HasBegunPlay() && IsMenuWorld() && (!Menu || !Menu->IsInViewport()))OpenMenu();
 return true;
}
void UPrototypeMenuSubsystem::OpenMenu()
{
 auto* PC=GetLocalPlayer()->GetPlayerController(GetLocalPlayer()->GetGameInstance()->GetWorld());if(!PC || !PC->IsLocalController())return;
 if(!Menu || !Menu->IsInViewport())
 {bPreviousCursor=PC->bShowMouseCursor;if(FSlateApplication::IsInitialized())PreviousFocus=FSlateApplication::Get().GetKeyboardFocusedWidget();}
 if(!Menu)Menu=CreateWidget<UPrototypeMenuWidget>(PC,UPrototypeMenuWidget::StaticClass());
 if(!Menu)return;
 if(!Menu->IsInViewport())Menu->AddToPlayerScreen(200);
 PC->FlushPressedKeys();
 PC->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(Menu->TakeWidget()).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
 PC->bShowMouseCursor=true;Menu->SetKeyboardFocus();
}
void UPrototypeMenuSubsystem::CloseMenu()
{
 const bool WasOpen=Menu!=nullptr;
 if(Menu){Menu->RemoveFromParent();Menu=nullptr;}
 if(auto* PC=BoundController.Get())
 {
  if(WasOpen && bPreviousCursor && PreviousFocus.IsValid() && PC->GetWorld()==BoundWorld.Get())
  {PC->SetInputMode(FInputModeGameAndUI().SetWidgetToFocus(PreviousFocus.Pin()).SetHideCursorDuringCapture(false));PC->bShowMouseCursor=true;}
  else {PC->SetInputMode(FInputModeGameOnly());PC->bShowMouseCursor=false;}
  PC->FlushPressedKeys();
 }
 PreviousFocus.Reset();bPreviousCursor=false;
}
void UPrototypeMenuSubsystem::ToggleMenu()
{
 if(Menu && Menu->IsInViewport()){if(!IsMenuWorld())CloseMenu();}
 else OpenMenu();
}
void UPrototypeMenuSubsystem::InvitationReceived(const FString&)
{
 OpenMenu();if(Menu)Menu->RefreshInvitation();
}
bool UPrototypeMenuSubsystem::HasInvitation() const
 {return Online && Online->HasPendingInvite();}
void UPrototypeMenuSubsystem::OperationFinished(FName Operation,bool Success)
{
 if(!Success && (Operation==TEXT("Connection") || Operation==TEXT("Travel")))OpenMenu();
}
