#include "ShopClockSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Shop/ShopClock.h"
#include "UI/ShopClockWidget.h"
#include "UObject/Package.h"

void UShopClockSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (World.GetNetMode() == NM_Client || UWorld::RemovePIEPrefix(World.GetOutermost()->GetName()) != TEXT("/Game/PrototypeLevel/ShopLevel"))
		return;

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	World.SpawnActor<AShopClock>(Params);
}

void UShopClockSubsystem::RegisterClock(AShopClock& SharedClock)
{
	Clock = &SharedClock;
}

void UShopClockSubsystem::Deinitialize()
{
	for (UShopClockWidget* Widget : LocalWidgets) Widget->RemoveFromParent();
	LocalWidgets.Reset();
	Clock.Reset();
	Super::Deinitialize();
}

bool UShopClockSubsystem::IsTickable() const
{
	return Super::IsTickable() && Clock.IsValid();
}

TStatId UShopClockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UShopClockSubsystem, STATGROUP_Tickables);
}

void UShopClockSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now < NextUpdateTime) return;
	NextUpdateTime = Now + 0.25;

	if (auto* SharedClock = Clock.Get(); SharedClock && SharedClock->HasAuthority())
		SharedClock->UpdateAuthorityState();
	UpdateLocalWidgets();
}

void UShopClockSubsystem::UpdateLocalWidgets()
{
	const auto* SharedClock = Clock.Get();
	UWorld* World = GetWorld();
	if (!SharedClock || !World->GetGameViewport()) return;

	LocalWidgets.RemoveAllSwap([World](UShopClockWidget* Widget)
	{
		const auto* Player = Widget->GetOwningPlayer();
		if (IsValid(Player) && Player->GetWorld() == World && Player->GetLocalPlayer()) return false;
		Widget->RemoveFromParent();
		return true;
	});

	const FShopClockState State = SharedClock->GetClockState();
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		auto* Player = It->Get();
		if (!Player || !Player->GetLocalPlayer()) continue;

		auto* Existing = LocalWidgets.FindByPredicate([Player](const UShopClockWidget* Widget)
		{
			return Widget->GetOwningPlayer() == Player;
		});
		UShopClockWidget* Widget = Existing ? Existing->Get() : CreateWidget<UShopClockWidget>(Player);
		if (!Widget) continue;
		Widget->UpdateClock(State);
		if (!Widget->IsInViewport() && !Widget->AddToPlayerScreen(10)) continue;
		if (!Existing) LocalWidgets.Add(Widget);
	}
}
