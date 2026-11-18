#include "ColaLiquidColorPatchLibrary.h"
#include "NiagaraClipboard.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

namespace
{
    // Cone angle and curl are independent of color. Keep the authored values
    // as a guard against accidentally modifying motion during this migration.
    TMap<FString, float> ReadMotionSettings(UNiagaraSystem& System)
    {
        TMap<FString, float> Result;
        for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
        {
            if (!Handle.GetEmitterData()) continue;
            TArray<UNiagaraScript*> Scripts;
            Handle.GetEmitterData()->GetScripts(Scripts);
            for (const UNiagaraScript* Script : Scripts)
            {
                if (!Script) continue;
                const auto& Store = Script->RapidIterationParameters;
                for (const auto& Parameter : Store.ReadParameterVariables())
                {
                    const FString Key = Parameter.GetName().ToString();
                    if (Parameter.GetType() == FNiagaraTypeDefinition::GetFloatDef()
                        && (Key.EndsWith(TEXT(".Cone Angle")) || Key.Contains(TEXT(".CurlNoise"))))
                    {
                        Result.Add(Script->GetPathName() + TEXT("|") + Key, Store.GetParameterValueOrDefault<float>(Parameter, 0.f));
                    }
                }
            }
        }
        return Result;
    }
}

bool UColaLiquidColorPatchLibrary::PatchLiquidSpawnColor(UNiagaraSystem* System, UObject* PreparedInputs)
{
    if (!System || !PreparedInputs) return false;
    const TMap<FString, float> OriginalMotion = ReadMotionSettings(*System);

    // The optional Cascade converter is only loaded when authoring. Read its two
    // staged inputs without making the project editor require that plugin at startup.
    const FArrayProperty* Property = FindFProperty<FArrayProperty>(PreparedInputs->GetClass(), TEXT("FunctionInputs"));
    const FObjectPropertyBase* Element = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
    if (!Element || Element->PropertyClass != UNiagaraClipboardFunctionInput::StaticClass()) return false;
    FScriptArrayHelper Values(Property, Property->ContainerPtrToValuePtr<void>(PreparedInputs));
    if (Values.Num() != 2) return false;
    const UNiagaraClipboardFunctionInput* Mode = nullptr;
    const UNiagaraClipboardFunctionInput* Color = nullptr;
    for (int32 I = 0; I < Values.Num(); ++I)
    {
        const auto* Input = Cast<UNiagaraClipboardFunctionInput>(Element->GetObjectPropertyValue(Values.GetRawPtr(I)));
        if (Input && Input->InputName == TEXT("Color Mode")) Mode = Input;
        if (Input && Input->InputName == TEXT("Color")) Color = Input;
    }
    if (!Mode || !Color) return false;

    auto View = MakeShared<FNiagaraSystemViewModel>();
    FNiagaraSystemViewModelOptions Options;
    Options.bCanSimulate = false;
    Options.bCanAutoCompile = false;
    Options.bIsForDataProcessingOnly = true;
    Options.MessageLogGuid = FGuid::NewGuid();
    View->Initialize(*System, Options);
    TArray<UNiagaraStackModuleItem*> InitializeModules;
    for (const auto& Emitter : View->GetEmitterHandleViewModels())
    {
        const FName Name = Emitter->GetName();
        if (Name != TEXT("ColaJet") && Name != TEXT("ColaFan")) continue;
        auto* Stack = Emitter->GetEmitterStackViewModel();
        auto* Root = Stack ? Stack->GetRootEntry() : nullptr;
        if (!Root) return false;
        Root->RefreshChildren();
        TArray<UNiagaraStackModuleItem*> Modules;
        Root->GetUnfilteredChildrenOfType(Modules, true);
        int32 Matches = 0;
        for (auto* Item : Modules)
        {
            const UObject* Script = Item->GetExternalAsset();
            if (Script && Script->GetPathName() == TEXT("/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle"))
            {
                InitializeModules.Add(Item);
                ++Matches;
            }
        }
        if (Matches != 1) return false;
    }
    if (InitializeModules.Num() != 2) return false;
    System->Modify();
    for (auto* Item : InitializeModules)
    {
        // Applying the static switch first makes the Color input editable.
        Item->SetInputValuesFromClipboardFunctionInputs({Mode});
        Item->SetInputValuesFromClipboardFunctionInputs({Color});
    }
    System->RequestCompile(true);
    System->WaitForCompilationComplete(true, false);
    const auto PatchedMotion = ReadMotionSettings(*System);
    for (const auto& Entry : OriginalMotion)
    {
        const float* Patched = PatchedMotion.Find(Entry.Key);
        if (!Patched || !FMath::IsNearlyEqual(*Patched, Entry.Value)) return false;
        UE_LOG(LogTemp, Display, TEXT("Cola spray retained %s = %g"), *Entry.Key, *Patched);
    }
    return System->IsValid();
}
