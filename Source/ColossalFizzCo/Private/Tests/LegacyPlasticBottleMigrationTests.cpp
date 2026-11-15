#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Package.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacyPlasticBottleMigrationTest,"ColossalFizzCo.Props.LegacyPlasticBottleMigration",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLegacyPlasticBottleMigrationTest::RunTest(const FString& Parameters)
{
	auto* Shell=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Assest/meshs/PlasticBottle/SM_PlasticBottle.SM_PlasticBottle"));
	auto* Skin=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Assest/meshs/PlasticBottle/SKM_PlasticBottle.SKM_PlasticBottle"));
	if (!Shell || !Skin) return false;
	TestEqual(TEXT("Old saved mesh path resolves to the supplied plastic shell"),LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Assest/meshs/Cola1.Cola1")),Shell);
	// An actual string-encoded save references the old object name, which the
	// default engine reader does not resolve through package-only redirects.
	const FString OldPackage=TEXT("/Temp/PropSaveRedirect_")+FGuid::NewGuid().ToString();
	auto* LegacyMesh=NewObject<UStaticMesh>(CreatePackage(*OldPackage),TEXT("OldShell"),RF_Public);
	const FString OldPath=LegacyMesh->GetPathName();
	auto* Saved=NewObject<UPropDatabaseSaveGame>();
	FPropProductDefinition SavedDef; SavedDef.ProductID=734; SavedDef.StaticMesh=LegacyMesh;
	Saved->WorldSaveData.ProductDefinitions.Add(SavedDef);
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Legacy object fixture saves"),UGameplayStatics::SaveGameToMemory(Saved,Bytes))) return false;
	LegacyMesh->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
	TArray<FCoreRedirect> Redirects{FCoreRedirect(ECoreRedirectFlags::Type_Object,OldPath,Shell->GetPathName())};
	FCoreRedirects::AddRedirectList(Redirects,TEXT("PropSaveRedirectTest"));
	ON_SCOPE_EXIT { FCoreRedirects::RemoveRedirectList(Redirects,TEXT("PropSaveRedirectTest")); };
	const auto* Decoded=UPropDatabaseSaveGame::LoadCompatibleMemory(Bytes);
	if (!TestNotNull(TEXT("Legacy string save decodes"),Decoded)) return false;
	if (!TestEqual(TEXT("Legacy fixture definition count"),Decoded->WorldSaveData.ProductDefinitions.Num(),1)) return false;
	TestEqual(TEXT("Saved object-name redirect preserves the actual shell"),Decoded->WorldSaveData.ProductDefinitions[0].StaticMesh.Get(),Shell);
	for (const TCHAR* Name:{TEXT("DA_BasicCola"),TEXT("DA_Cola"),TEXT("DA_EmptyBottel"),TEXT("DA_GoodCola"),TEXT("DA_NormalCola"),TEXT("DA_SpicyCola"),TEXT("DA_SweetCola")})
	{
		const FString Path=FString::Printf(TEXT("/Game/Assest/DataAssets/%s.%s"),Name,Name);
		const auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,*Path);
		if (!TestNotNull(Name,Product)) return false;
		TestEqual(TEXT("Migrated product uses plastic shell"),Product->StaticMesh.Get(),Shell);
		TestEqual(TEXT("Migrated product uses matching active shell"),Product->SkeletalMesh.Get(),Skin);
		TestEqual(TEXT("Physical material is plastic"),Product->BottleMaterialType,EBottleMaterialType::Plastic);
		TestNull(TEXT("Plastic does not inherit glass destruction"),Product->FragmentAssembly.Get());
		TestNotNull(TEXT("Liquid presentation is separate from shell"),Product->LiquidMaterial.Get());
	}
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	const FString Slot=TEXT("LegacyPlasticMigration_")+FGuid::NewGuid().ToString();
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot,0); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	FPropContentDefinition Content; Content.ContentID=1234; Content.BasePortionPrice=13; Manager->RegisterContentDefinition(Content);
	FPropProductDefinition Old;
	Old.ProductID=41; Old.StaticMeshID=72; Old.SourceDataAsset=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_BasicCola.DA_BasicCola"));
	Old.StaticMesh=Shell; Old.SkeletalMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Assest/meshs/SKM/SKM_ColaBottel_N.SKM_ColaBottel_N"));
	Old.BottleMaterialType=EBottleMaterialType::Glass; Old.bEnableInstanceUniqueData=true;
	Old.ConsumableProfile.NumOfPortions=4; Old.ConsumableProfile.Price=13;
	Old.DefaultUniqueData.EventData.ContentID=1234; Old.DefaultUniqueData.EventData.ContentLevel=.75f;
	Old.DefaultUniqueData.EventData.CapSealed=1;
	// Seed a genuinely old definition without routing it through the new registration migration.
	Manager->ProductDefinitions.Add(Old.ProductID,Old);
	const FTransform Pose(FRotator(0,30,0),FVector(300,400,500));
	const int32 ID=Manager->GenerateProductInstance(Old.ProductID,Pose);
	if (!ID || !Manager->FindUniqueData(ID)) return false;
	auto* Unique=Manager->FindUniqueData(ID);
	Unique->EventData.Strength=.81f; Unique->EventData.bClean=false;
	Unique->EventData.CreationTime=12; Unique->EventData.ContentCreationTime=20;
	Unique->ContinuousData.SealedPressure=.3f;
	Manager->WorldElapsedSeconds=100;
	if (!TestTrue(TEXT("Old definition saves"),Manager->SavePropDatabase(Slot))) return false;
	if (!TestTrue(TEXT("Legacy bottle loads through migration"),Manager->LoadPropDatabase(Slot))) return false;
	const auto* Record=Manager->FindISIDData(ID); const auto* Restored=Manager->FindUniqueData(ID);
	if (!Record || !Restored) return false;
	const auto* Definition=Manager->FindProductDefinition(Record->ProductID);
	TestEqual(TEXT("PID retained"),Record->ProductID,41);
	TestEqual(TEXT("Saved price retained"),Definition->ConsumableProfile.Price,13.f);
	TestEqual(TEXT("Saved definition becomes plastic"),Definition->BottleMaterialType,EBottleMaterialType::Plastic);
	TestEqual(TEXT("Active mesh refreshed from source"),Definition->SkeletalMesh.Get(),Skin);
	TestTrue(TEXT("World pose retained"),Manager->GetTransform(ID).Equals(Pose));
	TestEqual(TEXT("Contents retained"),Restored->EventData.ContentID,1234);
	TestEqual(TEXT("Fill retained"),Restored->EventData.ContentLevel,.75f);
	TestEqual(TEXT("Strength retained"),Restored->EventData.Strength,.81f);
	TestFalse(TEXT("Cleanliness retained"),Restored->EventData.bClean);
	TestEqual(TEXT("Bottle clock retained"),Restored->EventData.CreationTime,12.);
	TestEqual(TEXT("Liquid clock retained"),Restored->EventData.ContentCreationTime,20.);
	TestEqual(TEXT("Pressure retained"),Restored->ContinuousData.SealedPressure,.3f);
	TestTrue(TEXT("Legacy sealed bottle receives a real cap identity"),Restored->EventData.AttachedCapISID>0);
	TestEqual(TEXT("Legacy seal retained"),Restored->EventData.CapSealed,1.f);
	TestTrue(TEXT("Separate managed liquid is configured"),Definition->LiquidVisualPID>0 && Manager->GetChildISIDs(ID).Num()>0);
	return true;
}
#endif
