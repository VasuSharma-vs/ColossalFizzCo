#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourcePumpComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/CableMovementConstraintComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Resources/HoseCableActor.h"
#include "Resources/ResourcePortLibrary.h"
#include "Shop/ShopPlayerInteractionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CableHoseTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
struct FFixture
{
	UWorld* World=nullptr;
	FFixture()
	{
		const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
		World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	}
	~FFixture()
	{
		if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	AActor* Actor(FVector Position=FVector::ZeroVector)
	{
		auto* Result=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Result);
		Result->SetRootComponent(Root); Result->AddInstanceComponent(Root); Root->RegisterComponent(); Result->SetActorLocation(Position);
		return Result;
	}
	UResourceTransferEndpointComponent* Endpoint(EResourceTransportCategory Category,int32 ResourceID,float Capacity,float Stored,AActor* Owner=nullptr)
	{
		if (!Owner) Owner=Actor();
		auto* Result=NewObject<UResourceTransferEndpointComponent>(Owner); Owner->AddInstanceComponent(Result);
		Result->SetupAttachment(Owner->GetRootComponent()); Result->Category=Category; Result->ResourceID=ResourceID;
		Result->bCanSupply=Result->bCanReceive=true; Result->CapacityUnits=Capacity; Result->RegisterComponent(); Result->SetStoredUnits(Stored);
		return Result;
	}
	UHoseTransferComponent* Hose(EResourceTransportCategory Category,int32 ResourceID)
	{
		auto* Owner=Actor(); auto* Result=NewObject<UHoseTransferComponent>(Owner); Owner->AddInstanceComponent(Result);
		Result->Category=Category; Result->ResourceID=ResourceID; Result->MaxUnitsPerSecond=10; Result->bTransferEnabled=true; Result->RegisterComponent();
		Result->BufferCapacityUnits=10; Result->BurstPressureThreshold=100; Result->BlockedPressureRisePerSecond=0;
		return Result;
	}
	AActor* PullBody(FVector Position,float Weight=10,float Resistance=0,bool Fixed=false)
	{
		auto* Result=World->SpawnActor<AActor>(); auto* Root=NewObject<UBoxComponent>(Result);
		Result->SetRootComponent(Root); Result->AddInstanceComponent(Root); Root->SetMobility(EComponentMobility::Movable);
		Root->SetBoxExtent(FVector(5)); Root->SetCollisionProfileName(TEXT("BlockAll")); Root->RegisterComponent(); Result->SetActorLocation(Position);
		auto* Response=NewObject<UCablePullResponseComponent>(Result); Result->AddInstanceComponent(Response);
		Response->WeightKg=Weight; Response->ResistanceNewtons=Resistance; Response->bFixed=Fixed; Response->RegisterComponent();
		return Result;
	}
	UCollisionCableComponent* Link(AActor* Start,AActor* End,float Length)
	{
		auto* Cable=NewObject<UCollisionCableComponent>(Start); Start->AddInstanceComponent(Cable); Cable->SetupAttachment(Start->GetRootComponent());
		Cable->NumSegments=4; Cable->CableLength=Length; Cable->EndLocation=FVector::ZeroVector;
		Cable->bAttachStart=Cable->bAttachEnd=true; Cable->bStartAttachmentIsWorldAnchor=Cable->bEndAttachmentIsWorldAnchor=false;
		Cable->bEnableCollision=Cable->bEnableSleeping=Cable->bEnablePointWeight=Cable->bEnableNetworkSync=false;
		Cable->bEnableMovementRestrictions=true; Cable->LengthSlackTolerance=0; Cable->CableGravityScale=0; Cable->RegisterComponent();
		if (!Cable->HasBegunPlay()) Cable->BeginPlay();
		Cable->SetAttachEndToComponent(End->GetRootComponent()); Cable->ResetCable();
		return Cable;
	}
	UCableMovementConstraintComponent* Adapter(AActor* Driver,UCollisionCableComponent* Seed)
	{
		auto* Result=Driver->FindComponentByClass<UCableMovementConstraintComponent>();
		if (!Result) { Result=NewObject<UCableMovementConstraintComponent>(Driver); Driver->AddInstanceComponent(Result); Result->RegisterComponent(); }
		Result->RegisterCable(Seed); return Result;
	}
	bool ManagedStocks(APropManager*& Manager,int32& Source,int32& Target)
	{
		Manager=World->SpawnActor<APropManager>(); if (!Manager) return false;
		Manager->bEnableAutosave=false; Manager->InitializePropManager(); Manager->WorldElapsedSeconds=100;
		FPropContentDefinition Content; Content.ContentID=9301; Content.ExpiryProfile.bHasExpiry=false; Manager->RegisterContentDefinition(Content);
		FPropProductDefinition Product; Product.ProductID=9311; Product.bEnableInstanceUniqueData=true;
		Product.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")); Product.CustomActiveActorClass=AActivePropActor::StaticClass();
		Product.ConsumableProfile.NumOfPortions=100; Product.MaxOverfillLevel=1; Product.DefaultUniqueData.EventData.ContentID=9301;
		Product.DefaultUniqueData.EventData.ContentLevel=.04f; Product.DefaultUniqueData.EventData.Strength=1;
		Product.DefaultUniqueData.EventData.bClean=true; Product.DefaultUniqueData.EventData.CapSealed=0; Product.SupportedContentIDs={9301};
		Manager->RegisterProductDefinition(Product);
		Product.ProductID=9312; Product.ConsumableProfile.NumOfPortions=4; Product.DefaultUniqueData.EventData.ContentID=0;
		Product.DefaultUniqueData.EventData.ContentLevel=0; Manager->RegisterProductDefinition(Product);
		Source=Manager->GenerateProductInstance(9311,FTransform(FVector(0,0,1000)));
		Target=Manager->GenerateProductInstance(9312,FTransform(FVector(200,0,1000)));
		FPropEventSyncDelta Age; Age.ExtendedDirtyMask=EPropExtendedEventMask::ContentCreationTime; Age.ContentCreationTime=10;
		Manager->SubmitInstanceEventUpdate(Source,Age); Manager->SetTemperature(Source,35);
		return Source>0 && Target>0;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseTypedRulesTest,"ColossalFizzCo.Hose.TypedRules",CableHoseTests::Flags)
bool FHoseTypedRulesTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Content,1,10,8);
	auto* Target=F.Endpoint(EResourceTransportCategory::Content,1,10,0);
	auto* Hose=F.Hose(EResourceTransportCategory::Content,1);
	Target->Category=EResourceTransportCategory::Fuel;
	TestFalse(TEXT("Matching resource number cannot connect incompatible transport categories"),Hose->ConnectEndpoints(Source,Target));
	Target->Category=EResourceTransportCategory::Content; Target->ResourceID=2;
	TestFalse(TEXT("Content identity must match at both ends"),Hose->ConnectEndpoints(Source,Target));
	Target->ResourceID=1; Target->bCanReceive=false;
	TestFalse(TEXT("Supply-only endpoint cannot be a receiving connection"),Hose->ConnectEndpoints(Source,Target));
	Target->bCanReceive=true;
	TestFalse(TEXT("Endpoint cannot transfer to itself"),Hose->ConnectEndpoints(Source,Source));
	TestEqual(TEXT("Rejected connections preserve source inventory"),Source->GetStoredUnits(),8.f);
	if (!TestTrue(TEXT("Matching category, resource and direction connect"),Hose->ConnectEndpoints(Source,Target))) return false;
	Target->ResourceID=2;
	TestEqual(TEXT("Resource changes after connection cannot move incompatible content"),Hose->TransferForDuration(1),0.f);
	TestEqual(TEXT("Invalidated connection cannot debit the source"),Source->GetStoredUnits(),8.f);
	TestEqual(TEXT("Invalidated connection cannot credit the target"),Target->GetStoredUnits(),0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseLocalConservationTest,"ColossalFizzCo.Hose.LocalConservation",CableHoseTests::Flags)
