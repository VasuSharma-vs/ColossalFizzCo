#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Slate/SceneViewport.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DynamicBlueprintBinding.h"
#include "Components/PropGrabInputComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/CableMovementConstraintComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/PlayerUpgradesComponent.h"
#include "Resources/HoseCableActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "../Shop/WorldInteractionCarry.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropGrabGestureTest, "ColossalFizzCo.Props.GrabGesture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropGrabGestureTest::RunTest(const FString& Parameters)
{
	using Phase = EPropGrabInputPhase; using Intent = EPropGrabIntent;
	FPropGrabGesture Gesture;
	auto Step = [&](Phase P, double Time, uint64 Token) { return Gesture.Process(P, Time, Token, .4, .4); };
	TestEqual(TEXT("Press alone does not place"), Step(Phase::Started, 0, 0), Intent::None);
	TestEqual(TEXT("Short empty press does not grab early"), Step(Phase::Ongoing, .2, 0), Intent::None);
	TestEqual(TEXT("Holding G requests one pickup"), Step(Phase::Ongoing, .41, 0), Intent::Grab);
	TestEqual(TEXT("Same press cannot place its newly grabbed bottle"), Step(Phase::Ongoing, .6, 101), Intent::None);
	TestEqual(TEXT("Hold-and-release trigger cannot pick up again"), Step(Phase::Triggered, .7, 101), Intent::None);
	TestEqual(TEXT("Completed after Triggered is consumed"), Step(Phase::Completed, .71, 101), Intent::None);
	Step(Phase::Started, 1, 101);
	TestEqual(TEXT("Holding an existing bottle requests trace placement"), Step(Phase::Ongoing, 1.41, 101), Intent::Place);
	TestEqual(TEXT("Placement is emitted only once"), Step(Phase::Ongoing, 1.6, 0), Intent::None);
	TestEqual(TEXT("Release after placement does not regrab"), Step(Phase::Triggered, 1.7, 0), Intent::None);
	Step(Phase::Started, 2, 101);
	TestEqual(TEXT("One short G tap keeps bottle held"), Step(Phase::Canceled, 2.05, 101), Intent::None);
	Step(Phase::Started, 2.15, 101);
	TestEqual(TEXT("Second short G tap drops the same bottle"), Step(Phase::Canceled, 2.2, 101), Intent::Drop);
	TestEqual(TEXT("Repeated end event cannot drop twice"), Step(Phase::Completed, 2.21, 101), Intent::None);
	Step(Phase::Started, 3, 101); Step(Phase::Canceled, 3.05, 101);
	Step(Phase::Started, 3.15, 102);
	TestEqual(TEXT("Old tap cannot drop another logical bottle"), Step(Phase::Canceled, 3.2, 102), Intent::None);
	Step(Phase::Started, 4, 101);
	TestEqual(TEXT("Missed Ongoing frame still recognizes hold on release"), Step(Phase::Triggered, 4.5, 101), Intent::Place);
	return true;
}

