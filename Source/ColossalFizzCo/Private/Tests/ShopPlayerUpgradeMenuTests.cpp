#include "Misc/AutomationTest.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ScopeExit.h"
#include "OnlineSubsystemTypes.h"
#include "Props/InteractablePropManager.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "UI/ShopTerminalWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopPlayerUpgradeMenuTest, "ColossalFizzCo.PlayerUpgrades.Menu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShopPlayerUpgradeMenuTest::RunTest(const FString& Parameters)
{
	auto* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	auto* World = Game->GetWorld();
	if (!World) return false;
	ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	auto* Manager = World->SpawnActor<APropManager>();
	Manager->bEnableAutosave = false;
	auto* PC = World->SpawnActor<APlayerController>();
	auto* Pawn = World->SpawnActor<ACharacter>();
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Game->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(0));
	Player->PlayerController = PC;
	PC->Player = Player;

	auto* State = World->SpawnActor<APlayerState>();
	State->SetOwner(PC);
	PC->SetPlayerState(State);
	const FUniqueNetIdRef ProfileID = FUniqueNetIdString::Create(TEXT("menu-player"), FName(TEXT("Test")));
	State->SetUniqueId(FUniqueNetIdRepl(ProfileID));
	PC->Possess(Pawn);
	auto* Upgrades = NewObject<UPlayerUpgradesComponent>(State);
	State->AddInstanceComponent(Upgrades);
	Upgrades->RegisterComponent();
	if (!TestTrue(TEXT("The pawn resolves its player profile"), UPlayerUpgradesComponent::Find(Pawn) == Upgrades))
		return false;

	auto* Actions = NewObject<UShopPlayerInteractionComponent>(Pawn);
	Pawn->AddInstanceComponent(Actions);
	Actions->RegisterComponent();
	auto* Menu = CreateWidget<UShopTerminalWidget>(PC, UShopTerminalWidget::StaticClass());
	if (!TestNotNull(TEXT("Player upgrades widget initializes"), Menu)) return false;
	Menu->ConfigurePlayerUpgrades(Actions);
	const auto Slate = Menu->TakeWidget();
	Menu->Refresh();

	if (!TestEqual(TEXT("Only the four player paths have purchase tabs"), Menu->FlowUpgradeTabs.Num(), 4) ||
		!TestNotNull(TEXT("The player menu has a purchase button"), Menu->BuyFlowUpgradeButton.Get())) return false;
	TArray<UWidget*> Widgets;
	Menu->WidgetTree->GetAllWidgets(Widgets);
	TestTrue(TEXT("Full bottles are a fixed carry rule"), Widgets.ContainsByPredicate([](const UWidget* Widget)
	{
		const auto* Label = Cast<UTextBlock>(Widget);
		return Label && Label->GetText().ToString().StartsWith(TEXT("FULL: 1"));
	}));
	TestTrue(TEXT("First empty bottle upgrade previews the next carrying limit"),
		Menu->FlowUpgradeValue->GetText().ToString().Contains(TEXT("1 → 2 empty bottles")));
	TestFalse(TEXT("The button is disabled when the bank has no money"), Menu->BuyFlowUpgradeButton->GetIsEnabled());

	Menu->FlowUpgradeTabs[3]->OnClicked.Broadcast();
	TestTrue(TEXT("Stability shows percentage points of shake removed"),
		Menu->FlowUpgradeValue->GetText().ToString().Contains(TEXT("0% → 10% less movement shake")));
	const auto Price = Upgrades->GetNextUpgradeCostCents(EPlayerUpgradeType::HandStability);
	if (!TestEqual(TEXT("The wallet can fund this upgrade"),
		Manager->SharedWallet->Credit(FGuid::NewGuid(), Price, TEXT("PlayerUpgradeMenuTest")), EWalletOperationResult::Applied)) return false;
	Menu->Refresh();
	TestTrue(TEXT("The exact available balance enables a purchase"), Menu->BuyFlowUpgradeButton->GetIsEnabled());
	Menu->BuyFlowUpgradeButton->OnClicked.Broadcast();
	Menu->Refresh();
	TestEqual(TEXT("Buying from the menu grants the selected rank"), Upgrades->GetUpgradeRank(EPlayerUpgradeType::HandStability), 1);
	TestTrue(TEXT("A successful purchase refreshes current and next values"),
		Menu->FlowUpgradeValue->GetText().ToString().Contains(TEXT("10% → 20% less movement shake")));
	TestFalse(TEXT("The refreshed button reflects the remaining balance"), Menu->BuyFlowUpgradeButton->GetIsEnabled());
	Menu->Close();
	return true;
}
#endif
