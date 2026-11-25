#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "PlayerUpgradesSubsystem.generated.h"

class AGameModeBase;
class APlayerController;

/** Attaches the upgrade component while retaining the existing Blueprint game modes. */
UCLASS()
class UPlayerUpgradesSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void OnPlayerLoggedIn(AGameModeBase* GameMode, APlayerController* Player);
};