bool FHoseLocalConservationTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	for (const auto Category:{EResourceTransportCategory::Content,EResourceTransportCategory::Fuel,EResourceTransportCategory::Electricity})
	{
		auto* Source=F.Endpoint(Category,7,10,3); auto* Target=F.Endpoint(Category,7,2,0); auto* Hose=F.Hose(Category,7);
		if (!TestTrue(TEXT("Typed local stores connect"),Hose->ConnectEndpoints(Source,Target))) return false;
		TestEqual(TEXT("Partial acceptance reports actual remaining target capacity"),Hose->TransferForDuration(1),2.f);
		TestEqual(TEXT("Source debits units actually received by the hose"),Source->GetStoredUnits(),0.f);
		TestEqual(TEXT("Target receives exactly accepted units"),Target->GetStoredUnits(),2.f);
		TestEqual(TEXT("Unaccepted contents remain in finite hose inventory"),Hose->Buffer.Units,1.f);
		TestEqual(TEXT("Source, target and hose conserve the original amount"),Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,3.f);
		TestEqual(TEXT("Full target rejects further movement"),Hose->TransferForDuration(1),0.f);
		Hose->DisconnectEndpoints();
		TestEqual(TEXT("Disconnected hose cannot move resources"),Hose->TransferForDuration(1),0.f);
		TestEqual(TEXT("Disconnect retains trapped contents"),Hose->Buffer.Units,1.f);
		TestEqual(TEXT("Explicit purge releases exactly trapped liquid or energy"),Hose->PurgeContents(),1.f);
		TestEqual(TEXT("Repeating purge cannot duplicate a release"),Hose->PurgeContents(),0.f);
	}
	// Different stores on one equipment actor are distinct endpoints, not a self-transfer.
	auto* Equipment=F.Actor(); auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,8,10,5,Equipment);
	auto* Left=F.Endpoint(EResourceTransportCategory::Fuel,8,10,0,Equipment);
	auto* Right=F.Endpoint(EResourceTransportCategory::Fuel,8,10,0);
	auto* First=F.Hose(EResourceTransportCategory::Fuel,8); auto* Second=F.Hose(EResourceTransportCategory::Fuel,8);
	TestTrue(TEXT("Distinct stores on the same equipment actor may connect"),First->ConnectEndpoints(Source,Left));
	TestTrue(TEXT("A second hose may share the source store"),Second->ConnectEndpoints(Source,Right));
	TestEqual(TEXT("First branch consumes its requested rate"),First->TransferForDuration(.3f),3.f);
	TestEqual(TEXT("Second branch sees actual remaining source units"),Second->TransferForDuration(.3f),2.f);
	TestEqual(TEXT("Branching conserves total inventory without cached double debit"),Source->GetStoredUnits()+Left->GetStoredUnits()+Right->GetStoredUnits(),5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseAuthorityAndDisconnectTest,"ColossalFizzCo.Hose.AuthorityAndDisconnect",CableHoseTests::Flags)
bool FHoseAuthorityAndDisconnectTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Electricity,3,10,7);
	auto* Target=F.Endpoint(EResourceTransportCategory::Electricity,3,10,0);
	auto* Hose=F.Hose(EResourceTransportCategory::Electricity,3);
	Hose->GetOwner()->SwapRoles();
	TestFalse(TEXT("Client cannot establish authoritative resource connections"),Hose->ConnectEndpoints(Source,Target));
	Hose->GetOwner()->SwapRoles(); if (!Hose->ConnectEndpoints(Source,Target)) return false;
	Hose->GetOwner()->SwapRoles();
	TestEqual(TEXT("Client cannot move stored resources"),Hose->TransferForDuration(1),0.f);
	Hose->GetOwner()->SwapRoles();
	Hose->bTransferEnabled=false;
	TestEqual(TEXT("Closed transfer valve prevents flow"),Hose->TransferForDuration(1),0.f);
	Hose->bTransferEnabled=true; Hose->MaxUnitsPerSecond=-1;
	TestEqual(TEXT("Negative transfer rate cannot reverse or mint resources"),Hose->TransferForDuration(1),0.f);
	Hose->MaxUnitsPerSecond=10;
	TestEqual(TEXT("Negative elapsed time cannot move resources"),Hose->TransferForDuration(-1),0.f);
	Target->DestroyComponent();
	TestEqual(TEXT("Destroyed endpoint stops transfer before debit"),Hose->TransferForDuration(1),0.f);
	TestEqual(TEXT("All rejected transfers preserve source inventory"),Source->GetStoredUnits(),7.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseMotorAndBurstTest,"ColossalFizzCo.Hose.MotorStorageAndBurst",CableHoseTests::Flags)
bool FHoseMotorAndBurstTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,7,30,20);
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,7,10,10);
	auto* Motor=NewObject<UResourcePumpComponent>(Source->GetOwner()); Source->GetOwner()->AddInstanceComponent(Motor);
	Motor->PushPressure=1; Motor->RegisterComponent(); Source->ConnectedMotor=Motor;
	auto* Hose=F.Hose(EResourceTransportCategory::Fuel,7); Hose->BufferCapacityUnits=2; Hose->MaxUnitsPerSecond=2;
	Hose->bRequiresMotorDrive=true; Hose->BlockedPressureRisePerSecond=1;
	if (!TestTrue(TEXT("Full receiver remains a valid physical connection"),Hose->ConnectEndpoints(Source,Target))) return false;
	Hose->TransferForDuration(1);
	TestEqual(TEXT("Stopped required motor prevents intake"),Source->GetStoredUnits(),20.f);
	TestEqual(TEXT("Stopped required motor leaves hose empty"),Hose->Buffer.Units,0.f);
	Motor->bRunning=true; Hose->TransferForDuration(1);
	TestEqual(TEXT("Blocked receiver fills only the finite hose capacity"),Hose->Buffer.Units,2.f);
	TestEqual(TEXT("Only stored hose contents are debited from source"),Source->GetStoredUnits(),18.f);
	const float Pressure=Hose->CurrentPressure; Hose->TransferForDuration(1);
	TestTrue(TEXT("Running motor builds pressure against a full blocked hose"),Hose->CurrentPressure>Pressure);
	TestEqual(TEXT("Full hose cannot keep draining source inventory"),Source->GetStoredUnits(),18.f);
	Hose->bIntakeEnabled=false; Target->SetStoredUnits(0); Hose->TransferForDuration(1);
	TestEqual(TEXT("Drain-only mode empties trapped contents into receiver"),Hose->Buffer.Units,0.f);
	TestEqual(TEXT("Drain-only mode receives the actual trapped amount"),Target->GetStoredUnits(),2.f);
	TestEqual(TEXT("Drain-only mode does not draw fresh supply"),Source->GetStoredUnits(),18.f);
	Hose->bIntakeEnabled=true; Target->SetStoredUnits(10); Hose->TransferForDuration(1);
	const float SourceBeforeBurst=Source->GetStoredUnits(); const float Trapped=Hose->Buffer.Units;
	Hose->DisconnectEndpoints(); Hose->ResourceID=8; Source->ResourceID=Target->ResourceID=8;
	TestFalse(TEXT("Reconnection cannot relabel trapped fuel as a different resource"),Hose->ConnectEndpoints(Source,Target));
	Hose->ResourceID=7; Source->ResourceID=Target->ResourceID=7;
	if (!Hose->ConnectEndpoints(Source,Target)) return false;
	Motor->PushPressure=10; Hose->BurstPressureThreshold=4; Hose->BurstDelay=.1f;
	Hose->TransferForDuration(.2f);
	TestTrue(TEXT("Sustained motor overdrive bursts the hose"),Hose->bBurst);
	TestEqual(TEXT("Burst empties all trapped contents"),Hose->Buffer.Units,0.f);
	TestEqual(TEXT("Burst reports exactly the contents that were trapped"),Hose->TotalReleasedUnits,Trapped);
	TestEqual(TEXT("Burst of a full hose does not debit additional supply"),Source->GetStoredUnits(),SourceBeforeBurst);
	TestEqual(TEXT("Burst cannot be purged again"),Hose->PurgeContents(),0.f);
	Hose->TransferForDuration(1);
	TestEqual(TEXT("Burst hose cannot continue pumping"),Source->GetStoredUnits(),SourceBeforeBurst);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseManagedEscrowTest,"ColossalFizzCo.Hose.ManagedContentEscrow",CableHoseTests::Flags)
