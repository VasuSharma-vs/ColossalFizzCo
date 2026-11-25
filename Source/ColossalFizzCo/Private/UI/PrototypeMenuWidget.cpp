#include "UI/PrototypeMenuWidget.h"
#include "UI/PrototypeMenuSubsystem.h"
#include "UI/PrototypeMenuGameMode.h"
#include "UI/PrototypeMenuStyle.h"
#include "UI/PrototypeSettingsWidget.h"
#include "CrossplaySubsystem.h"
#include "CrossplaySettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"

namespace
{
 void LabelButton(UButton* Button,const FString& Label)
 {if(Button)if(auto* Text=Cast<UTextBlock>(Button->GetContent()))Text->SetText(FText::FromString(Label));}
}
void UPrototypeMenuRow::Choose(){if(auto* Owner=Menu.Get())Owner->ChooseRow(Index,bFriend);}
UButton* UPrototypeMenuWidget::FindAction(FName Name) const
 {const auto* Value=Actions.Find(Name);return Value ? Value->Get() : nullptr;}
UTextBlock* UPrototypeMenuWidget::Text(UPanelWidget* Parent,const FString& Value,int32 Size,bool Accent)
{
 auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Value));T->SetAutoWrapText(true);
 T->SetFont(FCoreStyle::GetDefaultFontStyle(Size>=24 ? TEXT("Bold") : TEXT("Regular"),Size));
 T->SetColorAndOpacity(Accent ? PrototypeMenuStyle::Amber : PrototypeMenuStyle::Cream);Parent->AddChild(T);
 if(auto* VerticalSlot=Cast<UVerticalBoxSlot>(T->Slot))VerticalSlot->SetPadding(FMargin(0,3));
 if(auto* ButtonSlot=Cast<UButtonSlot>(T->Slot))ButtonSlot->SetPadding(FMargin(16,9));return T;
}
UButton* UPrototypeMenuWidget::Button(UVerticalBox* Parent,FName Action,const FString& Label,bool Accent)
{
 auto* B=WidgetTree->ConstructWidget<UButton>();PrototypeMenuStyle::StyleButton(B,Accent);
 auto* T=Text(B,Label,18);T->SetColorAndOpacity(Accent ? PrototypeMenuStyle::Ink : PrototypeMenuStyle::Cream);T->SetJustification(ETextJustify::Center);
 auto* ButtonLayout=Parent->AddChildToVerticalBox(B);ButtonLayout->SetPadding(FMargin(0,4));
 FScriptDelegate Click;Click.BindUFunction(this,Action);B->OnClicked.Add(Click);Actions.Add(Action,B);return B;
}
TSharedRef<SWidget> UPrototypeMenuWidget::RebuildWidget()
{
 SetIsFocusable(true);if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
 if(!WidgetTree->RootWidget)
 {
  auto* Back=WidgetTree->ConstructWidget<UBorder>();Back->SetBrushColor(PrototypeMenuStyle::Ink);Back->SetPadding(FMargin(24));WidgetTree->RootWidget=Back;
  auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);Back->AddChild(Fit);
  auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(1080);Size->SetHeightOverride(620);Fit->AddChild(Size);
  auto* Card=WidgetTree->ConstructWidget<UBorder>();Card->SetBrushColor(FLinearColor(.045f,.065f,.075f));Card->SetPadding(FMargin(30));Size->AddChild(Card);
  MenuPages=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("PrototypeMenuPages"));Card->AddChild(MenuPages);
  auto* Layout=WidgetTree->ConstructWidget<UVerticalBox>();MenuPages->AddChild(Layout);
  Text(Layout,TEXT("COLOSSAL FIZZ CO"),36,true);
  Text(Layout,TEXT("NORMAL COLA PROTOTYPE  /  TWO-PLAYER CO-OP"),15)->SetColorAndOpacity(PrototypeMenuStyle::Muted);
  AccountText=Text(Layout,TEXT(""),17);
  StatusText=Text(Layout,TEXT("Choose how to open your shop."),17);
  InvitationPanel=WidgetTree->ConstructWidget<UVerticalBox>();Layout->AddChild(InvitationPanel);
  Body=WidgetTree->ConstructWidget<UVerticalBox>();auto* BodySlot=Layout->AddChildToVerticalBox(Body);BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));BodySlot->SetPadding(FMargin(0,14,0,10));
  Text(Layout,TEXT("Lemonade  /  Mint  /  Sweet  /  Spicy  /  Zero Calories"),14)->SetColorAndOpacity(PrototypeMenuStyle::Muted);
 }
 return Super::RebuildWidget();
}
void UPrototypeMenuWidget::NativeConstruct()
{
 Super::NativeConstruct();Service=GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrossplaySubsystem>() : nullptr;
 bOnlineAvailable=Service && GetDefault<UCrossplaySettings>()->bEnableOnline && Service->GetConfigurationIssues().IsEmpty();
 if(auto* Local=GetOwningLocalPlayer())MenuOwner=Local->GetSubsystem<UPrototypeMenuSubsystem>();
 if(Service)
 {
  Service->OnStatus.AddDynamic(this,&UPrototypeMenuWidget::Status);Service->OnOperation.AddDynamic(this,&UPrototypeMenuWidget::Operation);
  Service->OnLobbies.AddDynamic(this,&UPrototypeMenuWidget::Lobbies);Service->OnFriends.AddDynamic(this,&UPrototypeMenuWidget::FriendList);Service->OnMembersChanged.AddDynamic(this,&UPrototypeMenuWidget::Members);
  Service->OnInvite.AddDynamic(this,&UPrototypeMenuWidget::Invitation);
 }
 Render();
 if(StatusText)StatusText->SetText(FText::FromString(InSession() ? TEXT("Your shop session is connected.") : Service && !Service->Identity.OnlineId.IsEmpty() ? TEXT("Host a shop or join a public session.") : TEXT("Choose how to open your shop.")));
}
void UPrototypeMenuWidget::NativeDestruct()
{
 if(SettingsPanel)SettingsPanel->Exit();
 if(Service){Service->OnStatus.RemoveAll(this);Service->OnOperation.RemoveAll(this);Service->OnLobbies.RemoveAll(this);Service->OnFriends.RemoveAll(this);Service->OnMembersChanged.RemoveAll(this);Service->OnInvite.RemoveAll(this);}
 Super::NativeDestruct();
}
bool UPrototypeMenuWidget::IsGameplay() const
 {return MenuOwner ? !MenuOwner->IsMenuWorld() : GetWorld() && UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==APrototypeMenuGameMode::ShopMap();}
