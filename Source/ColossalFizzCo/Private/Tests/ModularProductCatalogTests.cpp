#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/PropProductCatalog.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductIdentityCatalogTest,"ColossalFizzCo.Products.StableCatalogIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProductIdentityCatalogTest::RunTest(const FString&)
{
 auto* C=NewObject<UPropProductCatalog>();auto* A=NewObject<UBottleProductDataAsset>();auto* B=NewObject<UBottleProductDataAsset>();
 A->StaticMesh=B->StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 C->ReserveLegacyPIDs({1,901,1000,1400});
 const int32 First=C->RegisterIdentity(A,EPropProductCategory::Bottle,FText::FromString(TEXT("Bottle A")));
 TestTrue(TEXT("New IDs skip all reserved legacy IDs"),First>1400);
 const int32 Second=C->RegisterIdentity(B,EPropProductCategory::Bottle,FText::FromString(TEXT("Bottle B")));
 C->Products.Swap(0,1);C->Products[1].Name=FText::FromString(TEXT("Renamed bottle"));C->AssignMissingPIDs();
 TestEqual(TEXT("Reordering and renaming never renumber"),C->FindByAsset(A)->PID,First);
 TestEqual(TEXT("Second identity stays stable"),C->FindByAsset(B)->PID,Second);
 TestEqual(TEXT("Existing collision is refused"),C->RegisterIdentity(B,EPropProductCategory::Content,FText::FromString(TEXT("Collision")),First),0);
 TestTrue(TEXT("Identity fixture is valid before introducing a collision"),C->ValidateCatalog().IsEmpty());
 C->Products[0].PID=First;TestTrue(TEXT("Duplicate PID is specifically reported"),C->ValidateCatalog().ContainsByPredicate([](const FString& E){return E.Contains(TEXT("duplicate PID"));}));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModularBottleProductsTest,"ColossalFizzCo.Products.ModularBottleLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FModularBottleProductsTest::RunTest(const FString&)
{
 const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
 if(!W)return false;GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 ON_SCOPE_EXIT{W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* M=W->SpawnActor<APropManager>();M->bEnableAutosave=false;M->ProductCatalog.Reset();M->InitializePropManager();
 auto* Catalog=NewObject<UPropProductCatalog>(M);auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
 auto* Bottle=NewObject<UBottleProductDataAsset>(Catalog);Bottle->StaticMesh=Cube;Bottle->CustomActiveActorClass=AActivePropActor::StaticClass();
 Bottle->NumOfPortions=4;Bottle->MaxOverfillLevel=1;Bottle->DefaultCapSealed=0;
 auto* Large=DuplicateObject<UBottleProductDataAsset>(Bottle,Catalog);Large->NumOfPortions=8;
 Catalog->RegisterIdentity(Bottle,EPropProductCategory::Bottle,FText::FromString(TEXT("Small bottle")),1000);
 Catalog->RegisterIdentity(Large,EPropProductCategory::Bottle,FText::FromString(TEXT("Large bottle")),1001);
 auto* Content=NewObject<UPropContentDataAsset>(Catalog);Content->Properties.BasePortionPrice=2;
 FPropBottleContentVisual Small;Small.Bottle.PID=1000;Small.Mesh=Cube;
 FPropBottleContentVisual Big=Small;Big.Bottle.PID=1001;Big.Mesh=Sphere;Big.Attachment.SetLocation(FVector(0,0,3));
 Content->Bottles={Small,Big};Catalog->RegisterIdentity(Content,EPropProductCategory::Content,FText::FromString(TEXT("Same cola")),2000);
 auto* Cap=NewObject<UPropCapDataAsset>(Catalog);Cap->Mesh=Cube;Cap->ActiveActorClass=AActivePropActor::StaticClass();Cap->SupportedBottles={FPropProductReference(1000)};
 Catalog->RegisterIdentity(Cap,EPropProductCategory::Cap,FText::FromString(TEXT("Small cap")),3000);
 M->ProductCatalog=Catalog;if(!TestTrue(TEXT("Typed catalog registers without game-specific assets"),M->InitializeProductCatalog()))return false;
 const int32 Empty=M->GenerateProductInstance(1000,FTransform(FVector(0,0,200)));
 const int32 Other=M->GenerateProductInstance(1001,FTransform(FVector(500,0,200)));
 TestTrue(TEXT("Empty bottle has no liquid child"),M->GetChildISIDs(Empty).IsEmpty());
 auto* Cylinder=NewObject<UInteractablePropDataAsset>(M);Cylinder->StaticMesh=Cube;Cylinder->NumOfPortions=40;Cylinder->InitialContentProduct.PID=2000;Cylinder->DefaultContentLevel=1;
 const int32 Source=M->GenerateProductInstance(M->RegisterProductFromDataAsset(Cylinder),FTransform(FVector(0,100,200)));
 const auto Session=M->BeginLiquidTransfer(Source,Empty);
 TestTrue(TEXT("Compatible content starts"),Session.IsValid());TestEqual(TEXT("Three portions transfer"),M->TransferLiquid(Session,3),3.f);
 const auto Kids=M->GetChildISIDs(Empty);if(!TestEqual(TEXT("Exactly one content child"),Kids.Num(),1))return false;
 const int32 Child=Kids[0];TestEqual(TEXT("Content child owns its product PID"),M->FindISIDData(Child)->ProductID,2000);
 TestEqual(TEXT("Bottle PID remains unchanged"),M->FindISIDData(Empty)->ProductID,1000);
 TestEqual(TEXT("Current portions are derived precisely on bottle"),M->FindISIDData(Empty)->CurrentPortions,3.f);
 TestEqual(TEXT("Current level uses bottle capacity"),M->FindUniqueData(Empty)->EventData.ContentLevel,.75f);
 TestEqual(TEXT("Repeated fill adds remaining portion"),M->TransferLiquid(Session,1),1.f);M->EndLiquidTransfer(Session);
 TestEqual(TEXT("Refill preserves child ISID"),M->GetChildISIDs(Empty)[0],Child);
 auto Second=M->BeginLiquidTransfer(Source,Other);TestEqual(TEXT("Other bottle receives eight portions"),M->TransferLiquid(Second,8),8.f);M->EndLiquidTransfer(Second);
 const int32 OtherChild=M->GetChildISIDs(Other)[0];
 TestEqual(TEXT("Both shapes share one content PID"),M->FindISIDData(OtherChild)->ProductID,2000);
 TestTrue(TEXT("Shapes have distinct mesh batches"),M->GetComponentFromISID(Child)!=M->GetComponentFromISID(OtherChild));
 TestTrue(TEXT("Second shape uses its content mesh"),M->GetComponentFromISID(OtherChild)->GetStaticMesh()==Sphere);
 TestFalse(TEXT("Unsupported cap is rejected"),M->IsAssemblyCompatible(1001,2000,3000));
 const auto Before=M->FindUniqueData(Other)->EventData;const int32 Count=M->GetChildISIDs(Other).Num();
 const int32 BadCap=M->GenerateProductInstance(3000,M->GetTransform(Other));
 TestFalse(TEXT("Unsupported installation does not mutate either record"),M->AttachPhysicalCap(Other,BadCap));
 TestEqual(TEXT("Rejected cap keeps no owner"),M->FindISIDData(BadCap)->Attachment.OwnerISID,0);
 TestEqual(TEXT("Rejected cap does not change bottle children"),M->GetChildISIDs(Other).Num(),Count);
 TestEqual(TEXT("Rejected cap preserves content"),M->FindUniqueData(Other)->EventData.ContentLevel,Before.ContentLevel);
 auto Incompatible=*M->FindContentDefinition(2000);Incompatible.ContentID=2001;Incompatible.SupportedBottlePIDs={1001};M->RegisterContentDefinition(Incompatible);
 auto SourceData=*M->FindUniqueData(Source);SourceData.EventData.ContentID=2001;M->UniqueDataByISID.Add(Source,SourceData);
 TestFalse(TEXT("Unsupported content is rejected before transfer session"),M->BeginLiquidTransfer(Source,Empty).IsValid());
 TestEqual(TEXT("Rejected fill preserves content child identity"),M->GetChildISIDs(Empty)[0],Child);
 auto Alternate=*M->FindProductDefinition(2000);Alternate.ProductID=2001;Alternate.StaticMeshID=2001;Alternate.CatalogPID=2001;M->RegisterProductDefinition(Alternate);
 FPropEventSyncDelta Drain;Drain.SetFlag(EPropEventDirtyMask::ContentLevel);Drain.ContentLevel=0;M->SubmitInstanceEventUpdate(Other,Drain);
 auto Refill=M->BeginLiquidTransfer(Source,Other);
 TestTrue(TEXT("Completely empty bottle accepts a different selected content"),Refill.IsValid());
 TestEqual(TEXT("Old empty flavor identity cannot block fresh filling"),M->TransferLiquid(Refill,1),1.f);M->EndLiquidTransfer(Refill);
 TestEqual(TEXT("Refill selects the new content PID"),M->FindUniqueData(Other)->EventData.ContentID,2001);
 TestEqual(TEXT("Refill creates exactly one new content child"),M->GetChildISIDs(Other).Num(),1);
 const int32 CapID=M->GenerateProductInstance(3000,M->GetTransform(Empty));
 if(!TestTrue(TEXT("Compatible physical cap attaches"),M->AttachPhysicalCap(Empty,CapID)))return false;
 TestTrue(TEXT("Cap seals normally"),M->ClosePhysicalCap(Empty,.6f));
 TestTrue(TEXT("Direct child cache contains content and cap"),M->FindISIDData(Empty)->ChildISIDs.Contains(Child) && M->FindISIDData(Empty)->ChildISIDs.Contains(CapID));
 auto* Part=M->GetAssemblyComponent(CapID);if(!Part)return false;Part->AddLocalRotation(FRotator(0,23,0));const auto Pose=Part->GetComponentTransform();
 const int32 Slot=M->FindISIDData(Child)->ISMI;
 TestTrue(TEXT("Complete assembly demotes"),M->ConvertASMToISM(Empty,EPropState::AtRest,true,true));
 AActivePropActor* Active=nullptr;TestTrue(TEXT("Interacting with cap promotes root"),M->PromoteInstanceToASM(CapID,EPropActivationReason::Interaction,false,Active));
 TestTrue(TEXT("Complete assembly returns at exact cap pose"),M->GetAssemblyComponent(CapID) && M->GetAssemblyComponent(CapID)->GetComponentTransform().Equals(Pose,.01f));
 TestEqual(TEXT("Content ISM slot survives transitions"),M->FindISIDData(Child)->ISMI,Slot);
 FPropSerializedInstance SavedChild;TestTrue(TEXT("Content child serializes"),M->SerializeInstance(Child,SavedChild));
 auto* Save=NewObject<UPropDatabaseSaveGame>();Save->WorldSaveData.SavedInstances.Add(SavedChild);
 TArray<uint8> Bytes;TestTrue(TEXT("Assembly state writes to engine archive"),UGameplayStatics::SaveGameToMemory(Save,Bytes));
 auto* Loaded=Cast<UPropDatabaseSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
 if(!TestNotNull(TEXT("Assembly archive reloads"),Loaded))return false;
 const auto& Restored=Loaded->WorldSaveData.SavedInstances[0];
 TestEqual(TEXT("Save keeps content ISID"),Restored.ISID,Child);
 TestEqual(TEXT("Save keeps immediate owner"),Restored.Attachment.OwnerISID,Empty);
 TestEqual(TEXT("Save keeps bottle geometry selector"),Restored.VisualBottlePID,1000);
 TestTrue(TEXT("Save keeps exact relative attachment"),Restored.Attachment.RelativeTransform.Equals(SavedChild.Attachment.RelativeTransform));
 TestTrue(TEXT("Cap detaches"),M->DetachPhysicalCap(Empty));
 TestEqual(TEXT("Detached cap retains ISID, clears owner"),M->FindISIDData(CapID)->Attachment.OwnerISID,0);
 TestFalse(TEXT("Root direct children clear detached cap"),M->FindISIDData(Empty)->ChildISIDs.Contains(CapID));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModularFreshRestoreTest,"ColossalFizzCo.Products.FreshManagerRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FModularFreshRestoreTest::RunTest(const FString&)
{
 const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);if(!W)return false;
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 const FString Slot=TEXT("Automation_ModularRestore_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
 ON_SCOPE_EXIT{UGameplayStatics::DeleteGameInSlot(Slot,0);W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* M=W->SpawnActor<APropManager>();M->bEnableAutosave=false;M->InitializePropManager();
 auto* Preset=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalMint.DA_NormalMint"));
 const int32 Root=M->GenerateProductInstance(M->RegisterProductFromDataAsset(Preset),FTransform(FVector(100,200,300)));
 if(!Root)return false;
 const int32 Cap=M->FindUniqueData(Root)->EventData.AttachedCapISID;
 auto Edge=M->FindISIDData(Cap)->Attachment;Edge.RelativeTransform=FTransform(FRotator(0,23,0),FVector(5,8,12));M->SetAssemblyEdge(Cap,Edge);M->RefreshAssembly(Root);
 TMap<int32,FPropSerializedInstance> Before;
 for(const auto& P:M->ISIDData){FPropSerializedInstance S;if(M->SerializeInstance(P.Key,S))Before.Add(P.Key,S);}
 if(!TestTrue(TEXT("Schema-six assembly saves"),M->SavePropDatabase(Slot,0)))return false;
 M->Destroy();M=W->SpawnActor<APropManager>();M->bEnableAutosave=false;M->InitializePropManager();
 if(!TestTrue(TEXT("Fresh manager loads graph"),M->LoadPropDatabase(Slot,0)))return false;
 TestEqual(TEXT("Restore creates no duplicate children"),M->ISIDData.Num(),Before.Num());
 for(const auto& P:Before)
 {
  const auto* R=M->FindISIDData(P.Key);if(!TestNotNull(TEXT("Every saved identity survives fresh restore"),R))return false;
  TestEqual(TEXT("Saved PID retained"),R->ProductID,P.Value.ProductID);
  TestEqual(TEXT("Immediate owner retained"),R->Attachment.OwnerISID,P.Value.Attachment.OwnerISID);
  TestTrue(TEXT("Schema-six exact relative transform retained"),R->Attachment.RelativeTransform.Equals(P.Value.Attachment.RelativeTransform,.001f));
  if(R->Attachment.OwnerISID)TestTrue(TEXT("Inverse child index rebuilt"),M->GetChildISIDs(R->Attachment.OwnerISID).Contains(P.Key));
 }
 auto* SharedBottle=M->FindProductDefinition(1000)->SourceDataAsset.Get();const int32 OriginalMaximum=SharedBottle->NumOfPortions;
 ON_SCOPE_EXIT{SharedBottle->NumOfPortions=OriginalMaximum;};
 const float SavedPortions=M->FindISIDData(Root)->CurrentPortions;
 SharedBottle->NumOfPortions=OriginalMaximum*2;
 if(!TestTrue(TEXT("Saved bottle reloads after a shared capacity edit"),M->LoadPropDatabase(Slot,0)))return false;
 TestEqual(TEXT("Changing saved capacity cannot create liquid"),M->FindISIDData(Root)->CurrentPortions,SavedPortions);
 TestEqual(TEXT("Saved amount is half of the enlarged capacity"),M->FindUniqueData(Root)->EventData.ContentLevel,.5f);
 SharedBottle->NumOfPortions=OriginalMaximum*4;
 if(!TestTrue(TEXT("Runtime catalog capacity edit applies"),M->InitializeProductCatalog()))return false;
 TestEqual(TEXT("Live capacity edit preserves actual portions"),M->FindISIDData(Root)->CurrentPortions,SavedPortions);
 TestEqual(TEXT("Live fill follows the new maximum"),M->FindUniqueData(Root)->EventData.ContentLevel,.25f);
 return true;
}
#endif
