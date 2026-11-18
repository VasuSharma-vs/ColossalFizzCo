#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopProcurementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleLoadLifecycleTest,"ColossalFizzCo.Props.BottleLoadLifecycle",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleLoadLifecycleTest::RunTest(const FString& Parameters)
{
	FString SourceSave;
	if (FParse::Value(FCommandLine::Get(),TEXT("BottleSourceSave="),SourceSave))
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes,*SourceSave)) return false;
		const auto* Saved=UPropDatabaseSaveGame::LoadCompatibleMemory(Bytes);
		if (!TestNotNull(TEXT("Reported save decodes without modifying it"),Saved)) return false;
		int32 Bottles=0,Open=0,Partial=0,Empty=0,OldOffsets=0,RestingOpen=0,MissingActors=0;
		for (const auto& Item:Saved->WorldSaveData.SavedInstances)
		{
			const auto* Def=Saved->WorldSaveData.ProductDefinitions.FindByPredicate([&](const auto& D){return D.ProductID==Item.ProductID;});
			if (!Def) { ++MissingActors; continue; }
			if (Def->bIsBottleCap && Item.Attachment.OwnerISID && Item.Attachment.RelativeTransform.GetLocation().Size()>1) ++OldOffsets;
			if (Def->bIsBottleCap || Def->bLiquidVisualOnly || Def->ConsumableProfile.NumOfPortions<=0 || !Item.bHasUniqueData) continue;
			++Bottles; const auto& E=Item.UniqueData.EventData;
			Open+=E.CapSealed<1; Empty+=E.ContentLevel<=0; Partial+=E.ContentLevel>0 && E.ContentLevel<.999f;
			RestingOpen+=E.CapSealed<1 && E.ContentLevel>0 && Item.State==EPropState::AtRest;
		}
		const FString Audit=FString::Printf(TEXT("schema=%d elapsed=%.3f instances=%d definitions=%d releases=%d bottles=%d open=%d partial=%d empty=%d resting_open=%d old_cap_offsets=%d missing_definitions=%d\n"),
			Saved->WorldSaveData.SchemaVersion,Saved->WorldSaveData.WorldElapsedSeconds,Saved->WorldSaveData.SavedInstances.Num(),Saved->WorldSaveData.ProductDefinitions.Num(),Saved->WorldSaveData.ReleasedLiquids.Num(),Bottles,Open,Partial,Empty,RestingOpen,OldOffsets,MissingActors);
		AddInfo(Audit);
		FFileHelper::SaveStringToFile(Audit,*(FPaths::ProjectSavedDir()/TEXT("ReportedBottleSaveAudit.txt")));
	}
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	const FString Slot=TEXT("BottleLoadLifecycle_")+FGuid::NewGuid().ToString();
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot,0); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	// The deterministic motion/rocket stepping below supplies the same frame
	// interval to visual interpolation without advancing unrelated world actors.
	World->DeltaTimeSeconds=.02f;
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	Manager->SchemaVersion=5; // This fixture deliberately writes the pre-modular mount schema.
	if (!Manager->ShopSupplies->InitializeCatalog()) return false;
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalLemonade.DA_NormalLemonade"));
	if (!Product) return false;
	const int32 PID=Manager->RegisterProductFromDataAsset(Product);
	const FTransform FallenPose(FRotator(0,0,90),FVector(0,0,700));
	const int32 Closed=Manager->GenerateProductInstance(PID,FallenPose);
	const int32 Open=Manager->GenerateProductInstance(PID,FTransform(FVector(1200,0,700)));
	const int32 Moving=Manager->GenerateProductInstance(PID,FTransform(FVector(2400,0,700)));
	if (!Closed || !Open || !Moving) return false;
	const int32 Cap=Manager->FindUniqueData(Closed)->EventData.AttachedCapISID;
	const int32 OpenCap=Manager->FindUniqueData(Open)->EventData.AttachedCapISID;
	// Build an intended resting partial bottle without changing its history.
	Manager->SetAssemblyEdge(OpenCap,FPropAttachment());
	auto* OpenData=Manager->FindUniqueData(Open); OpenData->EventData.AttachedCapISID=0; OpenData->EventData.CapSealed=0; OpenData->EventData.ContentLevel=.375f;
	Manager->FindUniqueData(OpenCap)->EventData.AttachedBottleISID=0;
	Manager->ClearInstanceDirty(Open);
	Manager->MarkInstanceDirty(Open,EPropDirtyFlags::RuntimeData);
	auto* History=Manager->FindUniqueData(Closed);
	History->EventData.CreationTime=12; History->EventData.ContentCreationTime=20; History->EventData.Strength=.85f; History->EventData.bClean=false;
	Manager->SetBottleAgitationPressure(Closed,.2f,false);
	Manager->WorldElapsedSeconds=100;
	if (!Manager->SetInstanceToUnsettled(Moving)) return false;
	auto* MovingActor=Manager->GetActiveActorFromISID(Moving);
	MovingActor->PropMesh->SetPhysicsLinearVelocity(FVector(40,0,0)); MovingActor->LinearVelocity=FVector(40,0,0);
	for (int32 Pass=0;Pass<2;++Pass)
	{
		// Cover both an older mount definition and a save already updated to the
		// identity recipe but still carrying the obsolete edge translation.
		auto Def=*Manager->FindProductDefinition(PID);
		Def.CapAttachmentTransform=Pass==0 ? FTransform(FVector(0,0,170)) : FTransform::Identity;
		Manager->RegisterProductDefinition(Def);
		auto Edge=Manager->FindISIDData(Cap)->Attachment;
		Edge.RelativeTransform=FTransform(FRotator(0,37,0),FVector(0,0,170)); Manager->SetAssemblyEdge(Cap,Edge);
		if (!TestTrue(TEXT("Reported old cap pose saves"),Manager->SavePropDatabase(Slot))) return false;
		if (!TestTrue(TEXT("Saved bottle lifecycle restores"),Manager->LoadPropDatabase(Slot))) return false;
		const auto* Restored=Manager->FindUniqueData(Closed);
		TestTrue(TEXT("Saved fallen bottle pose retained"),Manager->GetTransform(Closed).Equals(FallenPose,.001f));
		TestEqual(TEXT("Full content retained"),Restored->EventData.ContentLevel,1.f);
		TestEqual(TEXT("Pressure retained"),Restored->ContinuousData.SealedPressure,.2f);
		TestEqual(TEXT("Damage retained"),Restored->EventData.Strength,.85f);
		TestEqual(TEXT("Bottle clock retained"),Restored->EventData.CreationTime,12.);
		TestEqual(TEXT("Liquid clock retained"),Restored->EventData.ContentCreationTime,20.);
		TestEqual(TEXT("Cap ISID retained"),Restored->EventData.AttachedCapISID,Cap);
		const auto& Mount=Manager->FindISIDData(Cap)->Attachment.RelativeTransform;
		TestTrue(TEXT("Shared-pivot cap position repaired"),Mount.GetLocation().IsNearlyZero(.001));
		TestTrue(TEXT("Intentional manual twist retained"),Mount.GetRotation().Equals(FRotator(0,37,0).Quaternion(),.001f));
		TestTrue(TEXT("Intended resting open bottle stays ISM"),Manager->FindISIDData(Open)->Representation==EPropRepresentation::ISM && !Manager->GetActiveActorFromISID(Open));
		TestEqual(TEXT("Configured saved partial level retained"),Manager->FindUniqueData(Open)->EventData.ContentLevel,.375f);
		TestTrue(TEXT("Actual saved moving bottle restores active"),Manager->GetActiveActorFromISID(Moving)!=nullptr);
		for (int32 Cycle=0;Cycle<3;++Cycle)
		{
			AActivePropActor* Active=nullptr;
			if (!Manager->PromoteInstanceToASM(Closed,EPropActivationReason::Interaction,false,Active)) return false;
			auto* Piece=Manager->GetAssemblyComponent(Cap);
			if (!Piece) return false;
			TestTrue(TEXT("Cap remains at shared origin after promotion"),Piece->GetRelativeLocation().IsNearlyZero(.001));
			// Explicit edge repair must also update an existing authoritative piece.
			auto Correct=Manager->FindISIDData(Cap)->Attachment;
			auto Temporary=Correct; Temporary.RelativeTransform.SetLocation(FVector(0,0,170)); Manager->SetAssemblyEdge(Cap,Temporary); Manager->SetAssemblyEdge(Cap,Correct);
			TestTrue(TEXT("Live component follows repaired edge"),Piece->GetRelativeLocation().IsNearlyZero(.001));
			Manager->SetBottleAgitationPressure(Closed,0,false);
			if (auto* Bottle=Cast<AColaPropActor>(Active)) { Bottle->LiquidMotion.Reset(); Bottle->ResetForInstanceBinding(); Manager->RefreshAssembly(Closed); }
			if (!Manager->SetInstanceToRest(Closed)) return false;
		}
		Manager->SetBottleAgitationPressure(Closed,.2f,false);
	}
	// A load/transition-complete kinematic holder may move the root without a
	// component attachment. Physical motion, not a forced pressure assignment,
	// must drive the complete pressure/rocket sequence.
	auto* Pawn=World->SpawnActor<ACharacter>(); Pawn->SetActorLocation(FallenPose.GetLocation()+FVector(200,0,0));
	if (!TestTrue(TEXT("Loaded fallen bottle can be picked up"),Manager->BeginGrab(Closed,Pawn,Pawn->GetRootComponent()))) return false;
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Closed));
	if (!Bottle || !Bottle->RocketComp) return false;
	Bottle->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Bottle->RocketMinDistance=Bottle->RocketMaxDistance=600; Bottle->RocketMinSpeed=Bottle->RocketMaxSpeed=1500;
	Bottle->RocketDirectionVariation=0; Bottle->RocketComp->TippingSettings.bEnableTipping=false; Bottle->RocketComp->DraggingSettings.bEnableDragging=false;
	// This save/load fixture verifies immediate physical ownership after fuel loss;
	// the authored rebound-apex policy is exercised by RocketLifecycle tests.
	Bottle->RocketComp->FallingSettings.PhysicsHandoffMode=ERocketPhysicsHandoffMode::OnFuelDepleted;
	bool Inflated=false;
	for (int32 I=0;I<150 && !Bottle->RocketComp->IsPoweredFlight();++I)
	{
		Bottle->SetActorLocation(FallenPose.GetLocation()+FVector(40*FMath::Sin(float(I)*.36f),0,0));
		static_cast<AActor*>(Bottle)->Tick(.02f);
		Inflated|=Bottle->SkeletalMeshComponent && Bottle->SkeletalMeshComponent->GetMorphTarget(TEXT("OverfullLevel"))>0;
	}
	if (!TestTrue(TEXT("Physical held shaking starts rocket after reload"),Bottle->RocketComp->IsPoweredFlight())) return false;
	TestTrue(TEXT("Pressure produces visible shell inflation"),Inflated);
	TestFalse(TEXT("Rocket releases the holder"),Bottle->bIsHeld);
	TestEqual(TEXT("Rocket ejects the real cap"),Bottle->GetCapSealed(),0.f);
	bool Partial=false;
	for (int32 I=0;I<250 && Bottle->RocketComp->IsPoweredFlight();++I)
	{
		static_cast<UActorComponent*>(Bottle->RocketComp.Get())->TickComponent(.02f,LEVELTICK_All,nullptr);
		Partial|=Bottle->GetContentLevel()>0 && Bottle->GetContentLevel()<.99f;
	}
	TestTrue(TEXT("Liquid decreases during flight"),Partial);
	TestEqual(TEXT("Rocket ends exactly empty"),Bottle->GetContentLevel(),0.f);
	TestTrue(TEXT("Physics takes over after depletion"),Bottle->PropMesh->IsSimulatingPhysics());
	if (!SourceSave.IsEmpty())
	{
		TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*SourceSave);
		auto* Reported=UPropDatabaseSaveGame::LoadCompatibleMemory(Bytes);
		if (!Reported || !UGameplayStatics::SaveGameToSlot(Reported,Slot,0)) return false;
		const bool Loaded=Manager->LoadPropDatabase(Slot);
		AddInfo(FString::Printf(TEXT("Reported autosave restore=%d expected_records=%d actual_records=%d"),Loaded,Reported->WorldSaveData.SavedInstances.Num(),Manager->ISIDData.Num()));
		TestTrue(TEXT("Actual reported autosave restores completely instead of falling back to fresh stock"),Loaded);
		int32 Missing=0,HistoryChanges=0,PoseChanges=0,BadCapMounts=0;
		for (const auto& Item:Reported->WorldSaveData.SavedInstances)
		{
			const auto* R=Manager->FindISIDData(Item.ISID);
			if (!R) { ++Missing; continue; }
			if (!Item.Attachment.OwnerISID && !Manager->GetTransform(Item.ISID).Equals(Item.ActiveTransform,.01f)) ++PoseChanges;
			if (Item.bHasUniqueData)
			{
				const auto* D=Manager->FindUniqueData(Item.ISID);
				const auto& E=Item.UniqueData.EventData;
				if (!D || D->EventData.ContentID!=E.ContentID || D->EventData.ContentLevel!=E.ContentLevel
					|| D->EventData.Strength!=E.Strength || D->EventData.bClean!=E.bClean
					|| D->EventData.ContentCreationTime!=E.ContentCreationTime
					|| D->EventData.CreationTime!=E.CreationTime
					|| D->ContinuousData.SealedPressure!=Item.UniqueData.ContinuousData.SealedPressure) ++HistoryChanges;
			}
			const auto* Def=Manager->FindProductDefinition(R->ProductID);
			const auto* Parent=Manager->FindISIDData(R->Attachment.OwnerISID);
			const auto* BottleDef=Parent ? Manager->FindProductDefinition(Parent->ProductID) : nullptr;
			if (Def && Def->bIsBottleCap && BottleDef)
			{
				const auto Mount=Def->CapMountOffset*BottleDef->CapAttachmentTransform;
				if (!R->Attachment.RelativeTransform.GetLocation().Equals(Mount.GetLocation(),.001f)
					|| !R->Attachment.RelativeTransform.GetScale3D().Equals(Mount.GetScale3D(),.001f)) ++BadCapMounts;
			}
		}
		AddInfo(FString::Printf(TEXT("Reported save preservation missing=%d changed_history=%d changed_root_pose=%d bad_cap_mount=%d"),Missing,HistoryChanges,PoseChanges,BadCapMounts));
		TestEqual(TEXT("Every reported identity survives"),Missing,0);
		TestEqual(TEXT("Reported contents/pressure/clocks/damage/cleanliness remain exact"),HistoryChanges,0);
		TestEqual(TEXT("Reported root world poses remain exact"),PoseChanges,0);
		TestEqual(TEXT("Every attached reported cap uses its recipe mount"),BadCapMounts,0);
	}
	return true;
}
#endif
