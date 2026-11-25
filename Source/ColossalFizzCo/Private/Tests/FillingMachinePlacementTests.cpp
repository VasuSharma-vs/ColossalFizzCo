#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "Shop/ColaFlowController.h"
#include "Shop/MachinePlacementGeometry.h"
#include "Shop/SocketColaFillingMachine.h"
#include "UI/ShopMachinePlacementPreview.h"

namespace
{
	UBoxComponent* AddPlacementBox(UWorld& World, const FVector& Location, const FVector& HalfSize)
	{
		auto* Actor = World.SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Actor->AddInstanceComponent(Box);
		Box->InitBoxExtent(HalfSize);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->RegisterComponent();
		Box->SetWorldLocation(Location);
		return Box;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFillingMachinePlacementTest, "ColossalFizzCo.Shop.FillingMachinePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFillingMachinePlacementTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	AddPlacementBox(*World, FVector(0, 0, -10), FVector(1700, 1700, 10));

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Player = World->SpawnActor<ACharacter>(FVector(0, 0, 90), FRotator::ZeroRotator, Spawn);
	auto* Machine = World->SpawnActor<ASocketColaFillingMachine>(FVector(200, -600, 100), FRotator::ZeroRotator, Spawn);
	auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Player || !Machine || !Mesh) return false;
	for (UStaticMeshComponent* Part : TInlineComponentArray<UStaticMeshComponent*>(Machine)) Part->SetStaticMesh(nullptr);
	Machine->PropMesh->SetStaticMesh(Mesh);
	Machine->SetActorScale3D(FVector(1.8, .8, 1.2));
	Machine->SetActorEnableCollision(false);

	FVector Eye;
	FRotator PlayerView;
	Player->GetActorEyesViewPoint(Eye, PlayerView);
	const FVector Point(500, 0, 0);
	const FVector Aim = (Point - Eye).GetSafeNormal();
	FTransform Pose;
	if (!TestTrue(TEXT("A rotated filling machine fits on the traced floor"),
		ShopMachinePlacement::TracePlacement(*Machine, *Player, Eye, Aim, 45.f, Pose))) return false;
	TestTrue(TEXT("The candidate uses the chosen yaw"), FMath::IsNearlyEqual(Pose.Rotator().Yaw, 45.0, .01));
	TestTrue(TEXT("Authoritative validation accepts the same pose"), ShopMachinePlacement::CanPlace(*Machine, *Player, Pose));

	const auto* Blocker = AddPlacementBox(*World, Pose.GetLocation() + FVector(35, 15, 0), FVector(15));
	TestFalse(TEXT("An off-centre blocker prevents placing the machine at that pose"), ShopMachinePlacement::CanPlace(*Machine, *Player, Pose));
	Blocker->GetOwner()->SetActorEnableCollision(false);
	TestTrue(TEXT("Removing the obstruction makes the pose valid again"), ShopMachinePlacement::CanPlace(*Machine, *Player, Pose));

