#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Props/InteractablePropManager.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopTableOfferComponent.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTablePurchaseTest, "ColossalFizzCo.Shop.TablePurchase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShopTablePurchaseTest::RunTest(const FString& Parameters)
{
	using EResult = EShopTablePurchaseResult;
	const FString FixtureName = TEXT("TablePurchase_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*(TEXT("/Temp/Automation/UEDPIE_3_") + FixtureName));
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FixtureName), Package, true,
		ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("A standalone world was created"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	APropManager* Manager = World->SpawnActor<APropManager>();
	if (!TestNotNull(TEXT("The shop manager exists"), Manager)) return false;
	Manager->bEnableAutosave = false;
	Manager->InitializePropManager();
	USharedTeamWalletComponent* Wallet = Manager->SharedWallet;
	if (!TestNotNull(TEXT("The shared wallet exists"), Wallet)) return false;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("The fixture rack mesh exists"), Cube)) return false;

	const auto AddTable = [&](int64 Price, FName ID = NAME_None, double RackOffset = 0)
	{
		AActor* Table = World->SpawnActor<AActor>();
		auto* Root = NewObject<USceneComponent>(Table);
		Table->AddInstanceComponent(Root);
		Table->SetRootComponent(Root);
		Root->RegisterComponent();
		auto* Rack = NewObject<UStaticMeshComponent>(Table);
		Table->AddInstanceComponent(Rack);
		Rack->SetupAttachment(Root);
		Rack->SetRelativeLocation(FVector(RackOffset, 0, 0));
		Rack->SetStaticMesh(Cube);
		Rack->RegisterComponent();
		auto* Offer = NewObject<UShopTableOfferComponent>(Table);
		Offer->PriceCents = Price;
		Offer->OfferID = ID;
		Table->AddInstanceComponent(Offer);
		Offer->RegisterComponent();
		return Offer;
	};
	AddTable(400, NAME_None, -800);
	const FName OtherID(TEXT("SecondRack"));
	AddTable(750, OtherID, 800);
	AddTable(0);
	UShopTableOfferComponent* Duplicate = AddTable(1500, OtherID);
	TArray<FShopTableOffer> Offers;
	UShopTableOfferComponent::GetOffers(World, Offers);
	if (!TestEqual(TEXT("Only a priced, unambiguous table is listed"), Offers.Num(), 1)) return false;
	const FName FirstID = Offers[0].OfferID;
	TestTrue(TEXT("A generated offer key is map-specific and strips the PIE prefix"),
		FirstID.ToString().StartsWith(TEXT("/Temp/Automation/") + FixtureName + TEXT(":")));
	TestFalse(TEXT("An unnamed table still has a player-facing label"), Offers[0].DisplayName.IsEmpty());
	TestEqual(TEXT("The first offer projects at its mesh center"), Offers[0].WorldLocation.X, -800.0);
	TestEqual(TEXT("Duplicate offer IDs cannot be bought"),
		UShopTableOfferComponent::Purchase(World, OtherID, 750), EResult::Unavailable);
	Duplicate->DestroyComponent();

	TestEqual(TEXT("A correct quote alone cannot buy a table without enough funds"),
		UShopTableOfferComponent::Purchase(World, FirstID, 400), EResult::InsufficientFunds);
	Wallet->Credit(FGuid::NewGuid(), 400, TEXT("TableFixtureFunding"));
	TestEqual(TEXT("An altered price is rejected"),
		UShopTableOfferComponent::Purchase(World, FirstID, 1), EResult::PriceChanged);
	TestEqual(TEXT("A rejected price does not debit the wallet"), Wallet->GetWalletState().BalanceCents, int64(400));
	TestEqual(TEXT("Exact remaining balance buys the table"),
		UShopTableOfferComponent::Purchase(World, FirstID, 400), EResult::Purchased);
	TestEqual(TEXT("The first purchase charges the exact table price"), Wallet->GetWalletState().BalanceCents, int64(0));
	const int64 PurchaseRevision = Wallet->GetWalletState().Revision;
	TestEqual(TEXT("The same table cannot be sold twice"),
		UShopTableOfferComponent::Purchase(World, FirstID, 400), EResult::AlreadyOwned);
	TestEqual(TEXT("A repeated purchase leaves the wallet ledger alone"), Wallet->GetWalletState().Revision, PurchaseRevision);

	const FString SaveSlot = TEXT("Automation_") + FixtureName;
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(SaveSlot, 0); };
	Manager->SaveDirtyInstances(SaveSlot);
	const UPropDatabaseSaveGame* Saved = UPropDatabaseSaveGame::LoadCompatibleSlot(SaveSlot, 0);
	if (!TestNotNull(TEXT("An incremental snapshot is written"), Saved)) return false;
	TestTrue(TEXT("The snapshot records only the first table"),
		Saved->WorldSaveData.PurchasedTableOffers == TArray<FName>{ FirstID });
	Wallet->Credit(FGuid::NewGuid(), 751, TEXT("TableFixtureFunding"));
	TestEqual(TEXT("The other table remains independently purchasable"),
		UShopTableOfferComponent::Purchase(World, OtherID, 750), EResult::Purchased);
	if (!TestTrue(TEXT("Reload restores the table ownership snapshot"), Manager->LoadPropDatabase(SaveSlot))) return false;

	UShopTableOfferComponent::GetOffers(World, Offers);
	const FShopTableOffer* Restored = Offers.FindByPredicate([&](const FShopTableOffer& Offer) { return Offer.OfferID == FirstID; });
	const FShopTableOffer* Other = Offers.FindByPredicate([&](const FShopTableOffer& Offer) { return Offer.OfferID == OtherID; });
	TestTrue(TEXT("The purchased table is still owned after reload"), Restored && Restored->bOwned);
	TestTrue(TEXT("The table purchased after the snapshot is available again"), Other && !Other->bOwned);
	TestTrue(TEXT("Tables sharing an actor pivot still project to different mesh centers"),
		Restored && Other && Other->WorldLocation.X == 800.0 && Other->WorldLocation != Restored->WorldLocation);
	TestEqual(TEXT("The wallet balance restores alongside the purchased tables"), Wallet->GetWalletState().BalanceCents, int64(0));
	return true;
}
#endif
