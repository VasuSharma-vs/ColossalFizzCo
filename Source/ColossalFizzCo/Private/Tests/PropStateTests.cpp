#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/ASMComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/PropBatchUpdateComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropStateRoundTripTest, "ColossalFizzCo.Props.StateRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropStateRoundTripTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AColaPropActor* Bottle = World->SpawnActor<AColaPropActor>();
	if (!TestNotNull(TEXT("Bottle actor"), Bottle)) return false;
	Bottle->NumOfPortions = 25;
	FPropEventUpdateData Event;
	Event.ContentLevel = 1.337f; Event.ContentID = 7; Event.Strength = 0.81f;
	Event.CapSealed = 1; Event.CapType = 1; Event.CreationTime = 123.5; Event.ContentCreationTime = 150.5; Event.EventSequence = 12;
	Bottle->OnApplyEventData(Event);
	FPropInstanceUniqueData Saved;
	FISIDRuntimeIndex Record;
	Bottle->WriteToISIDRecord(Record, &Saved);
	TestEqual(TEXT("Fractional overfill survives writeback"), Saved.EventData.ContentLevel, 1.337f);
	TestEqual(TEXT("Content identity survives writeback"), Saved.EventData.ContentID, 7);
	TestEqual(TEXT("Strength survives writeback"), Saved.EventData.Strength, 0.81f);
	TestEqual(TEXT("Container creation timestamp survives writeback"), Saved.EventData.CreationTime, 123.5);
	TestEqual(TEXT("Independent content timestamp survives writeback"), Saved.EventData.ContentCreationTime, 150.5);
	Event.EventSequence = 11; Event.ContentLevel = 0.5;
	Bottle->OnApplyEventData(Event);
	TestEqual(TEXT("Older discrete update cannot roll back fill"), Bottle->GetContentLevel(), 1.337f);
	Bottle->LastDiscreteEventTime = 200;
	FPropContinuousUpdateData Continuous;
	Continuous.LastUpdatedTime = 199; Continuous.Temperature = 80;
	Bottle->OnApplyContinuousData(Continuous);
	TestNotEqual(TEXT("Thermal packet predating event rejected"), Bottle->GetCurrentTemperature(), 80.0f);
	Continuous.LastUpdatedTime = 201; Continuous.Temperature = 6;
	Bottle->OnApplyContinuousData(Continuous);
	TestEqual(TEXT("Fresh thermal update accepted"), Bottle->GetCurrentTemperature(), 6.0f);
	TestEqual(TEXT("Thermal update preserves creation timestamp"), Bottle->GetCreationTime(), 123.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleTemperatureStrengthTest, "ColossalFizzCo.Props.BottleTemperatureStrength", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleTemperatureStrengthTest::RunTest(const FString& Parameters)
{
	const UClass* BottleClass = LoadClass<AColaPropActor>(nullptr, TEXT("/Game/Props/Colas/BP_Cola.BP_Cola_C"));
	const AColaPropActor* Defaults = BottleClass ? BottleClass->GetDefaultObject<AColaPropActor>() : nullptr;
	if (!TestNotNull(TEXT("BP_Cola product asset"), Defaults ? Defaults->PropDataAsset.Get() : nullptr)) return false;
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	Manager->bEnableAutosave = false;
	Manager->ASMComponent->MinASMCount = 1;
	Manager->ASMComponent->MaxASMCount = 1;
	Manager->ASMComponent->ExpandBatchSize = 1;
	Manager->WorldElapsedSeconds = 100.0;

	const int32 PID = Manager->RegisterProductFromDataAsset(Defaults->PropDataAsset);
	const FPropProductDefinition* Product = Manager->FindProductDefinition(PID);
	if (!TestNotNull(TEXT("Registered BP_Cola product"), Product)) return false;
	const float FullHeat = Product->MaxTemperatureThreshold;
	const float SofteningThreshold = Product->HighTempDamageThreshold;
	const float PressureLimit = Product->PressureFailureThreshold;
	const float ImpactDamage = Product->StrengthDamageRate;
	const int32 ISID = Manager->GenerateProductInstance(PID);
	UInteractableISMComponent* ISM = Manager->GetComponentFromISID(ISID);
	if (!TestNotNull(TEXT("Resting bottle instance"), ISM)) return false;
	const int32 Slot = ISM->FindISMI(ISID) * ISM->NumCustomDataFloats + 3;
	if (!TestTrue(TEXT("ISM strength custom data slot"), ISM->PerInstanceSMCustomData.IsValidIndex(Slot))) return false;

	FPropEventSyncDelta Initial;
	Initial.SetFlag(EPropEventDirtyMask::Strength);
	Initial.SetFlag(EPropEventDirtyMask::ContentLevel);
	Initial.Strength = 0.8f;
	Initial.ContentLevel = 0.0f;
	if (!TestTrue(TEXT("Set the bottle's stored strength"), Manager->SubmitInstanceEventUpdate(ISID, Initial))) return false;
	Manager->SetTemperature(ISID, FullHeat);
	Manager->AmbientTemperature = FullHeat;
	Manager->WorldElapsedSeconds += 2.0;
	Manager->BatchUpdateComponent->ProcessBatchUpdate(2.0f);
	TestEqual(TEXT("Sustained heat preserves stored strength"), Manager->FindUniqueData(ISID)->EventData.Strength, 0.8f);
	TestEqual(TEXT("Hot ISM publishes weakened strength"), ISM->PerInstanceSMCustomData[Slot], 0.6f);

	AActivePropActor* Active = nullptr;
	if (!TestTrue(TEXT("Promote the hot bottle"), Manager->PromoteInstanceToASM(ISID, EPropActivationReason::Interaction, true, Active))) return false;
	AColaPropActor* Bottle = Cast<AColaPropActor>(Active);
	if (!TestNotNull(TEXT("Active cola bottle"), Bottle)) return false;
	TestEqual(TEXT("Heat penalty survives promotion"), Bottle->GetTemperatureStrengthDebuff(), 0.2f);
	TestEqual(TEXT("Hot ASM uses weakened strength"), Bottle->GetStrength(), 0.6f);
	TestEqual(TEXT("Heat reduces pressure tolerance"), Bottle->GetBottlePressureTolerance(), PressureLimit * 0.6f);
	const auto& PrimitiveData = Bottle->PropMesh->GetCustomPrimitiveData().Data;
	if (!TestTrue(TEXT("ASM strength custom data slot"), PrimitiveData.IsValidIndex(3))) return false;
	TestEqual(TEXT("Hot ASM publishes weakened strength"), PrimitiveData[3], 0.6f);

	Manager->WorldElapsedSeconds += 2.0;
	Manager->BatchUpdateComponent->ProcessBatchUpdate(2.0f);
	TestEqual(TEXT("Authored heat without a melted product leaves the bottle usable"), Bottle->AcceptedFailureType, EContainmentFailureType::None);
	Manager->SetTemperature(ISID, (FullHeat + SofteningThreshold) * 0.5f);
	TestEqual(TEXT("Partly cooled bottle recovers half the penalty"), Bottle->GetStrength(), 0.7f);
	Bottle->ApplyBottleImpact(FVector::UpVector * (2.0f * Bottle->DropImpactThreshold), 1);
	TestEqual(TEXT("Impact subtracts only from stored strength"), Bottle->GetBaseStrength(), 0.8f - ImpactDamage);
	Manager->AmbientTemperature = 21.0f;
	Manager->WorldElapsedSeconds += 600.0;
	Manager->BatchUpdateComponent->ProcessBatchUpdate(600.0f);
	TestEqual(TEXT("Natural cooling clears the penalty"), Bottle->GetTemperatureStrengthDebuff(), 0.0f);
	TestEqual(TEXT("Cooled bottle retains impact damage"), Bottle->GetStrength(), 0.8f - ImpactDamage);
	TestEqual(TEXT("Pressure tolerance recovers to the damaged bottle's limit"), Bottle->GetBottlePressureTolerance(), PressureLimit * (0.8f - ImpactDamage));
	TestEqual(TEXT("Cooled ASM publishes recovered strength"), PrimitiveData[3], 0.8f - ImpactDamage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleDemotionTest, "ColossalFizzCo.Props.DemotionPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleDemotionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AColaPropActor* Bottle = World->SpawnActor<AColaPropActor>();
	if (!Bottle) return false;
	FPropEventUpdateData Event;
	Event.ContentLevel = 0.4f; Event.CapSealed = 0;
	Bottle->OnApplyEventData(Event);
	TestFalse(TEXT("Open filled bottle cannot demote"), Bottle->CanDemoteToISM());
	Event.ContentLevel = 0; Event.EventSequence++;
	Bottle->OnApplyEventData(Event);
	TestTrue(TEXT("Empty uncapped stationary bottle may demote"), Bottle->CanDemoteToISM());
	Bottle->bIsHeld = true;
	TestFalse(TEXT("Held bottle cannot demote"), Bottle->CanDemoteToISM());
	Bottle->bIsHeld = false;
	const int32 Generation = Bottle->GetPoolGeneration();
	Bottle->ResetTransientPoolState();
	TestEqual(TEXT("Pool reset advances generation"), Bottle->GetPoolGeneration(), Generation + 1);
	return true;
}
#endif