	FTransform Floating = Pose;
	Floating.AddToTranslation(FVector(0, 0, 80));
	TestFalse(TEXT("A pose without a supporting surface cannot commit"), ShopMachinePlacement::CanPlace(*Machine, *Player, Floating));
	FTransform NearPose;
	TestTrue(TEXT("A close floor trace moves the preview farther from the player"),
		ShopMachinePlacement::TracePlacement(*Machine, *Player, Eye, FVector(.05, 0, -1), 0.f, NearPose));
	TestTrue(TEXT("The preview fits outside the player's collision"), NearPose.GetLocation().X > 140.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlowControllerPlacementGeometryTest, "ColossalFizzCo.Shop.FlowControllerPlacementGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowControllerPlacementGeometryTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	AddPlacementBox(*World, FVector(0, 0, -10), FVector(1700, 1700, 10));

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Player = World->SpawnActor<ACharacter>(FVector(0, 0, 90), FRotator::ZeroRotator, Spawn);
	auto* Machine = World->SpawnActor<AColaFlowController>(FVector(200, -600, 200), FRotator::ZeroRotator, Spawn);
	if (!Player || !Machine || !Machine->Housing->GetStaticMesh()) return false;
	auto* Preview = World->SpawnActor<AShopMachinePlacementPreview>(AShopMachinePlacementPreview::StaticClass(), Machine->GetActorTransform(), Spawn);
	if (!Preview) return false;
	Machine->SetActorHiddenInGame(true);
	Machine->SetActorEnableCollision(false);
	if (!TestTrue(TEXT("The Flow Controller uses the shared translucent preview"), Preview->InitializeFromMachine(*Machine))) return false;
	const TInlineComponentArray<UStaticMeshComponent*> PreviewMeshes(Preview);
	if (!TestFalse(TEXT("The controller preview copies visible mesh parts"), PreviewMeshes.IsEmpty())) return false;
	TestFalse(TEXT("The preview has actor collision disabled"), Preview->GetActorEnableCollision());
	for (const auto* Mesh : PreviewMeshes)
		TestEqual(TEXT("Each preview mesh also disables collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	auto* Material = Cast<UMaterialInstanceDynamic>(PreviewMeshes[0]->GetMaterial(0));
	if (!TestNotNull(TEXT("The preview uses a dynamic tint material"), Material)) return false;
	TestTrue(TEXT("The preview material has 0.5 opacity"), FMath::IsNearlyEqual(Material->K2_GetScalarParameterValue(TEXT("Opacity")), .5f));

	FVector Eye;
	FRotator View;
	Player->GetActorEyesViewPoint(Eye, View);
	const FVector Aim(.05, 0, -1);
	FTransform Pose;
	if (!TestTrue(TEXT("A near floor hit places the Flow Controller farther along the aim"),
		ShopMachinePlacement::TracePlacement(*Machine, *Player, Eye, Aim, 65.f, Pose))) return false;
	TestTrue(TEXT("The farther controller candidate keeps the chosen yaw"), FMath::IsNearlyEqual(Pose.Rotator().Yaw, 65.0, .01));
	const auto* Housing = Machine->Housing.Get();
	const auto RelativeHousing = Housing->GetComponentTransform().GetRelativeTransform(Machine->GetActorTransform());
	const FVector HousingCenter = Pose.TransformPosition(RelativeHousing.TransformPosition(Housing->GetStaticMesh()->GetBoundingBox().GetCenter()));
	TestTrue(TEXT("The fallback moves the controller beyond the player radius"),
		FVector::Dist2D(Player->GetActorLocation(), HousingCenter) > Player->GetSimpleCollisionRadius() + 20.0);
	TestTrue(TEXT("The server accepts the same supported candidate"), ShopMachinePlacement::CanPlace(*Machine, *Player, Pose));
	Preview->SetPlacementPreview(Pose, true);
	TestTrue(TEXT("A legal candidate shows mint green"), Material->K2_GetVectorParameterValue(TEXT("Tint")).Equals(FLinearColor(.2f, 1.f, .58f, .5f)));

	auto* Obstruction = AddPlacementBox(*World, HousingCenter, FVector(8));
	Obstruction->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Obstruction->SetCollisionResponseToAllChannels(ECR_Ignore);
	Obstruction->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	FTransform BlockedPose;
	TestFalse(TEXT("An overlap-only obstruction rejects the preview pose"),
		ShopMachinePlacement::TracePlacement(*Machine, *Player, Eye, Aim, 65.f, BlockedPose));
	TestFalse(TEXT("The same overlap rejects the server placement"), ShopMachinePlacement::CanPlace(*Machine, *Player, BlockedPose));
	Preview->SetPlacementPreview(BlockedPose, false);
	TestTrue(TEXT("An obstructed candidate shows red"), Material->K2_GetVectorParameterValue(TEXT("Tint")).Equals(FLinearColor(1.f, .17f, .18f, .5f)));
	return true;
}

#endif
