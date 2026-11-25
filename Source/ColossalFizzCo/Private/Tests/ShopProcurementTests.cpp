#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"
#include "Components/ASMComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Templates/UnrealTemplate.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopProcurementTest,"ColossalFizzCo.Shop.Procurement",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopProcurementTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	auto* Product = LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_EmptyBottel.DA_EmptyBottel"));
	if (!TestNotNull(TEXT("Real empty-bottle product exists"),Product) || !TestNotNull(TEXT("Manager owns procurement service"),Manager->ShopSupplies.Get())) return false;
	auto* Catalog = NewObject<UShopSupplyCatalog>(Manager);
	FShopSupplyOffer Offer; Offer.OfferID=TEXT("EmptyGlass"); Offer.Product=Product; Offer.UnitCostCents=400;
	FShopSupplyOffer DispenserOffer = Offer;
	DispenserOffer.OfferID = TEXT("EmptyBottle");
	DispenserOffer.UnitCostCents = 137;
	Catalog->Offers = {Offer, DispenserOffer};
	Manager->ShopSupplies->Catalog=Catalog;
	TArray<int32> Delivered;
	const FTransform Delivery(FRotator::ZeroRotator,FVector(0,0,200),FVector::OneVector);
	const FTransform Outlet(FRotator(12.f, 67.f, 4.f), FVector(2650, 340, 500), FVector::OneVector);
	int32 DispensedISID = INDEX_NONE;
	TestEqual(TEXT("New game bank remains zero"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(0));
	TestEqual(TEXT("Supplies cannot be bought without funds"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::InsufficientFunds);
	TestTrue(TEXT("Rejected purchase delivers nothing"),Delivered.IsEmpty());
	const int32 BeforeUnfundedPurchase = Manager->ISIDData.Num();
	TestEqual(TEXT("The dispenser also requires funds"), Manager->ShopSupplies->PurchaseDispensedBottle(Outlet, DispensedISID), EShopPurchaseResult::InsufficientFunds);
	TestEqual(TEXT("No bottle ID is returned for an unfunded dispense"), DispensedISID, 0);
	TestEqual(TEXT("An unfunded dispense leaves instance count unchanged"), Manager->ISIDData.Num(), BeforeUnfundedPurchase);
	TestEqual(TEXT("An unfunded dispense leaves the balance unchanged"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	Manager->SharedWallet->Credit(FGuid::NewGuid(),10000,TEXT("IsolatedProcurementTestFunding"));
	const FGuid PurchaseID=FGuid::NewGuid();
	TestEqual(TEXT("Funded purchase succeeds"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::Success);
	TestEqual(TEXT("Three physical bottle identities are delivered"),Delivered.Num(),3);
	TSet<int32> UniqueIDs(Delivered);
	TestEqual(TEXT("Purchased identities are distinct"),UniqueIDs.Num(),3);
	for (int32 I=0;I<Delivered.Num();++I)
		for (int32 J=I+1;J<Delivered.Num();++J)
			TestFalse(TEXT("Native sized bottle deliveries do not intersect"),
				Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Delivered[I])).Intersect(
				Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Delivered[J]))));
	for (int32 ID : Delivered)
	{
		const auto* Unique=Manager->FindUniqueData(ID);
		if (!TestNotNull(TEXT("Purchased bottle has persistent state"),Unique)) return false;
		TestEqual(TEXT("Purchased bottle is empty"),Unique->EventData.ContentLevel,0.f);
		TestTrue(TEXT("Purchased bottle retains clean condition"),Unique->EventData.bClean);
		const auto* Actor=Manager->GetActiveActorFromISID(ID);
		TestTrue(TEXT("Delivered supply starts as a physical active object"),Actor && Actor->PropMesh->IsSimulatingPhysics());
	}
	TestEqual(TEXT("Bank debits exact configured cost"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(8800));
	const int64 Revision=Manager->SharedWallet->GetWalletState().Revision;
	TestEqual(TEXT("Retry does not purchase twice"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::AlreadyPurchased);
	TestTrue(TEXT("Retry creates no new deliverables"),Delivered.IsEmpty());
	TestEqual(TEXT("Retry keeps ledger revision"),Manager->SharedWallet->GetWalletState().Revision,Revision);
	TestEqual(TEXT("Reusing purchase ID for another quantity rejected"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,2,Delivery,Delivered),EShopPurchaseResult::OperationConflict);
	TestEqual(TEXT("Negative quantity rejected"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,-1,Delivery,Delivered),EShopPurchaseResult::InvalidQuantity);
	Manager->ShopSupplies->bUseDeliveryArea=true; Manager->ShopSupplies->DeliveryColumns=2; Manager->ShopSupplies->DeliveryRows=1;
	const FTransform Area(FVector(1000,0,200)); TArray<int32> First,Second,Blocked;
	TestEqual(TEXT("First independent order occupies a delivery slot"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,First),EShopPurchaseResult::Success);
	TestEqual(TEXT("Second independent order finds another slot"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,Second),EShopPurchaseResult::Success);
	if (First.Num()!=1 || Second.Num()!=1) return false;
	TestFalse(TEXT("Independent orders never overlap their physical bottles"),Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(First[0])).Intersect(Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Second[0]))));
	const int64 BeforeFull=Manager->SharedWallet->GetWalletState().BalanceCents;
	TestEqual(TEXT("Full delivery area rejects another order"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,Blocked),EShopPurchaseResult::DeliveryFailed);
	TestEqual(TEXT("Full delivery area cannot charge the bank"),Manager->SharedWallet->GetWalletState().BalanceCents,BeforeFull);
	TestTrue(TEXT("Failed delivery has no partial goods"),Blocked.IsEmpty());
	Manager->InitializeInstanceTransform(First[0],FTransform(FVector(2000,0,200)));
	TestEqual(TEXT("Removing goods makes the physical slot reusable"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,Blocked),EShopPurchaseResult::Success);
	const auto Saved=Manager->SharedWallet->ExportCommittedState();
	TestTrue(TEXT("Purchase ledger restores"),Manager->SharedWallet->RestoreCommittedState(Saved));
	TestEqual(TEXT("Purchase retry remains idempotent after ledger restore"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::AlreadyPurchased);
	Catalog->Offers.Add(Offer);
	int64 Quote=0;
	TestFalse(TEXT("Ambiguous offer IDs rejected"),Manager->ShopSupplies->Quote(Offer.OfferID,1,Quote));

	const auto BeforeDispense = Manager->SharedWallet->GetWalletState();
	if (!TestEqual(TEXT("The dispenser bypasses the full depot grid"), Manager->ShopSupplies->PurchaseDispensedBottle(Outlet, DispensedISID), EShopPurchaseResult::Success)) return false;
	TestEqual(TEXT("The dispenser charges one catalog-priced empty bottle"), Manager->SharedWallet->GetWalletState().BalanceCents,
		BeforeDispense.BalanceCents - DispenserOffer.UnitCostCents);
	const auto* Bottle = Manager->GetActiveActorFromISID(DispensedISID);
	if (!TestNotNull(TEXT("The dispensed bottle is a real active prop"), Bottle)) return false;
	TestTrue(TEXT("The catalog bottle starts at the exact outlet transform"), Bottle->GetActorTransform().Equals(Outlet));
	TestTrue(TEXT("The dispensed bottle starts in Settling with physics enabled"),
		Manager->IsSettling(DispensedISID) && Bottle->PropMesh->IsSimulatingPhysics());
	const auto* Unique = Manager->FindUniqueData(DispensedISID);
	TestTrue(TEXT("The dispensed bottle contains no liquid"), Unique && Unique->EventData.ContentLevel == 0.f);

	const auto BeforeFailure = Manager->SharedWallet->GetWalletState();
	const int32 InstanceCountBeforeFailure = Manager->ISIDData.Num();
	FTransform FailedOutlet = Outlet;
	FailedOutlet.AddToTranslation(FVector(0, 3000, 0));
	{
		// Let the bottle instance be allocated, then force physical promotion to fail.
		TGuardValue<TObjectPtr<UASMComponent>> NoActivePropPool(Manager->ASMComponent, nullptr);
		TestEqual(TEXT("Without an active prop pool the dispenser reports a failed delivery"),
			Manager->ShopSupplies->PurchaseDispensedBottle(FailedOutlet, DispensedISID), EShopPurchaseResult::DeliveryFailed);
	}
	TestEqual(TEXT("A failed dispenser delivery clears the output ID"), DispensedISID, 0);
	TestEqual(TEXT("A failed physical promotion leaves no new prop instances"), Manager->ISIDData.Num(), InstanceCountBeforeFailure);
	const auto AfterFailure = Manager->SharedWallet->GetWalletState();
	TestEqual(TEXT("A failed physical promotion does not charge the bank"), AfterFailure.BalanceCents, BeforeFailure.BalanceCents);
	TestEqual(TEXT("A failed physical promotion does not advance ledger revision"), AfterFailure.Revision, BeforeFailure.Revision);

	const auto* StandardCatalog = LoadObject<UShopSupplyCatalog>(nullptr,
		TEXT("/Game/NormalColaPrototype/DA_NormalColaSupplyCatalog.DA_NormalColaSupplyCatalog"));
	const auto* StandardCap = StandardCatalog ? StandardCatalog->FindOffer(TEXT("TwistCap")) : nullptr;
	if (!TestNotNull(TEXT("The real catalog has the standard twist cap"), StandardCap)) return false;
	auto& CapOffer = Catalog->Offers.Add_GetRef(*StandardCap);
	CapOffer.UnitCostCents = 53;
	const FTransform CapOutlet(FRotator(-10.f, 26.f, 15.f), FVector(2950, 340, 500), FVector::OneVector);
	int32 CapISID = 0;
	if (!TestEqual(TEXT("The cap dispenser produces a real twist cap"), Manager->ShopSupplies->PurchaseDispensedCap(CapOutlet, CapISID),
		EShopPurchaseResult::Success)) return false;
	TestEqual(TEXT("The cap dispenser charges the catalog price for one cap"), Manager->SharedWallet->GetWalletState().BalanceCents,
		AfterFailure.BalanceCents - CapOffer.UnitCostCents);
	const auto* Cap = Manager->GetActiveActorFromISID(CapISID);
	if (!TestNotNull(TEXT("The dispensed cap is a physical active prop"), Cap)) return false;
	TestTrue(TEXT("The cap starts in Settling at the exact outlet transform"), Cap->GetActorTransform().Equals(CapOutlet)
		&& Manager->IsSettling(CapISID) && Cap->PropMesh->IsSimulatingPhysics());
	const int32 PlasticBottlePID = Manager->RegisterProductFromDataAsset(StandardCatalog->EmptyBottleProduct.LoadSynchronous());
	TestTrue(TEXT("The dispensed cap fits a standard plastic bottle"), Manager->IsAssemblyCompatible(PlasticBottlePID, 0, Cap->ProductID));

	const auto* StandardSeal = StandardCatalog->FindOffer(TEXT("TemporarySeal"));
	if (!TestNotNull(TEXT("The real catalog has the temporary seal"), StandardSeal)) return false;
	auto& SealOffer = Catalog->Offers.Add_GetRef(*StandardSeal);
	SealOffer.UnitCostCents = 107;
	const FTransform SealOutlet(FRotator(15.f, 33.f, 8.f), FVector(3250, 340, 500), FVector::OneVector);
	const int64 BeforeSeal = Manager->SharedWallet->GetWalletState().BalanceCents;
	int32 SealISID = 0;
	if (!TestEqual(TEXT("The seal dispenser produces a real temporary seal"), Manager->ShopSupplies->PurchaseDispensedSeal(SealOutlet, SealISID),
		EShopPurchaseResult::Success)) return false;
	TestEqual(TEXT("The seal dispenser charges the catalog price for one seal"), Manager->SharedWallet->GetWalletState().BalanceCents,
		BeforeSeal - SealOffer.UnitCostCents);
	const auto* Seal = Manager->GetActiveActorFromISID(SealISID);
	if (!TestNotNull(TEXT("The dispensed seal is a physical active prop"), Seal)) return false;
	TestTrue(TEXT("The seal starts in Settling at the exact outlet transform"), Seal->GetActorTransform().Equals(SealOutlet)
		&& Manager->IsSettling(SealISID) && Seal->PropMesh->IsSimulatingPhysics());
	const auto* SealData = Manager->FindUniqueData(SealISID);
	TestTrue(TEXT("The filling table recognizes the dispensed prop as an unused temporary seal"),
		Manager->ShopSupplies->IsTemporarySealProduct(Seal->ProductID) && SealData && !SealData->EventData.bTemporarySealUsed);

	const auto* CapDefinition = Manager->FindProductDefinition(Cap->ProductID);
	if (!TestTrue(TEXT("The cap has registered mesh bounds"), CapDefinition && CapDefinition->StaticMesh)) return false;
	const FTransform ClearanceOutlet(FRotator(37.f, 46.f, 71.f), FVector(6500, 340, 900), FVector::OneVector);
	const FBox LocalBounds = CapDefinition->StaticMesh->GetBoundingBox();
	const FBox SpawnBounds = LocalBounds.TransformBy(ClearanceOutlet);
	const double DropDistance = SpawnBounds.GetSize().Z + 100.0;
	const FVector OffCenter = ClearanceOutlet.TransformPosition(LocalBounds.GetCenter() + FVector(.75 * LocalBounds.GetExtent().X, 0, 0));
	auto AddBlocker = [&](ECollisionChannel ObjectType, const FVector& Location, const FVector& HalfExtent)
	{
		auto* Obstacle = World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Obstacle);
		Obstacle->SetRootComponent(Box);
		Obstacle->AddInstanceComponent(Box);
		Box->InitBoxExtent(HalfExtent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ObjectType);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		return Box;
	};
	auto CheckBlockedDispense = [&](const TCHAR* Scenario)
	{
		const auto Before = Manager->SharedWallet->GetWalletState();
		const int32 BeforeCount = Manager->ISIDData.Num();
		int32 BlockedISID = INDEX_NONE;
		TestEqual(Scenario, Manager->ShopSupplies->PurchaseDispensedCap(ClearanceOutlet, BlockedISID), EShopPurchaseResult::DropAreaBlocked);
		TestTrue(TEXT("The readout recheck detects the same blocker"),
			Manager->ShopSupplies->IsDispenserDropAreaBlocked(TEXT("TwistCap"), ClearanceOutlet, nullptr));
		TestEqual(TEXT("A blocked outlet returns no product ID"), BlockedISID, 0);
		TestEqual(TEXT("A blocked outlet adds no prop instances"), Manager->ISIDData.Num(), BeforeCount);
		const auto After = Manager->SharedWallet->GetWalletState();
		TestEqual(TEXT("A blocked outlet does not charge the bank"), After.BalanceCents, Before.BalanceCents);
		TestEqual(TEXT("A blocked outlet does not advance ledger revision"), After.Revision, Before.Revision);
	};

	auto* MovingBlocker = AddBlocker(ECC_PhysicsBody, OffCenter - FVector(0, 0, DropDistance * .5), FVector(.4));
	CheckBlockedDispense(TEXT("The rotated footprint catches an off-center physics blocker below the outlet"));
	MovingBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	auto* StartingBlocker = AddBlocker(ECC_WorldStatic, OffCenter, FVector(.4));
	CheckBlockedDispense(TEXT("An initial overlap with WorldStatic also blocks dispensing"));
	StartingBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("The readout recheck clears when the blocker leaves"),
		Manager->ShopSupplies->IsDispenserDropAreaBlocked(TEXT("TwistCap"), ClearanceOutlet, nullptr));

	FVector OutsideSweep = SpawnBounds.GetCenter();
	OutsideSweep.Z = SpawnBounds.Min.Z - DropDistance - 3.0;
	MovingBlocker->SetWorldLocation(OutsideSweep);
	MovingBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	int32 ClearISID = 0;
	if (!TestEqual(TEXT("A physics blocker just below the clearance range permits dispensing"),
		Manager->ShopSupplies->PurchaseDispensedCap(ClearanceOutlet, ClearISID), EShopPurchaseResult::Success)) return false;

	FTransform FloorOutlet = ClearanceOutlet;
	FloorOutlet.AddToTranslation(FVector(0, 3000, 0));
	const FBox FloorBounds = LocalBounds.TransformBy(FloorOutlet);
	FVector FloorCenter = FloorBounds.GetCenter();
	FloorCenter.Z = FloorBounds.Min.Z - 30.0;
	const auto* Floor = AddBlocker(ECC_WorldStatic, FloorCenter,
		FVector(FloorBounds.GetExtent().X + 20.0, FloorBounds.GetExtent().Y + 20.0, 3.0));
	const FVector SweepStart = FloorOutlet.TransformPosition(LocalBounds.GetCenter());
	const FVector SweepEnd = SweepStart - FVector(0, 0, FloorBounds.GetSize().Z + 100.0);
	FHitResult FloorHit;
	const bool bHit = World->SweepSingleByChannel(FloorHit, SweepStart, SweepEnd, FloorOutlet.GetRotation(), ECC_PhysicsBody,
		FCollisionShape::MakeBox(LocalBounds.GetExtent().ComponentMax(FVector(.1))), FCollisionQueryParams(SCENE_QUERY_STAT(TestDispenserFloor), false));
	const auto* HitComponent = FloorHit.GetComponent();
	int32 FloorISID = 0;
	const auto FloorResult = Manager->ShopSupplies->PurchaseDispensedCap(FloorOutlet, FloorISID);
	AddInfo(FString::Printf(TEXT("Floor clearance hit=%d actor=%s component=%s expected=%d type=%d penetrating=%d normal=%s distance=%.2f result=%d"),
		bHit, *GetNameSafe(FloorHit.GetActor()), *GetNameSafe(HitComponent), HitComponent == Floor,
		HitComponent ? int32(HitComponent->GetCollisionObjectType()) : -1, FloorHit.bStartPenetrating,
		*FloorHit.ImpactNormal.ToString(), FloorHit.Distance, int32(FloorResult)));
	if (!TestEqual(TEXT("An upward-facing floor with no initial overlap permits dispensing"), FloorResult, EShopPurchaseResult::Success)) return false;
	TestTrue(TEXT("A floor does not shift or rotate the authored outlet pose"), Manager->GetTransform(FloorISID).Equals(FloorOutlet));
	return true;
}
#endif