bool UPrototypeMenuWidget::InSession() const
 {
  if(!Service)return false;
  // A return world can briefly retain a local roster after disconnect cleanup.
  // That roster alone must not hide browsing/sign-out in a standalone menu.
  const bool ConnectedRoster=GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone && !Service->GetMembers().IsEmpty();
  return Service->Phase==ECrossplayPhase::Lobby || Service->Phase==ECrossplayPhase::Starting || Service->Phase==ECrossplayPhase::Playing || ConnectedRoster;
 }
void UPrototypeMenuWidget::Render()
{
 if(!Body)return;
 const bool Signed=Service && !Service->Identity.OnlineId.IsEmpty();
 const FString NewView=IsGameplay() ? TEXT("Game") : InSession() ? TEXT("Lobby") : Signed ? TEXT("Browse") : TEXT("Login");
 if(AccountText)AccountText->SetText(FText::FromString(Signed ? TEXT("Signed in as ")+Service->Identity.DisplayName : TEXT("Epic sign-in is needed for online sessions. Solo play works offline.")));
 if(View!=NewView)
 {
  View=NewView;Actions.Reset();Rows.Reset();Body->ClearChildren();SessionRows=nullptr;FriendRows=nullptr;MembersText=nullptr;NameInput=nullptr;PrivateInput=nullptr;
  auto* Columns=WidgetTree->ConstructWidget<UHorizontalBox>();auto* ContentSlot=Body->AddChildToVerticalBox(Columns);ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  auto Column=[&](bool ScrollAll)
  {
   auto* V=WidgetTree->ConstructWidget<UVerticalBox>();UWidget* Content=V;
   if(ScrollAll){auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->AddChild(V);Content=Scroll;}
   auto* S=Columns->AddChildToHorizontalBox(Content);S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));S->SetPadding(FMargin(0,0,24,0));return V;
  };
  auto* Left=Column(true);auto* Right=Column(false);
  if(View==TEXT("Login"))
  {
   Text(Left,TEXT("A little operator.\nA colossal shop."),30,true);
   Text(Left,TEXT("Fill and cap giant cola bottles, stock your shelves and serve your customers. Open the shop solo or invite a teammate."),20);
   Text(Right,TEXT("Open your shop"),26);
   Button(Right,TEXT("Login"),TEXT("Sign in with Epic Games"),true);
   Button(Right,TEXT("Offline"),TEXT("Play solo offline"));Button(Right,TEXT("Settings"),TEXT("Settings"));Button(Right,TEXT("Quit"),TEXT("Quit game"));
   if(!bOnlineAvailable)
    Text(Right,TEXT("Online play is unavailable in this launch. Use the Epic prototype launcher to sign in."),16)->SetColorAndOpacity(PrototypeMenuStyle::Muted);
  }
  else if(View==TEXT("Browse"))
  {
   Text(Left,TEXT("Host a shop"),26);Text(Left,TEXT("Session name"),16);
   NameInput=WidgetTree->ConstructWidget<UEditableTextBox>();NameInput->SetText(FText::FromString(LobbyTitle));
   auto InputStyle=NameInput->GetWidgetStyle();InputStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18));NameInput->SetWidgetStyle(InputStyle);
   Left->AddChild(NameInput);NameInput->OnTextChanged.AddDynamic(this,&UPrototypeMenuWidget::NameChanged);
   PrivateInput=WidgetTree->ConstructWidget<UCheckBox>();PrivateInput->SetIsChecked(bPrivate);Text(PrivateInput,TEXT("Invite-only session"),18);Left->AddChild(PrivateInput);PrivateInput->OnCheckStateChanged.AddDynamic(this,&UPrototypeMenuWidget::PrivacyChanged);
   Button(Left,TEXT("Host"),TEXT("Host session"),true);Button(Left,TEXT("Offline"),TEXT("Play solo offline"));Button(Left,TEXT("Settings"),TEXT("Settings"));Button(Left,TEXT("Logout"),TEXT("Sign out"));Button(Left,TEXT("Quit"),TEXT("Quit game"));
   Text(Right,TEXT("Join a shop"),26);Button(Right,TEXT("Find"),TEXT("Refresh public sessions"));
   auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Right->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));SessionRows=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(SessionRows);
  }
  else
  {
   Text(Left,View==TEXT("Game") ? TEXT("Your shop session") : TEXT("Your team"),26);
   MembersText=Text(Left,TEXT("Connecting to the session..."),20);
   if(View==TEXT("Lobby"))
   {Button(Left,TEXT("Ready"),TEXT("Ready"));Button(Left,TEXT("Start"),TEXT("Start prototype"),true);Text(Left,TEXT("All connected players must be ready. The host starts the shop."),16)->SetColorAndOpacity(PrototypeMenuStyle::Muted);}
   else Button(Left,TEXT("Resume"),TEXT("Resume shop"),true);
   Button(Left,TEXT("Settings"),TEXT("Settings"));Button(Left,TEXT("Microphone"),TEXT("Microphone: muted"));Button(Left,TEXT("Leave"),TEXT("Leave to main menu"));
   Text(Right,TEXT("Invite a teammate"),26);Button(Right,TEXT("Friends"),TEXT("Refresh Epic friends"));
   auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Right->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));FriendRows=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(FriendRows);
   Text(Right,TEXT("Friends must have access to this prototype. Sessions support two players."),15)->SetColorAndOpacity(PrototypeMenuStyle::Muted);
  }
  if(Service){Lobbies(Service->Lobbies);FriendList(Service->Friends);Members(Service->GetMembers());}
  RefreshInvitation();
 }
 UpdateEnabled();
}
void UPrototypeMenuWidget::UpdateEnabled()
{
 const bool Signed=Service && !Service->Identity.OnlineId.IsEmpty();const bool Session=InSession();
 const bool Working=bBusy || (Service && (Service->Phase==ECrossplayPhase::Authenticating || Service->Phase==ECrossplayPhase::Creating || Service->Phase==ECrossplayPhase::Joining || Service->Phase==ECrossplayPhase::Searching || Service->Phase==ECrossplayPhase::Starting || Service->Phase==ECrossplayPhase::Leaving));
 for(auto& Item:Actions)if(Item.Value)Item.Value->SetIsEnabled(!Working);
 auto Enable=[&](FName Name,bool Enabled){if(auto* B=FindAction(Name))B->SetIsEnabled(!Working && Enabled);};
 Enable(TEXT("Login"),Service && !Signed && bOnlineAvailable);
 Enable(TEXT("Host"),Signed && !Session && !LobbyTitle.TrimStartAndEnd().IsEmpty());Enable(TEXT("Find"),Signed && !Session);Enable(TEXT("Logout"),Signed && !Session);
 const auto Team=Service ? Service->GetMembers() : TArray<FCrossplayMember>();
 const auto* Self=Team.FindByPredicate([&](const auto& M){return Service && M.OnlineId==Service->Identity.OnlineId;});
 const bool Lobby=Service && Service->Phase==ECrossplayPhase::Lobby;
 Enable(TEXT("Ready"),Lobby && Self);Enable(TEXT("Start"),Lobby && Self && Self->bOwner && !Team.IsEmpty() && !Team.ContainsByPredicate([](const auto& M){return !M.bReady;}));
 LabelButton(FindAction(TEXT("Ready")),Self && Self->bReady ? TEXT("Unready") : TEXT("Ready"));
 Enable(TEXT("Friends"),Signed && Session);Enable(TEXT("Microphone"),Signed && Session);
 if(Service){bMuted=Service->IsMicrophoneMuted();LabelButton(FindAction(TEXT("Microphone")),bMuted ? TEXT("Microphone: muted - unmute") : TEXT("Microphone: on - mute"));}
 Enable(TEXT("Accept"),Signed && !Session);
 if(NameInput)NameInput->SetIsEnabled(!Working);if(PrivateInput)PrivateInput->SetIsEnabled(!Working);
 for(UPrototypeMenuRow* Row:Rows)if(Row)Row->SetIsEnabled(!Working && Row->bAvailable && (Row->bFriend ? Signed && Session && Team.Num()<2 : Signed && !Session));
}
void UPrototypeMenuWidget::Begin(FName Op,TFunctionRef<bool()> Request)
{
 if(!Service || bBusy)return;bBusy=true;Pending=Op;
 const bool Started=Request();
 if(!Started){bBusy=false;Pending=NAME_None;if(StatusText)StatusText->SetText(FText::FromString(Service->LastError.IsEmpty() ? TEXT("That action is unavailable right now.") : Service->LastError));}
 UpdateEnabled();
}
void UPrototypeMenuWidget::Login(){Begin(TEXT("Login"),[&]{return Service->LoginEpicAccountPortal();});}
void UPrototypeMenuWidget::Offline(){if(!bBusy && !InSession())UGameplayStatics::OpenLevel(this,FName(APrototypeMenuGameMode::ShopMap()),true);}
void UPrototypeMenuWidget::Host(){Begin(TEXT("Host"),[&]{return Service->HostLobby(LobbyTitle,bPrivate);});}
void UPrototypeMenuWidget::Find(){Begin(TEXT("Find"),[&]{return Service->FindLobbies();});}
void UPrototypeMenuWidget::Start(){Begin(TEXT("Start"),[&]{return Service->StartGame();});}
void UPrototypeMenuWidget::Ready()
{
 if(!Service || bBusy)return;const auto Team=Service->GetMembers();
 if(const auto* Self=Team.FindByPredicate([&](const auto& M){return M.OnlineId==Service->Identity.OnlineId;}))Service->SetReady(!Self->bReady);
}
void UPrototypeMenuWidget::Leave()
{
 if(bBusy)return;if(InSession())Begin(TEXT("Leave"),[&]{return Service->LeaveLobby();});
 else UGameplayStatics::OpenLevel(this,FName(APrototypeMenuGameMode::MenuMap()),true);
}
void UPrototypeMenuWidget::Logout(){Begin(TEXT("Logout"),[&]{return Service->Logout();});}
void UPrototypeMenuWidget::Friends(){Begin(TEXT("Friends"),[&]{return Service->ReadFriends(ECrossplayStore::Epic);});}
void UPrototypeMenuWidget::Accept(){Begin(TEXT("Join"),[&]{return Service->AcceptInvite();});}
void UPrototypeMenuWidget::Microphone()
 {if(Service && Service->SetMicrophoneMuted(!Service->IsMicrophoneMuted()))UpdateEnabled();else if(StatusText)StatusText->SetText(FText::FromString(TEXT("Microphone unavailable. Check your audio device and microphone permission.")));}
