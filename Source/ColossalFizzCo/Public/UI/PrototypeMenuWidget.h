#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "CrossplayTypes.h"
#include "PrototypeMenuWidget.generated.h"
class UCrossplaySubsystem;
class UPrototypeMenuSubsystem;
class UVerticalBox;
class UTextBlock;
class UEditableTextBox;
class UCheckBox;
class UPrototypeMenuWidget;
class UPrototypeSettingsWidget;
class UWidgetSwitcher;

UCLASS()
class COLOSSALFIZZCO_API UPrototypeMenuRow : public UButton
{
 GENERATED_BODY()
public:
 TWeakObjectPtr<UPrototypeMenuWidget> Menu;
 int32 Index=0;
 bool bFriend=false;
 bool bAvailable=true;
 UFUNCTION() void Choose();
};

/** Game-owned presentation over UniversalCrossplay; no online logic duplicated. */
UCLASS()
class COLOSSALFIZZCO_API UPrototypeMenuWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
 void RefreshInvitation();
 void ChooseRow(int32 Index,bool bFriend);
 UUserWidget* GetMenuFocusWidget();
 UFUNCTION(BlueprintPure,Category="Prototype|Menu") UButton* FindAction(FName Name) const;
private:
 UPROPERTY(Transient) TObjectPtr<UCrossplaySubsystem> Service;
 UPROPERTY(Transient) TObjectPtr<UPrototypeMenuSubsystem> MenuOwner;
 UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> MenuPages;
 UPROPERTY(Transient) TObjectPtr<UPrototypeSettingsWidget> SettingsPanel;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> Body;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> SessionRows;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> FriendRows;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> InvitationPanel;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> AccountText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> MembersText;
 UPROPERTY(Transient) TObjectPtr<UEditableTextBox> NameInput;
 UPROPERTY(Transient) TObjectPtr<UCheckBox> PrivateInput;
 UPROPERTY(Transient) TMap<FName,TObjectPtr<UButton>> Actions;
 UPROPERTY(Transient) TArray<TObjectPtr<UPrototypeMenuRow>> Rows;
 FString LobbyTitle=TEXT("Colossal Fizz Co shop");
 FString View;
 bool bPrivate=false,bBusy=false,bMuted=true,bOnlineAvailable=false;
 FName Pending;
 UTextBlock* Text(class UPanelWidget* Parent,const FString& Value,int32 Size=18,bool Accent=false);
 UButton* Button(UVerticalBox* Parent,FName Action,const FString& Label,bool Accent=false);
 void Render();
 void UpdateEnabled();
 void Begin(FName Operation,TFunctionRef<bool()> Request);
 bool IsGameplay() const;
 bool InSession() const;
 UFUNCTION() void Login();
 UFUNCTION() void Offline();
 UFUNCTION() void Host();
 UFUNCTION() void Find();
 UFUNCTION() void Ready();
 UFUNCTION() void Start();
 UFUNCTION() void Leave();
 UFUNCTION() void Logout();
 UFUNCTION() void Friends();
 UFUNCTION() void Accept();
 UFUNCTION() void Microphone();
 UFUNCTION() void Settings();
 void CloseSettings();
 UFUNCTION() void Resume();
 UFUNCTION() void Quit();
 UFUNCTION() void NameChanged(const FText& Value);
 UFUNCTION() void PrivacyChanged(bool Checked);
 UFUNCTION() void Status(ECrossplayPhase Phase,const FString& Message);
 UFUNCTION() void Operation(FName Operation,bool Success);
 UFUNCTION() void Lobbies(const TArray<FCrossplayLobby>& Values);
 UFUNCTION() void FriendList(const TArray<FCrossplayFriend>& Values);
 UFUNCTION() void Members(const TArray<FCrossplayMember>& Values);
 UFUNCTION() void Invitation(const FString& LobbyId);
};
