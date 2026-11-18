#include "CollisionCableEditorTools.h"
#include "AssetToolsModule.h"
#include "CableComponent.h"
#include "Components/CollisionCableComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "IAssetTools.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

bool UCollisionCableEditorTools::CreateCollisionCableVariant(const FString& SourcePath, const FString& DestinationPath)
{
    UBlueprint* Source = LoadObject<UBlueprint>(nullptr, *SourcePath);
    if (!Source || !Source->SimpleConstructionScript || !FPackageName::IsValidLongPackageName(DestinationPath)
        || FPackageName::DoesPackageExist(DestinationPath) || Source->GetOutermost()->GetName() == DestinationPath) return false;
    UClass* OldClass = UCableComponent::StaticClass();
    UClass* NewClass = UCollisionCableComponent::StaticClass();
    bool HasCable = false;
    for (const USCS_Node* Node : Source->SimpleConstructionScript->GetAllNodes()) HasCable |= Node->ComponentClass == OldClass;
    if (!HasCable) return false;

    IAssetTools& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UBlueprint* Variant = Cast<UBlueprint>(Assets.DuplicateAsset(FPackageName::GetLongPackageAssetName(DestinationPath),
        FPackageName::GetLongPackagePath(DestinationPath), Source));
    if (!Variant) return false;
    for (USCS_Node* Node : Variant->SimpleConstructionScript->GetAllNodes())
    {
        if (Node->ComponentClass != OldClass) continue;
        UActorComponent* OldTemplate = Node->ComponentTemplate;
        const FName TemplateName = OldTemplate->GetFName();
        UObject* TemplateOuter = OldTemplate->GetOuter();
        OldTemplate->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
        auto* Replacement = NewObject<UCollisionCableComponent>(TemplateOuter, TemplateName, RF_Public | RF_ArchetypeObject | RF_Transactional);
        for (TFieldIterator<FProperty> It(OldClass); It; ++It)
        {
            FProperty* OldProperty = *It;
            if (OldProperty->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_InstancedReference)) continue;
            FProperty* NewProperty = NewClass->FindPropertyByName(OldProperty->GetFName());
            if (NewProperty && NewProperty->SameType(OldProperty)) NewProperty->CopyCompleteValue(
                NewProperty->ContainerPtrToValuePtr<void>(Replacement), OldProperty->ContainerPtrToValuePtr<void>(OldTemplate));
        }
        Replacement->SubstepTime = 1.f / 120.f;
        Replacement->SolverIterations = 8;
        Replacement->bEnableStiffness = false;
        Replacement->bEnableCollision = true;
        Replacement->NumSides = 8;
        Replacement->PrimaryComponentTick.TickGroup = TG_PostPhysics;
        Replacement->PrimaryComponentTick.bStartWithTickEnabled = true;
        Replacement->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Node->ComponentClass = NewClass; Node->ComponentTemplate = Replacement;
    }
    auto RetargetType = [&](FEdGraphPinType& Type)
    {
        if (Type.PinSubCategoryObject.Get() == OldClass) Type.PinSubCategoryObject = NewClass;
        if (Type.PinValueType.TerminalSubCategoryObject.Get() == OldClass) Type.PinValueType.TerminalSubCategoryObject = NewClass;
    };
    for (FBPVariableDescription& Variable : Variant->NewVariables) RetargetType(Variable.VarType);
    TArray<UEdGraph*> Graphs; Variant->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (auto* Call = Cast<UK2Node_CallFunction>(Node))
                if (Call->FunctionReference.GetMemberParentClass() == OldClass)
                    Call->FunctionReference.SetExternalMember(Call->FunctionReference.GetMemberName(), NewClass);
            if (auto* Variable = Cast<UK2Node_Variable>(Node))
                if (Variable->VariableReference.GetMemberParentClass() == OldClass)
                    Variable->VariableReference.SetExternalMember(Variable->VariableReference.GetMemberName(), NewClass);
            if (auto* Editable = Cast<UK2Node_EditablePinBase>(Node))
                for (const TSharedPtr<FUserPinInfo>& Pin : Editable->UserDefinedPins) if (Pin) RetargetType(Pin->PinType);
            if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node))
                for (FBPVariableDescription& Variable : Entry->LocalVariables) RetargetType(Variable.VarType);
            for (UEdGraphPin* Pin : Node->Pins) if (Pin) RetargetType(Pin->PinType);
        }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Variant);
    FBlueprintEditorUtils::RefreshAllNodes(Variant);
    FKismetEditorUtilities::CompileBlueprint(Variant);
    if (Variant->Status == BS_Error)
    {
        UE_LOG(LogTemp, Error, TEXT("Collision cable variant failed compilation; nothing was saved."));
        return false;
    }
    FSavePackageArgs Save;
    Save.TopLevelFlags = RF_Public | RF_Standalone;
    const FString Filename = FPackageName::LongPackageNameToFilename(DestinationPath, FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Variant->GetOutermost(), Variant, *Filename, Save);
}