bool FHoseManagedEscrowTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	APropManager* Manager=nullptr; int32 Source=0,Target=0; if (!F.ManagedStocks(Manager,Source,Target)) return false;
	FHoseResourceBuffer Buffer; Buffer.Category=EResourceTransportCategory::Content; Buffer.ResourceID=9301;
	TestTrue(TEXT("Intake obeys finite buffer room"),FMath::IsNearlyEqual(Manager->IntakeHoseContent(Source,Buffer,2,3),2.f));
	TestEqual(TEXT("Buffered liquid preserves its creation time"),Buffer.ContentCreationTime,10.0);
	TestEqual(TEXT("Buffered liquid preserves its source temperature"),Buffer.ContentTemperature,35.f);
	TestEqual(TEXT("Partial delivery reports actual units"),Manager->DeliverHoseContent(Buffer,Target,1),1.f);
	TestEqual(TEXT("First delivery preserves original liquid age"),Manager->FindUniqueData(Target)->EventData.ContentCreationTime,10.0);
	Manager->SetTemperature(Source,15);
	FPropEventSyncDelta NewAge; NewAge.ExtendedDirtyMask=EPropExtendedEventMask::ContentCreationTime; NewAge.ContentCreationTime=20;
	Manager->SubmitInstanceEventUpdate(Source,NewAge);
	TestEqual(TEXT("Top-up accepts only remaining buffer space"),Manager->IntakeHoseContent(Source,Buffer,2,2),1.f);
	TestTrue(TEXT("Buffer mixing conserves temperature by actual units"),FMath::IsNearlyEqual(Buffer.ContentTemperature,25.f));
	TestEqual(TEXT("Buffer top-up cannot freshen older contents"),Buffer.ContentCreationTime,10.0);
	TestEqual(TEXT("Mixed contents deliver without loss"),Manager->DeliverHoseContent(Buffer,Target,2),2.f);
	TestTrue(TEXT("Receiving container conserves mixed heat"),FMath::IsNearlyEqual(Manager->FindUniqueData(Target)->ContinuousData.Temperature,85.f/3.f,.001f));
	TestTrue(TEXT("Last source portion enters the hose"),FMath::IsNearlyEqual(Manager->IntakeHoseContent(Source,Buffer,2,2),1.f,.001f));
	const FGuid Episode=FGuid::NewGuid();
	TestTrue(TEXT("Managed purge releases exact remaining buffer amount"),FMath::IsNearlyEqual(Manager->ReleaseHoseContent(Buffer,Episode,FVector(0,0,100)),1.f,.001f));
	TestEqual(TEXT("Managed purge is idempotent"),Manager->ReleaseHoseContent(Buffer,Episode,FVector(0,0,100)),0.f);
	double Released=0; for (const auto& Item:Manager->ReleasedLiquids) Released+=Item.Portions;
	const double Total=Manager->FindUniqueData(Source)->EventData.ContentLevel*100+Manager->FindUniqueData(Target)->EventData.ContentLevel*4+Buffer.Units+Released;
	TestTrue(TEXT("Canonical source, target, hose and release ledger conserve original contents"),FMath::IsNearlyEqual(Total,4.0,.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseDrivenRateTest,"ColossalFizzCo.Hose.SocketDriveRateLimit",CableHoseTests::Flags)
bool FHoseDrivenRateTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Electricity,9,20,20);
	auto* Target=F.Endpoint(EResourceTransportCategory::Electricity,9,20,0);
	Source->PushPressure=1; Target->PullPressure=1;
	auto* Motor=NewObject<UResourcePumpComponent>(Source->GetOwner()); Source->GetOwner()->AddInstanceComponent(Motor);
	Motor->PushPressure=1; Motor->bRunning=true; Motor->RegisterComponent(); Source->ConnectedMotor=Motor;
	auto* Hose=F.Hose(EResourceTransportCategory::Electricity,9); Hose->MaxUnitsPerSecond=2; Hose->MaxDrivenUnitsPerSecond=5; Hose->bRequiresMotorDrive=true;
	if (!Hose->ConnectEndpoints(Source,Target)) return false;
	const float Delivered=Hose->TransferForDuration(.2f);
	TestEqual(TEXT("Socket push, receiver pull and connected motor combine within driven rate limit"),Hose->CurrentFlowRate,5.f);
	TestTrue(TEXT("Driven transfer exceeds normal rate while respecting maximum throughput"),Delivered>.4f && Delivered<=1.0001f);
	TestTrue(TEXT("Electrical drive conserves source, target and buffered energy"),FMath::IsNearlyEqual(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,20.f,.001f));
	Motor->bRunning=false; const float Before=Source->GetStoredUnits(); Hose->TransferForDuration(1);
	TestEqual(TEXT("Required motor stop prevents intake despite passive socket pressure"),Source->GetStoredUnits(),Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseManagedBurstTest,"ColossalFizzCo.Hose.ManagedBurstConservation",CableHoseTests::Flags)
