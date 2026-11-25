#include "Shop/ShopPlayerInteractionComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Shop/ColaFlowController.h"
#include "Shop/MachinePlacementGeometry.h"
#include "Shop/SocketColaFillingMachine.h"
#include "TimerManager.h"
#include "UI/ShopMachinePlacementPreview.h"
#include "UI/ShopTerminalWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogMachinePlacement, Log, All);

namespace
{
	constexpr float PlacementHoldSeconds = .5f;
	constexpr float DegreesPerMouseUnit = .6f;

	APawn* GetMachinePlacementOperator(const AActor& Machine)
	{
		if (const auto* Table = Cast<ASocketColaFillingMachine>(&Machine)) return Table->GetPlacementOperator();
		if (const auto* Flow = Cast<AColaFlowController>(&Machine)) return Flow->GetPlacementOperator();
		return nullptr;
	}

	bool BeginPlacement(AActor& Machine, APawn& Pawn)
	{
		if (auto* Table = Cast<ASocketColaFillingMachine>(&Machine)) return Table->BeginMachinePlacement(Pawn);
		if (auto* Flow = Cast<AColaFlowController>(&Machine)) return Flow->BeginMachinePlacement(Pawn);
		return false;
	}

	bool FinishPlacement(AActor& Machine, APawn& Pawn, const FTransform& Pose)
	{
		if (auto* Table = Cast<ASocketColaFillingMachine>(&Machine)) return Table->FinishMachinePlacement(Pawn, Pose);
		if (auto* Flow = Cast<AColaFlowController>(&Machine)) return Flow->FinishMachinePlacement(Pawn, Pose);
		return false;
	}

	void CancelPlacement(AActor& Machine, APawn& Pawn)
	{
		if (auto* Table = Cast<ASocketColaFillingMachine>(&Machine)) Table->CancelMachinePlacement(Pawn);
		else if (auto* Flow = Cast<AColaFlowController>(&Machine)) Flow->CancelMachinePlacement(Pawn);
	}
}

APlayerController* UShopPlayerInteractionComponent::GetMachinePlacementController() const
{
	if (auto* Controller = Cast<APlayerController>(GetOwner())) return Controller;
	const auto* Pawn = Cast<APawn>(GetOwner());
	return Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
}

AActor* UShopPlayerInteractionComponent::FindMachineForPlacement(const APlayerController& Controller) const
{
	const auto* Pawn = Controller.GetPawn().Get();
	if (!Pawn || !GetWorld() || !FMath::IsFinite(MaxInteractionDistance) || MaxInteractionDistance <= 0) return nullptr;
	FVector Eye;
	FRotator View;
	if (Controller.IsLocalController()) Controller.GetPlayerViewPoint(Eye, View);
	else Pawn->GetActorEyesViewPoint(Eye, View);
	const double Distance = MaxInteractionDistance + FVector::Distance(Eye, Pawn->GetActorLocation());
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(MachinePlacementAim), false, Pawn);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + View.Vector() * Distance, ECC_Visibility, Query)
		|| FVector::DistSquared(Pawn->GetActorLocation(), Hit.ImpactPoint) > FMath::Square(double(MaxInteractionDistance))) return nullptr;
	if (auto* Table = Cast<ASocketColaFillingMachine>(Hit.GetActor()))
		return IsValid(Table) && Table->bIsActive && !Table->bIsHeld && Table->ISID > 0 ? Table : nullptr;
	auto* Flow = Cast<AColaFlowController>(Hit.GetActor());
	return IsValid(Flow) && !Flow->IsMachinePlacementActive() ? Flow : nullptr;
}

