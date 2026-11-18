#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ManualFillingEditorTools.generated.h"

/** Renders the actual native UMG dashboard in an isolated temporary world. */
UCLASS()
class UManualFillingEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable,Category="Manual Filling|Editor") static bool RenderDashboardPreview(const FString& OutputFile);
};
