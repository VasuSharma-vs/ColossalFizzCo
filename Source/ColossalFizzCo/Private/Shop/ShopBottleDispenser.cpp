#include "Shop/ShopBottleDispenser.h"

#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopProcurementComponent.h"
#include "TimerManager.h"

AShopBottleDispenser::AShopBottleDispenser()
{
	bReplicates=true;
	PrimaryActorTick.bCanEverTick=true;
	PrimaryActorTick.bStartWithTickEnabled=false;
}

void AShopBottleDispenser::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShopBottleDispenser, CycleStartTime);
	DOREPLIFETIME(AShopBottleDispenser, bDropAreaBlocked);
}

void AShopBottleDispenser::BeginPlay()
{
	Super::BeginPlay();
	FindControls();
	UpdateLever();
}

void AShopBottleDispenser::FindControls()
{
	for (USceneComponent* Component : TInlineComponentArray<USceneComponent*>(this))
	{
		const FName Name=Component->GetFName();
		if (Name==TEXT("Spanning arrow")) SpawnArrow=Cast<UArrowComponent>(Component);
		const bool bJoint=Name==TEXT("Leaver joint");
		if (!bJoint && Name!=TEXT("Leaver Handle") && Name!=TEXT("Leaver Handle 1") && Name!=TEXT("Leaver Handle 2")) continue;
		auto* Lever=Cast<UPrimitiveComponent>(Component);
		if (!Lever) continue;
		if (bJoint) LeverJoint=Lever;
		LeverComponents.Add(Lever);
		Lever->SetMobility(EComponentMobility::Movable);
		if (!Lever->IsQueryCollisionEnabled()) Lever->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Lever->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}
	if (LeverJoint) RestRotation=LeverJoint->GetRelativeRotation();
}

bool AShopBottleDispenser::IsLeverComponent(const UPrimitiveComponent* Component) const
{
	return Component && LeverComponents.Contains(Component);
}

bool AShopBottleDispenser::HasLeverInView(APawn& Player, float MaxDistance) const
{
	const auto* Controller=Cast<APlayerController>(Player.GetController());
	if (!Controller || Controller->GetPawn()!=&Player || Player.GetWorld()!=GetWorld()
		|| !FMath::IsFinite(MaxDistance) || MaxDistance<=0.f) return false;

	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	const FVector End=Eye+View.Vector()*(MaxDistance+FVector::Distance(Eye, Player.GetActorLocation()));
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BottleDispenser), false, &Player);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Query) || Hit.GetActor()!=this) return false;
	if (!IsLeverComponent(Hit.GetComponent()))
	{
		// The dispenser's simple body collision can cover part of an authored lever mesh.
		Query.bTraceComplex=true;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Query)
			|| Hit.GetActor()!=this || !IsLeverComponent(Hit.GetComponent())) return false;
	}
	return FVector::DistSquared(Player.GetActorLocation(), Hit.ImpactPoint)<=FMath::Square(double(MaxDistance));
}

APropManager* AShopBottleDispenser::FindShopManager() const
{
	if (auto* Manager=CachedManager.Get()) return Manager;
	if (!GetWorld()) return nullptr;
	TActorIterator<APropManager> Manager(GetWorld());
	if (!Manager) return nullptr;
	CachedManager=*Manager;
	return *Manager;
}

FName AShopBottleDispenser::GetDispenseOffer() const
{
	switch (DispensedSupply)
	{
	case EShopDispenserSupply::Bottle: return TEXT("EmptyBottle");
	case EShopDispenserSupply::Cap: return TEXT("TwistCap");
	case EShopDispenserSupply::Seal: return TEXT("TemporarySeal");
	}
	return NAME_None;
}

int64 AShopBottleDispenser::GetDispensePriceCents() const
{
	int64 PriceCents=0;
	if (const auto* Manager=FindShopManager(); Manager && Manager->ShopSupplies)
		Manager->ShopSupplies->Quote(GetDispenseOffer(), 1, PriceCents);
	return PriceCents;
}

