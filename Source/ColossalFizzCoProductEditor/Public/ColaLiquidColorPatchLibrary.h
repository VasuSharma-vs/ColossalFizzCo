#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ColaLiquidColorPatchLibrary.generated.h"

class UNiagaraSystem;

UCLASS()
class UColaLiquidColorPatchLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Apply the two prepared Initialize Particle color inputs to existing liquid emitters. */
    UFUNCTION(BlueprintCallable, Category="Cola|VFX")
    static bool PatchLiquidSpawnColor(UNiagaraSystem* System, UObject* PreparedInputs);
};
