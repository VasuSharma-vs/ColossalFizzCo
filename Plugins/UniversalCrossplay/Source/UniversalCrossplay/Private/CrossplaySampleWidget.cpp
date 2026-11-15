#include "CrossplaySampleWidget.h"
#include "CrossplaySubsystem.h"
#include "CrossplayNetwork.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"

void UCrossplaySampleRow::Choose() {if (Service) {if (bFriend) Service->InviteFriend(Index);else Service->JoinLobby(Index);}}
void UCrossplaySampleWidget::Button(UVerticalBox* P,const FString& Label,FName Function)
{
 auto* B=WidgetTree->ConstructWidget<UButton>();auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Label));B->AddChild(T);P->AddChild(B);
 FScriptDelegate D;D.BindUFunction(this,Function);B->OnClicked.Add(D);
}
TSharedRef<SWidget> UCrossplaySampleWidget::RebuildWidget()
{
 if (!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this);
 if (!WidgetTree->RootWidget)
 {
  auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(FLinearColor(.025f,.035f,.05f,.97f));Border->SetPadding(FMargin(24));WidgetTree->RootWidget=Border;
  auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Border->AddChild(Scroll);auto* Columns=WidgetTree->ConstructWidget<UHorizontalBox>();Scroll->AddChild(Columns);
  auto Column=[&](){auto* V=WidgetTree->ConstructWidget<UVerticalBox>();Columns->AddChild(V);return V;};
  auto* Actions=Column();auto* Centre=Column();auto* Right=Column();
  auto* Heading=WidgetTree->ConstructWidget<UTextBlock>();Heading->SetText(FText::FromString(TEXT("Multiplayer example")));Actions->AddChild(Heading);
  Status=WidgetTree->ConstructWidget<UTextBlock>();Status->SetAutoWrapText(true);Status->SetText(FText::FromString(TEXT("Offline. Sign in to use online lobbies.")));Centre->AddChild(Status);
  Button(Actions,TEXT("Store sign-in"),TEXT("LoginAuto"));Button(Actions,TEXT("Steam sign-in"),TEXT("LoginSteam"));Button(Actions,TEXT("Epic account portal"),TEXT("LoginEpic"));
  LobbyName=WidgetTree->ConstructWidget<UEditableTextBox>();LobbyName->SetHintText(FText::FromString(TEXT("Lobby name")));Actions->AddChild(LobbyName);
  Button(Actions,TEXT("Host public lobby"),TEXT("HostPublic"));Button(Actions,TEXT("Host private lobby"),TEXT("HostPrivate"));Button(Actions,TEXT("Find public lobbies"),TEXT("Find"));
  Button(Actions,TEXT("Accept invitation"),TEXT("Accept"));Button(Actions,TEXT("Ready / unready"),TEXT("Ready"));Button(Actions,TEXT("Start game as owner"),TEXT("Start"));
  Button(Actions,TEXT("Mute / unmute microphone"),TEXT("Mute"));Button(Actions,TEXT("Steam friends"),TEXT("SteamFriends"));Button(Actions,TEXT("Epic friends"),TEXT("EpicFriends"));
  Button(Actions,TEXT("Leave session"),TEXT("Leave"));Button(Actions,TEXT("Close menu"),TEXT("Close"));
  MemberText=WidgetTree->ConstructWidget<UTextBlock>();Centre->AddChild(MemberText);Results=WidgetTree->ConstructWidget<UVerticalBox>();Centre->AddChild(Results);
  Messages=WidgetTree->ConstructWidget<UTextBlock>();Messages->SetAutoWrapText(true);Right->AddChild(Messages);
  ChatText=WidgetTree->ConstructWidget<UEditableTextBox>();ChatText->SetHintText(FText::FromString(TEXT("Message (256 characters maximum)")));Right->AddChild(ChatText);Button(Right,TEXT("Send message"),TEXT("Send"));
 }
 return Super::RebuildWidget();
}
void UCrossplaySampleWidget::NativeConstruct()
{
 Super::NativeConstruct();Service=GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrossplaySubsystem>() : nullptr;if (!Service) return;
 Service->OnStatus.AddDynamic(this,&UCrossplaySampleWidget::ShowStatus);Service->OnLobbies.AddDynamic(this,&UCrossplaySampleWidget::ShowLobbies);Service->OnFriends.AddDynamic(this,&UCrossplaySampleWidget::ShowFriends);
 Service->OnMembersChanged.AddDynamic(this,&UCrossplaySampleWidget::ShowMembers);Service->OnChat.AddDynamic(this,&UCrossplaySampleWidget::ShowChat);ShowMembers(Service->GetMembers());
}
void UCrossplaySampleWidget::NativeDestruct()
{
 if (Service) {Service->OnStatus.RemoveAll(this);Service->OnLobbies.RemoveAll(this);Service->OnFriends.RemoveAll(this);Service->OnMembersChanged.RemoveAll(this);Service->OnChat.RemoveAll(this);}
 Super::NativeDestruct();
}
void UCrossplaySampleWidget::LoginAuto(){if(Service)Service->Login();}
void UCrossplaySampleWidget::LoginSteam(){if(Service)Service->Login(ECrossplayStore::Steam);}
void UCrossplaySampleWidget::LoginEpic(){if(Service)Service->LoginEpicAccountPortal();}
void UCrossplaySampleWidget::HostPublic(){if(Service)Service->HostLobby(LobbyName->GetText().ToString(),false);}
void UCrossplaySampleWidget::HostPrivate(){if(Service)Service->HostLobby(LobbyName->GetText().ToString(),true);}
void UCrossplaySampleWidget::Find(){if(Service)Service->FindLobbies();}
void UCrossplaySampleWidget::Leave(){if(Service)Service->LeaveLobby();}
void UCrossplaySampleWidget::Ready(){if(Service){bReady=!bReady;Service->SetReady(bReady);}}
void UCrossplaySampleWidget::Start(){if(auto* PC=GetOwningPlayer())if(auto* C=PC->FindComponentByClass<UCrossplayPlayerComponent>())C->RequestStartGame();}
void UCrossplaySampleWidget::Mute(){if(Service && Service->SetMicrophoneMuted(!bMuted))bMuted=!bMuted;}
void UCrossplaySampleWidget::Send(){if(Service && Service->SendChat(ChatText->GetText().ToString()))ChatText->SetText(FText::GetEmpty());}
void UCrossplaySampleWidget::Accept(){if(Service)Service->AcceptInvite();}
void UCrossplaySampleWidget::SteamFriends(){if(Service)Service->ReadFriends(ECrossplayStore::Steam);}
void UCrossplaySampleWidget::EpicFriends(){if(Service)Service->ReadFriends(ECrossplayStore::Epic);}
void UCrossplaySampleWidget::Close(){if(auto* PC=GetOwningPlayer()){PC->SetInputMode(FInputModeGameOnly());PC->bShowMouseCursor=false;}RemoveFromParent();}
void UCrossplaySampleWidget::ShowStatus(ECrossplayPhase,const FString& Text){if(Status)Status->SetText(FText::FromString(Text));}
void UCrossplaySampleWidget::ShowLobbies(const TArray<FCrossplayLobby>& Values)
{
 Results->ClearChildren();for(int32 I=0;I<Values.Num();++I){auto* Row=WidgetTree->ConstructWidget<UCrossplaySampleRow>();Row->Service=Service;Row->Index=I;Row->OnClicked.AddDynamic(Row,&UCrossplaySampleRow::Choose);auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(FString::Printf(TEXT("%s (%d/%d) — join"),*Values[I].Name,Values[I].Players,Values[I].Capacity)));Row->AddChild(T);Results->AddChild(Row);}
}
void UCrossplaySampleWidget::ShowFriends(const TArray<FCrossplayFriend>& Values)
{
 Results->ClearChildren();for(int32 I=0;I<Values.Num();++I){auto* Row=WidgetTree->ConstructWidget<UCrossplaySampleRow>();Row->Service=Service;Row->Index=I;Row->bFriend=true;Row->OnClicked.AddDynamic(Row,&UCrossplaySampleRow::Choose);auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Values[I].Name+TEXT(" — invite")));Row->AddChild(T);Results->AddChild(Row);}
}
void UCrossplaySampleWidget::ShowMembers(const TArray<FCrossplayMember>& Values)
{FString Text;for(const auto& M:Values)Text+=M.Name+TEXT(" [")+M.Platform+TEXT("] ")+(M.bOwner ? TEXT("Owner ") : TEXT(""))+(M.bReady ? TEXT("Ready\n") : TEXT("Not ready\n"));if(MemberText)MemberText->SetText(FText::FromString(Text));}
void UCrossplaySampleWidget::ShowChat(const FCrossplayChatLine&)
{FString Text;if(Service)for(const auto& M:Service->ChatHistory)Text+=M.Sender.Name+TEXT(": ")+M.Message+TEXT("\n");if(Messages)Messages->SetText(FText::FromString(Text));}
UCrossplaySampleWidget* UCrossplaySampleUI::OpenMenu(APlayerController* PC)
{
 if (!PC || !PC->IsLocalController()) return nullptr;
 TArray<UUserWidget*> Existing;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC,Existing,UCrossplaySampleWidget::StaticClass(),true);
 for(auto* Item:Existing)if(Item && Item->GetOwningPlayer()==PC)return Cast<UCrossplaySampleWidget>(Item);
 auto* W=CreateWidget<UCrossplaySampleWidget>(PC,UCrossplaySampleWidget::StaticClass());if (!W) return nullptr;
 W->AddToViewport(100);PC->SetInputMode(FInputModeGameAndUI().SetWidgetToFocus(W->TakeWidget()).SetHideCursorDuringCapture(false));PC->bShowMouseCursor=true;return W;
}