namespace PropGrabTests
{
	struct FWorldOwner
	{
		UWorld* World;
		UGameInstance* GameInstance;
		explicit FWorldOwner(UWorld* InWorld, UGameInstance* InGameInstance = nullptr) : World(InWorld), GameInstance(InGameInstance) {}
		~FWorldOwner()
		{
			if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			if (GameInstance) GameInstance->Shutdown();
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
	};
	class FGravityCheck : public IAutomationLatentCommand
	{
		FAutomationTestBase* Test;
		TSharedPtr<FWorldOwner> Scope;
		TWeakObjectPtr<AActivePropActor> Bottle;
		double BeforeZ;
		int32 Frames = 0;
	public:
		FGravityCheck(FAutomationTestBase* InTest, TSharedPtr<FWorldOwner> InScope, AActivePropActor* InBottle)
			: Test(InTest), Scope(InScope), Bottle(InBottle), BeforeZ(InBottle->GetActorLocation().Z) {}
		bool Update() override
		{
			// Physics scheduling uses engine frame numbers. Repeated synchronous Tick calls are not distinct frames.
			Scope->World->Tick(LEVELTICK_All, 1.f/60.f);
			if (++Frames < 12) return false;
			if (Test->TestTrue(TEXT("Released bottle remains valid during fall"), Bottle.IsValid()))
			{
				Test->AddInfo(FString::Printf(TEXT("Drop Z %.2f -> %.2f; velocity %s; world gravity %.1f; root is mesh %d"), BeforeZ, Bottle->GetActorLocation().Z,
					*Bottle->PropMesh->GetPhysicsLinearVelocity().ToString(), Scope->World->GetGravityZ(), Bottle->GetRootComponent() == Bottle->PropMesh));
				Test->TestTrue(TEXT("Released bottle actually falls under gravity"), Bottle->GetActorLocation().Z < BeforeZ - 1);
			}
			return true;
		}
	};
	bool Attach(AActivePropActor* Prop, UPrimitiveComponent* Parent)
	{
		UFunction* Function = Prop->FindFunction(TEXT("Request Attach to Component"));
		if (!Function) return false;
		FStructOnScope Params(Function);
		auto* Component = FindFProperty<FObjectPropertyBase>(Function, TEXT("Component"));
		if (!Component) return false;
		Component->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Parent);
		Prop->ProcessEvent(Function, Params.GetStructMemory());
		for (TFieldIterator<FBoolProperty> It(Function); It; ++It)
			if (It->HasAnyPropertyFlags(CPF_OutParm)) return It->GetPropertyValue_InContainer(Params.GetStructMemory());
		return false;
	}
	bool Detach(AActivePropActor* Prop)
	{
		UFunction* Function = Prop->FindFunction(TEXT("Request Detach from Component"));
		if (!Function) return false;
		FStructOnScope Params(Function); Prop->ProcessEvent(Function, Params.GetStructMemory());
		for (TFieldIterator<FBoolProperty> It(Function); It; ++It)
			if (It->HasAnyPropertyFlags(CPF_OutParm)) return It->GetPropertyValue_InContainer(Params.GetStructMemory());
		return false;
	}
	bool Move(AActivePropActor* Prop, FVector Location)
	{
		UFunction* Function = Prop->FindFunction(TEXT("Move Actor To Location"));
		if (!Function) return false;
		FStructOnScope Params(Function);
		auto* Value = FindFProperty<FStructProperty>(Function, TEXT("Location"));
		if (!Value || Value->Struct != TBaseStructure<FVector>::Get()) return false;
		*Value->ContainerPtrToValuePtr<FVector>(Params.GetStructMemory()) = Location;
		Prop->ProcessEvent(Function, Params.GetStructMemory());
		return Prop->GetActorLocation().Equals(Location, .01);
	}
	bool FireActionPhase(ACharacter* Player, const FString& ActionName, const FString& Phase)
	{
		// Dispatch the exact compiled callback Enhanced Input binds, without desktop input or a viewport.
		UClass* BindingClass = LoadClass<UDynamicBlueprintBinding>(nullptr, TEXT("/Script/EnhancedInput.EnhancedInputActionDelegateBinding"));
		auto* Binding = BindingClass ? UBlueprintGeneratedClass::GetDynamicBindingObject(Player->GetClass(), BindingClass) : nullptr;
		if (!Binding) return false;
		auto* Bindings = FindFProperty<FArrayProperty>(Binding->GetClass(), TEXT("InputActionDelegateBindings"));
		auto* Row = Bindings ? CastField<FStructProperty>(Bindings->Inner) : nullptr;
		if (!Row) return false;
		auto* ActionField = FindFProperty<FObjectPropertyBase>(Row->Struct, TEXT("InputAction"));
		auto* PhaseField = FindFProperty<FEnumProperty>(Row->Struct, TEXT("TriggerEvent"));
		auto* FunctionField = FindFProperty<FNameProperty>(Row->Struct, TEXT("FunctionNameToBind"));
		if (!ActionField || !PhaseField || !FunctionField) return false;
		FScriptArrayHelper Array(Bindings, Bindings->ContainerPtrToValuePtr<void>(Binding));
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			void* Data = Array.GetRawPtr(Index);
			UObject* Action = ActionField->GetObjectPropertyValue_InContainer(Data);
			const int64 Value = PhaseField->GetUnderlyingProperty()->GetSignedIntPropertyValue(PhaseField->ContainerPtrToValuePtr<void>(Data));
			if (!Action || Action->GetName() != ActionName || PhaseField->GetEnum()->GetNameStringByValue(Value) != Phase) continue;
			UFunction* Function = Player->FindFunction(FunctionField->GetPropertyValue_InContainer(Data));
			if (!Function) return false;
			FStructOnScope Params(Function);
			if (auto* Source = FindFProperty<FObjectPropertyBase>(Function, TEXT("SourceAction"))) Source->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Action);
			Player->ProcessEvent(Function, Params.GetStructMemory());
			return true;
		}
		return false;
	}
	bool FireGrabPhase(ACharacter* Player, const FString& Phase)
	{
		return FireActionPhase(Player,TEXT("IA_Grab"),Phase);
	}
	bool InvokeCarryRPC(ACharacter* Player, const FString& Name, AActor* Target)
	{
		for (TFieldIterator<UFunction> It(Player->GetClass()); It; ++It)
		{
			if (!It->GetName().Replace(TEXT(" "), TEXT("")).Equals(Name, ESearchCase::IgnoreCase)) continue;
			if (!It->HasAnyFunctionFlags(FUNC_NetServer)) return false;
			auto* Parameter = FindFProperty<FObjectPropertyBase>(*It, TEXT("Target"));
			if (!Parameter) return false;
			FStructOnScope Params(*It);
			Parameter->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Target);
			Player->ProcessEvent(*It, Params.GetStructMemory());
			return true;
		}
		return false;
	}
	bool GetCameraHit(ACharacter* Player, FHitResult& Hit)
	{
		for (TFieldIterator<UFunction> It(Player->GetClass()); It; ++It)
		{
			if (!It->GetName().Replace(TEXT(" "), TEXT("")).Equals(TEXT("PlayerHitLineTrace"), ESearchCase::IgnoreCase)) continue;
			FStructOnScope Params(*It);
			Player->ProcessEvent(*It, Params.GetStructMemory());
			for (TFieldIterator<FStructProperty> Value(*It); Value; ++Value)
				if (Value->HasAnyPropertyFlags(CPF_OutParm) && Value->Struct == FHitResult::StaticStruct())
				{
					Hit = *Value->ContainerPtrToValuePtr<FHitResult>(Params.GetStructMemory());
					return true;
				}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleGrabAdapterTest, "ColossalFizzCo.Props.AuthoredBottleGrabRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleGrabAdapterTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto WorldScope = MakeShared<PropGrabTests::FWorldOwner>(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave = false; Manager->InitializePropManager();
	auto* Product = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_BasicCola.DA_BasicCola"));
	if (!TestNotNull(TEXT("Production bottle definition"), Product)) return false;
	const int32 PID = Manager->RegisterProductFromDataAsset(Product);
	const int32 ID = Manager->GenerateProductInstance(PID, FTransform(FRotator::ZeroRotator, FVector(100,0,250), FVector(.15)));
	AActivePropActor* Bottle = nullptr;
	if (!TestTrue(TEXT("Production bottle promotes"), Manager->PromoteInstanceToASM(ID, EPropActivationReason::Interaction, false, Bottle)) || !Bottle) return false;
	TestEqual(TEXT("Actual production Blueprint used"), Bottle->GetClass()->GetPathName(), FString(TEXT("/Game/Props/Colas/BP_Cola.BP_Cola_C")));
	for (TFieldIterator<UFunction> It(Bottle->GetClass()); It; ++It)
		if (It->GetName().Contains(TEXT("Request")) || It->GetName().Contains(TEXT("Move Actor")))
		{
			FString Signature = It->GetName();
			for (TFieldIterator<FProperty> Param(*It); Param; ++Param) if (Param->HasAnyPropertyFlags(CPF_Parm)) Signature += TEXT(" | ") + Param->GetName() + TEXT(":") + Param->GetClass()->GetName();
			AddInfo(Signature);
		}
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	auto* Pawn = World->SpawnActor<ACharacter>(); Pawn->SetActorLocation(FVector(0,0,100));
	auto* Input = NewObject<UPropGrabInputComponent>(Pawn); Pawn->AddInstanceComponent(Input); Input->RegisterComponent();
	TestTrue(TEXT("Owned RPC may grab an available bottle"), Input->CanGrabTarget(Bottle));
	TestTrue(TEXT("Production attach interface succeeds"), PropGrabTests::Attach(Bottle, Pawn->GetMesh()));
	TestTrue(TEXT("Attach records exclusive holder in native registry"), Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.Get() == Pawn);
	TestTrue(TEXT("Owned RPC recognizes the real holder"), Input->OwnsHeldTarget(Bottle));
	auto* OtherPawn = World->SpawnActor<ACharacter>();
	auto* OtherInput = NewObject<UPropGrabInputComponent>(OtherPawn); OtherPawn->AddInstanceComponent(OtherInput); OtherInput->RegisterComponent();
	TestFalse(TEXT("Another player's RPC cannot release the bottle"), OtherInput->OwnsHeldTarget(Bottle));
	TestFalse(TEXT("Another player cannot steal the held bottle"), OtherInput->CanGrabTarget(Bottle));
	TestTrue(TEXT("Holding Actor RepNotify may safely repeat attach"), PropGrabTests::Attach(Bottle, Pawn->GetMesh()));
	TestFalse(TEXT("Held bottle has physics disabled"), Bottle->PropMesh->IsSimulatingPhysics());
	Bottle->PropMesh->IgnoreActorWhenMoving(Pawn,true);
	Bottle->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Production detach interface succeeds"), PropGrabTests::Detach(Bottle));
	TestFalse(TEXT("Release callback retains holder long enough to clear collision ignores"),Bottle->PropMesh->GetMoveIgnoreActors().Contains(Pawn));
	TestEqual(TEXT("Release restores collision suppressed by a customer pocket"),Bottle->PropMesh->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
	TestNull(TEXT("Actor is detached, not just a mesh subcomponent"), Bottle->GetAttachParentActor());
	TestFalse(TEXT("Drop clears held state"), Bottle->bIsHeld);
	TestTrue(TEXT("Drop enables actual bottle physics"), Bottle->PropMesh->IsSimulatingPhysics());
	TestTrue(TEXT("Dropped bottle retains gravity"), Bottle->PropMesh->IsGravityEnabled());
	TestTrue(TEXT("Drop releases native ownership"), !Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.IsValid());
	TestTrue(TEXT("Bottle can be grabbed after release"), PropGrabTests::Attach(Bottle, Pawn->GetMesh()));
	const FVector Scale = Bottle->GetActorScale3D();
	const FVector Destination(180,50,250);
	TestFalse(TEXT("Held placement cannot teleport outside interaction reach"), Bottle->MoveHeldToLocation(FVector(10000,0,250)));
	auto* Wall = World->SpawnActor<AActor>();
	auto* WallBox = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(WallBox); WallBox->SetBoxExtent(FVector(10,100,200));
	WallBox->SetCollisionProfileName(TEXT("BlockAll")); WallBox->RegisterComponent(); Wall->SetActorLocation(FVector(90,25,200));
	TestFalse(TEXT("Held placement cannot pass through a blocking wall"), Bottle->MoveHeldToLocation(Destination));
	Wall->Destroy();
	TestTrue(TEXT("Production move interface reaches trace location"), PropGrabTests::Move(Bottle, Destination));
	TestTrue(TEXT("Moving preserves scale"), Bottle->GetActorScale3D().Equals(Scale));
	TestTrue(TEXT("Moving updates the same ISID transform"), Manager->GetTransform(ID).GetLocation().Equals(Destination, .01));
	TestFalse(TEXT("Non-finite placement is rejected"), Bottle->MoveHeldToLocation(FVector(std::numeric_limits<double>::infinity(),0,0)));
	TestTrue(TEXT("Placement releases through production interface"), PropGrabTests::Detach(Bottle));
	TestFalse(TEXT("Released bottle cannot be moved by stale held placement"), Bottle->MoveHeldToLocation(Destination));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new PropGrabTests::FGravityCheck(this, WorldScope, Bottle)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGrabBlueprintInputTest, "ColossalFizzCo.Props.AuthoredGrabInputRouting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGrabBlueprintInputTest::RunTest(const FString& Parameters)
{
	// A windowless viewport gives the real Blueprint's screen-center deprojection valid projection data.
	// No Slate window is created or activated and no desktop input is injected.
	auto* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* World = GameInstance->GetWorld();
	if (!World) return false;
	auto WorldScope = MakeShared<PropGrabTests::FWorldOwner>(World, GameInstance);
	auto* ViewportClient = NewObject<UGameViewportClient>(GEngine);
	auto& Context = *GameInstance->GetWorldContext();
	Context.GameViewport = ViewportClient;
	ViewportClient->Init(Context, GameInstance, false);
	auto Viewport = FSceneViewport::Create(TStrongPtrVariant<FViewportClient>(ViewportClient), TSharedPtr<SViewport>());
	Viewport->SetInitialSize(FIntPoint(800, 600));
	ViewportClient->Viewport = &Viewport.Get();
	ON_SCOPE_EXIT { ViewportClient->Viewport = nullptr; Context.GameViewport = nullptr; };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave = false; Manager->InitializePropManager();
	auto* Controller = World->SpawnActor<APlayerController>();
	UClass* PlayerClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Production player Blueprint"), PlayerClass)) return false;
	// Input routing is exercised in isolation from the player's HUD/viewport BeginPlay setup.
	World->SetBegunPlay(false);
	auto* Player = World->SpawnActor<ACharacter>(PlayerClass, FVector(0,0,100), FRotator::ZeroRotator);
	World->SetBegunPlay(true); Controller->Possess(Player);
	Player->GetCharacterMovement()->DisableMovement(); // Empty fixture has no ground; keep the camera stationary.
	auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	LocalPlayer->ViewportClient = ViewportClient;
	LocalPlayer->PlayerController = Controller; Controller->Player = LocalPlayer;
	LocalPlayer->Size = FVector2D(1,1);
	Controller->SetViewTarget(Player); Controller->UpdateCameraManager(0.f);
	FVector RayOrigin, RayDirection;
	if (!TestTrue(TEXT("Windowless player has real screen-center deprojection"), Controller->DeprojectScreenPositionToWorld(400,300,RayOrigin,RayDirection))) return false;
	TestTrue(TEXT("Screen-center direction is normalized and nonzero"), RayDirection.IsNormalized());
	auto* Input = Player->FindComponentByClass<UPropGrabInputComponent>();
	if (!TestNotNull(TEXT("Saved player Blueprint contains native grab component"), Input)) return false;
	// The fixture skips the character's HUD BeginPlay. Register the native carry tick as the actor would.
	if (!Input->HasBegunPlay())
	{
		Input->RegisterAllComponentTickFunctions(true);
		Input->BeginPlay();
	}
	auto* Product = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_BasicCola.DA_BasicCola"));
	if (!Product) return false;
	const int32 PID = Manager->RegisterProductFromDataAsset(Product);
	const int32 ID = Manager->GenerateProductInstance(PID, FTransform(FRotator::ZeroRotator, FVector(100,0,200), FVector(.15)));
	AActivePropActor* Bottle = nullptr;
	if (!Manager->PromoteInstanceToASM(ID, EPropActivationReason::Interaction, false, Bottle) || !Bottle) return false;
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	auto* HoldingActor = FindFProperty<FObjectPropertyBase>(PlayerClass, TEXT("Holding Actor"));
	auto* HoldingState = FindFProperty<FByteProperty>(PlayerClass, TEXT("Holding State"));
	if (!TestNotNull(TEXT("Player carry compatibility reference"), HoldingActor) || !TestNotNull(TEXT("Player carry compatibility state"), HoldingState)) return false;
	auto* OtherPlayer = World->SpawnActor<ACharacter>();
	if (!TestTrue(TEXT("Other player exclusively holds bottle"), Bottle->AttachForInteraction(OtherPlayer->GetMesh()))) return false;
	TestTrue(TEXT("Saved server pickup RPC exists"), PropGrabTests::InvokeCarryRPC(Player, TEXT("RequestServertoAttachActortoComponent"), Bottle));
	TestTrue(TEXT("Actual pickup RPC preserves other player's ownership"), Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.Get() == OtherPlayer);
	TestNull(TEXT("Rejected pickup RPC does not retain a false held reference"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	TestTrue(TEXT("Saved server release RPC exists"), PropGrabTests::InvokeCarryRPC(Player, TEXT("RequestServertoDetachActortoComponent"), Bottle));
	TestTrue(TEXT("Actual release RPC cannot detach another player's bottle"), Bottle->bIsHeld && Bottle->GetAttachParentActor() == OtherPlayer);
	Bottle->DetachForInteraction();
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	if (!TestTrue(TEXT("Native lifecycle prepares a held bottle"), Bottle->AttachForInteraction(Player->GetMesh(), TEXT("Holding object")))) return false;
	HoldingActor->SetObjectPropertyValue_InContainer(Player, Bottle); HoldingState->SetPropertyValue_InContainer(Player, 1);
	TestTrue(TEXT("Owner's saved release RPC executes"), PropGrabTests::InvokeCarryRPC(Player, TEXT("RequestServertoDetachActortoComponent"), Bottle));
	TestFalse(TEXT("Owner's saved release RPC actually releases"), Bottle->bIsHeld);
	TestTrue(TEXT("Owner's saved release RPC restores physics"), Bottle->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Owner's saved release RPC clears carry reference"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	if (!TestTrue(TEXT("Regrab after owner's server release"), Bottle->AttachForInteraction(Player->GetMesh(), TEXT("Holding object")))) return false;
	HoldingActor->SetObjectPropertyValue_InContainer(Player, Bottle); HoldingState->SetPropertyValue_InContainer(Player, 1);
	TestTrue(TEXT("Actual compiled G Started callback exists"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .05f);
	TestTrue(TEXT("Actual compiled G Canceled callback exists"), PropGrabTests::FireGrabPhase(Player, TEXT("Canceled")));
	TestTrue(TEXT("First tap leaves bottle held"), Bottle->bIsHeld);
	World->Tick(LEVELTICK_All, .1f);
	TestTrue(TEXT("Second G press reaches saved native input adapter"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .05f);
	TestTrue(TEXT("Second G release callback executes"), PropGrabTests::FireGrabPhase(Player, TEXT("Canceled")));
	TestEqual(TEXT("Compiled Blueprint reads the native double-tap intent"), Input->LastIntent, EPropGrabIntent::Drop);
	TestFalse(TEXT("Actual GG callback releases the bottle"), Bottle->bIsHeld);
	TestTrue(TEXT("Actual GG callback restores physics"), Bottle->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Actual GG callback clears legacy held actor"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	TestEqual(TEXT("Actual GG callback clears legacy held state"), HoldingState->GetPropertyValue_InContainer(Player), uint8(0));
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	if (!TestTrue(TEXT("Bottle can be held for placement"), Bottle->AttachForInteraction(Player->GetMesh(), TEXT("Holding object")))) return false;
	HoldingActor->SetObjectPropertyValue_InContainer(Player, Bottle); HoldingState->SetPropertyValue_InContainer(Player, 1);
	TestTrue(TEXT("Hold placement reaches actual input Started callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .45f);
	FVector AimOrigin;
	FRotator AimRotation;
	Controller->GetPlayerViewPoint(AimOrigin, AimRotation);
	const FVector ExpectedLocation = AimOrigin + AimRotation.Vector() * 250;
	const FVector HandStart = Bottle->GetActorLocation();
	TestTrue(TEXT("Hold placement reaches actual input Ongoing callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Ongoing")));
	TestEqual(TEXT("Native placement consumes the input before Blueprint's immediate drop"), Input->LastIntent, EPropGrabIntent::None);
	TestTrue(TEXT("Placement starts from the hand without teleporting"), Bottle->GetActorLocation().Equals(HandStart, .01)
		&& Bottle->bIsHeld && !Bottle->GetAttachParentActor());
	Input->TickComponent(.10f, LEVELTICK_All, nullptr);
	const FVector PlacementInFlight = Bottle->GetActorLocation();
	TestTrue(TEXT("The held bottle moves toward the camera point while G stays down"), !PlacementInFlight.Equals(HandStart, .01)
		&& FVector::DistSquared(PlacementInFlight, ExpectedLocation) < FVector::DistSquared(HandStart, ExpectedLocation));
	TestTrue(TEXT("Hold release reaches actual Triggered callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Triggered")));
	TestTrue(TEXT("Hold completion reaches actual Completed callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Completed")));
	TestTrue(TEXT("Early placement release drops from its current position"), Bottle->GetActorLocation().Equals(PlacementInFlight, .01)
		&& !Bottle->bIsHeld && Bottle->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Placement clears the held actor"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	TestEqual(TEXT("Hold release emits no second interaction"), Input->LastIntent, EPropGrabIntent::None);
	if (!TestTrue(TEXT("Pickup ray has valid projection data"), Controller->DeprojectScreenPositionToWorld(400,300,RayOrigin,RayDirection))) return false;
	Bottle->PropMesh->SetSimulatePhysics(false);
	const FVector BoundsOffset = Bottle->PropMesh->Bounds.Origin - Bottle->GetActorLocation();
	Bottle->SetActorLocation(RayOrigin + RayDirection * 80 - BoundsOffset, false, nullptr, ETeleportType::TeleportPhysics);
	Manager->FindISIDData(ID)->ActiveTransform = Bottle->GetActorTransform();
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	FHitResult PickupHit;
	if (!TestTrue(TEXT("Production pickup ray query executes"), PropGrabTests::GetCameraHit(Player, PickupHit))) return false;
	AddInfo(FString::Printf(TEXT("Pickup ray hit %s at %s; target bounds %s extent %s"), *GetNameSafe(PickupHit.GetActor()), *PickupHit.ImpactPoint.ToString(), *Bottle->PropMesh->Bounds.Origin.ToString(), *Bottle->PropMesh->Bounds.BoxExtent.ToString()));
	if (!TestTrue(TEXT("Actual pickup ray hits the production bottle"), PickupHit.GetActor() == Bottle)) return false;
	// The isolated input fixture omits character BeginPlay/Tick; seed its existing tick cache from the real query.
	auto* TickHit = FindFProperty<FStructProperty>(PlayerClass, TEXT("Hit Result On tick"));
	if (!TestTrue(TEXT("Player trace compatibility cache exists"), TickHit && TickHit->Struct == FHitResult::StaticStruct())) return false;
	// Start off target, then aim at the bottle during the hold. A press-start cached trace must not win.
	*TickHit->ContainerPtrToValuePtr<FHitResult>(Player) = FHitResult();
	TestTrue(TEXT("Pickup reaches actual G Started callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .45f);
	*TickHit->ContainerPtrToValuePtr<FHitResult>(Player) = PickupHit;
	const FVector PickupStart = Bottle->GetActorLocation();
	TestTrue(TEXT("Pickup reaches actual G Ongoing callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Ongoing")));
	TestEqual(TEXT("Native pickup consumes the input before Blueprint's immediate attach"), Input->LastIntent, EPropGrabIntent::None);
	TestTrue(TEXT("Pickup claims the bottle without immediately attaching"), Bottle->GetActorLocation().Equals(PickupStart,.01)
		&& Bottle->bIsHeld && !Bottle->GetAttachParentActor() && !HoldingActor->GetObjectPropertyValue_InContainer(Player));
	TestFalse(TEXT("Another player cannot grab the moving bottle"), Manager->ValidateGrab(ID, OtherPlayer));
	// Manually step carry motion because these gestures run inside one engine frame.
	Input->TickComponent(.05f, LEVELTICK_All, nullptr);
	const FVector PickupInFlight = Bottle->GetActorLocation();
	const FName Grip(TEXT("Holding object"));
	const FVector Hand = Player->GetMesh()->GetSocketLocation(Grip);
	TestTrue(TEXT("Bottle moves partway to the hand while G is held"), !PickupInFlight.Equals(PickupStart,.01)
		&& FVector::DistSquared(PickupInFlight,Hand) < FVector::DistSquared(PickupStart,Hand) && !Bottle->GetAttachParentActor());
	TestTrue(TEXT("Cancel releases the actual G pull"), PropGrabTests::FireGrabPhase(Player, TEXT("Canceled")));
	TestTrue(TEXT("Release before reaching the hand drops from mid-travel"), Bottle->GetActorLocation().Equals(PickupInFlight,.01)
		&& !Bottle->bIsHeld && Bottle->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Canceled pickup leaves the hand empty"), HoldingActor->GetObjectPropertyValue_InContainer(Player));

	Bottle->PropMesh->SetSimulatePhysics(false);
	Bottle->SetActorLocation(PickupStart, false, nullptr, ETeleportType::TeleportPhysics);
	Manager->FindISIDData(ID)->ActiveTransform = Bottle->GetActorTransform();
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	*TickHit->ContainerPtrToValuePtr<FHitResult>(Player) = PickupHit;
	PropGrabTests::FireGrabPhase(Player, TEXT("Started"));
	World->Tick(LEVELTICK_All, .45f);
	PropGrabTests::FireGrabPhase(Player, TEXT("Ongoing"));
	Player->AddActorWorldOffset(FVector(0, 15, 0));
	for (int32 Step = 0; Step < 16; ++Step) Input->TickComponent(.05f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("The moving pickup attaches to the player's updated hand socket once it arrives"),
		HoldingActor->GetObjectPropertyValue_InContainer(Player) == Bottle && Bottle->bIsHeld
		&& Bottle->GetAttachParentActor() == Player && Bottle->GetActorLocation().Equals(Player->GetMesh()->GetSocketLocation(Grip),1));
	TestTrue(TEXT("Completed pickup retains native ownership"), Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.Get() == Player);
	TestTrue(TEXT("Pickup release reaches actual Triggered callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Triggered")));
	TestTrue(TEXT("Pickup completion reaches actual Completed callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Completed")));
	TestTrue(TEXT("Same press cannot place its newly picked up bottle"), Bottle->bIsHeld);

	PropGrabTests::FireGrabPhase(Player, TEXT("Started"));
	World->Tick(LEVELTICK_All, .45f);
	PropGrabTests::FireGrabPhase(Player, TEXT("Ongoing"));
	Controller->SetControlRotation(FRotator(0, 40, 0));
	Controller->UpdateCameraManager(0);
	Controller->GetPlayerViewPoint(AimOrigin, AimRotation);
	const FVector SteeredLocation = AimOrigin + AimRotation.Vector() * 250;
	for (int32 Step = 0; Step < 24; ++Step) Input->TickComponent(.05f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("An item moved to the new aim point waits there until G release"), Bottle->bIsHeld
		&& Bottle->GetActorLocation().Equals(SteeredLocation,2));
	PropGrabTests::FireGrabPhase(Player, TEXT("Completed"));
	TestTrue(TEXT("Release after reaching the aim point drops the item there"), !Bottle->bIsHeld
		&& Bottle->PropMesh->IsSimulatingPhysics() && Bottle->GetActorLocation().Equals(SteeredLocation,2));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerCarryBatchTest, "ColossalFizzCo.PlayerUpgrades.CarryBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlayerCarryBatchTest::RunTest(const FString&)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	const PropGrabTests::FWorldOwner WorldScope(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();

	auto* Manager = World->SpawnActor<APropManager>();
	if (!Manager) return false;
	Manager->bEnableAutosave = false;
	Manager->InitializePropManager();
	auto* PlayerClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!PlayerClass) return false;
	World->SetBegunPlay(false);
	auto* Player = World->SpawnActor<ACharacter>(PlayerClass, FVector(0, 0, 100), FRotator::ZeroRotator);
	World->SetBegunPlay(true);
	auto* Input = Player ? Player->FindComponentByClass<UPropGrabInputComponent>() : nullptr;
	if (!Input || !Player->GetMesh()) return false;
	auto* Profile = World->SpawnActor<APlayerState>();
	if (!Profile) return false;
	Player->SetPlayerState(Profile);
	auto* Upgrades = NewObject<UPlayerUpgradesComponent>(Profile);
	Profile->AddInstanceComponent(Upgrades);
	Upgrades->RegisterComponent();
	const auto* Ranks = FindFProperty<FStructProperty>(Upgrades->GetClass(), TEXT("UpgradeState"));
	if (!Ranks || Ranks->Struct != FPlayerUpgradeState::StaticStruct()) return false;

	const auto* BottleAsset = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_BasicCola.DA_BasicCola"));
	if (!BottleAsset) return false;
	const int32 PID = Manager->RegisterProductFromDataAsset(BottleAsset);
	const auto MakeBottle = [&](float Y, float Level)
	{
		const int32 ID = Manager->GenerateProductInstance(PID, FTransform(FRotator::ZeroRotator, FVector(100, Y, 100), FVector(.15)));
		FPropEventSyncDelta Content;
		Content.ISID = ID;
		Content.SetFlag(EPropEventDirtyMask::ContentLevel);
		Content.ContentLevel = Level;
		auto* Record = Manager->FindISIDData(ID);
		if (!Record || !Manager->SubmitInstanceEventUpdate(ID, Content)) return 0;
		Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100.0;
		return ID;
	};
	const int32 First = MakeBottle(0, 0), Second = MakeBottle(30, 0), Third = MakeBottle(60, 0);
	const int32 Extra = MakeBottle(90, 0), Partial = MakeBottle(120, .3f);
	if (!First || !Second || !Third || !Extra || !Partial) return false;
	const auto Take = [&](int32 ID)
	{
		if (!Input->CanCarryProduct(*Manager, ID)) return false;
		auto* Bottle = Manager->ReserveGrabForTravel(ID, Player);
		if (!Bottle) return false;
		Input->Travel.Actor = Bottle;
		Input->Travel.ISID = ID;
		Input->Travel.Sequence = 1;
		Input->CompleteTravelPickup(*Player, *Bottle);
		return Bottle->GetAttachParentActor() == Player;
	};
	const auto Primary = [&]() { return WorldInteractionCarry::GetCarryReference(*Player, TEXT("Holding Actor")); };

	if (!TestTrue(TEXT("Default rank uses the normal single hand"), Take(First))) return false;
	TestFalse(TEXT("Default rank rejects an extra empty bottle"), Input->CanCarryProduct(*Manager, Second));
	auto* State = Ranks->ContainerPtrToValuePtr<FPlayerUpgradeState>(Upgrades);
	State->EmptyBottleCarryRank = 2;
	if (!TestTrue(TEXT("The new rank immediately accepts two additional empty bottles"), Take(Second) && Take(Third))) return false;
	TestFalse(TEXT("A fourth empty bottle remains in the world"), Input->CanCarryProduct(*Manager, Extra));
	TestFalse(TEXT("A partially filled bottle cannot join the empty batch"), Input->CanCarryProduct(*Manager, Partial));
	TestFalse(TEXT("Liquid cannot be added to a bottle within a held batch"), UPropGrabInputComponent::CanFillHeldBottle(*Manager, First));

	Input->Server_DropBatchedActor(Manager->GetActiveActorFromISID(First));
	TestEqual(TEXT("Dropping one bottle advances the next bottle to the hand slot"), Primary(), static_cast<UObject*>(Manager->GetActiveActorFromISID(Second)));
	TestTrue(TEXT("The next bottle remains held after the first is placed"), Manager->FindISIDData(Third)->RuntimeData.InteractionOwner.Get() == Player);
	Manager->ReleaseGrab(Second, Player);
	TestTrue(TEXT("The sole remaining bottle can be filled"), UPropGrabInputComponent::CanFillHeldBottle(*Manager, Third));
	Input->Server_DropBatchedActor(Manager->GetActiveActorFromISID(Third));
	TestNull(TEXT("Dropping the last bottle clears the hand slot"), Primary());
	if (!TestTrue(TEXT("A partially filled bottle can be carried alone"), Take(Partial))) return false;
	TestFalse(TEXT("A loaded bottle still occupies the whole hand at maximum rank"), Input->CanCarryProduct(*Manager, Extra));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseAuthoredGrabInputTest,"ColossalFizzCo.Hose.Interaction.AuthoredGKeyRouting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHoseAuthoredGrabInputTest::RunTest(const FString& Parameters)
{
    auto* GameInstance=NewObject<UGameInstance>(GEngine);GameInstance->InitializeStandalone();
    UWorld* World=GameInstance->GetWorld();if(!World)return false;
    auto WorldScope=MakeShared<PropGrabTests::FWorldOwner>(World,GameInstance);
    auto* ViewportClient=NewObject<UGameViewportClient>(GEngine);auto& Context=*GameInstance->GetWorldContext();
    Context.GameViewport=ViewportClient;ViewportClient->Init(Context,GameInstance,false);
    auto Viewport=FSceneViewport::Create(TStrongPtrVariant<FViewportClient>(ViewportClient),TSharedPtr<SViewport>());
    Viewport->SetInitialSize(FIntPoint(800,600));ViewportClient->Viewport=&Viewport.Get();
    ON_SCOPE_EXIT{ViewportClient->Viewport=nullptr;Context.GameViewport=nullptr;};
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();
    auto* Manager=World->SpawnActor<APropManager>();Manager->bEnableAutosave=false;Manager->InitializePropManager();
    UClass* PlayerClass=LoadClass<ACharacter>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    UClass* HoseClass=LoadClass<AHoseCableActor>(nullptr,TEXT("/Game/Assest/Blueprints/Cola_Container/BP_ColaHose.BP_ColaHose_C"));
    if(!TestNotNull(TEXT("Saved production player"),PlayerClass)||!TestNotNull(TEXT("Saved requested hose"),HoseClass))return false;
    World->SetBegunPlay(false);auto* Player=World->SpawnActor<ACharacter>(PlayerClass,FVector(0,0,100),FRotator::ZeroRotator);World->SetBegunPlay(true);
    Player->GetCharacterMovement()->DisableMovement();Player->SetActorEnableCollision(false);
    auto* PC=World->SpawnActor<APlayerController>();PC->Possess(Player);
    auto* LocalPlayer=NewObject<ULocalPlayer>(GEngine);GameInstance->AddLocalPlayer(LocalPlayer,FPlatformUserId::CreateFromInternalId(0));
    LocalPlayer->ViewportClient=ViewportClient;LocalPlayer->PlayerController=PC;PC->Player=LocalPlayer;LocalPlayer->Size=FVector2D(1,1);
    PC->SetViewTarget(Player);PC->UpdateCameraManager(0);
    auto* Actions=Player->FindComponentByClass<UShopPlayerInteractionComponent>();
    auto* Input=Player->FindComponentByClass<UPropGrabInputComponent>();
    if(!TestNotNull(TEXT("Saved player has owned cable actions"),Actions)||!TestNotNull(TEXT("Saved player has G adapter"),Input))return false;
    FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);const FVector Center=Eye+View.Vector()*120;
    auto* Hose=World->SpawnActor<AHoseCableActor>(HoseClass,Center-FVector(0,0,100),FRotator::ZeroRotator);
    auto* Cable=Hose->Cable.Get();Cable->bEnableCollision=false;Cable->bEnableSleeping=false;Cable->CableGravityScale=0;
    Cable->CableLength=200;Cable->EndLocation=FVector(0,0,200);Cable->bAttachStart=Cable->bAttachEnd=false;Cable->ResetCable();Hose->Tick(0);
    TestTrue(TEXT("Actual G Started callback executes"),PropGrabTests::FireGrabPhase(Player,TEXT("Started")));
    World->Tick(LEVELTICK_All,.45f);
    TestTrue(TEXT("Actual G Ongoing callback executes"),PropGrabTests::FireGrabPhase(Player,TEXT("Ongoing")));
    TestTrue(TEXT("Holding G grabs the saved hose through native cable selection"),Actions->IsCableGrabActive());
    if(!Actions->IsCableGrabActive())return false;
    TestTrue(TEXT("The authoritative grab belongs to the real saved player"),Cable->GetActiveGrabs().Num()==1&&Cable->GetActiveGrabs()[0].Participant==Player);
    auto* Holding=FindFProperty<FObjectPropertyBase>(PlayerClass,TEXT("Holding Actor"));
    TestTrue(TEXT("Cable pickup never becomes a legacy actor attachment"),Holding&&!Holding->GetObjectPropertyValue_InContainer(Player));
    TestEqual(TEXT("Cable input consumes the legacy prop intent"),Input->LastIntent,EPropGrabIntent::None);
    PropGrabTests::FireGrabPhase(Player,TEXT("Triggered"));PropGrabTests::FireGrabPhase(Player,TEXT("Completed"));
    TestTrue(TEXT("Pickup release does not immediately release the new cable"),Actions->IsCableGrabActive());
    for(int32 Tap=0;Tap<2;++Tap)
    {
        PropGrabTests::FireGrabPhase(Player,TEXT("Started"));World->Tick(LEVELTICK_All,.05f);
        PropGrabTests::FireGrabPhase(Player,TEXT("Canceled"));World->Tick(LEVELTICK_All,.05f);
    }
    TestFalse(TEXT("Double-tap G releases the hose through the owned action path"),Actions->IsCableGrabActive());
    TestEqual(TEXT("Release removes the authoritative contributor"),Cable->GetActiveGrabs().Num(),0);

    auto* WorldInteraction = LoadClass<UInterface>(nullptr, TEXT("/Game/WorldInteractionSystem/BPI_World_Intractions_System.BPI_World_Intractions_System_C"));
    if (!TestTrue(TEXT("The saved cola hose implements the world interaction Blueprint interface"), WorldInteraction && HoseClass->ImplementsInterface(WorldInteraction))) return false;
    static const FName GripSocket(TEXT("HandGrip_L"));
    auto* HandMesh = Player->GetMesh();
    if (!TestTrue(TEXT("The saved player has the legacy left hand socket"), HandMesh->DoesSocketExist(GripSocket))) return false;

    auto StepPickup = [&](AHoseCableActor& PickedHose)
    {
        constexpr float Dt = 1.f / 60.f;
        PickedHose.Cable->TickComponent(Dt, LEVELTICK_All, nullptr);
        Actions->TickComponent(Dt, LEVELTICK_All, nullptr);
        PickedHose.Tick(Dt);
    };
    UResourceTransferEndpointComponent* SupplyPort = nullptr;
    auto GrabConnector = [&](bool bStart, bool bReleaseEarly = false)
    {
        PC->UpdateCameraManager(0);
        PC->GetPlayerViewPoint(Eye, View);
        const FVector AimedPoint = Eye + View.Vector() * 120;
        const FVector FarPoint = AimedPoint + FVector(0, SupplyPort ? 110 : 200, 0);
        TArray<FVector> Line;
        for (int32 Index = 0; Index <= Cable->NumSegments; ++Index)
        {
            const double Alpha = double(Index) / Cable->NumSegments;
            FVector Position = bStart ? FMath::Lerp(AimedPoint, FarPoint, Alpha) : FMath::Lerp(FarPoint, AimedPoint, Alpha);
            if (SupplyPort) Position.Z -= 70. * FMath::Sin(UE_DOUBLE_PI * Alpha);
            Line.Add(Position);
        }
        Cable->SetCableParticleLocations(Line);
        if (SupplyPort)
        {
            SupplyPort->GetOwner()->SetActorLocation(FarPoint);
            if (!Hose->StartPort && !TestTrue(TEXT("Tank end plugs in before pickup"), Hose->ConnectStart(SupplyPort))) return false;
        }
        Hose->Tick(0);
        const auto* Selected = bStart ? Hose->StartEndpointMesh.Get() : Hose->EndEndpointMesh.Get();
        const auto* Free = bStart ? Hose->EndEndpointMesh.Get() : Hose->StartEndpointMesh.Get();
        const FVector OriginalScale = Selected->GetComponentScale();
        const FTransform CableFrame = Cable->GetComponentTransform();
        PropGrabTests::FireGrabPhase(Player, TEXT("Started"));
        if (!Actions->IsCableGrabActive())
        {
            World->Tick(LEVELTICK_All, .45f);
            PropGrabTests::FireGrabPhase(Player, TEXT("Ongoing"));
        }
        if (!TestTrue(TEXT("G begins pulling the traced connector"), Actions->IsCableGrabActive())) return false;
        const int32 ExpectedPoint = bStart ? 0 : Cable->NumSegments;
        const FVector GripPositionAtStart = HandMesh->GetSocketLocation(GripSocket);
        TArray<FVector> ApproachPoints;
        Cable->GetCableParticleLocations(ApproachPoints);
        const double InitialDistance = FVector::Distance(ApproachPoints[ExpectedPoint], GripPositionAtStart);
        for (int32 Frame = 0; Frame < 4; ++Frame) StepPickup(*Hose);
        Cable->GetCableParticleLocations(ApproachPoints);
        const double RemainingDistance = FVector::Distance(ApproachPoints[ExpectedPoint], GripPositionAtStart);
        TestTrue(TEXT("The simulated cable end travels partway toward the hand before attaching"), Selected->GetAttachParent() == Hose->SceneRoot
            && RemainingDistance > 4. && RemainingDistance < InitialDistance);
        if (bReleaseEarly)
        {
            PropGrabTests::FireGrabPhase(Player, TEXT("Canceled"));
            TestFalse(TEXT("Early G release ends the approach"), Actions->IsCableGrabActive());
            TestEqual(TEXT("The released connector is free to fall"), Cable->GetActiveGrabs().Num(), 0);
            TestEqual(TEXT("Canceling leaves the far end plugged in"), Hose->StartPort.Get(), SupplyPort);
            return true;
        }
        for (int32 Frame = 0; Frame < 180 && Hose->FindHandHeldConnector(Player) == INDEX_NONE; ++Frame) StepPickup(*Hose);
        Cable->GetCableParticleLocations(ApproachPoints);
        TestTrue(TEXT("The approaching cable end reaches the hand"), ApproachPoints[ExpectedPoint].Equals(GripPositionAtStart, 4.1));
        PropGrabTests::FireGrabPhase(Player, TEXT("Completed"));
        if (!TestTrue(TEXT("G selects the cylinder through the saved attach request"), Actions->IsCableGrabActive())) return false;
        TestTrue(TEXT("The selected cylinder is attached to the live hand socket"),
            Selected->GetAttachParent() == HandMesh && Selected->GetAttachSocketName() == GripSocket);
        TestTrue(TEXT("Only the selected cylinder attaches"), Free->GetAttachParent() == Hose->SceneRoot);
        if (SupplyPort) TestTrue(TEXT("The tank end remains anchored after pickup"), Hose->StartPort == SupplyPort
            && Cable->GetStartAttachmentComponent() == SupplyPort);
        TestTrue(TEXT("Both grips preserve the cable's independent render frame"),
            Cable->GetAttachParent()==Hose->SceneRoot && Cable->GetComponentTransform().Equals(CableFrame));
        TestTrue(TEXT("Cylinder keeps its authored size"), Selected->GetComponentScale().Equals(OriginalScale, .01));
        TestEqual(TEXT("The authoritative hold records the correct end"), Hose->FindHandHeldConnector(Player), bStart ? 0 : Cable->NumSegments);
        TArray<FCableMovementAttachment> Attachments;
        Cable->GetMovementAttachments(Attachments);
        const int32 HeldIndex = bStart ? 0 : Cable->NumSegments;
        TestTrue(TEXT("The held point still belongs to the player for movement and pulling"), Attachments.ContainsByPredicate([&](const auto& Attachment)
        {
            return Attachment.PointIndex==HeldIndex && Attachment.Driver==Player && !Attachment.bFixed;
        }));
        Player->AddActorWorldOffset(FVector(10, 0, 0));
        Player->AddActorWorldRotation(FRotator(0, 55, 0));
        World->Tick(LEVELTICK_All, .016f);
        // This fixture advances several input phases inside one editor frame.
        // Step the solver explicitly, as in the other native cable tests.
        Cable->TickComponent(.016f, LEVELTICK_All, nullptr);
        TArray<FVector> Points;
        Cable->GetCableParticleLocations(Points);
        const FVector GripPosition = HandMesh->GetSocketLocation(GripSocket);
        const FVector EndPosition = bStart ? Points[0] : Points.Last();
        AddInfo(FString::Printf(TEXT("Hand grip %s: cable %s, socket %s, delta %.3f cm, simulation %.3f ms"),
            bStart ? TEXT("start") : TEXT("end"), *EndPosition.ToString(), *GripPosition.ToString(),
            FVector::Distance(EndPosition, GripPosition), Cable->LastSimulationTimeMs));
        TestTrue(TEXT("The pinned simulation end catches up to the moving hand"),
            EndPosition.Equals(GripPosition, .2));
        TestTrue(TEXT("The cylinder follows the hand while held"), Selected->GetComponentLocation().Equals(GripPosition, .2));
        TestTrue(TEXT("Walking and turning leave cable render placement stable"), Cable->GetComponentTransform().Equals(CableFrame));
        return true;
    };

    if (!GrabConnector(true)) return false;
    for (int32 Tap = 0; Tap < 2; ++Tap)
    {
        PropGrabTests::FireGrabPhase(Player, TEXT("Started"));
        World->Tick(LEVELTICK_All, .05f);
        PropGrabTests::FireGrabPhase(Player, TEXT("Canceled"));
        World->Tick(LEVELTICK_All, .05f);
    }
    TestFalse(TEXT("Double-tap G releases the hand grip"), Actions->IsCableGrabActive());
    TestTrue(TEXT("Released start cylinder returns to the hose root"), Hose->StartEndpointMesh->GetAttachParent() == Hose->SceneRoot);
    TestFalse(TEXT("Released start point is free again"), Cable->bAttachStart);

    auto* SupplyActor = World->SpawnActor<AActor>();
    SupplyPort = NewObject<UResourceTransferEndpointComponent>(SupplyActor);
    SupplyActor->SetRootComponent(SupplyPort);
    SupplyActor->AddInstanceComponent(SupplyPort);
    SupplyPort->Category = Hose->Transfer->Category;
    SupplyPort->ResourceID = Hose->Transfer->ResourceID;
    SupplyPort->CapacityUnits = 10;
    SupplyPort->bCanSupply = true;
    SupplyPort->RegisterComponent();
    if (!GrabConnector(false, true) || !GrabConnector(false)) return false;
    Hose->DisconnectStart();
    PC->UpdateCameraManager(0);
    PC->GetPlayerViewPoint(Eye, View);
    auto* Receiver = World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Receiver);
    Receiver->SetRootComponent(Box);
    Receiver->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(5));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();
    Receiver->SetActorLocation(Eye + View.Vector() * 170);
    auto* Port = NewObject<UResourceTransferEndpointComponent>(Receiver);
    Receiver->AddInstanceComponent(Port);
    Port->SetupAttachment(Box);
    Port->Category = Hose->Transfer->Category;
    Port->ResourceID = Hose->Transfer->ResourceID;
    Port->CapacityUnits = 10;
    Port->bCanReceive = true;
    Port->RegisterComponent();
    TestTrue(TEXT("E connects the hand-held receiving end"), Actions->TryOpenTerminalFromView());
    TestTrue(TEXT("The port becomes the cable end attachment"), Hose->EndPort == Port && Cable->GetAttachedComponent() == Port);
    TestEqual(TEXT("Connecting consumes the hand hold"), Hose->FindHandHeldConnector(Player), INDEX_NONE);
    TestTrue(TEXT("The connected cylinder is again driven by the cable"), Hose->EndEndpointMesh->GetAttachParent() == Hose->SceneRoot);
    TestFalse(TEXT("Successful connection clears local holding state"), Actions->IsCableGrabActive());

    Hose->Destroy();
    Receiver->Destroy();
    auto* Floor = World->SpawnActor<AActor>();
    auto* FloorBox = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(FloorBox);
    Floor->AddInstanceComponent(FloorBox);
    FloorBox->SetBoxExtent(FVector(750,750,20));
    FloorBox->SetCollisionProfileName(TEXT("BlockAll"));
    FloorBox->RegisterComponent();
    Floor->SetActorLocation(FVector(0,0,-20));

    for (const TCHAR* Name : {TEXT("BP_HoseCable"), TEXT("BP_ContentHose"), TEXT("BP_FuelHose"), TEXT("BP_ElectricityHose")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Resources/Hoses/%s.%s_C"),Name,Name);
        auto* PresetClass = LoadClass<AHoseCableActor>(nullptr,*Path);
        if (!TestNotNull(FString::Printf(TEXT("Saved hose preset %s"),Name),PresetClass)) return false;
        Player->SetActorLocation(FVector(0,0,100));
        auto* Preset = World->SpawnActor<AHoseCableActor>(PresetClass,FVector(140,-65,6),FRotator::ZeroRotator);
        if (!TestTrue(FString::Printf(TEXT("%s uses connector pickup"),Name),Preset && Preset->bCarryConnectorOnPickup)) return false;
        auto* PresetCable = Preset->Cable.Get();
        TArray<FVector> FloorPoints;
        for (int32 Index = 0; Index<=PresetCable->NumSegments; ++Index)
            FloorPoints.Add(FVector(140,-65+200.0*Index/PresetCable->NumSegments,6));
        PresetCable->SetCableParticleLocations(FloorPoints);
        Preset->Tick(0);
        Player->GetActorEyesViewPoint(Eye,View);
        PC->SetControlRotation((FVector(140,0,6)-Eye).Rotation());
        PC->UpdateCameraManager(0);

        PropGrabTests::FireGrabPhase(Player,TEXT("Started"));
        World->Tick(LEVELTICK_All,.45f);
        PropGrabTests::FireGrabPhase(Player,TEXT("Ongoing"));
        for (int32 Frame = 0; Frame < 180 && Preset->FindHandHeldConnector(Player) == INDEX_NONE; ++Frame) StepPickup(*Preset);
        PropGrabTests::FireGrabPhase(Player,TEXT("Completed"));
        if (!TestTrue(FString::Printf(TEXT("G on %s body lifts the reachable connector off the floor"),Name),
                Actions->IsCableGrabActive() && Preset->FindHandHeldConnector(Player)==0
                && Preset->StartEndpointMesh->GetAttachParent()==HandMesh && PresetCable->GetAttachParent()==Preset->SceneRoot)) return false;
        const FVector BeforeWalk = Player->GetActorLocation();
        const FVector AfterWalk = BeforeWalk + FVector(0,180,0);
        PresetCable->TickComponent(.016f,LEVELTICK_All,nullptr);
        if (auto* Constraint=Player->FindComponentByClass<UCableMovementConstraintComponent>())
        {
            TestTrue(FString::Printf(TEXT("A loose %s lets the player walk at the requested position"),Name),
                Constraint->ConstrainRequestedLocation(BeforeWalk,AfterWalk).Equals(AfterWalk,.01)
                && Constraint->TryPullConnectedChain(BeforeWalk,AfterWalk,.016f,1000).AllowedLocation.Equals(AfterWalk,.01));
        }
        else AddError(FString::Printf(TEXT("%s did not register its hand attachment with player movement"),Name));
        Player->SetActorLocation(AfterWalk);
        World->Tick(LEVELTICK_All,.016f);
        PresetCable->TickComponent(.016f,LEVELTICK_All,nullptr);
        PresetCable->GetCableParticleLocations(FloorPoints);
        TestTrue(FString::Printf(TEXT("%s cable and connector follow the moved hand"),Name),FloorPoints.Num()>1
            && FloorPoints[0].Equals(HandMesh->GetSocketLocation(GripSocket),.2)
            && Preset->StartEndpointMesh->GetComponentLocation().Equals(HandMesh->GetSocketLocation(GripSocket),.2));
        Actions->RequestCableRelease();
        TestEqual(FString::Printf(TEXT("%s releases its connector"),Name),Preset->FindHandHeldConnector(Player),INDEX_NONE);
        Preset->Destroy();
    }
    return true;
}
#endif