void UPrototypeMenuWidget::Settings()
{
 if(!SettingsPanel)
 {
  SettingsPanel=CreateWidget<UPrototypeSettingsWidget>(this,UPrototypeSettingsWidget::StaticClass(),TEXT("PrototypeSettingsPanel"));
  if(!SettingsPanel)return;
  SettingsPanel->OnBack.BindUObject(this,&UPrototypeMenuWidget::CloseSettings);
  MenuPages->AddChild(SettingsPanel);
 }
 MenuPages->SetActiveWidget(SettingsPanel);
 SettingsPanel->Enter();
}
void UPrototypeMenuWidget::CloseSettings()
{
 MenuPages->SetActiveWidgetIndex(0);
 if(auto* Action=FindAction(TEXT("Settings"));Action && Action->GetIsEnabled())Action->SetKeyboardFocus();
 else SetKeyboardFocus();
}
UUserWidget* UPrototypeMenuWidget::GetMenuFocusWidget()
{
 if(SettingsPanel && MenuPages->GetActiveWidget()==SettingsPanel)return SettingsPanel;
 return this;
}
void UPrototypeMenuWidget::Resume(){if(MenuOwner && IsGameplay())MenuOwner->CloseMenu();}
void UPrototypeMenuWidget::Quit(){UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false);}
void UPrototypeMenuWidget::NameChanged(const FText& Value){LobbyTitle=Value.ToString().Left(64);if(NameInput && Value.ToString().Len()>64)NameInput->SetText(FText::FromString(LobbyTitle));UpdateEnabled();}
void UPrototypeMenuWidget::PrivacyChanged(bool Checked){bPrivate=Checked;}
void UPrototypeMenuWidget::ChooseRow(int32 Index,bool Friend)
{
 if(Friend)
 {
  // Epic's native friend invite call returns synchronously; it has no facade completion delegate.
  if(StatusText)StatusText->SetText(FText::FromString(Service && Service->InviteFriend(Index) ? TEXT("Invitation requested.") : TEXT("The invitation could not be sent. Check that your friend has signed into this prototype.")));
 }
 else Begin(TEXT("Join"),[&]{return Service->JoinLobby(Index);});
}
void UPrototypeMenuWidget::Status(ECrossplayPhase Phase,const FString& Message)
 {if(StatusText)StatusText->SetText(FText::FromString(Phase==ECrossplayPhase::Authenticating ? TEXT("Signing in - complete the Epic sign-in window.") : Message));Render();}