bool FHoseManagedBurstTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	APropManager* Manager=nullptr; int32 SourceID=0,TargetID=0; if (!F.ManagedStocks(Manager,SourceID,TargetID)) return false;
	FPropEventSyncDelta Full; Full.SetFlag(EPropEventDirtyMask::ContentLevel); Full.SetFlag(EPropEventDirtyMask::ContentID);
	Full.ContentID=9301; Full.ContentLevel=1; Manager->SubmitInstanceEventUpdate(TargetID,Full);
	auto* Source=F.Endpoint(EResourceTransportCategory::Content,9301,10,0);
	auto* Target=F.Endpoint(EResourceTransportCategory::Content,9301,10,0);
	if (!Source->ConfigureManagedContent(Manager,SourceID,9301) || !Target->ConfigureManagedContent(Manager,TargetID,9301)) return false;
	auto* Hose=F.Hose(EResourceTransportCategory::Content,9301); Hose->BufferCapacityUnits=2;
	if (!TestTrue(TEXT("Managed full container connects while retaining finite hose storage"),Hose->ConnectEndpoints(Source,Target))) return false;
	Hose->TransferForDuration(1);
	TestTrue(TEXT("Canonical source debits the units trapped in hose"),FMath::IsNearlyEqual(Source->GetStoredUnits(),2.f,.001f));
	TestEqual(TEXT("Blocked managed delivery leaves contents in the hose"),Hose->Buffer.Units,2.f);
	Source->PushPressure=10; Hose->BurstPressureThreshold=4; Hose->BurstDelay=.1f; Hose->TransferForDuration(.2f);
	TestTrue(TEXT("Overpressure bursts managed content hose"),Hose->bBurst);
	double Released=0; for (const auto& Item:Manager->ReleasedLiquids) Released+=Item.Portions;
	TestTrue(TEXT("Burst enters actual trapped liquid in release ledger"),FMath::IsNearlyEqual(Released,2.0,.001));
	TestTrue(TEXT("Managed burst conserves source, receiver, buffer and spill"),FMath::IsNearlyEqual(double(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units)+Released,8.0,.001));
	Hose->PurgeContents(); Hose->TransferForDuration(1);
	double Again=0; for (const auto& Item:Manager->ReleasedLiquids) Again+=Item.Portions;
	TestEqual(TEXT("Repeated burst tick cannot duplicate ledger release"),Again,Released);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCableConnectedPullTest,"ColossalFizzCo.Cable.ConnectedPull.TautAndSlack",CableHoseTests::Flags)
bool FCableConnectedPullTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* A=F.PullBody(FVector(0,0,100)); auto* B=F.PullBody(FVector(100,0,100),10,20); auto* C=F.PullBody(FVector(200,0,100),20,30);
	auto* AB=F.Link(A,B,100); auto* BC=F.Link(B,C,100);
	AB->SetAttachEndToComponent(BC,TEXT("CableStart")); AB->ResetCable();
	auto* Adapter=F.Adapter(A,AB);
	const FVector Old=A->GetActorLocation(),Requested=Old-FVector(30,0,0);
	const auto Preview=Adapter->QueryConnectedPull(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Read-only force query leaves remote bodies in place"),B->GetActorLocation().Equals(FVector(100,0,100)) && C->GetActorLocation().Equals(FVector(200,0,100)));
	TestTrue(TEXT("Taut chain reports remote load"),Preview.TotalMassKg>=30 && Preview.RequiredForceNewtons>0);
	const auto Result=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Enough force permits requested driver movement"),Result.AllowedLocation.Equals(Requested,.1));
	TestTrue(TEXT("Tension moves every body along the attached chain"),B->GetActorLocation().Equals(FVector(70,0,100),.1) && C->GetActorLocation().Equals(FVector(170,0,100),.1));
	TestEqual(TEXT("Moved body count excludes the driver"),Result.MovedActorCount,2);
	TestTrue(TEXT("Chain moves farthest body before its upstream neighbor"),Result.MovedActors.Num()==2 && Result.MovedActors[0]==C && Result.MovedActors[1]==B);
	TestTrue(TEXT("Driver location is returned to the movement caller without teleporting it"),A->GetActorLocation().Equals(Old));
	auto* SlackA=F.PullBody(FVector(0,500,100)); auto* SlackB=F.PullBody(FVector(100,500,100)); auto* SlackC=F.PullBody(FVector(200,500,100));
	auto* SlackLink=F.Link(SlackA,SlackB,150); F.Link(SlackB,SlackC,100);
	SlackLink->bEnablePointWeight=true; SlackLink->PointMassKg=2;
	auto* SlackAdapter=F.Adapter(SlackA,SlackLink);
	const auto SlackQuery=SlackAdapter->QueryConnectedPull(SlackA->GetActorLocation(),SlackA->GetActorLocation()-FVector(30,0,0),.1f,1.e8f);
	TestEqual(TEXT("Slack carries its own cable point mass without downstream body mass"),SlackQuery.TotalMassKg,10.f);
	const auto Slack=SlackAdapter->TryPullConnectedChain(SlackA->GetActorLocation(),SlackA->GetActorLocation()-FVector(30,0,0),.1f,1.e8f);
	TestEqual(TEXT("Available slack does not move remote bodies"),Slack.MovedActorCount,0);
	TestTrue(TEXT("Slack leaves both downstream body positions unchanged"),SlackB->GetActorLocation().Equals(FVector(100,500,100)) && SlackC->GetActorLocation().Equals(FVector(200,500,100)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHosePhysicalBreakTest,"ColossalFizzCo.Hose.PhysicalDisconnectAndBurst",CableHoseTests::Flags)
bool FHosePhysicalBreakTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,4,10,5,F.Actor(FVector(0,0,100)));
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,4,5,5,F.Actor(FVector(200,0,100)));
	auto* Hose=F.World->SpawnActor<AHoseCableActor>();
	Hose->Cable->bEnableCollision=false; Hose->Transfer->Category=EResourceTransportCategory::Fuel; Hose->Transfer->ResourceID=4;
	Hose->Transfer->BufferCapacityUnits=2; Hose->Transfer->BurstPressureThreshold=100;
	if (!TestTrue(TEXT("Reusable hose connects both typed physical endpoints"),Hose->ConnectStart(Source) && Hose->ConnectEnd(Target))) return false;
	TestTrue(TEXT("Both cable ends follow their connected socket components"),Hose->Cable->GetAttachParent()==Source && Hose->Cable->GetAttachedComponent()==Target);
	Hose->Transfer->TransferForDuration(1);
	TestEqual(TEXT("Blocked receiver leaves two units trapped in the physical hose"),Hose->Transfer->Buffer.Units,2.f);
	Hose->DisconnectEnd();
	TestFalse(TEXT("Unplugging an end disables its physical anchor"),Hose->Cable->bAttachEnd);
	TestFalse(TEXT("Unplugging an end invalidates resource transfer"),Hose->Transfer->HasValidConnection());
	TestEqual(TEXT("Unplugging retains trapped contents"),Hose->Transfer->Buffer.Units,2.f);
	if (!Hose->ConnectEnd(Target)) return false;
	Source->PushPressure=10; Hose->Transfer->BurstPressureThreshold=4; Hose->Transfer->BurstDelay=.1f;
	Hose->Transfer->TransferForDuration(.2f);
	TestTrue(TEXT("Burst disconnects both gameplay socket references"),!Hose->StartPort && !Hose->EndPort);
	TestFalse(TEXT("Burst releases the cable start anchor"),Hose->Cable->bAttachStart);
	TestFalse(TEXT("Burst releases the cable end anchor"),Hose->Cable->bAttachEnd);
	TestEqual(TEXT("Physical burst empties exactly its trapped contents"),Hose->Transfer->TotalReleasedUnits,2.f);
	TestEqual(TEXT("Physical burst leaves no inventory inside hose"),Hose->Transfer->Buffer.Units,0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseSubstepFlowTest,"ColossalFizzCo.Hose.FrameIndependentFlowAndBurstDeadline",CableHoseTests::Flags)
