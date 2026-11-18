#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Resources/HoseCableActor.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ColaManufacturingTests
{
struct FFixture
{
	UWorld* World=nullptr;
	APropManager* Manager=nullptr;
	FFixture()
	{
		const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
		World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
		Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
		Manager->WorldElapsedSeconds=100;
		for (int32 ID:{9101,9102})
		{
			FPropContentDefinition Content; Content.ContentID=ID; Content.ExpiryProfile.bHasExpiry=false;
			Content.PressureStrengthMultiplier=1; Manager->RegisterContentDefinition(Content);
		}
		FPropProductDefinition Bottle;
		Bottle.ProductID=9201; Bottle.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
		Bottle.CustomActiveActorClass=AColaPropActor::StaticClass(); Bottle.bEnableInstanceUniqueData=true;
		Bottle.BottleMaterialType=EBottleMaterialType::Plastic; Bottle.ConsumableProfile.NumOfPortions=4;
		Bottle.DefaultUniqueData.EventData.ContentID=9101; Bottle.DefaultUniqueData.EventData.ContentLevel=.75f;
		Bottle.DefaultUniqueData.EventData.CapSealed=0; Bottle.DefaultUniqueData.EventData.Strength=1;
		Bottle.DefaultUniqueData.EventData.bClean=true; Bottle.SupportedContentIDs={9101};
		Bottle.PressureFailureThreshold=2; Bottle.MaxOverfillLevel=1.7f;
		Manager->RegisterProductDefinition(Bottle);
		FPropProductDefinition Glass=Bottle; Glass.ProductID=9204;
		Glass.BottleMaterialType=EBottleMaterialType::Glass; Glass.BurstBrokenProductID=0;
		Manager->RegisterProductDefinition(Glass);
		FPropProductDefinition Cap=Bottle; Cap.ProductID=9202; Cap.bIsBottleCap=true;
		Cap.CustomActiveActorClass=AActivePropActor::StaticClass(); Cap.ConsumableProfile.NumOfPortions=0;
		Cap.DefaultUniqueData.EventData.ContentID=0; Cap.DefaultUniqueData.EventData.ContentLevel=0;
		Cap.DefaultCapType=EBottleCapType::Twist; Cap.SupportedContentIDs.Reset();
		Manager->RegisterProductDefinition(Cap);
		FPropProductDefinition Source=Bottle; Source.ProductID=9203; Source.CustomActiveActorClass=AActivePropActor::StaticClass();
		Source.ConsumableProfile.NumOfPortions=100; Source.DefaultUniqueData.EventData.ContentLevel=1;
		Source.DefaultUniqueData.EventData.ContentID=9102; Source.SupportedContentIDs.Reset();
		Manager->RegisterProductDefinition(Source);
	}
	~FFixture()
	{
		if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	AColaPropActor* Bottle(FVector Location=FVector(0,0,1000),int32 ProductID=9201)
	{
		const int32 ID=Manager->GenerateProductInstance(ProductID,FTransform(Location));
		AActivePropActor* Active=nullptr; Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active);
		auto* Result=Cast<AColaPropActor>(Active);
		if (Result)
		{
			Result->PropMesh->SetSimulatePhysics(false); Result->bUseOriginalRocketFlight=true;
			Result->RocketComp->SetAllDebugEnabled(false); Result->RocketComp->TippingSettings.bEnableTipping=false;
			Result->RocketComp->DraggingSettings.bEnableDragging=false;
			FPropEventSyncDelta Manufacturing; Manufacturing.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal;
			Manufacturing.bManualFillPressure=true; Manager->SubmitInstanceEventUpdate(ID,Manufacturing);
		}
		return Result;
	}
	bool Seal(AColaPropActor* Bottle)
	{
		FPropEventSyncDelta Delta;
		return Bottle->BuildManufacturingSealDelta(Delta) && Manager->SubmitInstanceEventUpdate(Bottle->ISID,Delta);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaSealReleaseTest,"ColossalFizzCo.Props.Manufacturing.SealRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FColaSealReleaseTest::RunTest(const FString& Parameters)
{
	ColaManufacturingTests::FFixture F; if (!F.World) return false;
	auto* Bottle=F.Bottle(); if (!TestNotNull(TEXT("Native bottle"),Bottle)) return false;
	Bottle->RemoveBottleContent();
	if (!TestTrue(TEXT("Bottle supplies the temporary seal transaction"),F.Seal(Bottle))) return false;
	TestTrue(TEXT("Seal begins armed instead of consuming cap-fitting time"),F.Manager->FindUniqueData(Bottle->ISID)->EventData.bTemporarySealArmed);
	F.Manager->WorldElapsedSeconds+=20; Bottle->UpdatePlasticPressure(.01f);
	TestTrue(TEXT("Prepared empty bottle keeps its seal armed until used"),F.Manager->FindUniqueData(Bottle->ISID)->EventData.bTemporarySealArmed);
	TestEqual(TEXT("Prepared seal retains full integrity"),Bottle->GetManufacturingSealIntegrity(),1.f);
	FPropEventSyncDelta Contents; Contents.SetFlag(EPropEventDirtyMask::ContentLevel); Contents.SetFlag(EPropEventDirtyMask::ContentID);
	Contents.ContentLevel=.75f; Contents.ContentID=9101; F.Manager->SubmitInstanceEventUpdate(Bottle->ISID,Contents);
	F.Manager->SetBottleAgitationPressure(Bottle->ISID,.8f);
	const FString Slot=TEXT("Automation_ArmedSeal_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!TestTrue(TEXT("Armed seal state saves"),F.Manager->SavePropDatabase(Slot,0))) return false;
	const auto* Save=Cast<UPropDatabaseSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
	const auto* Saved=Save ? Save->WorldSaveData.SavedInstances.FindByPredicate([&](const auto& Item){return Item.ISID==Bottle->ISID;}) : nullptr;
	TestTrue(TEXT("Save snapshot retains the armed seal without an expiry deadline"),Saved && Saved->UniqueData.EventData.bTemporarySealArmed && Saved->UniqueData.EventData.TemporarySealUntil==0);
	UGameplayStatics::DeleteGameInSlot(Slot,0);
	TestFalse(TEXT("Armed seal does not trigger pressure failure"),Bottle->RocketComp->IsPoweredFlight());
	Bottle->NotifyManufacturingLeverReleased();
	const double Deadline=F.Manager->FindUniqueData(Bottle->ISID)->EventData.TemporarySealUntil;
	TestTrue(TEXT("Lever release starts future expiry"),Deadline>F.Manager->WorldElapsedSeconds);
	TestFalse(TEXT("Released seal is no longer armed"),F.Manager->FindUniqueData(Bottle->ISID)->EventData.bTemporarySealArmed);
	Bottle->NotifyManufacturingLeverReleased();
	TestEqual(TEXT("Repeat releases cannot refresh expiry"),F.Manager->FindUniqueData(Bottle->ISID)->EventData.TemporarySealUntil,Deadline);
	F.Manager->WorldElapsedSeconds=(F.Manager->WorldElapsedSeconds+Deadline)*.5;
	TestTrue(TEXT("Seal integrity degrades during cap-fitting window"),Bottle->GetManufacturingSealIntegrity()>0 && Bottle->GetManufacturingSealIntegrity()<1);
	F.Manager->WorldElapsedSeconds=Deadline+.01; Bottle->UpdatePlasticPressure(.01f);
	TestEqual(TEXT("Expired seal integrity reaches zero"),Bottle->GetManufacturingSealIntegrity(),0.f);
	TestTrue(TEXT("Unsettled pressure at expiry uses the normal bottle rocket failure"),Bottle->RocketComp->IsPoweredFlight());
	TestFalse(TEXT("Default failure uses original BP_Cola flight"),Bottle->RocketComp->IsPressureThrustActive());
	TestFalse(TEXT("Used seal cannot be installed again"),Bottle->CanApplyManufacturingSeal());
	Bottle->RemoveBottleContent(); Bottle->RocketComp->StopRocket(true);
	TestTrue(TEXT("Surviving empty settled bottle can be refilled without recycling its actor"),Bottle->CanAcceptManufacturingContent(9101));
	Contents.ContentLevel=1.2f; F.Manager->SubmitInstanceEventUpdate(Bottle->ISID,Contents);
	Bottle->OnManufacturingLiquidReceived(0);
	TestTrue(TEXT("Reused bottle can dispatch a new overfill failure episode"),Bottle->RocketComp->IsPoweredFlight());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaSealHitchExpiryTest,"ColossalFizzCo.Props.Manufacturing.SealHitchExpiry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FColaSealHitchExpiryTest::RunTest(const FString& Parameters)
{
	ColaManufacturingTests::FFixture F; if (!F.World) return false;
	auto* Unsafe=F.Bottle(); if (!Unsafe) return false;
	Unsafe->TemporarySealDuration=1; Unsafe->TemporarySealVentRate=.02f; Unsafe->ManualFillVentRate=1;
	F.Manager->SetBottleAgitationPressure(Unsafe->ISID,.8f);
	if (!F.Seal(Unsafe)) return false;
	F.Manager->WorldElapsedSeconds+=5; Unsafe->UpdatePlasticPressure(5);
	TestTrue(TEXT("Hitch crossing expiry evaluates pressure before post-expiry open venting"),Unsafe->RocketComp->IsPoweredFlight());
	TestEqual(TEXT("Hitch expiry still exhausts the seal"),Unsafe->GetManufacturingSealIntegrity(),0.f);
	auto* Safe=F.Bottle(FVector(0,500,1000)); if (!Safe) return false;
	Safe->TemporarySealDuration=1; Safe->TemporarySealVentRate=.02f; Safe->ManualFillVentRate=1;
	F.Manager->SetBottleAgitationPressure(Safe->ISID,.11f);
	if (!F.Seal(Safe)) return false;
	F.Manager->WorldElapsedSeconds+=5; Safe->UpdatePlasticPressure(5);
	TestFalse(TEXT("Pressure that settled while seal was intact remains safe across a hitch"),Safe->RocketComp->IsPoweredFlight());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaSealSafeAndCapTest,"ColossalFizzCo.Props.Manufacturing.SafeExpiryAndCap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FColaSealSafeAndCapTest::RunTest(const FString& Parameters)
{
	ColaManufacturingTests::FFixture F; if (!F.World) return false;
	auto* Safe=F.Bottle(); if (!Safe || !F.Seal(Safe)) return false;
	F.Manager->SetBottleAgitationPressure(Safe->ISID,.05f); Safe->NotifyManufacturingLeverReleased();
	F.Manager->WorldElapsedSeconds=F.Manager->FindUniqueData(Safe->ISID)->EventData.TemporarySealUntil+1;
	Safe->UpdatePlasticPressure(.01f);
	TestFalse(TEXT("Pressure settled before expiry does not launch"),Safe->RocketComp->IsPoweredFlight());
	TestEqual(TEXT("Safe expiry preserves liquid"),Safe->GetContentLevel(),.75f);
	auto* Capped=F.Bottle(FVector(0,500,1000)); if (!Capped || !F.Seal(Capped)) return false;
	F.Manager->SetBottleAgitationPressure(Capped->ISID,.4f); Capped->NotifyManufacturingLeverReleased();
	const double Deadline=F.Manager->FindUniqueData(Capped->ISID)->EventData.TemporarySealUntil;
	const int32 Cap=F.Manager->GenerateProductInstance(9202,FTransform(Capped->GetActorLocation()+FVector(20,0,0)));
	TestTrue(TEXT("Bottle accepts compatible physical cap during seal grace"),Capped->AttachManufacturingCap(Cap));
	TestTrue(TEXT("Bottle permits manual rotation while temporary seal protects pressure"),Capped->AdvanceManufacturingCap(.5f,false));
	TestTrue(TEXT("Rotation retains a partial physical closure"),Capped->GetCapSealed()>0 && Capped->GetCapSealed()<1);
	TestTrue(TEXT("Bottle completes closure during seal grace"),Capped->AdvanceManufacturingCap(.5f,false));
	TestEqual(TEXT("Cap is fully secured"),Capped->GetCapSealed(),1.f);
	F.Manager->WorldElapsedSeconds=Deadline+1; Capped->UpdatePlasticPressure(.01f);
	TestFalse(TEXT("Secured cap prevents temporary-seal expiry launch"),Capped->RocketComp->IsPoweredFlight());
	F.Manager->SetBottleAgitationPressure(Capped->ISID,3); Capped->AddAgitationPressure(.01f);
	TestTrue(TEXT("Secured cap still obeys bottle structural pressure limit"),Capped->RocketComp->IsPoweredFlight());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaManufacturingContentTest,"ColossalFizzCo.Props.Manufacturing.ContentAndAuthority",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FColaManufacturingContentTest::RunTest(const FString& Parameters)
{
	ColaManufacturingTests::FFixture F; if (!F.World) return false;
	auto* Bottle=F.Bottle(); if (!Bottle) return false; Bottle->RemoveBottleContent();
	const int32 Source=F.Manager->GenerateProductInstance(9203,FTransform(FVector(100,0,1000)));
	const float SourceLevel=F.Manager->FindUniqueData(Source)->EventData.ContentLevel;
	TestFalse(TEXT("Bottle rejects unsupported registered content"),Bottle->CanAcceptManufacturingContent(9102));
	auto* Equipment=F.World->SpawnActor<AActor>(); auto* Filler=NewObject<UBottleFillingComponent>(Equipment);
	Equipment->AddInstanceComponent(Filler); Filler->MaxConnectionDistance=3000; Filler->RegisterComponent();
	TestFalse(TEXT("Filler cannot begin unsupported bottle transfer"),Filler->StartFilling(Source,Bottle->ISID));
	TestEqual(TEXT("Rejected content does not debit supply"),F.Manager->FindUniqueData(Source)->EventData.ContentLevel,SourceLevel);
	TestEqual(TEXT("Rejected content leaves bottle empty"),Bottle->GetContentLevel(),0.f);
	TestTrue(TEXT("Bottle accepts its supported content"),Bottle->CanAcceptManufacturingContent(9101));
	Bottle->SwapRoles(); FPropEventSyncDelta Delta;
	TestFalse(TEXT("Client cannot install authoritative manufacturing seal"),Bottle->BuildManufacturingSealDelta(Delta));
	Bottle->NotifyManufacturingLeverReleased(); Bottle->SwapRoles();
	TestFalse(TEXT("Client release leaves canonical seal unchanged"),F.Manager->FindUniqueData(Bottle->ISID)->EventData.bTemporarySealUsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FManualCradleSequenceTest,"ColossalFizzCo.Shop.Manufacturing.CradleSequence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FManualCradleSequenceTest::RunTest(const FString& Parameters)
{
	ColaManufacturingTests::FFixture F; if (!F.World) return false;
	auto* Machine=F.World->SpawnActor<AManualColaFillingMachine>(); Machine->PropMesh->SetSimulatePhysics(false);
	auto* Bottle=F.Bottle(Machine->BottleHolderPoint->GetComponentLocation()); if (!Bottle) return false;
	F.Manager->InitializeInstanceTransform(Bottle->ISID,FTransform(Machine->BottleHolderPoint->GetComponentQuat(),Machine->BottleHolderPoint->GetComponentLocation()));
	Bottle->SetSeatedInFiller(true); Machine->Production->View.SeatedBottleISID=Bottle->ISID;
	auto* Player=F.World->SpawnActor<ACharacter>(); Player->SetActorLocation(Machine->GetActorLocation()+FVector(0,-100,150));
	Player->SetActorEnableCollision(false); Player->GetCharacterMovement()->DisableMovement();
	auto* Controller=F.World->SpawnActor<APlayerController>(); Controller->Possess(Player);
	auto* Interaction=NewObject<UShopPlayerInteractionComponent>(Player); Player->AddInstanceComponent(Interaction); Interaction->RegisterComponent();
	Machine->Production->View.LeverOperator=Player; Machine->Production->View.bFlowStopped=true;
	Machine->BottleFiller->LeverOperator=Player; Machine->BottleFiller->LeverExpiresAt=100;
	Machine->UpdateCradleMotion(0);
	TestTrue(TEXT("Resting cradle points the bottle neck above its bottom"),Machine->BottleHolderPoint->GetUpVector().Z>0);
	TestFalse(TEXT("Resting cradle cannot open filling flow"),Machine->IsCradleInFillPosition());
	Machine->Production->View.bLeverHeld=true;
	Machine->UpdateCradleMotion(Machine->CradleTravelSeconds*.5f);
	TestTrue(TEXT("Lever pull moves cradle through an intermediate pose"),Machine->CradleAlpha>0 && Machine->CradleAlpha<1);
	TestFalse(TEXT("Partial tilt does not permit filling"),Machine->IsCradleInFillPosition());
	Machine->UpdateCradleMotion(Machine->CradleTravelSeconds);
	TestTrue(TEXT("Completed tilt permits filling"),Machine->IsCradleInFillPosition());
	TestTrue(TEXT("Filling pose places neck lower than bottle bottom"),Machine->BottleHolderPoint->GetUpVector().Z<0);
	if (!TestTrue(TEXT("Held nozzle permits installing an armed seal on a filled bottle"),F.Seal(Bottle))) return false;
	F.Manager->SetBottleAgitationPressure(Bottle->ISID,.8f);
	F.Manager->WorldElapsedSeconds+=20; Bottle->UpdatePlasticPressure(.01f);
	TestTrue(TEXT("Held lever keeps filled bottle seal armed beyond its release duration"),Bottle->LocalUniqueData.EventData.bTemporarySealArmed);
	TestEqual(TEXT("Held lever preserves full seal integrity"),Bottle->GetManufacturingSealIntegrity(),1.f);
	Machine->Production->EndManualLever();
	TestFalse(TEXT("Lever release begins seal degradation"),Bottle->LocalUniqueData.EventData.bTemporarySealArmed);
	Machine->UpdateCradleMotion(Machine->CradleTravelSeconds);
	TestTrue(TEXT("Lever release returns bottle neck upward"),Machine->BottleHolderPoint->GetUpVector().Z>0);
	TestFalse(TEXT("Return pose closes the filling gate"),Machine->IsCradleInFillPosition());
	Bottle->bIsInMotion=true;
	TestFalse(TEXT("Moving bottle cannot be recaptured by cradle alignment"),Machine->Production->IsBottleAligned(Bottle->ISID));
	Bottle->bIsInMotion=false;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaManufacturingFailureStopsFlowTest,"ColossalFizzCo.Shop.Manufacturing.FailureStopsFlow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FColaManufacturingFailureStopsFlowTest::RunTest(const FString& Parameters)
{
	ColaManufacturingTests::FFixture F; if (!F.World) return false;
	FPropProductDefinition Equipment; Equipment.ProductID=9205; Equipment.bEnableInstanceUniqueData=true;
	Equipment.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
	Equipment.CustomActiveActorClass=AManualColaFillingMachine::StaticClass(); Equipment.ConsumableProfile.NumOfPortions=0;
	Equipment.DefaultUniqueData.EventData.Strength=1; F.Manager->RegisterProductDefinition(Equipment);
	const int32 MachineID=F.Manager->GenerateProductInstance(9205,FTransform::Identity);
	AActivePropActor* ActiveMachine=nullptr;
	if (!F.Manager->PromoteInstanceToASM(MachineID,EPropActivationReason::Interaction,false,ActiveMachine)) return false;
	auto* Machine=Cast<AManualColaFillingMachine>(ActiveMachine); if (!Machine) return false;
	Machine->PropMesh->SetSimulatePhysics(false);
	if (!Machine->PrepareReceivingHosePort(0,9101)) return false;
	auto* Bottle=F.Bottle(Machine->BottleHolderPoint->GetComponentLocation(),9204); if (!Bottle) return false;
	F.Manager->InitializeInstanceTransform(Bottle->ISID,FTransform(Machine->BottleHolderPoint->GetComponentQuat(),Machine->BottleHolderPoint->GetComponentLocation()));
	Bottle->SetSeatedInFiller(true); Bottle->ManualFillingStation=Machine->Production; Machine->Production->View.SeatedBottleISID=Bottle->ISID;
	auto* Player=F.World->SpawnActor<ACharacter>(); Player->SetActorLocation(Machine->GetActorLocation()+FVector(0,-100,150));
	Player->SetActorEnableCollision(false); Player->GetCharacterMovement()->DisableMovement();
	auto* Controller=F.World->SpawnActor<APlayerController>(); Controller->Possess(Player);
	auto* Interaction=NewObject<UShopPlayerInteractionComponent>(Player); Player->AddInstanceComponent(Interaction); Interaction->RegisterComponent();
	Machine->Production->View.LeverOperator=Player; Machine->Production->View.bLeverHeld=true;
	Machine->Production->View.bFlowStopped=true; Machine->BottleFiller->LeverOperator=Player; Machine->BottleFiller->LeverExpiresAt=100;
	Machine->UpdateCradleMotion(Machine->CradleTravelSeconds);
	const int32 Source=F.Manager->GenerateProductInstance(9203,FTransform(Machine->GetActorLocation()+FVector(100,0,100)));
	FPropEventSyncDelta Contents; Contents.SetFlag(EPropEventDirtyMask::ContentID); Contents.ContentID=9101;
	if (!F.Manager->SubmitInstanceEventUpdate(Source,Contents)) return false;
	AActivePropActor* SourceActor=nullptr;
	if (!F.Manager->PromoteInstanceToASM(Source,EPropActivationReason::Interaction,false,SourceActor)) return false;
	SourceActor->PropMesh->SetSimulatePhysics(false);
	auto* SourcePort=NewObject<UResourceTransferEndpointComponent>(SourceActor); SourceActor->AddInstanceComponent(SourcePort);
	SourcePort->SetupAttachment(SourceActor->GetRootComponent()); SourcePort->RegisterComponent();
	if (!SourcePort->ConfigureManagedContent(F.Manager,Source,9101)) return false;
	auto* Hose=F.World->SpawnActor<AHoseCableActor>(); Hose->Transfer->SetComponentTickEnabled(false);
	Hose->Transfer->MaxUnitsPerSecond=40; Hose->Transfer->BurstPressureThreshold=100;
	auto* ReceivingPort=Machine->GetReceivingHosePort(0); ReceivingPort->MaxIntakeUnitsPerSecond=100;
	if (!TestTrue(TEXT("Physical hose connects the canonical source to the manual machine"),Hose->ConnectStart(SourcePort) && Hose->ConnectEnd(ReceivingPort))) return false;
	Machine->Production->View.ConnectedSourceISID=0;
	auto* Filler=Machine->BottleFiller.Get(); Filler->PortionsPerSecond=40;
	if (!TestTrue(TEXT("Supported content starts filling aligned nonplastic bottle"),Filler->StartFilling(0,Bottle->ISID))) return false;
	// One fixed step adds two portions and crosses the bottle's overfill threshold.
	// The second step in this tick must not refill a failed bottle that remains an actor.
	Filler->TickComponent(.1f,LEVELTICK_All,nullptr);
	TestEqual(TEXT("Nonplastic bottle selects its native pressure burst failure"),Bottle->AcceptedFailureType,EContainmentFailureType::OverpressureBurst);
	TestEqual(TEXT("No replacement product is needed to stop a failed bottle"),Bottle->ProductID,9204);
	TestFalse(TEXT("Accepted bottle failure immediately closes liquid flow"),Filler->bIsFilling);
	TestEqual(TEXT("Failure removes the active transfer session"),F.Manager->FillSessions.Num(),0);
	TestFalse(TEXT("Station releases failed bottle restraint"),Bottle->IsSeatedInFiller());
	TestEqual(TEXT("Failure leaves bottle empty rather than refilling it"),Bottle->GetContentLevel(),0.f);
	const float Remaining=F.Manager->FindUniqueData(Source)->EventData.ContentLevel;
	TestTrue(TEXT("Only the triggering step debits source content"),FMath::IsNearlyEqual(Remaining,.98f));
	Filler->TickComponent(1.f,LEVELTICK_All,nullptr);
	TestEqual(TEXT("Stopped failure cannot debit source on later ticks"),F.Manager->FindUniqueData(Source)->EventData.ContentLevel,Remaining);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaArmedSealDeltaTest,"ColossalFizzCo.Props.Manufacturing.SealDelta",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FColaArmedSealDeltaTest::RunTest(const FString& Parameters)
{
	ColaManufacturingTests::FFixture F; if (!F.World) return false;
	auto* Bottle=F.Bottle(); if (!Bottle) return false;
	FPropEventSyncDelta Original; Original.ISID=Bottle->ISID; Original.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal;
	Original.bTemporarySealUsed=true; Original.bTemporarySealArmed=true; Original.bManualFillPressure=true;
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); bool Success=false; Original.NetSerialize(Writer,nullptr,Success);
	TestTrue(TEXT("Armed seal serializes in reliable event packet"),Success);
	FMemoryReader Reader(Bytes); FPropEventSyncDelta Decoded; Decoded.NetSerialize(Reader,nullptr,Success);
	TestTrue(TEXT("Armed seal deserializes without archive errors"),Success && !Reader.IsError());
	TestTrue(TEXT("Wire format preserves all manufacturing flags"),Decoded.bTemporarySealArmed && Decoded.bTemporarySealUsed && Decoded.bManualFillPressure);
	TestTrue(TEXT("Decoded state commits to canonical manager data"),F.Manager->SubmitInstanceEventUpdate(Bottle->ISID,Decoded));
	TestTrue(TEXT("Canonical and active bottle caches both retain armed state"),F.Manager->FindUniqueData(Bottle->ISID)->EventData.bTemporarySealArmed && Bottle->LocalUniqueData.EventData.bTemporarySealArmed);
	Decoded.bTemporarySealArmed=false; Decoded.TemporarySealUntil=F.Manager->WorldElapsedSeconds+4;
	TestTrue(TEXT("Release transition commits deadline"),F.Manager->SubmitInstanceEventUpdate(Bottle->ISID,Decoded));
	TestTrue(TEXT("Release clears armed state without losing used marker"),!Bottle->LocalUniqueData.EventData.bTemporarySealArmed && Bottle->LocalUniqueData.EventData.bTemporarySealUsed && Bottle->LocalUniqueData.EventData.TemporarySealUntil==Decoded.TemporarySealUntil);
	return true;
}
#endif
