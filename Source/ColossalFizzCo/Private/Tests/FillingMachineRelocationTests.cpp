#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Resources/HoseCableActor.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/SocketColaFillingMachine.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFillingMachineRelocationTest, "ColossalFizzCo.Shop.FillingMachineRelocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFillingMachineRelocationTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager = World->SpawnActor<APropManager>();
	Manager->bEnableAutosave = false;
	Manager->InitializePropManager();
	if (!Manager->ShopSupplies->InitializeCatalog()) return false;
	const auto* Catalog = Manager->ShopSupplies->Catalog.Get();
	const auto* CapOffer = Catalog->FindOffer(TEXT("TwistCap"));
	const auto* SealOffer = Catalog->FindOffer(TEXT("TemporarySeal"));
	if (!CapOffer || !SealOffer) return false;

	const auto SpawnProp = [Manager](int32 ProductID, const FTransform& Pose) -> AActivePropActor*
	{
		const int32 ID = Manager->GenerateProductInstance(ProductID, Pose);
		AActivePropActor* Active = nullptr;
		if (ID <= 0 || !Manager->PromoteInstanceToASM(ID, EPropActivationReason::Interaction, false, Active) || !Active) return nullptr;
		Active->PropMesh->SetSimulatePhysics(false);
		return Active;
	};
	FPropProductDefinition Equipment;
	Equipment.ProductID = 9419;
	Equipment.bEnableInstanceUniqueData = true;
	Equipment.StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Equipment.CustomActiveActorClass = ASocketColaFillingMachine::StaticClass();
	Manager->RegisterProductDefinition(Equipment);
	const FTransform Start(FVector(0, 0, 2000));
	auto* Machine = Cast<ASocketColaFillingMachine>(SpawnProp(Equipment.ProductID, Start));
	if (!TestNotNull(TEXT("The table has an active managed actor"), Machine)) return false;
	const int32 MachineID = Machine->ISID;
	auto* Cap = SpawnProp(Manager->RegisterProductFromDataAsset(CapOffer->Product.LoadSynchronous()),
		FTransform(Machine->CapInputVolume->GetComponentLocation()));
	auto* Seal = SpawnProp(Manager->RegisterProductFromDataAsset(SealOffer->Product.LoadSynchronous()),
		FTransform(Machine->SealInputVolume->GetComponentLocation()));
	if (!TestTrue(TEXT("The cap and seal are managed props"), Cap && Seal)) return false;
	Machine->Production->RefreshSupplyTrays(Cap->ISID, Seal->ISID);
	const auto TraysIntact = [&]()
	{
		const auto* Data = Manager->FindUniqueData(MachineID);
		return Data && Data->EventData.LoadedCapISIDs.Num() == 1 && Data->EventData.LoadedCapISIDs[0] == Cap->ISID
			&& Data->EventData.LoadedSealISIDs.Num() == 1 && Data->EventData.LoadedSealISIDs[0] == Seal->ISID
			&& Cap->GetAttachParentActor() == Machine && Seal->GetAttachParentActor() == Machine;
	};
	if (!TestTrue(TEXT("Both tray items are loaded and attached"), TraysIntact())) return false;
	auto* Bottle = Cast<AColaPropActor>(SpawnProp(Manager->RegisterProductFromDataAsset(Catalog->EmptyBottleProduct.LoadSynchronous()),
		Machine->BottleHolderPoint->GetComponentTransform()));
	if (!TestTrue(TEXT("A safe bottle can sit in the cradle"), Bottle && Machine->Production->SeatBottle(Bottle->ISID))) return false;
	const FTransform CapRelative = Cap->GetActorTransform().GetRelativeTransform(Start);
	const FTransform SealRelative = Seal->GetActorTransform().GetRelativeTransform(Start);
	const FTransform BottleRelative = Bottle->GetActorTransform().GetRelativeTransform(Start);

	FPropContentDefinition Cola;
	Cola.ContentID = 9420;
	Cola.ExpiryProfile.bHasExpiry = false;
	Manager->RegisterContentDefinition(Cola);
	auto* Inlet = Machine->GetHoseSocket(0);
	auto* Tank = World->SpawnActor<AActor>();
	auto* Supply = NewObject<UHoseSocketComponent>(Tank);
	Tank->SetRootComponent(Supply);
	Tank->AddInstanceComponent(Supply);
	Supply->ResourceID = Cola.ContentID;
	Supply->bCanSupply = true;
	Supply->bCanReceive = false;
	Supply->bAllowManagedContentBridge = true;
	Supply->RegisterComponent();
	Supply->SetWorldLocation(Inlet->GetComponentLocation() + FVector(0, -125, 0));
	Supply->SetStoredUnits(10.f);
	auto* Hose = World->SpawnActor<AHoseCableActor>();
	if (!TestTrue(TEXT("The hose connects the supply to the actual table inlet"),
		Supply->AttachHose(Hose) && Inlet->AttachHose(Hose))) return false;
	const auto HoseUnpluggedAtTable = [&]()
	{
		return !Inlet->ConnectedHose && !Hose->EndPort && Hose->StartPort == Supply && Supply->ConnectedHose == Hose;
	};

	auto* Player = World->SpawnActor<ACharacter>(Start.GetLocation() + FVector(0, -200, 100), FRotator::ZeroRotator);
	auto* Controller = World->SpawnActor<APlayerController>();
	Controller->Possess(Player);
	Machine->SetActorEnableCollision(true);
	Machine->Production->View.bLeverHeld = true;
	TestFalse(TEXT("An active lever prevents relocation"), Machine->BeginMachinePlacement(*Player));
	TestTrue(TEXT("A rejected move leaves both hose ends connected"), Hose->StartPort == Supply && Hose->EndPort == Inlet);
	Machine->Production->View.bLeverHeld = false;
	if (!TestTrue(TEXT("The idle table enters placement for its operator"), Machine->BeginMachinePlacement(*Player))) return false;
	TestTrue(TEXT("The original actor is hidden and non-solid during placement"), Machine->GetPlacementOperator() == Player
		&& Machine->IsHidden() && !Machine->GetActorEnableCollision() && Cap->IsHidden() && Seal->IsHidden() && Bottle->IsHidden());
	TestTrue(TEXT("Starting placement unplugs only the table side"), HoseUnpluggedAtTable());
	Machine->CancelMachinePlacement(*Player);
	TestTrue(TEXT("Cancel restores the original solid table and tray contents"), !Machine->GetPlacementOperator()
		&& !Machine->IsHidden() && Machine->GetActorEnableCollision() && Manager->GetTransform(MachineID).Equals(Start)
		&& TraysIntact() && !Cap->IsHidden() && !Seal->IsHidden() && !Bottle->IsHidden());

	const FTransform Destination(FRotator(0, 65, 0), FVector(250, 75, 2000), Start.GetScale3D());
	if (!TestTrue(TEXT("The same machine can complete a subsequent placement"), Machine->BeginMachinePlacement(*Player)
		&& Machine->FinishMachinePlacement(*Player, Destination))) return false;
	World->Tick(LEVELTICK_All, 1.f / 30.f);
	TestTrue(TEXT("Commit restores solid collision and retains the same managed actor and pose"), !Machine->GetPlacementOperator()
		&& !Machine->IsHidden() && Machine->GetActorEnableCollision() && Manager->GetActiveActorFromISID(MachineID) == Machine
		&& Manager->GetTransform(MachineID).Equals(Destination));
	TestTrue(TEXT("Both tray supplies retain their identities and relative poses"), TraysIntact()
		&& Cap->GetActorTransform().Equals(CapRelative * Destination, .05) && Seal->GetActorTransform().Equals(SealRelative * Destination, .05));
	TestTrue(TEXT("The safe bottle remains seated at the same relative cradle pose"), Bottle->IsSeatedInFiller()
		&& Bottle->GetActorTransform().Equals(BottleRelative * Destination, .05));
	TestTrue(TEXT("Moving the table keeps the distant hose end mounted"), HoseUnpluggedAtTable());
	return true;
}
#endif
