#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "OnlineSubsystemTypes.h"
#include "Props/InteractablePropManager.h"
#include "Shop/PlayerUpgradesComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerUpgradePurchaseTest,"ColossalFizzCo.Player.Upgrades.PurchaseAndSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerUpgradePurchaseTest::RunTest(const FString& Parameters)
{
	using EPath=EPlayerUpgradeType;
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	const FString Slot=TEXT("Automation_PlayerUpgrades_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString LegacySlot=Slot+TEXT("_Legacy");
	const FString InvalidSlot=Slot+TEXT("_Invalid");
	ON_SCOPE_EXIT
	{
		UGameplayStatics::DeleteGameInSlot(Slot,0);
		UGameplayStatics::DeleteGameInSlot(LegacySlot,0);
		UGameplayStatics::DeleteGameInSlot(InvalidSlot,0);
	};
	auto* Manager=World->SpawnActor<APropManager>();
	if (!Manager || !Manager->SharedWallet) return false;
	Manager->bEnableAutosave=false;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();

	const auto SpawnPlayer=[World](const FString& Name, const FVector& Position) -> ACharacter*
	{
		auto* Player=World->SpawnActor<ACharacter>(Position,FRotator::ZeroRotator);
		auto* Controller=World->SpawnActor<APlayerController>();
		auto* State=World->SpawnActor<APlayerState>();
		if (!Player || !Controller || !State) return nullptr;
		State->SetOwner(Controller);
		Controller->SetPlayerState(State);
		const FUniqueNetIdRef ProfileID=FUniqueNetIdString::Create(Name,FName(TEXT("Test")));
		State->SetUniqueId(FUniqueNetIdRepl(ProfileID));
		Controller->Possess(Player);
		return Player;
	};
	auto* First=SpawnPlayer(TEXT("upgrade-test-first"),FVector(0,0,1000));
	auto* Second=SpawnPlayer(TEXT("upgrade-test-second"),FVector(500,0,1000));
	if (!First || !Second) return false;
	UPlayerUpgradesComponent::RestoreForWorld(*Manager);
	auto* Upgrades=UPlayerUpgradesComponent::Find(First);
	auto* OtherUpgrades=UPlayerUpgradesComponent::Find(Second);
	if (!TestNotNull(TEXT("The shop finds or attaches a player-state component"),Upgrades)
		|| !TestNotNull(TEXT("The second player gets a separate component"),OtherUpgrades)) return false;
	TestTrue(TEXT("One player has one component across controller and pawn lookups"),
		UPlayerUpgradesComponent::Find(First->GetController())==Upgrades);
	TestTrue(TEXT("New players have default ranks"),Upgrades->GetUpgradeState()==FPlayerUpgradeState{});
	TestEqual(TEXT("Empty bottle handling starts at one"),Upgrades->GetCarryCapacity(EPath::EmptyBottleHandling),1);
	TestEqual(TEXT("Caps start at one"),Upgrades->GetCarryCapacity(EPath::CapHandling),1);
	TestEqual(TEXT("Seals start at one"),Upgrades->GetCarryCapacity(EPath::SealHandling),1);
	TestEqual(TEXT("Loaded bottles have a fixed limit of one"),UPlayerUpgradesComponent::LoadedBottleCarryLimit,1);
	TestEqual(TEXT("Rank zero keeps the original shake"),Upgrades->GetCarryShakeMultiplier(),1.f);
	const auto Balance=[Manager]() { return Manager->SharedWallet->GetWalletState().BalanceCents; };
	const FGuid FirstOperation=FGuid::NewGuid();
	TestEqual(TEXT("The first empty bottle rank has a configured price"),Upgrades->GetNextUpgradeCostCents(EPath::EmptyBottleHandling),int64(2000));
	if (!TestTrue(TEXT("The owning player can request the next rank"),Upgrades->RequestPurchase(EPath::EmptyBottleHandling,0,FirstOperation))) return false;
	TestEqual(TEXT("Without money the rank stays the same"),Upgrades->GetUpgradeRank(EPath::EmptyBottleHandling),0);
	constexpr int64 Funding=1000000;
	Manager->SharedWallet->Credit(FGuid::NewGuid(),Funding,TEXT("FixtureSales"));
	Upgrades->RequestPurchase(EPath::EmptyBottleHandling,0,FirstOperation);
	TestEqual(TEXT("A purchase increases the allowance immediately"),Upgrades->GetCarryCapacity(EPath::EmptyBottleHandling),2);
	TestEqual(TEXT("The purchase debits the shop bank once"),Balance(),Funding-2000);
	Upgrades->RequestPurchase(EPath::EmptyBottleHandling,0,FirstOperation);
	Upgrades->RequestPurchase(EPath::EmptyBottleHandling,0,FGuid::NewGuid());
	OtherUpgrades->RequestPurchase(EPath::EmptyBottleHandling,0,FirstOperation);
	TestEqual(TEXT("Replay, stale rank and cross-profile reuse do not debit"),Balance(),Funding-2000);
	TestEqual(TEXT("The other player's allowance stays independent"),OtherUpgrades->GetCarryCapacity(EPath::EmptyBottleHandling),1);

	const auto* DefinitionProperty=FindFProperty<FArrayProperty>(Upgrades->GetClass(),TEXT("UpgradeDefinitions"));
	if (!TestNotNull(TEXT("Definitions are exposed as an editable config property"),DefinitionProperty)) return false;
	auto& Rows=*DefinitionProperty->ContainerPtrToValuePtr<TArray<FPlayerUpgradeRow>>(Upgrades);
	auto* Edited=Rows.FindByPredicate([](const FPlayerUpgradeRow& Row)
	{
		return Row.Type==EPath::EmptyBottleHandling && Row.Rank==2;
	});
	if (!TestNotNull(TEXT("The second empty bottle rank is configurable"),Edited)) return false;
	Edited->PriceCents=4321;
	TestEqual(TEXT("The purchase quote uses the edited price"),Upgrades->GetNextUpgradeCostCents(EPath::EmptyBottleHandling),int64(4321));

	int32 Purchases=1;
	int64 Spent=2000;
	for (const auto Type:{EPath::EmptyBottleHandling,EPath::CapHandling,EPath::SealHandling,EPath::HandStability})
	{
		const int32 MaxRank=UPlayerUpgradesComponent::GetMaxUpgradeRank(Type);
		for (int32 Rank=Upgrades->GetUpgradeRank(Type); Rank<MaxRank; ++Rank)
		{
			const int64 Price=Upgrades->GetNextUpgradeCostCents(Type);
			if (!TestTrue(TEXT("Every purchasable rank has a positive price"),Price>0)) return false;
			Upgrades->RequestPurchase(Type,Rank,FGuid::NewGuid());
			if (!TestEqual(TEXT("Only the requested next rank is granted"),Upgrades->GetUpgradeRank(Type),Rank+1)) return false;
			Spent+=Price;
			++Purchases;
		}
		TestEqual(TEXT("A maxed path has no further price"),Upgrades->GetNextUpgradeCostCents(Type),int64(0));
		Upgrades->RequestPurchase(Type,MaxRank,FGuid::NewGuid());
	}
	TestEqual(TEXT("There are 15 purchasable ranks"),Purchases,15);
	TestEqual(TEXT("Only successful purchases change the bank"),Balance(),Funding-Spent);
	TestEqual(TEXT("Maximum empty bottle carry"),Upgrades->GetCarryCapacity(EPath::EmptyBottleHandling),3);
	TestEqual(TEXT("Maximum cap carry"),Upgrades->GetCarryCapacity(EPath::CapHandling),12);
	TestEqual(TEXT("Maximum seal carry"),Upgrades->GetCarryCapacity(EPath::SealHandling),12);
	TestEqual(TEXT("Maximum shake reduction halves movement shake"),Upgrades->GetCarryShakeMultiplier(),.5f);
	const auto Purchased=Upgrades->GetUpgradeState();

	Manager->SaveDirtyInstances(Slot);
	auto* Snapshot=UPropDatabaseSaveGame::LoadCompatibleSlot(Slot,0);
	if (!TestNotNull(TEXT("The incremental shop save writes"),Snapshot)) return false;
	TestEqual(TEXT("Only the profile with purchased ranks is saved"),Snapshot->WorldSaveData.PlayerUpgrades.Num(),1);
	for (auto& Entry:Snapshot->WorldSaveData.PlayerUpgrades) Entry.Value.SealCarryRank=5;
	if (!UGameplayStatics::SaveGameToSlot(Snapshot,InvalidSlot,0)) return false;
	TestFalse(TEXT("Out-of-range saved ranks cannot alter live state"),Manager->LoadPropDatabase(InvalidSlot));
	TestTrue(TEXT("The rejected snapshot keeps live ranks"),Upgrades->GetUpgradeState()==Purchased);
	TestEqual(TEXT("The rejected snapshot keeps the bank"),Balance(),Funding-Spent);

	auto* Legacy=Cast<UPropDatabaseSaveGame>(UGameplayStatics::CreateSaveGameObject(UPropDatabaseSaveGame::StaticClass()));
	Legacy->WorldSaveData.SchemaVersion=5;
	if (!UGameplayStatics::SaveGameToSlot(Legacy,LegacySlot,0)
		|| !TestTrue(TEXT("A rank-free save loads"),Manager->LoadPropDatabase(LegacySlot))) return false;
	TestTrue(TEXT("A rank-free save restores live rank zero"),Upgrades->GetUpgradeState()==FPlayerUpgradeState{});
	if (!TestTrue(TEXT("The matching purchased snapshot reloads"),Manager->LoadPropDatabase(Slot))) return false;
	TestTrue(TEXT("The account restores its four ranks"),Upgrades->GetUpgradeState()==Purchased);
	TestTrue(TEXT("Another account remains at rank zero"),OtherUpgrades->GetUpgradeState()==FPlayerUpgradeState{});
	TestEqual(TEXT("The matching bank balance restores with the ranks"),Balance(),Funding-Spent);
	return true;
}
#endif
