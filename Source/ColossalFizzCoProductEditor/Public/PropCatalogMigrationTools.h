#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PropCatalogMigrationTools.generated.h"
class USaveGame;
UCLASS()
class UPropCatalogMigrationTools : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Read an isolated save copy for migration inspection; never writes player progress. */
 UFUNCTION(BlueprintCallable,Category="Products|Migration") static USaveGame* ReadSaveCopy(const FString& File);
 UFUNCTION(BlueprintCallable,Category="Products|Migration") static FString InspectSavedIdentities(USaveGame* Save);
};
