#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "PrototypeMenuSubsystem.generated.h"
class UPrototypeMenuWidget;
class UCrossplaySubsystem;
class UInputComponent;
class APlayerController;
class SWidget;

/** Local presentation only; the reusable plugin remains the online authority. */
UCLASS()
class COLOSSALFIZZCO_API UPrototypeMenuSubsystem : public ULocalPlayerSubsystem
{
 GENERATED_BODY()
public:
 virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintCallable,Category="Prototype|Menu") void OpenMenu();
 UFUNCTION(BlueprintCallable,Category="Prototype|Menu") void ToggleMenu();
 void CloseMenu();
 bool IsMenuWorld() const;
 bool HasInvitation() const;
 UPrototypeMenuWidget* GetMenu() const {return Menu;}
private:
 UPROPERTY(Transient) TObjectPtr<UPrototypeMenuWidget> Menu;
 UPROPERTY(Transient) TObjectPtr<UCrossplaySubsystem> Online;
 UPROPERTY(Transient) TObjectPtr<UInputComponent> MenuInput;
 TWeakObjectPtr<APlayerController> BoundController;
 TWeakObjectPtr<UWorld> BoundWorld;
 FTSTicker::FDelegateHandle Ticker;
 bool bPreviousCursor=false;
 TWeakPtr<SWidget> PreviousFocus;
 bool Tick(float DeltaTime);
 void DetachController();
 UFUNCTION() void InvitationReceived(const FString& LobbyId);
 UFUNCTION() void OperationFinished(FName Operation,bool bSuccess);
};
