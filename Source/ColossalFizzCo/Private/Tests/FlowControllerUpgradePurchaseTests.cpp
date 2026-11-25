#include "Misc/AutomationTest.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ColaFlowController.h"
#include "Shop/SharedTeamWalletComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlowControllerUpgradePurchaseTest, "ColossalFizzCo.Hose.FlowController.UpgradePurchase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowControllerUpgradePurchaseTest::RunTest(const FString& Parameters)
{
	using EPath=EFlowControllerUpgradeType;
	using EResult=EFlowControllerPurchaseResult;
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
	auto* Manager=World->SpawnActor<APropManager>();
	if (!Manager || !Manager->SharedWallet) return false;
	Manager->bEnableAutosave=false;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	auto* Flow=World->SpawnActor<AColaFlowController>(FVector(0,0,1000),FRotator::ZeroRotator);
	auto* Other=World->SpawnActor<AColaFlowController>(FVector(600,0,1000),FRotator::ZeroRotator);
	if (!Flow || !Other) return false;
	auto* Player=World->SpawnActor<ACharacter>(Flow->UpgradeControl->Bounds.Origin+FVector(0,-130,25),FRotator::ZeroRotator);
	auto* PC=World->SpawnActor<APlayerController>();
	if (!Player || !PC) return false;
	Player->SetActorEnableCollision(false);
	PC->Possess(Player);
	const auto Buy=[&](EPath Type, int32 Expected, FGuid Request)
	{
		return Flow->PurchaseUpgrade(Player,350.f,Type,Expected,Request);
	};
	auto* Wallet=Manager->SharedWallet.Get();
	const auto Balance=[&]() { return Wallet->GetWalletState().BalanceCents; };
	const FGuid FirstRequest=FGuid::NewGuid();
	TestEqual(TEXT("A new shop starts with an empty shared bank"),Balance(),int64(0));
	TestEqual(TEXT("A rank requires shared-bank funds"),Buy(EPath::Lever,0,FirstRequest),EResult::InsufficientFunds);
	TestEqual(TEXT("A failed purchase leaves the lever at rank zero"),Flow->GetUpgradeRank(EPath::Lever),0);
	TestEqual(TEXT("A failed purchase does not debit the bank"),Balance(),int64(0));
	constexpr int64 Funding=1000000;
	Wallet->Credit(FGuid::NewGuid(),Funding,TEXT("FixtureSales"));
	const int64 FirstPrice=Flow->GetNextUpgradeCost(EPath::Lever);
	if (!TestEqual(TEXT("The first lever rank can be bought without port upgrades"),Buy(EPath::Lever,0,FirstRequest),EResult::Purchased)) return false;
	TestEqual(TEXT("A duplicate request does not buy another rank"),Buy(EPath::Lever,0,FirstRequest),EResult::AlreadyPurchased);
	TestEqual(TEXT("The same operation ID cannot be reused for another branch"),Buy(EPath::Suction,0,FirstRequest),EResult::Unavailable);
	TestEqual(TEXT("A stale requested rank cannot charge a second time"),Buy(EPath::Lever,0,FGuid::NewGuid()),EResult::RankChanged);
	TestEqual(TEXT("Only the displayed price was debited"),Balance(),Funding-FirstPrice);
	TestEqual(TEXT("A controller out of reach cannot be upgraded"),Other->PurchaseUpgrade(Player,350.f,EPath::Flow,0,FGuid::NewGuid()),EResult::Unavailable);

	int32 Purchases=1;
	int64 Spent=FirstPrice;
	for (const auto Type:{EPath::InputPorts,EPath::OutputPorts,EPath::Flow,EPath::Suction,EPath::Lever})
	{
		const int32 Maximum=AColaFlowController::GetMaxUpgradeRank(Type);
		for (int32 Rank=Flow->GetUpgradeRank(Type); Rank<Maximum; ++Rank)
		{
			const int64 Price=Flow->GetNextUpgradeCost(Type);
			if (!TestTrue(TEXT("Each successive rank has an editable positive price"),Price>0)
				|| !TestEqual(TEXT("Each purchase applies exactly one rank"),Buy(Type,Rank,FGuid::NewGuid()),EResult::Purchased)) return false;
			Spent+=Price;
			++Purchases;
		}
		TestEqual(TEXT("A completed branch cannot be purchased again"),Buy(Type,Maximum,FGuid::NewGuid()),EResult::MaxRank);
		TestEqual(TEXT("A completed branch no longer has a price"),Flow->GetNextUpgradeCost(Type),int64(0));
	}
	TestEqual(TEXT("The five branches have 30 purchases"),Purchases,30);
	TestEqual(TEXT("All successful purchases debit their exact prices"),Balance(),Funding-Spent);
	TestEqual(TEXT("The final manifold capacities are 4 and 12"),Flow->SelectorSockets.Num(),4);
	TestEqual(TEXT("The output manifold has 12 sockets"),Flow->FillerSockets.Num(),12);
	TestEqual(TEXT("The maximum intake rating is 8 portions per second"),Flow->GetIntakeRate(),8.f);
	TestEqual(TEXT("The maximum output rating is 8 portions per second"),Flow->GetOutputRate(),8.f);
	TestEqual(TEXT("The maximum suction strength is 1.50"),Flow->GetIntakeSuctionStrength(),1.5f);
	TestTrue(TEXT("Maximum flow increases inlet tolerance to 170 percent of its base"),
		FMath::IsNearlyEqual(Flow->GetInletPressureTolerance(),10.2f,UE_KINDA_SMALL_NUMBER));
	const auto* Saved=Manager->FindFlowControllerUpgrades(*Flow);
	TestTrue(TEXT("Each purchase records the live equipment ranks"),Saved && *Saved==Flow->UpgradeState);
	TestTrue(TEXT("Unpurchased equipment keeps all starting ranks"),Other->UpgradeState==FFlowControllerUpgradeState{});
	return true;
}
#endif