bool UCollisionCableEditorTools::UpgradeCableFeatureIntegration(const FString& BlueprintPath)
{
    UBlueprint* Blueprint=LoadObject<UBlueprint>(nullptr,*BlueprintPath);
    if(!Blueprint || !Blueprint->SimpleConstructionScript)return false;
    FName CableName;
    for(const auto* Node:Blueprint->SimpleConstructionScript->GetAllNodes())
        if(Node->ComponentClass->IsChildOf(UCollisionCableComponent::StaticClass())){CableName=Node->GetVariableName();break;}
    if(CableName.IsNone())return false;
    Blueprint->Modify();
    const auto* Schema=GetDefault<UEdGraphSchema_K2>();
    constexpr auto Marker=TEXT("NativeCableFeaturesV1");
    auto AddCable=[&](UEdGraph* Graph,int32 X,int32 Y)
    {
        auto* Node=NewObject<UK2Node_VariableGet>(Graph);Node->VariableReference.SetSelfMember(CableName);
        Graph->AddNode(Node,false,false);Node->CreateNewGuid();Node->AllocateDefaultPins();Node->NodePosX=X;Node->NodePosY=Y;
        Node->NodeComment=Marker;return Node;
    };
    auto AddGrabSwitch=[&](UEdGraph* Graph,int32 X,int32 Y)
    {
        auto* Cable=AddCable(Graph,X-200,Y+80);
        auto* Value=NewObject<UK2Node_VariableGet>(Graph);
        Value->VariableReference.SetExternalMember(GET_MEMBER_NAME_CHECKED(UCollisionCableComponent,bEnableGrabbing),UCollisionCableComponent::StaticClass());
        Graph->AddNode(Value,false,false);Value->CreateNewGuid();Value->AllocateDefaultPins();Value->NodePosX=X;Value->NodePosY=Y;
        Value->NodeComment=Marker;
        Schema->TryCreateConnection(Cable->GetValuePin(),Value->FindPinChecked(UEdGraphSchema_K2::PN_Self));
        return Value->GetValuePin();
    };
    TArray<UEdGraph*> Graphs;Blueprint->GetAllGraphs(Graphs);
    int32 Replaced=0,Gated=0;
    for(auto* Graph:Graphs)
    {
        const auto Nodes=Graph->Nodes;
        for(UEdGraphNode* Node:Nodes)
        {
            FName EventName;
            if(auto* Event=Cast<UK2Node_Event>(Node))EventName=Event->EventReference.GetMemberName();
            if(auto* Event=Cast<UK2Node_CustomEvent>(Node))EventName=Event->CustomFunctionName;
            FString Name=EventName.ToString().ToLower();Name.ReplaceInline(TEXT("_"),TEXT(""));Name.ReplaceInline(TEXT(" "),TEXT(""));
            auto* Then=Node->FindPin(UEdGraphSchema_K2::PN_Then,EGPD_Output);
            if(EventName.IsNone()||!Then)continue;
            if(Name.Contains(TEXT("ongoing")) && (Name.Contains(TEXT("intract"))||Name.Contains(TEXT("interact"))))
            {
                if(Then->LinkedTo.ContainsByPredicate([&](const auto* Pin){return Pin->GetOwningNode()->NodeComment==Marker;}))continue;
                // The old loop wrote CharacterMovement gravity and custom braking,
                // then disabled cable ticking after five seconds. Native adapters
                // now own these restrictions and the sleeping system owns ticking.
                auto* Wake=NewObject<UK2Node_CallFunction>(Graph);
                Wake->SetFromFunction(UCollisionCableComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UCollisionCableComponent,WakeCable)));
                Graph->AddNode(Wake,false,false);Wake->CreateNewGuid();Wake->AllocateDefaultPins();
                Wake->NodePosX=Node->NodePosX+350;Wake->NodePosY=Node->NodePosY;Wake->NodeComment=Marker;
                auto* Cable=AddCable(Graph,Wake->NodePosX-180,Wake->NodePosY+150);
                Then->BreakAllPinLinks();
                if(!Schema->TryCreateConnection(Then,Wake->GetExecPin()) ||
                    !Schema->TryCreateConnection(Cable->GetValuePin(),Wake->FindPinChecked(UEdGraphSchema_K2::PN_Self)))return false;
                ++Replaced;
            }
            else if(Name.Contains(TEXT("intractwithactor"))||Name.Contains(TEXT("interactwithactor"))||
                Name.Contains(TEXT("onintractionbegain"))||Name.Contains(TEXT("oninteractionbegin")))
            {
                if(Then->LinkedTo.ContainsByPredicate([&](const auto* Pin){return Pin->GetOwningNode()->NodeComment==Marker;}))continue;
                const auto OldLinks=Then->LinkedTo;
                auto* Branch=NewObject<UK2Node_IfThenElse>(Graph);Graph->AddNode(Branch,false,false);
                Branch->CreateNewGuid();Branch->AllocateDefaultPins();Branch->NodePosX=Node->NodePosX+300;Branch->NodePosY=Node->NodePosY;Branch->NodeComment=Marker;
                Then->BreakAllPinLinks();Schema->TryCreateConnection(Then,Branch->GetExecPin());
                Schema->TryCreateConnection(AddGrabSwitch(Graph,Branch->NodePosX-100,Branch->NodePosY+180),Branch->GetConditionPin());
                for(auto* Link:OldLinks)Schema->TryCreateConnection(Branch->GetThenPin(),Link);
                ++Gated;
            }
        }
        FString GraphName=Graph->GetName().ToLower();GraphName.ReplaceInline(TEXT(" "),TEXT(""));
        if(GraphName!=TEXT("cangrab"))continue;
        for(UEdGraphNode* Node:Nodes)if(auto* Result=Cast<UK2Node_FunctionResult>(Node))
            for(auto* Pin:Result->Pins)
            {
                if(Pin->Direction!=EGPD_Input||Pin->PinType.PinCategory!=UEdGraphSchema_K2::PC_Boolean)continue;
                if(Pin->LinkedTo.ContainsByPredicate([&](const auto* Linked){return Linked->GetOwningNode()->NodeComment==Marker;}))continue;
                auto* And=NewObject<UK2Node_CallFunction>(Graph);And->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BooleanAND")));
                Graph->AddNode(And,false,false);And->CreateNewGuid();And->AllocateDefaultPins();And->NodeComment=Marker;
                And->NodePosX=Result->NodePosX-300;And->NodePosY=Result->NodePosY;
                auto* A=And->FindPinChecked(TEXT("A"));auto* B=And->FindPinChecked(TEXT("B"));
                A->DefaultValue=Pin->DefaultValue;const auto OldLinks=Pin->LinkedTo;Pin->BreakAllPinLinks();
                for(auto* Link:OldLinks)Schema->TryCreateConnection(Link,A);
                Schema->TryCreateConnection(AddGrabSwitch(Graph,And->NodePosX-150,And->NodePosY+150),B);
                Schema->TryCreateConnection(And->GetReturnValuePin(),Pin);++Gated;
            }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error)return false;
    UE_LOG(LogTemp,Display,TEXT("CABLE_FEATURE_UPGRADE replaced movement loops=%d, gated grab paths=%d"),Replaced,Gated);
    FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(Blueprint->GetOutermost(),Blueprint,*FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Save);
}