void UShopPlayerInteractionComponent::PressMachinePlacementKey()
{
	if (bMachinePlacementKeyDown) return;
	bMachinePlacementKeyDown = true;
	if (LocalPlacementMachine.IsValid() || MachinePlacementPreview)
	{
		CancelMachinePlacementInput();
		return;
	}
	const auto* Controller = GetMachinePlacementController();
	if (!Controller || !Controller->IsLocalController() || !GetWorld()
		|| (IsTerminalOpen() && !TerminalWidget->IsWorldReadout())) return;
	auto* Machine = FindMachineForPlacement(*Controller);
	UE_LOG(LogMachinePlacement, Verbose, TEXT("Placement key aimed at %s"), *GetNameSafe(Machine));
	if (!Machine || GetMachinePlacementOperator(*Machine)) return;
	PendingPlacementMachine = Machine;
	MachinePlacementHoldTime = GetWorld()->GetTimeSeconds();
	GetWorld()->GetTimerManager().SetTimer(MachinePlacementHoldTimer, this, &UShopPlayerInteractionComponent::UpdateMachinePlacementHold, .05f, true);
}

void UShopPlayerInteractionComponent::ReleaseMachinePlacementKey()
{
	bMachinePlacementKeyDown = false;
	ClearMachinePlacementHold();
}

void UShopPlayerInteractionComponent::ClearMachinePlacementHold()
{
	PendingPlacementMachine.Reset();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MachinePlacementHoldTimer);
}

void UShopPlayerInteractionComponent::UpdateMachinePlacementHold()
{
	auto* Machine = PendingPlacementMachine.Get();
	const auto* Controller = GetMachinePlacementController();
	if (!Machine || !Controller || !Controller->IsInputKeyDown(EKeys::M) || FindMachineForPlacement(*Controller) != Machine
		|| (IsTerminalOpen() && !TerminalWidget->IsWorldReadout())
		|| LocalPlacementSequence == MAX_int64)
	{
		UE_LOG(LogMachinePlacement, Verbose, TEXT("Canceled placement hold for %s (M down %d)"), *GetNameSafe(Machine),
			Controller && Controller->IsInputKeyDown(EKeys::M));
		ClearMachinePlacementHold();
		return;
	}
	if (GetWorld()->GetTimeSeconds() - MachinePlacementHoldTime < PlacementHoldSeconds) return;
	ClearMachinePlacementHold();
	CancelSocketUnplugHold();
	LocalPlacementMachine = Machine;
	CloseFlowReadout();
	CloseFillingTableReadout();
	Server_BeginMachinePlacement(Machine, ++LocalPlacementSequence);
}

void UShopPlayerInteractionComponent::Server_BeginMachinePlacement_Implementation(AActor* Machine, int64 Sequence)
{
	const auto* Controller = GetMachinePlacementController();
	auto* Pawn = Controller ? Controller->GetPawn().Get() : nullptr;
	if (auto* Previous = ServerPlacementMachine.Get(); Previous && (!ServerPlacementPawn.IsValid()
		|| GetMachinePlacementOperator(*Previous) != ServerPlacementPawn.Get()))
	{
		ServerPlacementMachine.Reset();
		ServerPlacementPawn.Reset();
	}
	const bool bActive = IsValid(Pawn) && Pawn->HasAuthority() && IsValid(Machine) && Machine->GetWorld() == GetWorld()
		&& Sequence > ServerPlacementSequence && !ServerPlacementMachine.IsValid()
		&& FindMachineForPlacement(*Controller) == Machine && BeginPlacement(*Machine, *Pawn);
	UE_LOG(LogMachinePlacement, Verbose, TEXT("Server placement for %s active %d"), *GetNameSafe(Machine), bActive);
	if (bActive)
	{
		ServerPlacementSequence = Sequence;
		ServerPlacementMachine = Machine;
		ServerPlacementPawn = Pawn;
	}
	Client_MachinePlacementState(Machine, Sequence, bActive);
}

