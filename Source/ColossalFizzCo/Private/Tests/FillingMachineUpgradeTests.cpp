#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Props/InteractablePropDatabase.h"
#include "Props/InteractablePropManager.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Shop/FillingMachineUpgradeTypes.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/SocketColaFillingMachine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFillingMachineUpgradePurchaseTest, "ColossalFizzCo.Shop.FillingMachineUpgrades.Purchase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFillingMachineUpgradePurchaseTest::RunTest(const FString& Parameters)
{
	using EUpgrade = EFillingMachineUpgradeType;
	using EResult = EFillingMachinePurchaseResult;
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	auto* Manager = World->SpawnActor<APropManager>();
	if (!Manager || !Manager->SharedWallet) return false;
	Manager->bEnableAutosave = false;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	Manager->InitializePropManager();

	FPropProductDefinition Product;
	Product.ProductID = 9430;
	Product.bEnableInstanceUniqueData = true;
	Product.ConsumableProfile.NumOfPortions = 0;
	Product.StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Product.CustomActiveActorClass = ASocketColaFillingMachine::StaticClass();
	if (!Manager->RegisterProductDefinition(Product)) return false;
	const auto SpawnMachine = [Manager, &Product](FVector Location) -> ASocketColaFillingMachine*
	{
		const int32 ID = Manager->GenerateProductInstance(Product.ProductID, FTransform(Location));
		AActivePropActor* Active = nullptr;
		if (ID <= 0 || !Manager->PromoteInstanceToASM(ID, EPropActivationReason::Interaction, false, Active))
			return nullptr;
		auto* Machine = Cast<ASocketColaFillingMachine>(Active);
		if (Machine) Machine->PropMesh->SetSimulatePhysics(false);
		return Machine;
	};
	auto* Machine = SpawnMachine(FVector(0, 0, 1000));
	auto* Other = SpawnMachine(FVector(800, 0, 1000));
	if (!Machine || !Other) return false;
	auto* Player = World->SpawnActor<ACharacter>(Machine->PropMesh->Bounds.Origin + FVector(0, -145, 25), FRotator::ZeroRotator);
	auto* Controller = World->SpawnActor<APlayerController>();
	if (!Player || !Controller) return false;
	Player->SetActorEnableCollision(false);
	Controller->Possess(Player);

	const auto Buy = [&](EUpgrade Upgrade, int32 Expected, const FGuid& Request)
	{
		return Machine->PurchaseUpgrade(Player, 350.f, Upgrade, Expected, Request);
	};
	auto* Wallet = Manager->SharedWallet.Get();
	const auto Balance = [&]() { return Wallet->GetWalletState().BalanceCents; };
	TestEqual(TEXT("A new filling machine can store one cap"), Machine->Production->GetCapTrayCapacity(), 1);
	TestEqual(TEXT("A new filling machine can store one seal"), Machine->Production->GetSealTrayCapacity(), 1);
	TestEqual(TEXT("The pressure regulator has 100 purchases"), Machine->GetMaxUpgradeRank(EUpgrade::PressureRegulator), 100);
	const FGuid FirstRequest = FGuid::NewGuid();
	TestEqual(TEXT("An empty bank cannot buy an upgrade"), Buy(EUpgrade::CapTray, 0, FirstRequest), EResult::InsufficientFunds);
	TestEqual(TEXT("An unaffordable purchase does not change rank"), Machine->GetUpgradeRank(EUpgrade::CapTray), 0);

	constexpr int64 Funding = 100000000;
	Wallet->Credit(FGuid::NewGuid(), Funding, TEXT("FixtureSales"));
	int64 Spent = Machine->GetNextUpgradeCostCents(EUpgrade::CapTray);
	if (!TestEqual(TEXT("A cap tray purchase succeeds"), Buy(EUpgrade::CapTray, 0, FirstRequest), EResult::Purchased)) return false;
	TestEqual(TEXT("The tray gains one slot"), Machine->Production->GetCapTrayCapacity(), 2);
	TestEqual(TEXT("The same operation ID is idempotent"), Buy(EUpgrade::CapTray, 0, FirstRequest), EResult::AlreadyPurchased);
	TestEqual(TEXT("A duplicate operation cannot buy a different upgrade"), Buy(EUpgrade::SealTray, 0, FirstRequest), EResult::Unavailable);
	TestEqual(TEXT("A stale rank cannot charge for another purchase"), Buy(EUpgrade::CapTray, 0, FGuid::NewGuid()), EResult::RankChanged);
	TestEqual(TEXT("The wallet only pays once"), Balance(), Funding - Spent);
	TestEqual(TEXT("A distant machine cannot be upgraded"),
		Other->PurchaseUpgrade(Player, 350.f, EUpgrade::CapTray, 0, FGuid::NewGuid()), EResult::Unavailable);

	const int64 FirstPressurePrice = Machine->GetNextUpgradeCostCents(EUpgrade::PressureRegulator);
	for (const auto Upgrade : { EUpgrade::SealTray, EUpgrade::PressureRegulator, EUpgrade::TemperatureRegulator })
	{
		const int64 Price = Machine->GetNextUpgradeCostCents(Upgrade);
		if (!TestTrue(TEXT("The next rank has a price"), Price > 0)
			|| !TestEqual(TEXT("The next rank can be purchased independently"), Buy(Upgrade, 0, FGuid::NewGuid()), EResult::Purchased)) return false;
		Spent += Price;
	}
	TestEqual(TEXT("The seal tray gains one slot"), Machine->Production->GetSealTrayCapacity(), 2);
	TestTrue(TEXT("The first pressure rank reduces pressure by 0.05"),
		FMath::IsNearlyEqual(Machine->GetUpgradeValue(EUpgrade::PressureRegulator, 1), .05f));
	TestTrue(TEXT("A successive pressure rank costs more"), Machine->GetNextUpgradeCostCents(EUpgrade::PressureRegulator) > FirstPressurePrice);
	TestTrue(TEXT("The first temperature rank cools incoming liquid by 0.02"),
		FMath::IsNearlyEqual(Machine->GetUpgradeValue(EUpgrade::TemperatureRegulator, 1), .02f));
	TestEqual(TEXT("The shared bank pays exactly the successful costs"), Balance(), Funding - Spent);
	TestEqual(TEXT("The second table keeps its starting seal capacity"), Other->Production->GetSealTrayCapacity(), 1);

	auto Ranks = Manager->FindUniqueData(Machine->ISID)->EventData.FillingMachineUpgrades;
	FPropEventSyncDelta Maximum;
	Maximum.ExtendedDirtyMask = EPropExtendedEventMask::FillingMachineUpgrades;
	Ranks.SetRank(EUpgrade::CapTray, 99);
	Ranks.SetRank(EUpgrade::SealTray, 99);
	Ranks.SetRank(EUpgrade::PressureRegulator, 100);
	Maximum.FillingMachineUpgrades = Ranks;
	if (!TestTrue(TEXT("The machine accepts its valid maximum saved ranks"), Manager->SubmitInstanceEventUpdate(Machine->ISID, Maximum)))
		return false;
	TestEqual(TEXT("The fully upgraded cap tray stores 100 caps"), Machine->Production->GetCapTrayCapacity(), 100);
	TestEqual(TEXT("The fully upgraded seal tray stores 100 seals"), Machine->Production->GetSealTrayCapacity(), 100);
	TestTrue(TEXT("The final pressure rank reduces pressure by 5.00"),
		FMath::IsNearlyEqual(Machine->GetUpgradeValue(EUpgrade::PressureRegulator, 100), 5.f));
	TestEqual(TEXT("A maxed pressure regulator cannot be purchased again"),
		Buy(EUpgrade::PressureRegulator, 100, FGuid::NewGuid()), EResult::MaxRank);
	TestEqual(TEXT("A maxed branch has no further cost"), Machine->GetNextUpgradeCostCents(EUpgrade::PressureRegulator), int64(0));
	TestEqual(TEXT("A rejected max-rank purchase does not change the bank"), Balance(), Funding - Spent);

	const FString Slot = TEXT("Automation_FillingMachineRanks_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
	if (!TestTrue(TEXT("Upgrades can be written to a world save"), Manager->SavePropDatabase(Slot, 0))) return false;
	const auto* Save = UPropDatabaseSaveGame::LoadCompatibleSlot(Slot, 0);
	if (!TestNotNull(TEXT("The world save can be loaded"), Save)) return false;
	const auto* Entry = Save->WorldSaveData.SavedInstances.FindByPredicate([Machine](const auto& Instance)
	{
		return Instance.ISID == Machine->ISID;
	});
	if (!TestNotNull(TEXT("The save contains the upgraded table"), Entry)) return false;
	TestEqual(TEXT("The saved machine retains the full cap tray rank"),
		Entry->UniqueData.EventData.FillingMachineUpgrades.GetRank(EUpgrade::CapTray), 99);
	TestEqual(TEXT("The saved machine retains the full seal tray rank"),
		Entry->UniqueData.EventData.FillingMachineUpgrades.GetRank(EUpgrade::SealTray), 99);
	TestEqual(TEXT("The saved machine retains the full pressure rank"),
		Entry->UniqueData.EventData.FillingMachineUpgrades.GetRank(EUpgrade::PressureRegulator), 100);
	TestEqual(TEXT("The saved machine retains the purchased temperature rank"),
		Entry->UniqueData.EventData.FillingMachineUpgrades.GetRank(EUpgrade::TemperatureRegulator), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFillingMachineFullTraySyncTest, "ColossalFizzCo.Shop.FillingMachineUpgrades.FullTraySync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFillingMachineFullTraySyncTest::RunTest(const FString& Parameters)
{
	FPropEventSyncDelta Original;
	Original.ISID = 1000;
	Original.ExtendedDirtyMask = EPropExtendedEventMask::ManualEquipment | EPropExtendedEventMask::FillingMachineUpgrades;
	Original.FillingMachineUpgrades.SetRank(EFillingMachineUpgradeType::CapTray, 99);
	Original.FillingMachineUpgrades.SetRank(EFillingMachineUpgradeType::SealTray, 99);
	Original.FillingMachineUpgrades.SetRank(EFillingMachineUpgradeType::PressureRegulator, 100);
	Original.FillingMachineUpgrades.SetRank(EFillingMachineUpgradeType::TemperatureRegulator, 9);
	for (int32 Index = 0; Index < 100; ++Index)
	{
		Original.LoadedCapISIDs.Add(1010 + Index);
		Original.LoadedSealISIDs.Add(2010 + Index);
	}

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	bool bSuccess = false;
	Original.NetSerialize(Writer, nullptr, bSuccess);
	if (!TestTrue(TEXT("A full cap tray and a full seal tray serialize"), bSuccess)) return false;

	FMemoryReader Reader(Bytes);
	FPropEventSyncDelta Decoded;
	Decoded.NetSerialize(Reader, nullptr, bSuccess);
	TestTrue(TEXT("Full tray data deserializes"), bSuccess && !Reader.IsError());
	TestTrue(TEXT("All 100 cap identities replicate in order"), Decoded.LoadedCapISIDs == Original.LoadedCapISIDs);
	TestTrue(TEXT("All 100 seal identities replicate in order"), Decoded.LoadedSealISIDs == Original.LoadedSealISIDs);
	for (const auto Type : { EFillingMachineUpgradeType::CapTray, EFillingMachineUpgradeType::SealTray,
		EFillingMachineUpgradeType::PressureRegulator, EFillingMachineUpgradeType::TemperatureRegulator })
	{
		TestEqual(TEXT("The same update also carries each upgrade rank"),
			Decoded.FillingMachineUpgrades.GetRank(Type), Original.FillingMachineUpgrades.GetRank(Type));
	}
	return true;
}

#endif
