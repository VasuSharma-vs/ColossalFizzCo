#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Props/ColaPropActor.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopTableUpgradeTerminal.h"
#include "Shop/SocketColaFillingMachine.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTerminalViewTest,"ColossalFizzCo.Shop.CheckoutViewInteraction",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopTerminalViewTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Pawn=World->SpawnActor<ACharacter>(); auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
	auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	auto* Counter=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Counter);
	Counter->SetRootComponent(Root); Root->RegisterComponent(); Counter->SetActorLocation(FVector(0,700,460));
	auto* Housing=NewObject<UBoxComponent>(Counter); Counter->AddInstanceComponent(Housing); Housing->SetupAttachment(Root);
	Housing->SetRelativeLocation(FVector(0,0,-230)); Housing->SetBoxExtent(FVector(850,620,230));
	Housing->SetCollisionProfileName(TEXT("BlockAll")); Housing->RegisterComponent();
	auto* Control=NewObject<USceneComponent>(Counter); Counter->AddInstanceComponent(Control); Control->SetupAttachment(Root);
	Control->ComponentTags.Add(TEXT("ShopInteractionPoint")); Control->SetRelativeLocation(FVector(350,-610,20)); Control->RegisterComponent();
	auto* Scanner=NewObject<UShopScannerComponent>(Counter); Counter->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	const FVector Position(350,-80,556), Eye(350,-80,620), Direction=FVector(0,200,-160).GetSafeNormal();
	Pawn->SetActorLocation(Position);
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CheckoutLegacyTrace),false,Pawn);
	TestFalse(TEXT("Legacy 150cm trace cannot hit the counter from the player access deck"),World->LineTraceSingleByChannel(Hit,Eye,Eye+Direction*150,ECC_Visibility,Params));
	TestTrue(TEXT("Existing station reach accepts the front controls"),Actions->IsStationReachable(Counter));
	TestEqual(TEXT("Station-specific view trace reaches the actual giant desk collision"),Actions->FindTerminalInView(Eye,Direction),Counter);
	TestNull(TEXT("Looking away cannot select a nearby checkout"),Actions->FindTerminalInView(Eye,-Direction));
	TestNull(TEXT("Invalid view direction is rejected"),Actions->FindTerminalInView(Eye,FVector::ZeroVector));
	Pawn->SetActorLocation(FVector(-700,-80,556));
	TestNull(TEXT("Looking at a distant desk corner does not bypass the authored control reach"),Actions->FindTerminalInView(FVector(-700,-80,620),Direction));
	Pawn->SetActorLocation(Position);
	auto* Blocker=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(40,20,40)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
	Blocker->SetActorLocation(Eye+Direction*70);
	FHitResult BlockedHit;
	TestNull(TEXT("An obstructing object prevents opening through it"),Actions->FindTerminalInView(Eye,Direction,&BlockedHit));
	TestEqual(TEXT("The view trace reports the first blocker"),BlockedHit.GetActor(),Blocker);
	TestNull(TEXT("An unrelated blocker does not select a filling table"),Actions->FindAimedFillingTable(BlockedHit));
	Blocker->Destroy();

	auto* Table=World->SpawnActor<ASocketColaFillingMachine>(FVector(2400,0,560),FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The filling table is available"),Table)) return false;
	Table->ISID=310;
	const FVector Cradle=Table->BottleHolderPoint->GetComponentLocation();
	const FVector TableEye=Cradle-FVector(0,220,0);
	const FVector TableDirection(0,1,0);
	Pawn->SetActorLocation(TableEye-FVector(0,0,60));
	FHitResult TableHit;
	TestEqual(TEXT("The view trace reaches the filling table"),Actions->FindTerminalInView(TableEye,TableDirection,&TableHit),static_cast<AActor*>(Table));
	TestEqual(TEXT("A direct hit selects the passive filling readout"),Actions->FindAimedFillingTable(TableHit),Table);
	FHitResult Miss;
	Actions->FindTerminalInView(TableEye,-TableDirection,&Miss);
	TestNull(TEXT("Looking away does not select a nearby filling table"),Actions->FindAimedFillingTable(Miss));

	auto* Bottle=World->SpawnActor<AColaPropActor>(Cradle,FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The bottle is available"),Bottle)) return false;
	Bottle->ISID=311;
	Bottle->SetSeatedInFiller(true);
	Table->Production->View.SeatedBottleISID=Bottle->ISID;
	Table->BottleInputVolume->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
	auto* BottleTraceVolume=NewObject<UBoxComponent>(Bottle);
	Bottle->AddInstanceComponent(BottleTraceVolume);
	BottleTraceVolume->SetupAttachment(Bottle->GetRootComponent());
	BottleTraceVolume->SetBoxExtent(FVector(15));
	BottleTraceVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BottleTraceVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	BottleTraceVolume->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	BottleTraceVolume->RegisterComponent();
	FHitResult BottleHit;
	TestNull(TEXT("A bottle hit does not change the interaction target"),Actions->FindTerminalInView(TableEye,TableDirection,&BottleHit));
	TestEqual(TEXT("The first blocker is the seated bottle"),BottleHit.GetActor(),static_cast<AActor*>(Bottle));
	TestEqual(TEXT("A seated bottle hit identifies its filling table"),Actions->FindAimedFillingTable(BottleHit),Table);
	Bottle->SetSeatedInFiller(false);
	TestNull(TEXT("A loose bottle cannot select a filling table"),Actions->FindAimedFillingTable(BottleHit));

	Pawn->SetActorLocation(Position);
	PC->UnPossess();
	TestNull(TEXT("An unpossessed pawn cannot open a shop station"),Actions->FindTerminalInView(Eye,Direction));

	PC->Possess(Pawn);
	const FVector TerminalLocation(4000, 0, 0);
	auto* TableTerminal = World->SpawnActor<AShopTableUpgradeTerminal>(TerminalLocation, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The table upgrade terminal is placeable"), TableTerminal)) return false;
	const FVector TerminalEye = TerminalLocation + FVector(220, 0, 100);
	const FVector FacingTerminal(-1, 0, 0);
	Pawn->SetActorLocation(TerminalEye - FVector(0, 0, 60));
	TestEqual(TEXT("A direct E trace selects the table upgrade terminal"),
		Actions->FindTerminalInView(TerminalEye, FacingTerminal), static_cast<AActor*>(TableTerminal));
	TestNull(TEXT("Looking away misses the table upgrade terminal"),
		Actions->FindTerminalInView(TerminalEye, -FacingTerminal));
	Pawn->SetActorLocation(TerminalEye - FVector(0, 0, 60) - FacingTerminal * 500);
	TestNull(TEXT("A distant table upgrade terminal cannot be selected"),
		Actions->FindTerminalInView(TerminalEye - FacingTerminal * 500, FacingTerminal));
	return true;
}
#endif
