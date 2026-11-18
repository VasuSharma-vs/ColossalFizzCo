#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Components/WorldHitResponseComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Interfaces/WorldInteractionHitLibrary.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace RocketWorldHitTests
{
struct FWorld
{
	UWorld* World=nullptr;
	FWorld(bool bBeginPlay=true)
	{
		const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
		World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); if (bBeginPlay) World->GetWorldSettings()->NotifyBeginPlay();
	}
	~FWorld()
	{
		if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	AActor* Body(FVector Position)
	{
		auto* Actor=World->SpawnActor<AActor>();
		auto* Mesh=NewObject<UStaticMeshComponent>(Actor); Actor->AddInstanceComponent(Mesh); Actor->SetRootComponent(Mesh);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetWorldScale3D(FVector(.1)); Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
		Mesh->SetGenerateOverlapEvents(false); Mesh->RegisterComponent(); Mesh->SetMassOverrideInKg(NAME_None,1.f,true);
		Actor->SetActorLocation(Position); return Actor;
	}
	URocketProjectileComponent* Rocket(AActor* Actor)
	{
		auto* Component=NewObject<URocketProjectileComponent>(Actor); Actor->AddInstanceComponent(Component);
		Component->bLaunchOnInitialized=false; Component->SetAllDebugEnabled(false); Component->RegisterComponent(); return Component;
	}
	UWorldHitResponseComponent* Response(AActor* Actor)
	{
		auto* Component=NewObject<UWorldHitResponseComponent>(Actor); Actor->AddInstanceComponent(Component); Component->RegisterComponent(); return Component;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRocketWorldContactsTest,"ColossalFizzCo.Props.WorldHits.RocketContacts",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRocketWorldContactsTest::RunTest(const FString& Parameters)
{
	RocketWorldHitTests::FWorld F; if (!F.World) return false;
	auto* Source=F.Body(FVector(0,0,1000)); auto* Mesh=CastChecked<UStaticMeshComponent>(Source->GetRootComponent());
	auto* Rocket=F.Rocket(Source);
	auto* Player=F.World->SpawnActor<ACharacter>(FVector(400,0,1000),FRotator::ZeroRotator);
	Player->GetCharacterMovement()->DisableMovement(); Player->GetCapsuleComponent()->SetGenerateOverlapEvents(false);
	auto* Receiver=F.Response(Player);
	if (!TestTrue(TEXT("Physics mode launches"),Rocket->StartPressurePhysicsFlight())) return false;
	TestEqual(TEXT("Physics mode retains Chaos collision"),Mesh->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
	TestEqual(TEXT("Physics rocket overlaps pawn instead of depenetrating it"),Mesh->GetCollisionResponseToChannel(ECC_Pawn),ECR_Overlap);
	TestEqual(TEXT("Physics rocket overlaps dynamic props"),Mesh->GetCollisionResponseToChannel(ECC_WorldDynamic),ECR_Overlap);
	TestEqual(TEXT("Physics rocket overlaps custom ISM channel"),Mesh->GetCollisionResponseToChannel(ECC_GameTraceChannel1),ECR_Overlap);
	TestEqual(TEXT("Physics rocket overlaps custom active prop channel"),Mesh->GetCollisionResponseToChannel(ECC_GameTraceChannel2),ECR_Overlap);
	TestEqual(TEXT("Physics rocket still blocks static walls"),Mesh->GetCollisionResponseToChannel(ECC_WorldStatic),ECR_Block);
	auto* Wall=F.Body(FVector(1100,0,1000)); CastChecked<UPrimitiveComponent>(Wall->GetRootComponent())->SetCollisionObjectType(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RocketSurfaceTest),false,Source); FHitResult Surface;
	TestTrue(TEXT("Flight planning finds a blocking static surface"),Rocket->TraceFlightSurface(Surface,FVector(0,0,1000),FVector(1400,0,1000),ECC_WorldDynamic,Query));
	TestEqual(TEXT("Planning ignores overlapping player and reaches wall behind it"),Surface.GetActor(),Wall);
	Rocket->bUseOverlapHitEvents=false;
	TestTrue(TEXT("Legacy trace policy remains available"),Rocket->TraceFlightSurface(Surface,FVector(0,0,1000),FVector(1400,0,1000),ECC_WorldDynamic,Query));
	TestEqual(TEXT("Legacy policy sees blocking player"),Surface.GetActor(),static_cast<AActor*>(Player));
	Rocket->bUseOverlapHitEvents=true;
	auto* Trigger=F.Body(FVector(0,1000,1000)); auto* TriggerMesh=CastChecked<UPrimitiveComponent>(Trigger->GetRootComponent());
	TriggerMesh->SetCollisionProfileName(TEXT("Trigger"));
	TestFalse(TEXT("Unrelated trigger overlap is not reported as a bottle impact"),Rocket->ReportWorldHit(FHitResult(Trigger,TriggerMesh,Trigger->GetActorLocation(),-FVector::ForwardVector),FVector(1000,0,0)));
	Mesh->SetPhysicsLinearVelocity(FVector(1000,0,0));
	Source->SetActorLocation(FVector(800,0,1000),false,nullptr,ETeleportType::TeleportPhysics);
	Rocket->TickPhysicsWorldHits();
	TestEqual(TEXT("Swept fast crossing reaches player component even without target overlap events"),Receiver->LastWorldHit.SourceActor.Get(),Source);
	TestEqual(TEXT("Contact identifies actual capsule"),Receiver->LastWorldHit.TargetComponent.Get(),static_cast<UPrimitiveComponent*>(Player->GetCapsuleComponent()));
	TestTrue(TEXT("Contact carries positive incoming momentum"),Receiver->LastWorldHit.Impulse.X>0);
	const FGuid First=Receiver->LastWorldHit.EventID;
	Rocket->TickPhysicsWorldHits();
	TestEqual(TEXT("Repeated processing cannot duplicate contact"),Receiver->LastWorldHit.EventID,First);
	TestTrue(TEXT("Player receives data without automatic movement"),Player->GetCharacterMovement()->Velocity.IsNearlyZero());
	Source->SwapRoles();
	TestFalse(TEXT("Clients cannot send authoritative contacts"),Rocket->ReportWorldHit(Receiver->LastWorldHit.Hit,FVector(1000,0,0)));
	Source->SwapRoles();
	Rocket->ResetRocketSimulation();
	TestEqual(TEXT("Pooling restores authored profile response"),Mesh->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
	TestFalse(TEXT("Pooling restores authored overlap flag"),Mesh->GetGenerateOverlapEvents());
	Rocket->TippingSettings.bEnableTipping=false; Rocket->DraggingSettings.bEnableDragging=false;
	Rocket->LaunchRocket(FVector::ForwardVector,true,13,2000,1000);
	TestEqual(TEXT("Scripted powered flight is query only"),Mesh->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("Scripted flight disables physics"),Mesh->IsSimulatingPhysics());
	TestEqual(TEXT("Scripted flight overlaps physical props"),Mesh->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Overlap);
	TestEqual(TEXT("Scripted flight retains static blocking"),Mesh->GetCollisionResponseToChannel(ECC_WorldStatic),ECR_Block);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldHitInstanceRoutingTest,"ColossalFizzCo.Props.WorldHits.InstanceRouting",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldHitInstanceRoutingTest::RunTest(const FString& Parameters)
{
	RocketWorldHitTests::FWorld F; if (!F.World) return false;
	auto* Manager=F.World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	FPropProductDefinition Product; Product.ProductID=1001; Product.bEnableInstanceUniqueData=true;
	Product.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
	Product.InteractionProfile.bCanDamage=true; Product.StrengthDamageRate=.1f;
	Manager->RegisterProductDefinition(Product); Manager->WorldHitPromotionImpulseThreshold=100;
	const int32 First=Manager->GenerateProductInstance(1001,FTransform(FVector(400,0,1000)));
	const int32 Second=Manager->GenerateProductInstance(1001,FTransform(FVector(400,300,1000)));
	auto* ISM=Manager->GetComponentFromISID(Second); if (!TestNotNull(TEXT("Permanent ISM component"),ISM)) return false;
	const int32 Index=ISM->FindISMI(Second);
	ISM->SetCollisionObjectType(ECC_GameTraceChannel1); // Exercise the project's custom ISM object channel too.
	auto* Source=F.Body(FVector(0,300,1000)); auto* Rocket=F.Rocket(Source);
	FWorldInteractionHit Data; Data.EventID=FGuid::NewGuid(); Data.SourceActor=Source; Data.SourceComponent=Cast<UPrimitiveComponent>(Source->GetRootComponent());
	Data.TargetActor=ISM->GetOwner(); Data.TargetComponent=ISM; Data.Hit=FHitResult(ISM->GetOwner(),ISM,FVector(350,300,1000),-FVector::ForwardVector);
	Data.Hit.Item=Index; Data.ISMI=Index; Data.Impulse=FVector(99,0,0); Data.Velocity=FVector(1000,0,0); Data.MassKg=1; Data.bApplyHitForce=false;
	TestFalse(TEXT("Subthreshold hit leaves instance resting"),Manager->ReceiveWorldHitByICID(ISM->IISMComponentID,Index,Data));
	TestEqual(TEXT("Subthreshold representation remains ISM"),Manager->GetRepresentation(Second),EPropRepresentation::ISM);
	TestFalse(TEXT("Invalid index cannot select a neighbouring prop"),Manager->ReceiveWorldHitByICID(ISM->IISMComponentID,INDEX_NONE,Data));
	if (!TestTrue(TEXT("Source physics flight launches"),Rocket->StartPressurePhysicsFlight())) return false;
	Rocket->bApplyHitForceOnTargets=false;
	CastChecked<UPrimitiveComponent>(Source->GetRootComponent())->SetPhysicsLinearVelocity(FVector(1000,0,0));
	Source->SetActorLocation(FVector(800,300,1000),false,nullptr,ETeleportType::TeleportPhysics); Rocket->TickPhysicsWorldHits();
	auto* Active=Manager->GetActiveActorFromISID(Second);
	if (!TestNotNull(TEXT("Swept ISMI resolves and promotes correct stable ISID"),Active)) return false;
	TestEqual(TEXT("Neighbour in same component remains ISM"),Manager->GetRepresentation(First),EPropRepresentation::ISM);
	TestEqual(TEXT("Promotion retains permanent slot"),Manager->FindISIDData(Second)->ISMI,Index);
	TestTrue(TEXT("Force-disabled contact still damages promoted prop"),Active->GetStrength()<1.f);
	TestTrue(TEXT("Force-disabled promotion adds no impulse"),Active->PropMesh->GetPhysicsLinearVelocity().IsNearlyZero());
	const float Strength=Active->GetStrength();
	Data.EventID=FGuid::NewGuid(); Data.Impulse=FVector(100,0,0);
	TestTrue(TEXT("Existing active prop receives next event"),Manager->ReceiveWorldHitByISID(Second,Data));
	const float After=Active->GetStrength();
	TestTrue(TEXT("Damage happens once"),After<Strength);
	TestFalse(TEXT("Manager rejects duplicate event ID"),Manager->ReceiveWorldHitByISID(Second,Data));
	TestEqual(TEXT("Duplicate has no second damage"),Active->GetStrength(),After);
	Manager->SwapRoles(); Data.EventID=FGuid::NewGuid();
	TestFalse(TEXT("Client manager cannot promote or damage"),Manager->ReceiveWorldHitByISID(First,Data)); Manager->SwapRoles();
	const int32 Third=Manager->GenerateProductInstance(1001,FTransform(FVector(400,600,1000)));
	TestTrue(TEXT("Promotion API accepts damage with force disabled"),Manager->SetInstanceToUnsettled(Third,FVector(500,0,0),FVector(400,600,1000),10,false));
	auto* ThirdActor=Manager->GetActiveActorFromISID(Third); if (!TestNotNull(TEXT("API promoted actor"),ThirdActor)) return false;
	TestTrue(TEXT("Promotion bool preserves damage"),ThirdActor->GetStrength()<1.f);
	TestTrue(TEXT("Promotion bool skips movement"),ThirdActor->PropMesh->GetPhysicsLinearVelocity().IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldHitLegacyInterfaceTest,"ColossalFizzCo.Props.WorldHits.LegacyInterface",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldHitLegacyInterfaceTest::RunTest(const FString& Parameters)
{
	RocketWorldHitTests::FWorld F(false); if (!F.World) return false;
	UClass* PlayerClass=LoadClass<ACharacter>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Existing player Blueprint loads"),PlayerClass)) return false;
	// Construct authored components before BeginPlay, without unrelated player UI/controller setup.
	auto* Player=F.World->SpawnActor<ACharacter>(PlayerClass,FVector(400,0,1000),FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Existing Blueprint player"),Player)) return false;
	auto* Source=F.Body(FVector(0,0,1000)); auto* Receiver=Player->FindComponentByClass<UWorldHitResponseComponent>();
	if (!TestNotNull(TEXT("Player Blueprint authors its own RocketHitResponse component"),Receiver)) return false;
	FWorldInteractionHit Data; Data.EventID=FGuid::NewGuid(); Data.SourceActor=Source;
	Data.SourceComponent=Cast<UPrimitiveComponent>(Source->GetRootComponent()); Data.TargetActor=Player; Data.TargetComponent=Player->GetCapsuleComponent();
	Data.Hit=FHitResult(Player,Player->GetCapsuleComponent(),FVector(358,0,1000),-FVector::ForwardVector);
	Data.Velocity=Data.RelativeVelocity=FVector(1000,0,0); Data.MassKg=1; Data.bApplyHitForce=false;
	UClass* LegacyInterface=LoadObject<UClass>(nullptr,TEXT("/Game/WorldInteractionSystem/BPI_World_Intractions_System.BPI_World_Intractions_System_C"));
	if (!TestNotNull(TEXT("Legacy interface class"),LegacyInterface)) return false;
	TestTrue(TEXT("Dispatcher delivers full packet to authored player component"),UWorldInteractionHitLibrary::DispatchWorldHit(Data));
	TestEqual(TEXT("Player component receives the event identity"),Receiver->LastWorldHit.EventID,Data.EventID);
	TestFalse(TEXT("Player component receives force-disabled permission"),Receiver->LastWorldHit.bApplyHitForce);
	TestFalse(TEXT("Player receiver defaults to no automatic knockback"),Receiver->bApplyHitForce);
	UClass* ATMClass=LoadClass<AActor>(nullptr,TEXT("/Game/DirtyActors/BP_ATM.BP_ATM_C"));
	if (!TestNotNull(TEXT("Existing legacy world-interaction receiver"),ATMClass)) return false;
	auto* ATM=F.World->SpawnActor<AActor>(ATMClass,FVector(1000,0,1000),FRotator::ZeroRotator);
	Data.TargetActor=ATM; Data.TargetComponent=ATM->FindComponentByClass<UPrimitiveComponent>();
	TestTrue(TEXT("Legacy fixture implements world interaction interface"),ATMClass->ImplementsInterface(LegacyInterface));
	TestTrue(TEXT("Existing Hit Actor Blueprint interface accepts bridge message"),UWorldInteractionHitLibrary::SendWorldHitToObject(ATM,Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldHitBoundedForceTest,"ColossalFizzCo.Props.WorldHits.BoundedForce",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldHitBoundedForceTest::RunTest(const FString& Parameters)
{
	auto F=MakeShared<RocketWorldHitTests::FWorld>(); if (!F->World) return false;
	auto* Target=F->Body(FVector(0,0,1000)); auto* Mesh=CastChecked<UPrimitiveComponent>(Target->GetRootComponent());
	Mesh->SetEnableGravity(false); Mesh->SetSimulatePhysics(true); Mesh->SetMassOverrideInKg(NAME_None,.05f,true);
	FWorldInteractionHit Data; Data.Impulse=FVector(100000,0,0); Data.bApplyHitForce=false;
	TestFalse(TEXT("Force request can be disabled independently of hit data"),UWorldInteractionHitLibrary::ApplyBoundedWorldHitImpulse(Mesh,Data));
	Data.bApplyHitForce=true;
	if (!TestTrue(TEXT("Opt-in response accepts physical impulse"),UWorldInteractionHitLibrary::ApplyBoundedWorldHitImpulse(Mesh,Data,1,120))) return false;
	class FCheck : public IAutomationLatentCommand
	{
		FAutomationTestBase* Test; TSharedPtr<RocketWorldHitTests::FWorld> Fixture; UPrimitiveComponent* Body;
	public:
		FCheck(FAutomationTestBase* T,TSharedPtr<RocketWorldHitTests::FWorld> F,UPrimitiveComponent* B):Test(T),Fixture(F),Body(B) {}
		bool Update() override
		{
			Fixture->World->Tick(LEVELTICK_All,1.f/60);
			const float Speed=Body->GetPhysicsLinearVelocity().Size();
			Test->TestTrue(TEXT("Light prop speed change is bounded by receiver's chosen cap"),Speed>0 && Speed<=120.1f);
			return true;
		}
	};
	ADD_LATENT_AUTOMATION_COMMAND(FCheck(this,F,Mesh));
	return true;
}
#endif