FText AShopBottleDispenser::GetDispensedSupplyLabel() const
{
	switch (DispensedSupply)
	{
	case EShopDispenserSupply::Bottle: return NSLOCTEXT("ShopHUD", "DispenserBottleLabel", "EMPTY BOTTLE");
	case EShopDispenserSupply::Cap: return NSLOCTEXT("ShopHUD", "DispenserCapLabel", "TWIST CAP");
	case EShopDispenserSupply::Seal: return NSLOCTEXT("ShopHUD", "DispenserSealLabel", "TEMPORARY SEAL");
	}
	return FText::GetEmpty();
}

FTransform AShopBottleDispenser::GetOutletSpawnTransform() const
{
	return FTransform(SpawnArrow->GetComponentQuat(), SpawnArrow->GetComponentLocation());
}

void AShopBottleDispenser::SetDropAreaBlocked(bool bBlocked)
{
	if (bDropAreaBlocked==bBlocked) return;
	bDropAreaBlocked=bBlocked;
	if (bBlocked)
		GetWorldTimerManager().SetTimer(DropAreaTimer, this, &AShopBottleDispenser::RefreshDropAreaClearance, .2f, true);
	else
		GetWorldTimerManager().ClearTimer(DropAreaTimer);
	ForceNetUpdate();
}

void AShopBottleDispenser::RefreshDropAreaClearance()
{
	const auto* Manager=FindShopManager();
	const bool bStillBlocked=IsValid(SpawnArrow) && Manager && Manager->ShopSupplies
		&& Manager->ShopSupplies->IsDispenserDropAreaBlocked(GetDispenseOffer(), GetOutletSpawnTransform(), this);
	SetDropAreaBlocked(bStillBlocked);
}

bool AShopBottleDispenser::TryDispense(APawn* Player, float MaxDistance)
{
	if (!HasAuthority() || !IsValid(Player) || !LeverJoint || !SpawnArrow || bDispensing
		|| GetCooldownRemaining()>0.f || !HasLeverInView(*Player, MaxDistance)) return false;
	const auto* Manager=FindShopManager();
	if (!Manager || !Manager->ShopSupplies) return false;

	TGuardValue<bool> Guard(bDispensing, true);
	int32 DeliveredISID=0;
	const FTransform SpawnTransform=GetOutletSpawnTransform();
	EShopPurchaseResult Result=EShopPurchaseResult::InvalidOffer;
	switch (DispensedSupply)
	{
	case EShopDispenserSupply::Bottle: Result=Manager->ShopSupplies->PurchaseDispensedBottle(SpawnTransform, DeliveredISID, this); break;
	case EShopDispenserSupply::Cap: Result=Manager->ShopSupplies->PurchaseDispensedCap(SpawnTransform, DeliveredISID, this); break;
	case EShopDispenserSupply::Seal: Result=Manager->ShopSupplies->PurchaseDispensedSeal(SpawnTransform, DeliveredISID, this); break;
	}
	if (Result==EShopPurchaseResult::DropAreaBlocked) SetDropAreaBlocked(true);
	if (Result!=EShopPurchaseResult::Success) return false;

	CycleStartTime=GetServerTime();
	SetDropAreaBlocked(false);
	UpdateLever();
	ForceNetUpdate();
	return true;
}

double AShopBottleDispenser::GetServerTime() const
{
	const auto* World=GetWorld();
	if (!World) return 0.0;
	const auto* GameState=World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

float AShopBottleDispenser::GetCycleDuration() const
{
	return FMath::Max(0.1f, LeverCycleSeconds);
}

float AShopBottleDispenser::GetCooldownRemaining() const
{
	if (CycleStartTime<0.0) return 0.f;
	return static_cast<float>(FMath::Clamp(CycleStartTime+GetCycleDuration()-GetServerTime(), 0.0, double(GetCycleDuration())));
}

void AShopBottleDispenser::OnRep_CycleStartTime()
{
	UpdateLever();
}

void AShopBottleDispenser::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLever();
}

void AShopBottleDispenser::UpdateLever()
{
	const float Remaining=GetCooldownRemaining();
	SetActorTickEnabled(Remaining>0.f);
	if (!LeverJoint) return;
	const float Progress=1.f-Remaining/GetCycleDuration();
	const float Pull=1.f-FMath::Abs(2.f*Progress-1.f);
	FRotator Rotation=RestRotation;
	Rotation.Pitch-=90.f*FMath::SmoothStep(0.f, 1.f, Pull);
	LeverJoint->SetRelativeRotation(Rotation);
}