bool FHoseSubstepFlowTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto Run=[&](int32 Frames,float Dt)
	{
		auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,12,100,100);
		auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,12,100,0);
		auto* Hose=F.Hose(EResourceTransportCategory::Fuel,12); Hose->BufferCapacityUnits=1; Hose->MaxUnitsPerSecond=50;
		if (!Hose->ConnectEndpoints(Source,Target)) return -1.f;
		float Total=0; for (int32 I=0;I<Frames;++I) Total+=Hose->TransferForDuration(Dt);
		TestTrue(TEXT("Continuous flow conserves source, target and finite buffer"),FMath::IsNearlyEqual(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,100.f,.002f));
		TestTrue(TEXT("Supported frame duration does not exhaust transfer substep budget"),Hose->DroppedTransferTime<=UE_SMALL_NUMBER);
		return Total;
	};
	const float LargeFrame=Run(1,1),SmallFrames=Run(20,.05f);
	TestTrue(TEXT("Small hose buffer does not impose a per-frame throughput cap"),FMath::IsNearlyEqual(LargeFrame,50.f,.002f));
	TestTrue(TEXT("One large transfer step matches twenty smaller frames"),FMath::IsNearlyEqual(LargeFrame,SmallFrames,.002f));
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,13,100,100);
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,13,100,0);
	Source->bMotorRunning=true; Source->MotorPressureBoost=10;
	auto* Hose=F.Hose(EResourceTransportCategory::Fuel,13); Hose->BufferCapacityUnits=20; Hose->MaxUnitsPerSecond=50;
	Hose->bRequiresMotorDrive=true; Hose->BurstPressureThreshold=4; Hose->BurstDelay=.1f;
	if (!Hose->ConnectEndpoints(Source,Target)) return false;
	const float Delivered=Hose->TransferForDuration(1);
	TestTrue(TEXT("Overdrive ruptures at the configured deadline within a long frame"),Hose->bBurst);
	TestTrue(TEXT("Only pre-rupture elapsed time may pump resources"),FMath::IsNearlyEqual(Delivered,5.f,.002f) && FMath::IsNearlyEqual(Hose->LastIntakeUnits,5.f,.002f));
	TestTrue(TEXT("No source debit occurs after the burst instant"),FMath::IsNearlyEqual(Source->GetStoredUnits(),95.f,.002f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseManagedPortResolverTest,"ColossalFizzCo.Hose.ManagedPortResolver",CableHoseTests::Flags)
bool FHoseManagedPortResolverTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	APropManager* Manager=nullptr; int32 SourceID=0,TargetID=0; if (!F.ManagedStocks(Manager,SourceID,TargetID)) return false;
	AActivePropActor* SourceActor=nullptr; AActivePropActor* TargetActor=nullptr;
	if (!Manager->PromoteInstanceToASM(SourceID,EPropActivationReason::Interaction,false,SourceActor)
		|| !Manager->PromoteInstanceToASM(TargetID,EPropActivationReason::Interaction,false,TargetActor)) return false;
	auto* Source=UResourcePortLibrary::ResolveResourcePort(SourceActor,EResourceTransportCategory::Content,9301,true);
	auto* Target=UResourcePortLibrary::ResolveResourcePort(TargetActor,EResourceTransportCategory::Content,9301,false);
	if (!TestTrue(TEXT("Managed props expose source and receiving content ports without Blueprint components"),Source && Target)) return false;
	TestEqual(TEXT("Source port binds canonical instance identity"),Source->GetManagedContentISID(),SourceID);
	TestEqual(TEXT("Receiving port binds canonical instance identity"),Target->GetManagedContentISID(),TargetID);
	TestTrue(TEXT("Repeated resolution reuses the same managed adapter"),UResourcePortLibrary::ResolveResourcePort(SourceActor,EResourceTransportCategory::Content,9301,true)==Source);
	TestNull(TEXT("Content props do not acquire invented fuel storage"),UResourcePortLibrary::ResolveResourcePort(SourceActor,EResourceTransportCategory::Fuel,9301,true));
	TargetActor->SwapRoles();
	TestNull(TEXT("Client cannot create or rebind a managed resource port"),UResourcePortLibrary::ResolveResourcePort(TargetActor,EResourceTransportCategory::Content,9301,false));
	TargetActor->SwapRoles();
	auto* Hose=F.Hose(EResourceTransportCategory::Content,9301);
	if (!TestTrue(TEXT("Resolved managed ports connect to normal hose transfer"),Hose->ConnectEndpoints(Source,Target))) return false;
	TestTrue(TEXT("Resolved ports transfer actual canonical content"),Hose->TransferForDuration(.1f)>0);
	TestTrue(TEXT("Managed adapter flow conserves canonical stocks and hose buffer"),FMath::IsNearlyEqual(Source->GetStoredUnits()+Target->GetStoredUnits()+Hose->Buffer.Units,4.f,.002f));
	const float BeforePool=Manager->FindUniqueData(SourceID)->EventData.ContentLevel;
	if (!TestTrue(TEXT("Managed source can demote while its adapter disconnects transport"),Manager->DemoteActorToISM(SourceID))) return false;
	TestFalse(TEXT("Pooling invalidates the old logical hose connection"),Hose->HasValidConnection());
	TestEqual(TEXT("Pooled adapter exposes no stale instance inventory"),Source->GetStoredUnits(),0.f);
	TestEqual(TEXT("Disconnected pooled source cannot continue transferring"),Hose->TransferForDuration(1),0.f);
	TestEqual(TEXT("Pooling cleanup preserves canonical source inventory"),Manager->FindUniqueData(SourceID)->EventData.ContentLevel,BeforePool);
	AActivePropActor* ReboundActor=nullptr;
	if (!Manager->PromoteInstanceToASM(SourceID,EPropActivationReason::Interaction,false,ReboundActor)) return false;
	auto* Rebound=UResourcePortLibrary::ResolveResourcePort(ReboundActor,EResourceTransportCategory::Content,9301,true);
	TestTrue(TEXT("Reactivated prop adapter resolves its current canonical identity"),Rebound && Rebound->GetManagedContentISID()==SourceID);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseAuthoredPortResolverTest,"ColossalFizzCo.Hose.AuthoredPortSelection",CableHoseTests::Flags)
bool FHoseAuthoredPortResolverTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Equipment=F.Actor();
	F.Endpoint(EResourceTransportCategory::Content,1,10,5,Equipment);
	auto* FuelSource=F.Endpoint(EResourceTransportCategory::Fuel,2,10,5,Equipment); FuelSource->bCanReceive=false;
	auto* FuelReceiver=F.Endpoint(EResourceTransportCategory::Fuel,2,10,0,Equipment); FuelReceiver->bCanSupply=false;
	TestTrue(TEXT("Resolver searches beyond the first incompatible authored component"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Fuel,2,true)==FuelSource);
	TestTrue(TEXT("Resolver selects a compatible authored port in the requested direction"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Fuel,2,false)==FuelReceiver);
	TestNull(TEXT("Resolver does not invent a missing electrical port"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Electricity,2,false));
	TestNull(TEXT("Resolver cannot reinterpret another resource ID"),UResourcePortLibrary::ResolveResourcePort(Equipment,EResourceTransportCategory::Fuel,3,true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseOwnedConnectionTest,"ColossalFizzCo.Hose.OwnedConnectionGuards",CableHoseTests::Flags)
bool FHoseOwnedConnectionTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Player=F.World->SpawnActor<ACharacter>(); Player->SetActorLocation(FVector(0,-50,100));
	Player->SetActorEnableCollision(false); Player->GetCharacterMovement()->DisableMovement();
	auto* Controller=F.World->SpawnActor<APlayerController>(); Controller->Possess(Player);
	auto* Interaction=NewObject<UShopPlayerInteractionComponent>(Player); Player->AddInstanceComponent(Interaction); Interaction->RegisterComponent();
	auto* Source=F.Endpoint(EResourceTransportCategory::Fuel,7,10,5,F.Actor(FVector(50,0,100)));
	auto* Target=F.Endpoint(EResourceTransportCategory::Fuel,7,10,0,F.Actor(FVector(200,0,100)));
	auto* Wrong=F.Endpoint(EResourceTransportCategory::Electricity,7,10,0,F.Actor(FVector(200,50,100)));
	auto* Hose=F.World->SpawnActor<AHoseCableActor>(FVector(0,0,100),FRotator::ZeroRotator);
	Hose->Transfer->Category=EResourceTransportCategory::Fuel; Hose->Cable->bEnableCollision=false;
	TArray<FVector> Points; Hose->Cable->GetCableParticleLocations(Points);
	if (Points.Num()<2 || !TestTrue(TEXT("Owned interaction grabs the source end"),Interaction->ExecuteCableGrab(Hose->Cable,Points[0],Points[0],1))) return false;
	if (!TestTrue(TEXT("Owned source connection adopts the actual resource identity"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Source->GetOwner(),2))) return false;
	TestEqual(TEXT("Empty hose adopts source resource ID"),Hose->Transfer->ResourceID,7);
	Hose->Cable->GetCableParticleLocations(Points); Player->SetActorLocation(Points.Last()+FVector(0,-50,0));
	if (!TestTrue(TEXT("Owned interaction grabs the receiving end"),Interaction->ExecuteCableGrab(Hose->Cable,Points.Last(),Points.Last(),3))) return false;
	TestFalse(TEXT("Owned connection cannot bypass the socket category"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Wrong->GetOwner(),4));
	TestTrue(TEXT("Valid receiving socket connects after rejected mismatch"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Target->GetOwner(),5));
	TestFalse(TEXT("Replayed connection sequence is rejected"),Interaction->ExecuteConnectResourceHose(Hose->Cable,Target->GetOwner(),5));
	TestTrue(TEXT("Only accepted owned requests establish physical endpoints"),Hose->StartPort==Source && Hose->EndPort==Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCablePullCycleTest,"ColossalFizzCo.Cable.ConnectedPull.CycleDeduplication",CableHoseTests::Flags)
bool FCablePullCycleTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* A=F.PullBody(FVector(0,0,100)); auto* B=F.PullBody(FVector(100,0,100),10,20); auto* C=F.PullBody(FVector(200,0,100),20,30);
	auto* AB=F.Link(A,B,100); F.Link(B,C,100); auto* Adapter=F.Adapter(A,AB);
	const FVector Old=A->GetActorLocation(),Requested=Old-FVector(20,0,0);
	const auto Baseline=Adapter->QueryConnectedPull(Old,Requested,.1f,1.e8f);
	F.Link(A,C,200);
	const auto Loop=Adapter->QueryConnectedPull(Old,Requested,.1f,1.e8f);
	TestFalse(TEXT("Finite cable cycle terminates without exhausting graph traversal"),Loop.bTraversalLimitReached);
	TestTrue(TEXT("Shared bodies are charged once across branching cycle paths"),FMath::IsNearlyEqual(Loop.TotalMassKg,Baseline.TotalMassKg,.01f));
	const auto Moved=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestEqual(TEXT("Cycle moves each remote body only once"),Moved.MovedActorCount,2);
	TestTrue(TEXT("Cycle does not multiply remote displacement"),B->GetActorLocation().Equals(FVector(80,0,100),.1) && C->GetActorLocation().Equals(FVector(180,0,100),.1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCablePullResetTest,"ColossalFizzCo.Cable.ConnectedPull.ResetAfterTeleport",CableHoseTests::Flags)
bool FCablePullResetTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* Source=F.PullBody(FVector(0,0,100)); auto* Target=F.PullBody(FVector(100,0,100));
	auto* Cable=F.Link(Source,Target,100); Cable->bEnablePointWeight=true; Cable->PointMassKg=2;
	Source->FindComponentByClass<UCablePullResponseComponent>()->PullForceNewtons=0;
	auto* Adapter=F.Adapter(Source,Cable);
	const FVector RestoredSource(-2000,0,100),RestoredTarget(-1900,0,100);
	Source->SetActorLocation(RestoredSource,false,nullptr,ETeleportType::TeleportPhysics);
	Target->SetActorLocation(RestoredTarget,false,nullptr,ETeleportType::TeleportPhysics);
	Cable->ResetCable();
	Adapter->TickComponent(.1f,LEVELTICK_All,nullptr);
	TestTrue(TEXT("Explicit cable reset preserves a restored source transform on the next movement tick"),Source->GetActorLocation().Equals(RestoredSource,.01));
	TestTrue(TEXT("Reset does not interpret restored endpoint placement as a pull on its neighbor"),Target->GetActorLocation().Equals(RestoredTarget,.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCablePullBlockingTest,"ColossalFizzCo.Cable.ConnectedPull.BlockingAndAuthority",CableHoseTests::Flags)
bool FCablePullBlockingTest::RunTest(const FString& Parameters)
{
	CableHoseTests::FFixture F; if (!F.World) return false;
	auto* A=F.PullBody(FVector(0,0,100)); auto* B=F.PullBody(FVector(100,0,100),10,100);
	auto* AB=F.Link(A,B,100); auto* Adapter=F.Adapter(A,AB);
	const FVector Old=A->GetActorLocation(),Requested=Old-FVector(30,0,0);
	const auto Weak=Adapter->TryPullConnectedChain(Old,Requested,.1f,0);
	TestTrue(TEXT("Insufficient force blocks taut-chain progress"),Weak.bBlocked && Weak.AllowedLocation.Equals(Old,.1));
	TestTrue(TEXT("Insufficient force leaves downstream body unmoved"),B->GetActorLocation().Equals(FVector(100,0,100)));
	B->FindComponentByClass<UCablePullResponseComponent>()->bFixed=true;
	const auto Fixed=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Pinned equipment remains an anchor regardless of available force"),Fixed.bBlocked && Fixed.AllowedLocation.Equals(Old,.1));
	B->FindComponentByClass<UCablePullResponseComponent>()->bFixed=false;
	AB->SetPointFixed(2,true);
	const auto InteriorFixed=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Pinned interior cable point anchors its upstream span"),InteriorFixed.bBlocked && InteriorFixed.AllowedLocation.Equals(Old,.1));
	AB->SetPointFixed(2,false);
	A->SwapRoles(); const auto Client=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f); A->SwapRoles();
	TestEqual(TEXT("Client never mutates remote chain actors"),Client.MovedActorCount,0);
	TestTrue(TEXT("Client attempt preserves downstream location"),B->GetActorLocation().Equals(FVector(100,0,100)));
	auto* Wall=F.PullBody(FVector(80,0,100),10,0,true);
	Cast<UBoxComponent>(Wall->GetRootComponent())->SetBoxExtent(FVector(2,40,40));
	const auto Blocked=Adapter->TryPullConnectedChain(Old,Requested,.1f,1.e8f);
	TestTrue(TEXT("Swept remote body cannot pass through blocking world geometry"),B->GetActorLocation().X>=86-.2);
	TestTrue(TEXT("Blocked downstream movement also restricts the driver"),Blocked.AllowedLocation.X>Requested.X+.5);
	return true;
}
#endif
