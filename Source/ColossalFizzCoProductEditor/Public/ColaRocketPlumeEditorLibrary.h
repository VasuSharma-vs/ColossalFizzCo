#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ColaRocketPlumeEditorLibrary.generated.h"

class UNiagaraSystem;

UCLASS()
class UColaRocketPlumeEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Declare the typed array inputs used by the shared plume generator. */
    UFUNCTION(BlueprintCallable, Category="Cola|VFX")
    static bool AddNozzleParameters(UNiagaraSystem* System);

    /** Expose the editable liquid color on a single-nozzle plume. */
    UFUNCTION(BlueprintCallable, Category="Cola|VFX")
    static bool AddStandaloneColorParameter(UNiagaraSystem* System);

    /** Remove the generated plume layers before rebuilding them in the same package. */
    UFUNCTION(BlueprintCallable, Category="Cola|VFX")
    static void ResetPlumeEmitters(UNiagaraSystem* System);
};
