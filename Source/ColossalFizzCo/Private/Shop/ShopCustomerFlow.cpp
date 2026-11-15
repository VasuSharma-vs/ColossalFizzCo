#include "Shop/ShopCustomerFlow.h"
#include "Shop/NormalColaNavigationConfig.h"
#include "Shop/ShopCustomerVisitComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/Character.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavAreas/NavArea_Obstacle.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"

AShopCustomerFlow::AShopCustomerFlow() { PrimaryActorTick.bCanEverTick=false; SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root"))); }
void AShopCustomerFlow::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || GetNetMode()==NM_Client) return;
	// Imported fixtures can have coarser navigation than physical collision. A null
	// area needs explicit capsule clearance because area modifiers are applied after erosion.
	const auto* DefaultCustomer=CustomerClass ? CustomerClass->GetDefaultObject<ACharacter>() : nullptr;
	const float Clearance=(DefaultCustomer ? DefaultCustomer->GetCapsuleComponent()->GetScaledCapsuleRadius() : 204.f)+60.f;
	auto AddNavigationBlocker=[this,Clearance](UPrimitiveComponent* Geometry,const FVector& Extent,TSubclassOf<UNavArea> Area)
	{
		FActorSpawnParameters Params; Params.Owner=this; Params.ObjectFlags|=RF_Transient;
		auto* Blocker=GetWorld()->SpawnActor<AActor>(Params);
		if (!Blocker) return;
		auto* Root=NewObject<USceneComponent>(Blocker);
		Blocker->SetRootComponent(Root); Root->RegisterComponent();
		Blocker->SetActorLocation(Geometry->GetComponentLocation());
		if (!Geometry->IsA<UCapsuleComponent>()) Blocker->SetActorLocation(Geometry->Bounds.Origin);
		Root->AttachToComponent(Geometry,FAttachmentTransformRules::KeepWorldTransform);
		auto* Modifier=NewObject<UNavModifierComponent>(Blocker);
		Blocker->AddInstanceComponent(Modifier);
		Modifier->FailsafeExtent=Extent+FVector(Clearance,Clearance,0);
		Modifier->bIncludeAgentHeight=true;
		Modifier->SetAreaClass(Area);
		Modifier->ForceNavigationRelevancy(true);
		Modifier->RegisterComponent();
		UE_LOG(LogTemp,Verbose,TEXT("SHOP_ROUTE_OBSTACLE source=%s bounds=%s relevant=%d"),*GetNameSafe(Geometry->GetOwner()),*Modifier->GetNavigationBounds().ToString(),Modifier->IsNavigationRelevant());
		NavigationBlockers.Add(Blocker);
	};
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		auto* Mesh=It->GetStaticMeshComponent();
		if (Mesh && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetPathName()==TEXT("/Game/Assest/HighDetailedMeshs/TheMerchantsEngine/TheMerchantsEngine.TheMerchantsEngine"))
			AddNavigationBlocker(Mesh,Mesh->Bounds.BoxExtent,UNavArea_Null::StaticClass());
	}
	const UClass* ExistingPatronClass=LoadClass<ACharacter>(nullptr,TEXT("/Game/AI/BasicNPC/BP_Basic_NPC.BP_Basic_NPC_C"));
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		PrepareCustomerMovement(*It);
		// Existing patrons keep their size, location and behavior. Route around their
		// capsules instead of asking local avoidance to squeeze through a blocked aisle.
		if (ExistingPatronClass && It->IsA(ExistingPatronClass) && !It->FindComponentByClass<UShopCustomerVisitComponent>())
		{
			auto* Capsule=It->GetCapsuleComponent();
			AddNavigationBlocker(Capsule,FVector(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),UNavArea_Obstacle::StaticClass());
		}
	}
	for (AActor* Bucket:Buckets) if (IsValid(Bucket)) BucketHomes.Add(Bucket,Bucket->GetActorTransform());
	GetWorldTimerManager().SetTimer(ArrivalTimer,FTimerDelegate::CreateWeakLambda(this,[this]{TryArrival();}),FMath::Max(1.f,ArrivalInterval),true,1.f);
}
void AShopCustomerFlow::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* ComponentWorld=GetWorld()) ComponentWorld->GetTimerManager().ClearTimer(ArrivalTimer);
	if (GetWorld() && !GetWorld()->bIsTearingDown) for (AActor* Blocker:NavigationBlockers) if (IsValid(Blocker)) Blocker->Destroy();
	NavigationBlockers.Reset();
	Super::EndPlay(Reason);
}
UShopScannerComponent* AShopCustomerFlow::GetScanner() const { return IsValid(CheckoutStation) ? CheckoutStation->FindComponentByClass<UShopScannerComponent>() : nullptr; }
USceneComponent* AShopCustomerFlow::GetBucketDock() const
{
	if (!IsValid(CheckoutStation)) return nullptr;
	TArray<USceneComponent*> Parts; CheckoutStation->GetComponents(Parts);
	for (auto* Part:Parts) if (Part->ComponentHasTag(TEXT("ShopBucketDock"))) return Part;
	return nullptr;
}
bool AShopCustomerFlow::StockIsClaimed(int32 ID) const
{
	const auto* Claimant=StockClaims.Find(ID); return Claimant && Claimant->IsValid();
}
bool AShopCustomerFlow::ClaimStock(int32 ID,UShopCustomerVisitComponent* Visit)
{
	auto* Manager=APropManager::Get(this);
	if (!HasAuthority() || !IsValid(Visit) || Visit->GetFlow()!=this || !Manager || !Manager->ShopCheckout || !Manager->ShopCheckout->IsAvailableColaStockISID(ID)) return false;
	if (const auto* Claimant=StockClaims.Find(ID)) if (Claimant->IsValid() && Claimant->Get()!=Visit) return false;
	StockClaims.Add(ID,Visit); return true;
}
void AShopCustomerFlow::ReleaseClaims(UShopCustomerVisitComponent* Visit)
{
	for (auto It=StockClaims.CreateIterator(); It; ++It) if (!It.Value().IsValid() || It.Value().Get()==Visit) It.RemoveCurrent();
}
int32 AShopCustomerFlow::QueuePosition(UShopCustomerVisitComponent* Visit)
{
	Queue.RemoveAll([](const auto& Item){return !Item.IsValid();}); return Queue.IndexOfByPredicate([Visit](const auto& Item){return Item.Get()==Visit;});
}
FVector AShopCustomerFlow::QueueLocation(int32 Index) const
{
	const auto* Default=CustomerClass ? CustomerClass->GetDefaultObject<ACharacter>() : nullptr;
	float Spacing=Default ? FMath::Max(140.f,Default->GetCapsuleComponent()->GetScaledCapsuleRadius()*2+60) : 140.f;
	for (const AActor* Container:Buckets) if (IsValid(Container)) if (const auto* Body=Cast<UPrimitiveComponent>(Container->GetRootComponent()))
	{
		const auto Bounds=Body->CalcBounds(FTransform(FQuat::Identity,FVector::ZeroVector,Container->GetActorScale3D()));
		// Match the carry pose's 25 cm forward gap, then leave 60 cm beyond the
		// next customer's capsule. A capsule-only queue stacks giant buckets.
		const float Diameter=Default ? Default->GetCapsuleComponent()->GetScaledCapsuleRadius()*2 : 408.f;
		Spacing=FMath::Max(Spacing,Diameter+Bounds.BoxExtent.X*2+85);
	}
	if (!IsValid(QueuePoint)) return GetActorLocation();
	FVector Direction=QueuePoint->GetActorForwardVector(); Direction.Z=0;
	if (!Direction.Normalize())
	{
		Direction=IsValid(CheckoutStation) ? CheckoutStation->GetActorLocation()-QueuePoint->GetActorLocation() : FVector::ForwardVector;
		Direction.Z=0; Direction=Direction.GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
	}
	const FVector AuthoredFront=QueuePoint->GetActorLocation();
	auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());
	const auto* Data=Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	if (!Data || !Default) return AuthoredFront-Direction*FMath::Max(0,Index)*Spacing;
	const float Radius=Default->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const auto Filter=UNavigationQueryFilter::GetQueryFilter(*Data,this,UNormalColaCustomerFilter::StaticClass());
	const FVector Extent(Radius+400,Radius+400,200);
	FNavLocation Front;
	FVector Floor=AuthoredFront;
	// Markers may be authored at eye height. Use the live customer's navigation
	// floor, just as visit MoveTo does, rather than selecting a shelf-top island.
	for (const ACharacter* Customer:Customers) if (IsValid(Customer) && !Customer->IsHidden())
	{ Floor.Z=Customer->GetNavAgentLocation().Z; break; }
	if (!Nav->ProjectPointToNavigation(Floor,Front,Extent,Data,Filter)) return AuthoredFront-Direction*FMath::Max(0,Index)*Spacing;
	TArray<FVector,TInlineAllocator<8>> Slots; Slots.Add(Front.Location);
	for (int32 Slot=1; Slot<=FMath::Clamp(Index,0,7); ++Slot)
	{
		FVector Candidate=Slots.Last()-Direction*Spacing;
		// Projection near a counter can collapse neighboring authored positions.
		// Search farther back, then require actual capsule clearance from every slot.
		for (int32 Attempt=0; Attempt<8; ++Attempt)
		{
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Candidate,Projected,Extent,Data,Filter)
				&& !Slots.ContainsByPredicate([&](const FVector& Occupied){return FVector::DistSquared2D(Occupied,Projected.Location)<FMath::Square(Spacing-1);}))
			{ Candidate=Projected.Location; break; }
			Candidate-=Direction*(Spacing*.5f);
		}
		Slots.Add(Candidate);
	}
	return Slots.Last();
}
void AShopCustomerFlow::JoinQueue(UShopCustomerVisitComponent* Visit) { if (HasAuthority() && IsValid(Visit)) Queue.AddUnique(Visit); }
void AShopCustomerFlow::LeaveQueue(UShopCustomerVisitComponent* Visit) { Queue.RemoveAll([Visit](const auto& Item){return !Item.IsValid() || Item.Get()==Visit;}); }
void AShopCustomerFlow::ReturnBucket(AActor* Bucket,UShopCustomerVisitComponent* Visit)
{
	const auto* Claim=BucketClaims.Find(Bucket);
	if (!HasAuthority() || !IsValid(Bucket) || !Claim || Claim->Get()!=Visit) return;
	Bucket->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (const auto* Home=BucketHomes.Find(Bucket)) Bucket->SetActorTransform(*Home,false,nullptr,ETeleportType::TeleportPhysics);
	BucketClaims.Remove(Bucket); Bucket->ForceNetUpdate();
}
void AShopCustomerFlow::ReleaseVisit(UShopCustomerVisitComponent* Visit) { LeaveQueue(Visit); ReleaseClaims(Visit); }