void UPrototypeMenuWidget::Operation(FName Op,bool Success)
{
 if(Op==Pending || Op==TEXT("Connection") || Op==TEXT("Travel")){bBusy=false;Pending=NAME_None;}
 if(!Success && StatusText)StatusText->SetText(FText::FromString(Service && !Service->LastError.IsEmpty() ? Service->LastError : TEXT("The online request failed. Please try again.")));
 if(Success && Op==TEXT("Friends") && StatusText)StatusText->SetText(FText::FromString(TEXT("Epic friends updated.")));
 Render();RefreshInvitation();
}
void UPrototypeMenuWidget::Lobbies(const TArray<FCrossplayLobby>& Values)
{
 if(!SessionRows)return;SessionRows->ClearChildren();Rows.RemoveAll([](const auto& R){return !R || !R->bFriend;});
 if(Values.IsEmpty())Text(SessionRows,TEXT("No sessions shown. Refresh to find public shops."),17)->SetColorAndOpacity(PrototypeMenuStyle::Muted);
 for(int32 I=0;I<Values.Num();++I)
 {
  const auto& Item=Values[I];auto* Row=WidgetTree->ConstructWidget<UPrototypeMenuRow>();Row->Menu=this;Row->Index=I;Row->bAvailable=Item.Players<Item.Capacity;
  PrototypeMenuStyle::StyleButton(Row,false);Text(Row,FString::Printf(TEXT("%s\n%d / %d players   -   %s"),*Item.Name,Item.Players,Item.Capacity,Row->bAvailable ? TEXT("Join") : TEXT("Full")),17);Row->OnClicked.AddDynamic(Row,&UPrototypeMenuRow::Choose);SessionRows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,4));Rows.Add(Row);
 }
 UpdateEnabled();
}
void UPrototypeMenuWidget::FriendList(const TArray<FCrossplayFriend>& Values)
{
 if(!FriendRows)return;FriendRows->ClearChildren();Rows.RemoveAll([](const auto& R){return !R || R->bFriend;});
 if(Values.IsEmpty())Text(FriendRows,TEXT("No friends listed. Refresh your Epic friends, or add your teammate in Epic first."),17)->SetColorAndOpacity(PrototypeMenuStyle::Muted);
 for(int32 I=0;I<Values.Num();++I)
 {
  auto* Row=WidgetTree->ConstructWidget<UPrototypeMenuRow>();Row->Menu=this;Row->Index=I;Row->bFriend=true;PrototypeMenuStyle::StyleButton(Row,false);
  Text(Row,Values[I].Name+TEXT("   -   Invite"),17);Row->OnClicked.AddDynamic(Row,&UPrototypeMenuRow::Choose);FriendRows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,4));Rows.Add(Row);
 }
 UpdateEnabled();
}
void UPrototypeMenuWidget::Members(const TArray<FCrossplayMember>& Values)
{
 if(MembersText)
 {
  FString Value;for(const auto& Member:Values)Value+=Member.Name+(Member.bOwner ? TEXT("  [Host]") : TEXT(""))+(Service && Service->Phase==ECrossplayPhase::Lobby ? Member.bReady ? TEXT("  -  Ready\n") : TEXT("  -  Not ready\n") : TEXT("\n"));
  MembersText->SetText(FText::FromString(Value.IsEmpty() ? IsGameplay() && !InSession() ? TEXT("Solo shop") : TEXT("Connecting to the session...") : Value));
 }
 Render();UpdateEnabled();
}
void UPrototypeMenuWidget::RefreshInvitation()
{
 if(!InvitationPanel)return;InvitationPanel->ClearChildren();Actions.Remove(TEXT("Accept"));
 if(MenuOwner ? MenuOwner->HasInvitation() : Service && Service->HasPendingInvite())
 {Text(InvitationPanel,InSession() ? TEXT("New invitation received. Leave this session to accept it.") : TEXT("You have a shop invitation."),17,true);Button(InvitationPanel,TEXT("Accept"),TEXT("Accept invitation"));}
 UpdateEnabled();
}
void UPrototypeMenuWidget::Invitation(const FString&){RefreshInvitation();}
FReply UPrototypeMenuWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
 if(Event.GetKey()==EKeys::Escape)
 {
  if(!Event.IsRepeat())
  {
   if(GetMenuFocusWidget()==SettingsPanel.Get())SettingsPanel->Back();
   else Resume();
  }
  return FReply::Handled();
 }
 return Super::NativeOnKeyDown(Geometry,Event);
}