void UShopPlayerInteractionComponent::Server_FinishMachinePlacement_Implementation(AActor* Machine, int64 Sequence, FTransform Pose)
{
	const auto* Controller = GetMachinePlacementController();
	auto* Pawn = ServerPlacementPawn.Get();
	bool bActive = IsValid(Pawn) && Pawn->HasAuthority() && Controller && Controller->GetPawn() == Pawn
		&& IsValid(Machine) && Machine->GetWorld() == GetWorld() && ServerPlacementMachine.Get() == Machine
		&& GetMachinePlacementOperator(*Machine) == Pawn && Sequence == ServerPlacementSequence;
	const bool bClear = bActive && ShopMachinePlacement::CanPlace(*Machine, *Pawn, Pose);
	if (bClear && FinishPlacement(*Machine, *Pawn, Pose))
	{
		ServerPlacementMachine.Reset();
		ServerPlacementPawn.Reset();
		bActive = false;
	}
	UE_LOG(LogMachinePlacement, Verbose, TEXT("Finish placement for %s at %s (clear %d, active %d, actual %s)"),
		*GetNameSafe(Machine), *Pose.GetLocation().ToCompactString(), bClear, bActive,
		IsValid(Machine) ? *Machine->GetActorLocation().ToCompactString() : TEXT("none"));
	Client_MachinePlacementState(Machine, Sequence, bActive);
}

void UShopPlayerInteractionComponent::Server_CancelMachinePlacement_Implementation(AActor* Machine, int64 Sequence)
{
	if (!IsValid(Machine) || ServerPlacementMachine.Get() != Machine || Sequence != ServerPlacementSequence) return;
	if (auto* Pawn = ServerPlacementPawn.Get(); Pawn && Pawn->HasAuthority()) CancelPlacement(*Machine, *Pawn);
	ServerPlacementMachine.Reset();
	ServerPlacementPawn.Reset();
}

void UShopPlayerInteractionComponent::Client_MachinePlacementState_Implementation(AActor* Machine, int64 Sequence, bool bActive)
{
	if (Sequence != LocalPlacementSequence || LocalPlacementMachine.Get() != Machine) return;
	bMachinePlacementCommitPending = false;
	if (!bActive)
	{
		CloseMachinePlacementPreview();
		return;
	}
	if (MachinePlacementPreview) return;
	auto* Controller = GetMachinePlacementController();
	if (!IsValid(Machine) || !Controller || !Controller->IsLocalController() || !GetWorld() || !BindMachinePlacementInput(*Controller))
	{
		CancelMachinePlacementInput();
		return;
	}
	FActorSpawnParameters Spawn;
	Spawn.Owner = Controller;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MachinePlacementPreview = GetWorld()->SpawnActor<AShopMachinePlacementPreview>(
		AShopMachinePlacementPreview::StaticClass(), Machine->GetActorTransform(), Spawn);
	if (!MachinePlacementPreview || !MachinePlacementPreview->InitializeFromMachine(*Machine))
	{
		UE_LOG(LogMachinePlacement, Verbose, TEXT("Could not render placement preview for %s"), *GetNameSafe(Machine));
		CancelMachinePlacementInput();
		return;
	}
	MachinePlacementYaw = float(Machine->GetActorRotation().Yaw);
	if (Controller->IsInputKeyDown(EKeys::RightMouseButton)) StartMachinePlacementRotation();
	UpdateMachinePlacementPreview();
	if (!MachinePlacementPreview) return;
	FTimerManagerTimerParameters Parameters;
	Parameters.bLoop = true;
	Parameters.bMaxOncePerFrame = true;
	GetWorld()->GetTimerManager().SetTimer(MachinePlacementPreviewTimer, this, &UShopPlayerInteractionComponent::UpdateMachinePlacementPreview, .01f, Parameters);
}

