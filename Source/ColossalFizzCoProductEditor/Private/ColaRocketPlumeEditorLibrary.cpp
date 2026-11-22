#include "ColaRocketPlumeEditorLibrary.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"

bool UColaRocketPlumeEditorLibrary::AddNozzleParameters(UNiagaraSystem* System)
{
    if (!System) return false;
    const FNiagaraVariable Count(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.NozzleCount"));
    const FNiagaraVariable Positions(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayPosition::StaticClass()), TEXT("User.NozzlePositions"));
    const FNiagaraVariable Directions(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat3::StaticClass()), TEXT("User.NozzleDirections"));
    const FNiagaraVariable Scales(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat::StaticClass()), TEXT("User.NozzleScales"));
    const FNiagaraVariable Colors(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayColor::StaticClass()), TEXT("User.NozzleColors"));
    auto& Store = System->GetExposedParameters();
    System->Modify();
    for (const FNiagaraVariable* Input : {&Count, &Positions, &Directions, &Scales, &Colors})
    {
        if (Store.IndexOf(*Input) == INDEX_NONE)
        {
            // This system is still being authored and has no open asset editor.
            // Initialize array DIs in the system store before linking graph pins.
            Store.AddParameter(*Input, true);
        }
    }
    auto* ScaleArray = Cast<UNiagaraDataInterfaceArrayFloat>(Store.GetDataInterface(Scales));
    if (ScaleArray && ScaleArray->FloatData.IsEmpty()) ScaleArray->FloatData.Add(1.f);
    auto* ColorArray = Cast<UNiagaraDataInterfaceArrayColor>(Store.GetDataInterface(Colors));
    if (ColorArray && ColorArray->ColorData.IsEmpty()) ColorArray->ColorData.Add(FLinearColor(.25f, .08f, .02f, 1.f));
    return Store.IndexOf(Count) != INDEX_NONE && Store.GetDataInterface(Positions) && Store.GetDataInterface(Directions) && ScaleArray && ColorArray;
}

bool UColaRocketPlumeEditorLibrary::AddStandaloneColorParameter(UNiagaraSystem* System)
{
    if (!System) return false;
    const FNiagaraVariable Color(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.ColaSprayColor"));
    auto& Store = System->GetExposedParameters();
    if (Store.IndexOf(Color) != INDEX_NONE) return true;
    System->Modify();
    return Store.SetParameterValue(FLinearColor(.25f, .08f, .02f, 1.f), Color, true);
}

bool UColaRocketPlumeEditorLibrary::AddOutletParameters(UNiagaraSystem* System)
{
    if (!AddStandaloneColorParameter(System)) return false;
    System->Modify();
    auto& Store = System->GetExposedParameters();
    for (const FName Name : {FName(TEXT("User.OutletFlow")), FName(TEXT("User.OutletPressure"))})
    {
        const FNiagaraVariable Input(FNiagaraTypeDefinition::GetFloatDef(), Name);
        if (Store.IndexOf(Input)==INDEX_NONE && !Store.SetParameterValue(4.f, Input, true)) return false;
    }
    return true;
}

void UColaRocketPlumeEditorLibrary::ResetPlumeEmitters(UNiagaraSystem* System)
{
    if (!System) return;
    static const TSet<FName> GeneratedNames = {TEXT("ColaJet"), TEXT("ColaFan"), TEXT("FoamSpray"), TEXT("FizzBubbles"), TEXT("LingeringBubbles")};
    TSet<FGuid> Handles;
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (GeneratedNames.Contains(Handle.GetName())) Handles.Add(Handle.GetId());
    }
    System->Modify();
    System->RemoveEmitterHandlesById(Handles);
}
