#include "Tests/NormalColaPrototypeTestActor.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace MachineVisualNetwork
{
FString MaterialPath(UMaterialInterface* Material)
{
    while (auto* Dynamic=Cast<UMaterialInstanceDynamic>(Material)) Material=Dynamic->Parent;
    return Material?Material->GetPathName():TEXT("None");
}
TSharedPtr<FJsonObject> Describe(const AManualColaFillingMachine& Machine)
{
    auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("isid"),Machine.ISID); Row->SetNumberField(TEXT("product_id"),Machine.ProductID);
    Row->SetNumberField(TEXT("asmid"),Machine.ASMID);
    Row->SetBoolField(TEXT("active"),Machine.bIsActive);
    Row->SetBoolField(TEXT("hidden"),Machine.IsHidden());
    Row->SetStringField(TEXT("class"),Machine.GetClass()->GetPathName());
    Row->SetStringField(TEXT("actor"),Machine.GetName());
    Row->SetStringField(TEXT("prop_data_asset"),Machine.PropDataAsset?Machine.PropDataAsset->GetPathName():TEXT("None"));
    const auto* Manager=Machine.PropManagerReference.Get();
    const auto* Record=Manager?Manager->FindISIDData(Machine.ISID):nullptr;
    Row->SetNumberField(TEXT("record_product_id"),Record?Record->ProductID:0);
    const auto* Definition=Machine.ResolveProductDefinition();
    Row->SetStringField(TEXT("resolved_source_asset"),Definition && Definition->SourceDataAsset?Definition->SourceDataAsset->GetPathName():TEXT("None"));
    Row->SetStringField(TEXT("resolved_mesh"),Definition && Definition->StaticMesh?Definition->StaticMesh->GetPathName():TEXT("None"));
    Row->SetStringField(TEXT("resolved_active_class"),Definition && Definition->CustomActiveActorClass?Definition->CustomActiveActorClass->GetPathName():TEXT("None"));
    TArray<TSharedPtr<FJsonValue>> DefinitionMaterials;
    if (Definition) for (auto Material:Definition->MaterialOverrides) DefinitionMaterials.Add(MakeShared<FJsonValueString>(MaterialPath(Material)));
    Row->SetArrayField(TEXT("resolved_materials"),DefinitionMaterials);
    Row->SetStringField(TEXT("mesh"),Machine.PropMesh->GetStaticMesh()?Machine.PropMesh->GetStaticMesh()->GetPathName():TEXT("None"));
    Row->SetStringField(TEXT("scale"),Machine.PropMesh->GetRelativeScale3D().ToString());
    TArray<TSharedPtr<FJsonValue>> Materials;
    for (int32 I=0;I<Machine.PropMesh->GetNumMaterials();++I) Materials.Add(MakeShared<FJsonValueString>(MaterialPath(Machine.PropMesh->GetMaterial(I))));
    Row->SetArrayField(TEXT("materials"),Materials); return Row;
}
bool Write(const FString& File,const TSharedRef<FJsonObject>& Object)
{
    FString Text; const auto Writer=TJsonWriterFactory<>::Create(&Text);
    return FJsonSerializer::Serialize(Object,Writer) && FFileHelper::SaveStringToFile(Text,*File);
}
}