void UShopPlayerInteractionComponent::UpdateMachinePlacementPreview()
{
	const auto* Controller = GetMachinePlacementController();
	const auto* Pawn = Controller ? Controller->GetPawn().Get() : nullptr;
	const auto* Machine = LocalPlacementMachine.Get();
	if (!Pawn || !Machine || !IsValid(MachinePlacementPreview))
	{
		CancelMachinePlacementInput();
		return;
	}
	if (bMachinePlacementRotating)
	{
		if (Controller->IsInputKeyDown(EKeys::RightMouseButton))
		{
			float MouseX = 0, MouseY = 0;
			Controller->GetInputMouseDelta(MouseX, MouseY);
			MachinePlacementYaw = FMath::UnwindDegrees(MachinePlacementYaw + MouseX * DegreesPerMouseUnit);
		}
		else StopMachinePlacementRotation();
	}
	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	FTransform Pose = Machine->GetActorTransform();
	const bool bCanPlace = ShopMachinePlacement::TracePlacement(*Machine, *Pawn, Eye, View.Vector(), MachinePlacementYaw, Pose);
	MachinePlacementPreview->SetPlacementPreview(Pose, bCanPlace);
}

void UShopPlayerInteractionComponent::CommitMachinePlacementInput()
{
	if (bMachinePlacementCommitPending || !IsValid(MachinePlacementPreview)) return;
	const auto* Controller = GetMachinePlacementController();
	const auto* Pawn = Controller ? Controller->GetPawn().Get() : nullptr;
	auto* Machine = LocalPlacementMachine.Get();
	if (!Pawn || !Machine) return;
	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	FTransform Pose = Machine->GetActorTransform();
	if (!ShopMachinePlacement::TracePlacement(*Machine, *Pawn, Eye, View.Vector(), MachinePlacementYaw, Pose))
	{
		MachinePlacementPreview->SetPlacementPreview(Pose, false);
		return;
	}
	bMachinePlacementCommitPending = true;
	Server_FinishMachinePlacement(Machine, LocalPlacementSequence, Pose);
}

void UShopPlayerInteractionComponent::CancelMachinePlacementInput()
{
	ClearMachinePlacementHold();
	if (auto* Machine = LocalPlacementMachine.Get()) Server_CancelMachinePlacement(Machine, LocalPlacementSequence);
	CloseMachinePlacementPreview();
}

void UShopPlayerInteractionComponent::StartMachinePlacementRotation()
{
	if (!bMachinePlacementRotating && MachinePlacementPreview && MachinePlacementInput)
		if (auto* Controller = Cast<APlayerController>(MachinePlacementInput->GetOwner()))
		{
			Controller->SetIgnoreLookInput(true);
			bMachinePlacementRotating = true;
		}
}

void UShopPlayerInteractionComponent::StopMachinePlacementRotation()
{
	if (!bMachinePlacementRotating) return;
	bMachinePlacementRotating = false;
	if (auto* Controller = MachinePlacementInput ? Cast<APlayerController>(MachinePlacementInput->GetOwner()) : nullptr)
		Controller->SetIgnoreLookInput(false);
}

