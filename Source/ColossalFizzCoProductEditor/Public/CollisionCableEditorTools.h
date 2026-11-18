#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CollisionCableEditorTools.generated.h"

UCLASS()
class UCollisionCableEditorTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Create a separate variant; never edits the original or overwrites an existing destination. */
    UFUNCTION(BlueprintCallable, Category="Cable|Editor")
    static bool CreateCollisionCableVariant(const FString& SourcePath, const FString& DestinationPath);

    /** Upgrade only the custom cable variant. Preserve endpoint interfaces, but replace its old gravity/braking loop with the native movement adapter. */
    UFUNCTION(BlueprintCallable, Category="Cable|Editor")
    static bool UpgradeCableFeatureIntegration(const FString& BlueprintPath);
};