bool ANormalColaPrototypeTestActor::TickMachineVisualTest()
{
#if !UE_BUILD_SHIPPING
    if (!FParse::Param(FCommandLine::Get(),TEXT("ManualMachineVisualTest"))) return false;
    if (bMachineVisualComplete) return true;
    FString Directory,TestRole;
    if (!FParse::Value(FCommandLine::Get(),TEXT("MachineVisualDir="),Directory)
        || !FParse::Value(FCommandLine::Get(),TEXT("MachineVisualRole="),TestRole)) return true;
    IFileManager::Get().MakeDirectory(*Directory,true);
    const double Now=FPlatformTime::Seconds();
    if (MachineVisualStartedAt<0) MachineVisualStartedAt=Now;
    auto Failure=[&](const FString& Reason)
    {
        auto Result=MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("passed"),false); Result->SetStringField(TEXT("reason"),Reason);
        MachineVisualNetwork::Write(Directory/(TestRole+TEXT("_failed.json")),Result);
        UE_LOG(LogTemp,Error,TEXT("MACHINE_VISUAL_NETWORK_FAILED role=%s reason=%s"),*TestRole,*Reason);
        bMachineVisualComplete=true; FPlatformMisc::RequestExitWithStatus(false,1);
    };
    auto* PropManager=APropManager::Get(this);
    if (!PropManager)
    {
        if (Now-MachineVisualStartedAt>60) Failure(TEXT("Replicated prop manager never became ready"));
        return true;
    }
    const FString ManifestPath=Directory/TEXT("server_ready.json");
    if (HasAuthority())
    {
        PropManager->bEnableAutosave=false;
        if (!bMachineVisualPrepared)
        {
            if (Now-MachineVisualStartedAt<1) return true;
            auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_ManualFillingMachine.DA_ManualFillingMachine"));
            const int32 ProductID=Product?PropManager->RegisterProductFromDataAsset(Product):0;
            const int32 ID=ProductID>0?PropManager->GenerateProductInstance(ProductID,FTransform(FVector(3000,0,400))):0;
            if (ID<=0 || !PropManager->SetInstanceToUnsettled(ID)) { Failure(TEXT("Could not spawn an additional catalog machine")); return true; }
            bMachineVisualPrepared=true;
        }
        if (!IFileManager::Get().FileExists(*ManifestPath))
        {
            TArray<TSharedPtr<FJsonValue>> Machines;
            for (TActorIterator<AManualColaFillingMachine> It(GetWorld());It;++It)
            {
                if (!It->bIsActive || It->ISID<=0 || !It->PropMesh) continue;
                const auto* Definition=PropManager->FindProductDefinition(It->ProductID);
                if (!Definition || !Definition->StaticMesh || It->PropMesh->GetStaticMesh()!=Definition->StaticMesh)
                { Failure(FString::Printf(TEXT("Server machine %d does not use its canonical product mesh"),It->ISID)); return true; }
                Machines.Add(MakeShared<FJsonValueObject>(MachineVisualNetwork::Describe(**It)));
            }
            if (Machines.Num()<2) { Failure(TEXT("Expected the placed machine and an additional spawned machine")); return true; }
            auto Manifest=MakeShared<FJsonObject>(); Manifest->SetArrayField(TEXT("machines"),Machines);
            Manifest->SetBoolField(TEXT("authority"),true);
            if (!MachineVisualNetwork::Write(ManifestPath,Manifest)) { Failure(TEXT("Cannot write server visual manifest")); return true; }
            UE_LOG(LogTemp,Display,TEXT("MACHINE_VISUAL_SERVER_READY count=%d"),Machines.Num());
        }
        if (IFileManager::Get().FileExists(*(Directory/TEXT("client_passed.json"))) && IFileManager::Get().FileExists(*(Directory/TEXT("late_passed.json"))))
        {
            auto Result=MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("passed"),true);
            MachineVisualNetwork::Write(Directory/TEXT("server_passed.json"),Result);
            bMachineVisualComplete=true; FPlatformMisc::RequestExit(false);
        }
        else if (Now-MachineVisualStartedAt>120) Failure(TEXT("Both receiving clients did not verify before timeout"));
        return true;
    }
    FString ManifestText; TSharedPtr<FJsonObject> Manifest;
    if (!FFileHelper::LoadFileToString(ManifestText,*ManifestPath)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText),Manifest) || !Manifest.IsValid())
    {
        if (Now-MachineVisualStartedAt>60) Failure(TEXT("Server visual manifest was unavailable"));
        return true;
    }
    FString Mismatch; TArray<TSharedPtr<FJsonValue>> Observed;
    for (const auto& Value:Manifest->GetArrayField(TEXT("machines")))
    {
        const auto Expected=Value->AsObject(); const int32 ID=int32(Expected->GetNumberField(TEXT("isid")));
        auto* Machine=Cast<AManualColaFillingMachine>(PropManager->GetActiveActorFromISID(ID));
        if (!Machine || !Machine->bIsActive || !Machine->PropMesh || !PropManager->FindProductDefinition(Machine->ProductID))
        { Mismatch=FString::Printf(TEXT("Waiting for active machine and definitions: %d"),ID); break; }
        auto Actual=MachineVisualNetwork::Describe(*Machine); Observed.Add(MakeShared<FJsonValueObject>(Actual));
        FVector ExpectedScale; ExpectedScale.InitFromString(Expected->GetStringField(TEXT("scale")));
        bool Matches=Machine->ProductID==int32(Expected->GetNumberField(TEXT("product_id")))
            && Actual->GetNumberField(TEXT("record_product_id"))==Expected->GetNumberField(TEXT("record_product_id"))
            && Actual->GetStringField(TEXT("resolved_source_asset"))==Expected->GetStringField(TEXT("resolved_source_asset"))
            && Actual->GetStringField(TEXT("resolved_mesh"))==Expected->GetStringField(TEXT("resolved_mesh"))
            && Actual->GetStringField(TEXT("resolved_active_class"))==Expected->GetStringField(TEXT("resolved_active_class"))
            && Actual->GetStringField(TEXT("class"))==Expected->GetStringField(TEXT("class"))
            && Actual->GetStringField(TEXT("mesh"))==Expected->GetStringField(TEXT("mesh"))
            && Machine->PropMesh->GetRelativeScale3D().Equals(ExpectedScale,.001);
        const auto& ExpectedMaterials=Expected->GetArrayField(TEXT("materials")); const auto& ActualMaterials=Actual->GetArrayField(TEXT("materials"));
        Matches&=ExpectedMaterials.Num()==ActualMaterials.Num();
        if (Matches) for (int32 I=0;I<ExpectedMaterials.Num();++I) Matches&=ExpectedMaterials[I]->AsString()==ActualMaterials[I]->AsString();
        if (!Matches)
        { Mismatch=FString::Printf(TEXT("Machine %d root differs: expected mesh=%s scale=%s; actual mesh=%s scale=%s"),ID,*Expected->GetStringField(TEXT("mesh")),*Expected->GetStringField(TEXT("scale")),*Actual->GetStringField(TEXT("mesh")),*Actual->GetStringField(TEXT("scale"))); break; }
    }
    TArray<TSharedPtr<FJsonValue>> VisibleUnbound;
    if (Mismatch.IsEmpty()) for (TActorIterator<AManualColaFillingMachine> It(GetWorld());It;++It)
    {
        // A correct replicated replacement must not hide a corrupted, still-visible placed actor.
        if (It->ISID>0 || It->IsHidden() || !It->PropMesh || !It->PropMesh->IsVisible()) continue;
        VisibleUnbound.Add(MakeShared<FJsonValueObject>(MachineVisualNetwork::Describe(**It)));
        const auto* Defaults=It->GetClass()->GetDefaultObject<AManualColaFillingMachine>();
        const auto* Authored=It->PropDataAsset.Get();
        UStaticMesh* ExpectedMesh=Authored && Authored->StaticMesh?Authored->StaticMesh.Get():Defaults->PropMesh->GetStaticMesh().Get();
        bool Matches=ExpectedMesh && It->PropMesh->GetStaticMesh()==ExpectedMesh
            && It->PropMesh->GetRelativeScale3D().Equals(Defaults->PropMesh->GetRelativeScale3D(),.001);
        if (Matches) for (int32 I=0;I<It->PropMesh->GetNumMaterials();++I)
        {
            UMaterialInterface* ExpectedMaterial=Authored && Authored->MaterialOverrides.IsValidIndex(I) && Authored->MaterialOverrides[I]
                ?Authored->MaterialOverrides[I].Get()
                :(Defaults->PropMesh->GetStaticMesh()==ExpectedMesh?Defaults->PropMesh->GetMaterial(I):ExpectedMesh->GetMaterial(I));
            Matches&=MachineVisualNetwork::MaterialPath(It->PropMesh->GetMaterial(I))==MachineVisualNetwork::MaterialPath(ExpectedMaterial);
        }
        if (!Matches)
        {
            Mismatch=FString::Printf(TEXT("Visible unbound machine %s ASMID=%d differs from authored root: expected=%s actual=%s"),
                *It->GetName(),It->ASMID,*GetNameSafe(ExpectedMesh),*GetNameSafe(It->PropMesh->GetStaticMesh()));
            break;
        }
    }
    if (!Mismatch.IsEmpty())
    {
        auto Diagnostic=MakeShared<FJsonObject>(); Diagnostic->SetStringField(TEXT("waiting_for"),Mismatch);
        TArray<TSharedPtr<FJsonValue>> Actors;
        for (TActorIterator<AManualColaFillingMachine> It(GetWorld());It;++It)
            if (It->PropMesh) Actors.Add(MakeShared<FJsonValueObject>(MachineVisualNetwork::Describe(**It)));
        Diagnostic->SetArrayField(TEXT("all_machine_actors"),Actors);
        MachineVisualNetwork::Write(Directory/(TestRole+TEXT("_observed.json")),Diagnostic);
        MachineVisualMatchedAt=-1;
        if (Now-MachineVisualStartedAt>60) Failure(Mismatch);
        return true;
    }
    if (MachineVisualMatchedAt<0) MachineVisualMatchedAt=Now;
    if (Now-MachineVisualMatchedAt<2) return true; // Remain correct after later property/definition callbacks.
    auto Result=MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("passed"),true); Result->SetBoolField(TEXT("authority"),false);
    Result->SetArrayField(TEXT("machines"),Observed); Result->SetStringField(TEXT("role"),TestRole);
    Result->SetArrayField(TEXT("visible_unbound_machines"),VisibleUnbound);
    if (!MachineVisualNetwork::Write(Directory/(TestRole+TEXT("_passed.json")),Result)) { Failure(TEXT("Cannot write client visual evidence")); return true; }
    UE_LOG(LogTemp,Display,TEXT("MACHINE_VISUAL_NETWORK_PASSED role=%s count=%d"),*TestRole,Observed.Num());
    bMachineVisualComplete=true; FPlatformMisc::RequestExit(false);
    return true;
#else
    return false;
#endif
}
