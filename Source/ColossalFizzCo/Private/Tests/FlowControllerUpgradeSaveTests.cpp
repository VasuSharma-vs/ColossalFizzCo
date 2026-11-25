#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ColaFlowController.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlowControllerUpgradeSaveTest, "ColossalFizzCo.Hose.FlowController.UpgradeSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowControllerUpgradeSaveTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false);
	const FString Fixture = TEXT("FlowUpgradeSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString MapPath = TEXT("/Temp/Automation/") + Fixture;
	auto* Package = CreatePackage(*(TEXT("/Temp/Automation/UEDPIE_3_") + Fixture));
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*Fixture), Package, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	auto* Manager = World->SpawnActor<APropManager>();
	auto* First = World->SpawnActor<AColaFlowController>(FVector(0, 0, 1000), FRotator::ZeroRotator);
	auto* Second = World->SpawnActor<AColaFlowController>(FVector(500, 0, 1000), FRotator::ZeroRotator);
	if (!Manager || !First || !Second) return false;
	Manager->bEnableAutosave = false;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	const FString FirstKey = Manager->GetFlowControllerUpgradeKey(*First);
	TestTrue(TEXT("Identity includes the map package without the PIE prefix"), FirstKey.StartsWith(MapPath + TEXT(".")));
	TestNotEqual(TEXT("Two controllers in the same map have distinct keys"), FirstKey, Manager->GetFlowControllerUpgradeKey(*Second));

	const FString Slot = TEXT("Automation_FlowControllerUpgrade_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString LegacySlot = Slot + TEXT("_Legacy");
	const FString InvalidSlot = Slot + TEXT("_Invalid");
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		UGameplayStatics::DeleteGameInSlot(LegacySlot, 0);
		UGameplayStatics::DeleteGameInSlot(InvalidSlot, 0);
	};
	UPropDatabaseSaveGame* Legacy = Cast<UPropDatabaseSaveGame>(UGameplayStatics::CreateSaveGameObject(UPropDatabaseSaveGame::StaticClass()));
	Legacy->WorldSaveData.SchemaVersion = 5;
	if (!TestTrue(TEXT("A rank-free schema-five snapshot saves"), UGameplayStatics::SaveGameToSlot(Legacy, LegacySlot, 0))) return false;

	FFlowControllerUpgradeState Ranks;
	Ranks.InputPortsRank = 1;
	Ranks.OutputPortsRank = 2;
	Ranks.FlowRank = 4;
	Ranks.SuctionRank = 2;
	Ranks.LeverRank = 3;
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 900, TEXT("FixtureFunding"));
	const auto Result = Manager->SharedWallet->CommitWithResources(FGuid::NewGuid(), -500, TEXT("FixtureFlowUpgrade"), [&]
	{
		return Manager->RecordFlowControllerUpgrades(*First, Ranks);
	});
	if (!TestEqual(TEXT("Equipment ranks and wallet debit commit together"), Result, EWalletOperationResult::Applied)) return false;

	Manager->SaveDirtyInstances(Slot);
	auto* Saved = UPropDatabaseSaveGame::LoadCompatibleSlot(Slot, 0);
	if (!TestNotNull(TEXT("An incremental world save writes upgrade state"), Saved)
		|| !TestEqual(TEXT("The snapshot stores only the upgraded machine"), Saved->WorldSaveData.FlowControllerUpgrades.Num(), 1)) return false;
	if (!TestTrue(TEXT("A play-session reload restores the snapshot"), Manager->LoadPropDatabase(Slot))) return false;
	const auto* Restored = Manager->FindFlowControllerUpgrades(*First);
	TestTrue(TEXT("All five ranks restore by equipment identity"), Restored && *Restored == Ranks);
	TestNull(TEXT("The identical second controller has independent ranks"), Manager->FindFlowControllerUpgrades(*Second));
	TestEqual(TEXT("The live controller gains its restored inlet ports"), First->SelectorSockets.Num(), 2);
	TestEqual(TEXT("The live controller gains its restored outlet ports"), First->FillerSockets.Num(), 3);
	TestEqual(TEXT("The other controller stays at its starter count"), Second->FillerSockets.Num(), 1);
	TestEqual(TEXT("The corresponding wallet balance is restored"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(400));

	for (auto& Entry : Saved->WorldSaveData.FlowControllerUpgrades) Entry.Value.FlowRank = 99;
	if (!TestTrue(TEXT("A corrupt upgrade fixture writes"), UGameplayStatics::SaveGameToSlot(Saved, InvalidSlot, 0))) return false;
	TestFalse(TEXT("Invalid saved ranks are rejected"), Manager->LoadPropDatabase(InvalidSlot));
	TestEqual(TEXT("Failed load preserves live outlet count"), First->FillerSockets.Num(), 3);
	TestEqual(TEXT("Failed load preserves the current bank"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(400));

	if (!TestTrue(TEXT("A save without purchases still loads"), Manager->LoadPropDatabase(LegacySlot))) return false;
	TestNull(TEXT("Legacy saves default to unpurchased ranks"), Manager->FindFlowControllerUpgrades(*First));
	TestEqual(TEXT("Loading a rank-free save restores the base ports of an active controller"), First->FillerSockets.Num(), 1);
	TestEqual(TEXT("Loading a rank-free save restores the matching bank"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	return true;
}
#endif
