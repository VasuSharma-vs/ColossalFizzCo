#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CrossplaySettings.generated.h"
UCLASS(Config=Engine,DefaultConfig,meta=(DisplayName="Universal Crossplay"))
class UNIVERSALCROSSPLAY_API UCrossplaySettings:public UDeveloperSettings
{
 GENERATED_BODY()
public:
 UPROPERTY(Config,EditAnywhere,Category="Services") bool bEnableOnline=false;
 UPROPERTY(Config,EditAnywhere,Category="Integration") bool bAutomaticPlayerComponents=false;
 UPROPERTY(Config,EditAnywhere,Category="Voice") bool bEnableVoice=true;
 UPROPERTY(Config,EditAnywhere,Category="Voice") bool bMicrophoneMutedByDefault=true;
 UPROPERTY(Config,EditAnywhere,Category="Lobby",meta=(ClampMin="2",ClampMax="2")) int32 Capacity=2;
 UPROPERTY(Config,EditAnywhere,Category="Lobby") FString CompatibilityId;
 UPROPERTY(Config,EditAnywhere,Category="Maps",meta=(AllowedClasses="/Script/Engine.World")) FSoftObjectPath LobbyMap;
 UPROPERTY(Config,EditAnywhere,Category="Maps",meta=(AllowedClasses="/Script/Engine.World")) FSoftObjectPath GameplayMap;
 UPROPERTY(Config,EditAnywhere,Category="Maps",meta=(AllowedClasses="/Script/Engine.World")) FSoftObjectPath ReturnMap;
 UPROPERTY(Config,EditAnywhere,Category="Timeouts",meta=(ClampMin="15",ClampMax="180")) float OperationTimeout=90;
 UPROPERTY(Config,EditAnywhere,Category="Progression") TArray<FString> AllowedAchievementIds;
 UPROPERTY(Config,EditAnywhere,Category="Progression") TArray<FString> AllowedStatisticIds;
};
