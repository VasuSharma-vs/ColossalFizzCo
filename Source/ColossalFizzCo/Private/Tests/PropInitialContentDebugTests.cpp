#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopSupplyCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropInitialContentDebugTest,"ColossalFizzCo.Props.InitialContentDebug",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropInitialContentDebugTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalMint.DA_NormalMint"));
	if (!TestNotNull(TEXT("Actual bottle product"),Product)) return false;
	auto Register=[&](bool bRandom,float Min,float Max)
	{
		const FTransform Pose(FVector(0,0,500));
		auto* Actor=World->SpawnActorDeferred<AColaPropActor>(AColaPropActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Actor->bAutoConvertToISMIfAtRest=false; Actor->PropDataAsset=Product;
		Actor->bDebugRandomizeContentLevelOnISMRegistration=bRandom;
		Actor->DebugMinInitialContentLevel=Min; Actor->DebugMaxInitialContentLevel=Max;
		Actor->FinishSpawning(Pose);
		return Manager->ConvertPlacedActorToISM(Actor,true);
	};
	const int32 Standard=Register(false,.2f,.8f);
	if (!TestTrue(TEXT("Default-off placed bottle registers"),Standard>0)) return false;
	TestEqual(TEXT("Default-off preserves product contents"),Manager->FindUniqueData(Standard)->EventData.ContentLevel,Product->DefaultContentLevel);
	TSet<float> Samples;
	for (int32 I=0;I<8;++I)
	{
		const int32 ID=Register(true,I%2 ? .8f : .2f,I%2 ? .2f : .8f);
		if (!TestTrue(TEXT("Randomized placed bottle registers"),ID>0)) return false;
		const float Fill=Manager->FindUniqueData(ID)->EventData.ContentLevel;
		TestTrue(TEXT("Server-sampled contents respect range, including reversed inputs"),Fill>=.2f && Fill<=.8f);
		Samples.Add(Fill);
		auto* ISM=Manager->GetComponentFromISID(ID); const int32 Index=ISM->FindISMI(ID);
		TestEqual(TEXT("Randomized canonical contents reach the actual ISM shader slot"),ISM->PerInstanceSMCustomData[Index*ISM->NumCustomDataFloats],Fill);
	}
	TestTrue(TEXT("Different bottles receive varied initial contents"),Samples.Num()>1);
	const int32 Half=Register(true,.5f,.5f);
	if (!TestTrue(TEXT("Fixed debug range supports reproducible half-fill checks"),Half>0)) return false;
	TestEqual(TEXT("Half fill is canonical, not only a material override"),Manager->FindUniqueData(Half)->EventData.ContentLevel,.5f);
	const FString Slot=TEXT("Automation_InitialContent_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot,0); };
	if (!TestTrue(TEXT("Random contents save"),Manager->SavePropDatabase(Slot,0)) || !TestTrue(TEXT("Random contents restore"),Manager->LoadPropDatabase(Slot,0))) return false;
	if (!TestTrue(TEXT("Restored identity promotes"),Manager->SetInstanceToUnsettled(Half,FVector::ZeroVector,FVector::ZeroVector,60))) return false;
	auto* Actor=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Half));
	if (!TestNotNull(TEXT("Restored active bottle"),Actor)) return false;
	Actor->bDebugRandomizeContentLevelOnISMRegistration=true; Actor->DebugMinInitialContentLevel=0; Actor->DebugMaxInitialContentLevel=0;
	TestEqual(TEXT("Debug settings cannot reroll an already registered or restored bottle"),Actor->ResolveRegistrationContentLevel(1.f),.5f);
	Actor->UpdateDynamicMaterial(false);
	float Fill=0; Actor->DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Level")),Fill);
	TestEqual(TEXT("Restored promotion retains visible half fill"),Fill,.5f);
	Actor->LiquidMotion.Reset();
	TestTrue(TEXT("Normal demotion remains available"),Manager->SetInstanceToRest(Half));
	TestEqual(TEXT("Pooling does not reroll the debug fill"),Manager->FindUniqueData(Half)->EventData.ContentLevel,.5f);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacedBottleRegistrationRollbackTest,"ColossalFizzCo.Props.PlacedRegistrationRollback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPlacedBottleRegistrationRollbackTest::RunTest(const FString&)
{
 const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
 if(!World)return false;
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT{World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 auto* Manager=World->SpawnActor<APropManager>();Manager->bEnableAutosave=false;Manager->InitializePropManager();
 const auto* Original=Manager->ShopSupplies->Catalog.LoadSynchronous();
 if(!TestNotNull(TEXT("Real assembly catalog exists"),Original))return false;
 if(!Manager->ShopSupplies->InitializeCatalog())return false;
 auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalMint.DA_NormalMint"));
 if(!TestNotNull(TEXT("Real bottle source exists"),Product))return false;
 auto InvalidCap=*Manager->FindProductDefinition(Product->InitialCapProduct.PID);InvalidCap.StaticMesh=nullptr;
 Manager->RegisterProductDefinition(InvalidCap);
 const int32 PID=Manager->RegisterProductFromDataAsset(Product);
 auto Definition=*Manager->FindProductDefinition(PID);Definition.LiquidVisualPID=0;
 Manager->RegisterProductDefinition(Definition);
 const FTransform Pose(FVector(0,0,500));
 auto* Actor=World->SpawnActorDeferred<AColaPropActor>(AColaPropActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 Actor->bAutoConvertToISMIfAtRest=false;Actor->PropDataAsset=Product;Actor->FinishSpawning(Pose);
 if(!TestEqual(TEXT("Placed fixture starts without a logical identity"),Actor->ISID,0))return false;
 TestEqual(TEXT("Invalid assembly configuration rejects registration"),Manager->ConvertPlacedActorToISM(Actor,true),0);
 TestEqual(TEXT("Rejected first registration does not leave a retired identity on the actor"),Actor->ISID,0);
 TestTrue(TEXT("Rejected registration preserves the authored position"),Actor->GetActorTransform().Equals(Pose));
 TestTrue(TEXT("Rejected registration does not park or hide the placed actor"),!Actor->IsHidden());
 TestTrue(TEXT("Rejected registration retains actor collision"),Actor->GetActorEnableCollision());
 return true;
}
#endif
