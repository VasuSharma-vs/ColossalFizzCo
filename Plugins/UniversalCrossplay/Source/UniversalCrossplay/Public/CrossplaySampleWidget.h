#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CrossplayTypes.h"
#include "CrossplaySampleWidget.generated.h"
class UCrossplaySubsystem;class UVerticalBox;class UEditableTextBox;class UTextBlock;
UCLASS()
class UNIVERSALCROSSPLAY_API UCrossplaySampleRow:public UButton
{
 GENERATED_BODY()
public:
 int32 Index=0;bool bFriend=false;
 UPROPERTY(Transient) TObjectPtr<UCrossplaySubsystem> Service;
 UFUNCTION() void Choose();
};
/** Optional example UI. Games can replace this entirely and bind the subsystem's events. */
UCLASS(Blueprintable)
class UNIVERSALCROSSPLAY_API UCrossplaySampleWidget:public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
private:
 UPROPERTY(Transient) TObjectPtr<UCrossplaySubsystem> Service;
 UPROPERTY(Transient) TObjectPtr<UEditableTextBox> LobbyName;
 UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ChatText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> MemberText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Messages;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> Results;
 bool bReady=false,bMuted=true;
 void Button(UVerticalBox* Parent,const FString& Label,FName Function);
 UFUNCTION() void LoginAuto();UFUNCTION() void LoginSteam();UFUNCTION() void LoginEpic();
 UFUNCTION() void HostPublic();UFUNCTION() void HostPrivate();UFUNCTION() void Find();
 UFUNCTION() void Leave();UFUNCTION() void Ready();UFUNCTION() void Start();UFUNCTION() void Mute();
 UFUNCTION() void Send();UFUNCTION() void Accept();UFUNCTION() void SteamFriends();UFUNCTION() void EpicFriends();UFUNCTION() void Close();
 UFUNCTION() void ShowStatus(ECrossplayPhase P,const FString& Text);
 UFUNCTION() void ShowLobbies(const TArray<FCrossplayLobby>& Values);
 UFUNCTION() void ShowFriends(const TArray<FCrossplayFriend>& Values);
 UFUNCTION() void ShowMembers(const TArray<FCrossplayMember>& Values);
 UFUNCTION() void ShowChat(const FCrossplayChatLine& Line);
};
UCLASS()
class UNIVERSALCROSSPLAY_API UCrossplaySampleUI:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Crossplay|Example") static UCrossplaySampleWidget* OpenMenu(APlayerController* Player);
};
