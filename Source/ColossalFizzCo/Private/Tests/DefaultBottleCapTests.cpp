#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/StaticMeshComponent.h"
#include "Props/PropActivationVolume.h"
#include "Components/PropActivationVolumeComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Props/ColaPropActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopProcurementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDefaultBottleCapTest,"ColossalFizzCo.Shop.DefaultBottleCapIdentity",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDefaultBottleCapTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!TestTrue(TEXT("Five-flavor catalog initializes"),Manager->ShopSupplies->InitializeCatalog())) return false;
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalMint.DA_NormalMint"));
	if (!TestNotNull(TEXT("Real sealed starter bottle"),Product)) return false;
	const int32 Bottle=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,300)));
	if (!TestTrue(TEXT("Starter bottle identity is generated"),Bottle>0)) return false;
	FPropEventSyncDelta History; History.ISID=Bottle; History.SetFlag(EPropEventDirtyMask::Strength); History.Strength=.83f;
	History.SetFlag(EPropEventDirtyMask::CreationTime); History.CreationTime=17;
	if (!TestTrue(TEXT("Valid normalized bottle history is accepted"),Manager->SubmitInstanceEventUpdate(Bottle,History))) return false;
	const auto Before=Manager->FindUniqueData(Bottle)->EventData;
	if (!TestTrue(TEXT("Factory cap identity exists before any promotion"),Before.AttachedCapISID>0)) return false;
	const auto Children=Manager->GetChildISIDs(Bottle);
	if (!TestEqual(TEXT("Shell owns exactly liquid and cap"),Children.Num(),2)) return false;
	const int32 Liquid = Children[0] == Before.AttachedCapISID ? Children[1] : Children[0];
	const int32 LiquidSlot = Manager->GetISMI(Liquid);
	const int32 BeforePromotionCount=Manager->ISIDData.Num();
	AActivePropActor* Actor=nullptr;
	if (!TestTrue(TEXT("Starter bottle promotes"),Manager->PromoteInstanceToASM(Bottle,EPropActivationReason::Interaction,false,Actor))) return false;
	const int32 Cap=Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID;
	TestEqual(TEXT("Promotion creates no logical identities"),Manager->ISIDData.Num(),BeforePromotionCount);
	if (!TestTrue(TEXT("Default cap receives an independent identity"),Cap>0 && Cap!=Bottle)) return false;
	TestEqual(TEXT("Factory cap links back to bottle"),Manager->FindUniqueData(Cap)->EventData.AttachedBottleISID,Bottle);
	TestEqual(TEXT("Initialization preserves bottle damage"),Manager->FindUniqueData(Bottle)->EventData.Strength,Before.Strength);
	TestEqual(TEXT("Initialization preserves bottle creation time"),Manager->FindUniqueData(Bottle)->EventData.CreationTime,Before.CreationTime);
	TestEqual(TEXT("Initialization preserves liquid"),Manager->FindUniqueData(Bottle)->EventData.ContentLevel,Before.ContentLevel);
	TestEqual(TEXT("Initialization preserves liquid expiry origin"),Manager->FindUniqueData(Bottle)->EventData.ContentCreationTime,Before.ContentCreationTime);
	TestEqual(TEXT("Initialization preserves seal"),Manager->FindUniqueData(Bottle)->EventData.CapSealed,Before.CapSealed);
	auto* LiquidMesh=Manager->GetAssemblyComponent(Liquid);
	TestTrue(TEXT("Active liquid shares the shell actor with disabled physics/collision"),LiquidMesh && LiquidMesh->GetOwner()==Actor && LiquidMesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Shell uses supplied new mesh"),Actor->PropMesh->GetStaticMesh() && Actor->PropMesh->GetStaticMesh()->GetName()==TEXT("SM_PlasticBottle"));
	const UStaticMesh* StaticShell = Actor->PropMesh->GetStaticMesh();
	const auto* ActiveShell = Actor->SkeletalMeshComponent ? Actor->SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
	if (!TestNotNull(TEXT("Visible skeletal shell is assigned"),ActiveShell) || !StaticShell || !LiquidMesh) return false;
	TestEqual(TEXT("Resting shell only has its shell material"),StaticShell->GetStaticMaterials().Num(),1);
	TestEqual(TEXT("Active shell only has its shell material"),ActiveShell->GetMaterials().Num(),1);
	TestEqual(TEXT("Active shell retains its authored material"),Actor->SkeletalMeshComponent->GetMaterial(0),ActiveShell->GetMaterials()[0].MaterialInterface.Get());
	TestNull(TEXT("No artificial skeletal liquid slot"),Actor->SkeletalMeshComponent->GetMaterial(1));
	TestNull(TEXT("No artificial static liquid slot"),Actor->PropMesh->GetMaterial(1));
	auto* Cola = Cast<AColaPropActor>(Actor);
	if (!TestNotNull(TEXT("Modular bottle uses native cola behavior"),Cola) || !TestNotNull(TEXT("Liquid uses its skeletal visual"),Cola->AssemblyLiquidSkeletalMesh.Get())) return false;
	TestNotNull(TEXT("Separate active inner mesh owns the liquid MID in slot zero"),Cast<UMaterialInstanceDynamic>(Cola->AssemblyLiquidSkeletalMesh->GetMaterial(0)));
	TestFalse(TEXT("Old static shell collider is hidden"),Cola->PropMesh->IsVisible());
	TestFalse(TEXT("Static inner does not overlap skeletal inner"),LiquidMesh->IsVisible());
	const auto* BottleDefinition = Manager->FindProductDefinition(Manager->FindISIDData(Bottle)->ProductID);
	TestTrue(TEXT("Artist-aligned cap mount is identity"),BottleDefinition->CapAttachmentTransform.Equals(FTransform::Identity));
	auto HighTolerance = *BottleDefinition; HighTolerance.PressureFailureThreshold = 100; Manager->RegisterProductDefinition(HighTolerance);
	TestEqual(TEXT("Runtime shared tuning remains applied"),Manager->FindProductDefinition(HighTolerance.ProductID)->PressureFailureThreshold,100.f);
	Manager->SetBottleAgitationPressure(Bottle,50);
	Cola->ResetForInstanceBinding(); Manager->RefreshAssembly(Bottle); Cola->UpdateMorphTargets();
	TestTrue(TEXT("Pressure deforms actual shell"),Cola->SkeletalMeshComponent->GetMorphTarget(TEXT("OverfullLevel"))>0);
	TestEqual(TEXT("Shell and liquid share one normalized pressure morph"),Cola->AssemblyLiquidSkeletalMesh->GetMorphTarget(TEXT("OverfullLevel")),Cola->SkeletalMeshComponent->GetMorphTarget(TEXT("OverfullLevel")));
	auto* OriginalLiquid = Cola->AssemblyLiquidSkeletalMesh.Get();
	Actor->InitializeFromISIDRecord(*Manager->FindISIDData(Bottle),BottleDefinition,Manager);
	Manager->RefreshAssembly(Bottle);
	TestEqual(TEXT("Repeated state refresh retains the same liquid component"),Cola->AssemblyLiquidSkeletalMesh.Get(),OriginalLiquid);
	Cola->PropMesh->SetVisibility(true,true);
	Cola->UpdateDynamicMaterial(false);
	TestFalse(TEXT("A propagated activation visibility update cannot reveal collider shell"),Cola->PropMesh->IsVisible());
	TestFalse(TEXT("A propagated visibility update cannot reveal static liquid"),LiquidMesh->IsVisible());
	Cola->LinearVelocity=FVector(100,0,0); Cola->AngularVelocity=FVector::ZeroVector; Cola->bHasAgitationHistory=false;
	Manager->SetBottleAgitationPressure(Bottle,0);
	for (int32 I=0; I<30; ++I) Cola->UpdateMotionPressure(.05f);
	TestEqual(TEXT("Steady gentle carry produces no agitation"),Cola->LocalUniqueData.ContinuousData.SealedPressure,0.f);
	for (int32 I=0; I<30; ++I) { Cola->LinearVelocity=FVector(I%2 ? -500 : 500,0,0); Cola->UpdateMotionPressure(.05f); }
	AddInfo(FString::Printf(TEXT("SHAKE_DIAGNOSTIC pressure=%.3f tolerance=%.3f cap=%d seal=%.3f threshold=%.3f"),Cola->GetBottlePressure(),Cola->GetBottlePressureTolerance(),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,Cola->GetCapSealed(),Manager->FindProductDefinition(HighTolerance.ProductID)->PressureFailureThreshold));
	TestTrue(TEXT("Repeated aggressive shaking builds material pressure"),Cola->LocalUniqueData.ContinuousData.SealedPressure>.5f);
	Manager->SetBottleAgitationPressure(Bottle,0); Cola->LinearVelocity=FVector::ZeroVector; Cola->AngularVelocity=FVector(0,0,720); Cola->bHasAgitationHistory=false;
	for (int32 I=0; I<30; ++I) Cola->UpdateMotionPressure(.05f);
	TestTrue(TEXT("Aggressive spinning accumulates pressure"),Cola->LocalUniqueData.ContinuousData.SealedPressure>.5f);
	Manager->SetBottleAgitationPressure(Bottle,0); Cola->AngularVelocity=FVector::ZeroVector;
	Cola->ApplyBottleImpact(FVector(0,0,500),0);
	TestEqual(TEXT("Harmless impulse does not build pressure"),Cola->LocalUniqueData.ContinuousData.SealedPressure,0.f);
	Cola->LastImpactTime=-1; Cola->ApplyBottleImpact(FVector(0,0,2000),0);
	TestTrue(TEXT("Moderate drop builds pressure below damage threshold"),Cola->LocalUniqueData.ContinuousData.SealedPressure>0);
	const float OneHit=Cola->LocalUniqueData.ContinuousData.SealedPressure;
	Cola->ApplyBottleImpact(FVector(0,0,2000),0);
	TestEqual(TEXT("Sustained same-frame contact cannot multiply agitation"),Cola->LocalUniqueData.ContinuousData.SealedPressure,OneHit);
	Manager->SetBottleAgitationPressure(Bottle,0); Cola->SmoothedPressureExpansion=0;
	Cola->LiquidMotion.Reset();
	Manager->RegisterProductDefinition(*BottleDefinition);
	TestEqual(TEXT("Cap hit activates existing shell"),Manager->MakeISMToASM(Cap,EPropState::Unsettled,EPropActivationReason::Interaction,false),Actor);
	auto* Part=Manager->GetAssemblyComponent(Cap);
	if (Part) TestEqual(TEXT("Attached cap does not obstruct its carrier capsule"),Part->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
	TestTrue(TEXT("Default cap is a component of the active bottle"),Part && Part->GetOwner()==Actor && !Manager->GetActiveActorFromISID(Cap));
	FPropProductDefinition Nested;
	Nested.ProductID=Manager->AllocateProductID(); Nested.StaticMesh=Part ? Part->GetStaticMesh() : nullptr; Nested.ConsumableProfile.NumOfPortions=0;
	Manager->RegisterProductDefinition(Nested);
	const int32 Mark=Manager->GenerateProductInstance(Nested.ProductID,Manager->GetTransform(Cap));
	const FTransform Twist(FRotator(0,21,0),FVector(2,3,4));
	if (!TestTrue(TEXT("Nested piece attaches"),Manager->AttachPropChild(Cap,Mark,Twist))) return false;
	TestEqual(TEXT("Nested hit resolves to shell"),Manager->ResolveRootISID(Mark),Bottle);
	TestFalse(TEXT("Circular attach rejected"),Manager->AttachPropChild(Mark,Bottle,FTransform::Identity));
	TestFalse(TEXT("Missing parent rejected"),Manager->AttachPropChild(9999999,Bottle,FTransform::Identity));
	TestEqual(TEXT("Invalid owner lookup"),Manager->ResolveRootISID(-5),0);
	auto* NestedMesh=Manager->GetAssemblyComponent(Mark);
	if (!TestNotNull(TEXT("Nested piece rendered in parent actor"),NestedMesh)) return false;
	NestedMesh->AddLocalRotation(FRotator(0,13,0)); const FTransform Final=NestedMesh->GetComponentTransform();
	const int32 Count=Manager->ISIDData.Num();
	TArray<FPropBulkUpdateBatch> Snapshot;
	Manager->BuildInitialPropSnapshot(Snapshot);
	int32 SeenRecords=0, SeenProducts=0;
	for (const auto& Batch : Snapshot)
	{
		TestTrue(TEXT("No initial batch mixes whole catalogs or exceeds instance limit"),
			Batch.ProductDefinitions.Num()+Batch.ContentDefinitions.Num()<=1 && Batch.Records.Num()<=2);
		for (const auto& Item : Batch.Records)
		{
			if (Item.ISID == Mark) TestTrue(TEXT("Joining peer receives current nested twist"),Item.Attachment.RelativeTransform.Equals(NestedMesh->GetRelativeTransform(),.01));
		}
		SeenRecords+=Batch.Records.Num(); SeenProducts+=Batch.ProductDefinitions.Num();
	}
	TestEqual(TEXT("Snapshot reaches every logical instance"),SeenRecords,Count);
	TestEqual(TEXT("Each shared product sent once"),SeenProducts,Manager->ProductDefinitions.Num());
	TestTrue(TEXT("Explicit first/last snapshot boundaries"),Snapshot[0].bIsFirstInitBatch && Snapshot.Last().bIsFinalInitBatch);
	const auto* BottleDef=Manager->FindProductDefinition(Manager->FindISIDData(Bottle)->ProductID);
	auto CapDef=*Manager->FindProductDefinition(Manager->FindISIDData(Cap)->ProductID);
	CapDef.SupportedBottleMaterials={EBottleMaterialType::Glass}; Manager->RegisterProductDefinition(CapDef);
	TestFalse(TEXT("Cap can reject a bottle that permits its twist mechanism"),Manager->IsAssemblyCompatible(BottleDef->ProductID,Before.ContentID,CapDef.ProductID));
	CapDef.SupportedBottleMaterials={EBottleMaterialType::Plastic}; Manager->RegisterProductDefinition(CapDef);
	TestTrue(TEXT("Repeated default-cap initialization is idempotent"),Manager->EnsureDefaultPhysicalCap(Bottle));
	TestEqual(TEXT("No additional cap is allocated"),Manager->ISIDData.Num(),Count);
	if (!TestTrue(TEXT("Starter assembly pools"),Manager->ConvertASMToISM(Bottle,EPropState::AtRest,true,true))) return false;
	TestTrue(TEXT("Final nested transform retained at rest"),Manager->GetTransform(Mark).Equals(Final,.01));
	const auto* RestShell = Manager->GetComponentFromISID(Bottle);
	const auto* RestLiquid = Manager->GetComponentFromISID(Liquid);
	TestTrue(TEXT("Resting shell retains authored material"),RestShell && RestShell->GetMaterial(0)==StaticShell->GetMaterial(0));
	TestTrue(TEXT("Separate resting inner mesh owns the liquid MID in slot zero"),RestLiquid && Cast<UMaterialInstanceDynamic>(RestLiquid->GetMaterial(0)));
	TestEqual(TEXT("Liquid ISM slot stable"),Manager->GetISMI(Liquid),LiquidSlot);
	for (int32 Child : Children)
	{
		const auto* Batch=Manager->GetComponentFromISID(Child);
		FTransform Visible;
		TestTrue(TEXT("Settled children physically restored from parking"),Batch && Batch->GetInstanceTransform(Manager->GetISMI(Child),Visible,true) && Visible.Equals(Manager->GetTransform(Child),.01));
	}
	const FString Slot=TEXT("Automation_DefaultCap_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!TestTrue(TEXT("Factory cap identity saves"),Manager->SavePropDatabase(Slot,0))
		|| !TestTrue(TEXT("Factory cap identity loads"),Manager->LoadPropDatabase(Slot,0))) return false;
	TestEqual(TEXT("Restored nested owner"),Manager->FindISIDData(Mark)->Attachment.OwnerISID,Cap);
	TestTrue(TEXT("Saved nested world pose"),Manager->GetTransform(Mark).Equals(Final,.01));
	TestEqual(TEXT("Reload keeps the same cap ISID"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,Cap);
	if (!TestTrue(TEXT("Reloaded bottle promotes"),Manager->PromoteInstanceToASM(Bottle,EPropActivationReason::Interaction,false,Actor))) return false;
	TestEqual(TEXT("Repromotion does not allocate another cap"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,Cap);
	TestTrue(TEXT("Factory cap can be physically removed"),Manager->DetachPhysicalCap(Bottle));
	TestEqual(TEXT("Detached cap remains nested piece root"),Manager->ResolveRootISID(Mark),Cap);
	// Count records, including IDs far beyond a single replication window.
	for (int32 I=0; I<1024; ++I)
	{
		if (!Manager->GenerateProductInstance(Nested.ProductID,FTransform(FVector(700,120,I*25)))) return false;
	}
	Manager->BuildInitialPropSnapshot(Snapshot);
	TSet<int32> BulkIDs;
	for (const auto& Page : Snapshot)
	{
		if (!TestTrue(TEXT("Large inventory remains paged"), Page.Records.Num()<=2)) return false;
		for (const auto& Item : Page.Records) BulkIDs.Add(Item.ISID);
	}
	TestEqual(TEXT("Large inventory includes each distinct ISID"),BulkIDs.Num(),Manager->ISIDData.Num());
	TestTrue(TEXT("Open bottle does not regenerate a cap"),Manager->EnsureDefaultPhysicalCap(Bottle));
	TestEqual(TEXT("Removed cap remains detached"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,0);
	TestNotNull(TEXT("Removed physical cap remains in manager"),Manager->FindISIDData(Cap));
	const auto* ShellDefinition = Manager->FindProductDefinition(Manager->FindISIDData(Bottle)->ProductID);
	const FTransform Neck = ShellDefinition->CapAttachmentTransform * Manager->GetTransform(Bottle);
	Manager->InitializeInstanceTransform(Cap,Neck);
	auto* Zone = World->SpawnActorDeferred<APropActivationVolume>(APropActivationVolume::StaticClass(),Neck,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Zone->ActivationVolume->SetBoxExtent(FVector(220));
	Zone->FinishSpawning(Neck);
	Zone->ActivationVolume->SetKeepingPropsActive(true);
	Zone->ActivationVolume->RefreshProps();
	TestTrue(TEXT("Loose cap starts active within the volume"),Manager->IsInstanceKeptActive(Cap));
	TestTrue(TEXT("Loose cap at native neck height can join the active bottle inside the volume"),Manager->AttachPhysicalCap(Bottle,Cap));
	TestTrue(TEXT("Active zone still owns one assembled body"),Manager->GetActiveActorFromISID(Bottle) && !Manager->GetActiveActorFromISID(Cap) && Manager->GetAssemblyComponent(Cap));
	return true;
}
#endif