bool AShopCustomerFlow::TryArrival()
{
	if (!HasAuthority() || GetNetMode()==NM_Client) return false;
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->PropManagerReady || !Manager->ShopSupplies)
	{ ArrivalStatus=TEXT("Waiting for Prop Manager and supply service"); return false; }
	if (!Manager->ShopSupplies->InitializeCatalog()) { ArrivalStatus=TEXT("Supply catalog initialization failed"); return false; }
	if (!CustomerClass || !GetScanner() || !GetBucketDock() || !SpawnPoint || !EntrancePoint || !QueuePoint || !BucketReturnPoint || !DeparturePoint)
	{ ArrivalStatus=TEXT("Missing customer, checkout, bucket dock or visit route configuration"); return false; }
	auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation Entrance;
	const auto* CustomerDefault=CustomerClass->GetDefaultObject<ACharacter>();
	const FVector EntranceFloor=EntrancePoint->GetActorLocation()-FVector(0,0,CustomerDefault->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	// An unrelated dynamic tile rebuild must not stop arrivals at a valid entrance.
	if (!Nav || !Nav->ProjectPointToNavigation(EntranceFloor,Entrance,FVector(400,400,200)))
	{
		const auto* Data=Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
		ArrivalStatus=FString::Printf(TEXT("Waiting for walkable giant entrance navigation: bounds=%d activeTiles=%d building=%d"),
			Nav ? Nav->GetNavigationBounds().Num() : 0,Data ? Data->GetNumActiveTiles() : 0,
			Nav && UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()));
		return false;
	}
	Customers.RemoveAll([](const auto& Customer){return !IsValid(Customer);});
	for (ACharacter* Customer:Customers) if (auto* Visit=Customer->FindComponentByClass<UShopCustomerVisitComponent>())
		if (bRecycleDepartedCustomers && (Visit->Phase==EShopVisitPhase::Departed || Visit->Phase==EShopVisitPhase::Aborted)) Visit->RetireVisit();
	Queue.RemoveAll([](const auto& Item){return !Item.IsValid();});
	// A destroyed visitor cannot leave a station locked forever. Existing compensation gates still apply.
	if (Queue.IsEmpty())
	{
		auto* Scanner=GetScanner(); const auto State=Scanner->GetScannerState();
		if (State.Checkout.CustomerID.IsValid()) Scanner->ReleaseCustomer(State.Checkout.CustomerID,State.SessionID);
	}
	const auto Available=Manager->ShopCheckout->GetAvailableColaStockISIDs();
	if (!Available.ContainsByPredicate([this](int32 ID){return !StockIsClaimed(ID);}))
	{ ArrivalStatus=TEXT("Waiting for available shelf stock"); return false; }
	AActor* FreeBucket=nullptr;
	for (AActor* Bucket:Buckets) if (IsValid(Bucket))
	{
		const auto* Claim=BucketClaims.Find(Bucket);
		if (!Claim || !Claim->IsValid()) { FreeBucket=Bucket; break; }
	}
	if (!FreeBucket) { ArrivalStatus=TEXT("Waiting for a free shopping bucket"); return false; }
	ACharacter* Next=nullptr;
	for (ACharacter* Customer:Customers) if (const auto* Visit=Customer->FindComponentByClass<UShopCustomerVisitComponent>()) if (Visit->Phase==EShopVisitPhase::Inactive) { Next=Customer; break; }
	if (!Next && Customers.Num()<FMath::Clamp(MaxCustomers,1,8))
	{
		Next=GetWorld()->SpawnActor<ACharacter>(CustomerClass,SpawnPoint->GetActorTransform());
		if (Next) Customers.Add(Next);
	}
	if (!Next) { ArrivalStatus=TEXT("Waiting for a customer slot or clear spawn location"); return false; }
	auto* Visit=Next->FindComponentByClass<UShopCustomerVisitComponent>();
	if (!Visit) { ArrivalStatus=TEXT("Customer class has no visit component"); UE_LOG(LogTemp,Error,TEXT("Prototype customer %s has no native visit component"),*GetNameSafe(Next)); return false; }
	Next->SetActorLocationAndRotation(SpawnPoint->GetActorLocation(),SpawnPoint->GetActorRotation(),false,nullptr,ETeleportType::TeleportPhysics);
	Next->SetActorHiddenInGame(false); Next->SetActorEnableCollision(true);
	if (!Next->GetController()) Next->SpawnDefaultController();
	BucketClaims.Add(FreeBucket,Visit);
	if (Visit->StartVisit(this,FreeBucket)) { ArrivalStatus=TEXT("Customer visit started"); return true; }
	ArrivalStatus=TEXT("Customer could not begin its configured visit"); BucketClaims.Remove(FreeBucket); return false;
}

void AShopCustomerFlow::PrepareCustomerMovement(ACharacter* Customer)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !Customer || Customer->IsPlayerControlled()) return;
	if (auto* Movement=Customer->GetCharacterMovement())
	{
		Movement->AvoidanceConsiderationRadius=FMath::Max(800.f,Customer->GetCapsuleComponent()->GetScaledCapsuleRadius()*6);
		Movement->SetAvoidanceEnabled(true);
	}
}