bool UShopPlayerInteractionComponent::BindMachinePlacementInput(APlayerController& Controller)
{
	GetWorld()->GetTimerManager().ClearTimer(MachinePlacementInputReleaseTimer);
	if (MachinePlacementInput && MachinePlacementInput->GetOwner() == &Controller) return true;
	UnbindMachinePlacementInput();
	auto* EnhancedInput = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller.GetLocalPlayer());
	if (!EnhancedInput) return false;
	constexpr int32 PlacementInputPriority = 110;
	MachinePlacementInput = NewObject<UEnhancedInputComponent>(&Controller);
	MachinePlacementInput->Priority = PlacementInputPriority;
	MachinePlacementInput->RegisterComponent();
	MachinePlacementMapping = NewObject<UInputMappingContext>(this);
	auto* Place = NewObject<UInputAction>(MachinePlacementMapping);
	// Consume the lower-priority Attack mapping while the raw click below controls placement.
	MachinePlacementMapping->MapKey(Place, EKeys::LeftMouseButton);
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	EnhancedInput->AddMappingContext(MachinePlacementMapping, PlacementInputPriority, Options);
	UInputComponent& Input = *MachinePlacementInput;
	const TSharedRef<bool> ClickHeld = MakeShared<bool>(Controller.IsInputKeyDown(EKeys::LeftMouseButton));
	FInputKeyBinding Press(FInputChord(EKeys::LeftMouseButton), IE_Pressed);
	Press.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, ClickHeld]
	{
		if (*ClickHeld) return;
		*ClickHeld = true;
		CommitMachinePlacementInput();
	});
	Input.KeyBindings.Add(MoveTemp(Press));
	FInputKeyBinding Release(FInputChord(EKeys::LeftMouseButton), IE_Released);
	Release.KeyDelegate.GetDelegateForManualSet().BindLambda([ClickHeld] { *ClickHeld = false; });
	Input.KeyBindings.Add(MoveTemp(Release));
	Input.BindKey(EKeys::RightMouseButton, IE_Pressed, this, &UShopPlayerInteractionComponent::StartMachinePlacementRotation);
	Input.BindKey(EKeys::RightMouseButton, IE_Released, this, &UShopPlayerInteractionComponent::StopMachinePlacementRotation);
	Input.BindKey(EKeys::Escape, IE_Pressed, this, &UShopPlayerInteractionComponent::CancelMachinePlacementInput);
	Controller.PushInputComponent(MachinePlacementInput);
	return true;
}

void UShopPlayerInteractionComponent::CloseMachinePlacementPreview()
{
	LocalPlacementMachine.Reset();
	bMachinePlacementCommitPending = false;
	if (IsValid(MachinePlacementPreview)) MachinePlacementPreview->Destroy();
	MachinePlacementPreview = nullptr;
	StopMachinePlacementRotation();
	if (GetWorld())
	{
		auto& Timers = GetWorld()->GetTimerManager();
		Timers.ClearTimer(MachinePlacementPreviewTimer);
		// The attack action fires on release. Keep its suppression until that
		// release has passed through the input stack, even when a click placed the machine.
		if (MachinePlacementInput)
			Timers.SetTimer(MachinePlacementInputReleaseTimer, this, &UShopPlayerInteractionComponent::ReleaseMachinePlacementInputWhenReady, .05f, true);
	}
}

void UShopPlayerInteractionComponent::ReleaseMachinePlacementInputWhenReady()
{
	const auto* Controller = MachinePlacementInput ? Cast<APlayerController>(MachinePlacementInput->GetOwner()) : nullptr;
	if (!MachinePlacementPreview && (!Controller || !Controller->IsInputKeyDown(EKeys::LeftMouseButton))) UnbindMachinePlacementInput();
}

void UShopPlayerInteractionComponent::UnbindMachinePlacementInput()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MachinePlacementInputReleaseTimer);
	StopMachinePlacementRotation();
	if (!MachinePlacementInput) return;
	if (auto* Controller = Cast<APlayerController>(MachinePlacementInput->GetOwner()))
	{
		if (auto* EnhancedInput = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()))
			EnhancedInput->RemoveMappingContext(MachinePlacementMapping);
		Controller->PopInputComponent(MachinePlacementInput);
	}
	MachinePlacementMapping = nullptr;
	MachinePlacementInput->DestroyComponent();
	MachinePlacementInput = nullptr;
}

void UShopPlayerInteractionComponent::CleanupMachinePlacement()
{
	CancelMachinePlacementInput();
	bMachinePlacementKeyDown = false;
	UnbindMachinePlacementInput();
	if (auto* Machine = ServerPlacementMachine.Get())
		if (auto* Pawn = ServerPlacementPawn.Get(); Pawn && Pawn->HasAuthority()) CancelPlacement(*Machine, *Pawn);
	ServerPlacementMachine.Reset();
	ServerPlacementPawn.Reset();
}
