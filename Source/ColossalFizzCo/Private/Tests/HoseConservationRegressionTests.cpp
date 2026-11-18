#include "HoseTransferTestObserver.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "UObject/StrongObjectPtr.h"

void UHoseTransferTestObserver::DestroyHoseOnDelivery(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float Units,float Pressure,float Rate)
{
	++DeliveryCalls;
	if (Hose && Hose->GetOwner()) Hose->GetOwner()->Destroy();
}

void UHoseTransferTestObserver::RecordRelease(EResourceTransportCategory Category,int32 ResourceID,float Units)
{
	++ReleaseCalls; ReleasedUnits+=Units;
}

#if WITH_DEV_AUTOMATION_TESTS
namespace HoseConservationTests
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
	AActor* Actor()
	{
		auto* Owner=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Owner);
		Owner->SetRootComponent(Root); Owner->AddInstanceComponent(Root); Root->RegisterComponent(); return Owner;
	}
	UResourceTransferEndpointComponent* Endpoint(float Capacity,float Units)
	{
		auto* Owner=Actor(); auto* Port=NewObject<UResourceTransferEndpointComponent>(Owner); Owner->AddInstanceComponent(Port);
		Port->SetupAttachment(Owner->GetRootComponent()); Port->CapacityUnits=Capacity; Port->RegisterComponent(); Port->SetStoredUnits(Units); return Port;
	}
	UHoseTransferComponent* Hose()
	{
		auto* Owner=Actor(); auto* Hose=NewObject<UHoseTransferComponent>(Owner); Owner->AddInstanceComponent(Hose);
		Hose->MaxUnitsPerSecond=10; Hose->BufferCapacityUnits=10; Hose->BurstPressureThreshold=100;
		Hose->RegisterComponent(); Hose->SetComponentTickEnabled(false); return Hose;
	}
	bool ManagedStocks(APropManager*& Manager,int32& Source,int32& Target)
	{
		Manager=World->SpawnActor<APropManager>(); if (!Manager) return false;
		Manager->bEnableAutosave=false; Manager->InitializePropManager(); Manager->WorldElapsedSeconds=100;
		FPropContentDefinition Content; Content.ContentID=9401; Content.ExpiryProfile.bHasExpiry=false; Manager->RegisterContentDefinition(Content);
		FPropProductDefinition Product; Product.ProductID=9411; Product.bEnableInstanceUniqueData=true;
		Product.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")); Product.CustomActiveActorClass=AActivePropActor::StaticClass();
		Product.ConsumableProfile.NumOfPortions=100; Product.MaxOverfillLevel=1; Product.DefaultUniqueData.EventData.ContentID=9401;
		Product.DefaultUniqueData.EventData.ContentLevel=.04f; Product.DefaultUniqueData.EventData.Strength=1;
		Product.DefaultUniqueData.EventData.bClean=true; Product.DefaultUniqueData.EventData.CapSealed=0; Product.SupportedContentIDs={9401};
		Manager->RegisterProductDefinition(Product);
		Product.ProductID=9412; Product.ConsumableProfile.NumOfPortions=4; Product.DefaultUniqueData.EventData.ContentID=0;
		Product.DefaultUniqueData.EventData.ContentLevel=0; Manager->RegisterProductDefinition(Product);
		Source=Manager->GenerateProductInstance(9411,FTransform(FVector(0,0,1000)));
		Target=Manager->GenerateProductInstance(9412,FTransform(FVector(200,0,1000)));
		return Source>0 && Target>0;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseFloatPrecisionConservationTest,"ColossalFizzCo.Hose.FloatPrecisionConservation",HoseConservationTests::Flags)
bool FHoseFloatPrecisionConservationTest::RunTest(const FString& Parameters)
{
	HoseConservationTests::FFixture F; if (!F.World) return false;
	// A nearly closed valve must not manufacture liquid when a large source cannot represent its debit.
	auto* Source=F.Endpoint(200,200); auto* Target=F.Endpoint(10,0); auto* Hose=F.Hose();
	Source->MaxSupplyUnitsPerSecond=.000008f;
	if (!TestTrue(TEXT("Tiny valve flow connects normally"),Hose->ConnectEndpoints(Source,Target))) return false;
	for (int32 I=0;I<100;++I) Hose->TransferForDuration(.05f);
	TestEqual(TEXT("Sub-precision requests cannot increase combined inventory"),double(Source->GetStoredUnits())+Target->GetStoredUnits()+Hose->Buffer.Units,200.0);
	TestEqual(TEXT("Dispensing ledger counts only actual source reduction"),Source->TotalSuppliedUnits,200.f-Source->GetStoredUnits());
	TestEqual(TEXT("Receiver gains nothing from unchanged source inventory"),Target->GetStoredUnits(),0.f);

	// A receiver too large to represent a small credit must leave the contents in the hose.
	auto* LargeTarget=F.Endpoint(2000000000.f,1000000000.f); auto* OtherSource=F.Endpoint(10,10); auto* OtherHose=F.Hose();
	if (!TestTrue(TEXT("Large receiver connects"),OtherHose->ConnectEndpoints(OtherSource,LargeTarget))) return false;
	TestEqual(TEXT("Unrepresentable receiver credit reports no delivery"),OtherHose->TransferForDuration(.5f),0.f);
	TestEqual(TEXT("Unaccepted liquid stays in hose inventory"),OtherHose->Buffer.Units,5.f);
	TestEqual(TEXT("Large receiver retains its exact starting inventory"),LargeTarget->GetStoredUnits(),1000000000.f);
	TestEqual(TEXT("Source and trapped liquid conserve all ten units"),OtherSource->GetStoredUnits()+OtherHose->Buffer.Units,10.f);

	// Ordinary nonbinary flow still moves resources and reports the same amount as both inventory changes.
	auto* NormalSource=F.Endpoint(200,200); auto* NormalTarget=F.Endpoint(10,0); auto* NormalHose=F.Hose();
	NormalSource->MaxSupplyUnitsPerSecond=8;
	if (!NormalHose->ConnectEndpoints(NormalSource,NormalTarget)) return false;
	const float Delivered=NormalHose->TransferForDuration(.05f);
	TestTrue(TEXT("Representable normal flow remains usable"),Delivered>.39f && Delivered<=.4f);
	TestEqual(TEXT("Normal source debit equals actual receiver plus hose credit"),double(200)-NormalSource->GetStoredUnits(),double(NormalTarget->GetStoredUnits())+NormalHose->Buffer.Units);
	TestEqual(TEXT("Reported delivery is actual receiver growth"),Delivered,NormalTarget->GetStoredUnits());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseDestroyDuringDeliveryTest,"ColossalFizzCo.Hose.DestroyDuringDeliveryConservation",HoseConservationTests::Flags)
bool FHoseDestroyDuringDeliveryTest::RunTest(const FString& Parameters)
{
	HoseConservationTests::FFixture F; if (!F.World) return false;
	for (const bool Managed:{false,true})
	{
		auto* Source=F.Endpoint(10,4); auto* Target=F.Endpoint(10,0); auto* Hose=F.Hose();
		APropManager* Manager=nullptr; int32 SourceID=0,TargetID=0;
		if (Managed)
		{
			if (!F.ManagedStocks(Manager,SourceID,TargetID)) return false;
			Source->SetStoredUnits(0); Target->SetStoredUnits(0); Hose->ResourceID=9401;
			if (!Source->ConfigureManagedContent(Manager,SourceID,9401) || !Target->ConfigureManagedContent(Manager,TargetID,9401)) return false;
		}
		if (!TestTrue(TEXT("Destruction fixture connects"),Hose->ConnectEndpoints(Source,Target))) return false;
		Hose->bDrainEnabled=false; Hose->TransferForDuration(.15f);
		if (!TestTrue(TEXT("Hose holds liquid before receiver callback"),Hose->Buffer.Units>1.f)) return false;
		TStrongObjectPtr<UHoseTransferTestObserver> Observer(NewObject<UHoseTransferTestObserver>());
		Target->OnResourceReceived.AddDynamic(Observer.Get(),&UHoseTransferTestObserver::DestroyHoseOnDelivery);
		Hose->OnContentsReleased.AddDynamic(Observer.Get(),&UHoseTransferTestObserver::RecordRelease);
		Hose->bDrainEnabled=true;
		const float Delivered=Hose->TransferForDuration(.05f);
		TestEqual(TEXT("Real delivery callback destroys the hose exactly once"),Observer->DeliveryCalls,1);
		TestTrue(TEXT("Some liquid was delivered before destruction"),Delivered>0);
		TestTrue(TEXT("Hose actor was destroyed by the delivery listener"),Hose->GetOwner()->IsActorBeingDestroyed());
		TestEqual(TEXT("Destruction emits one remaining-contents release"),Observer->ReleaseCalls,1);
		TestTrue(TEXT("Destruction releases nonzero trapped contents"),Observer->ReleasedUnits>1.f);
		TestEqual(TEXT("Destroyed hose retains no unaccounted buffer"),Hose->Buffer.Units,0.f);
		TestEqual(TEXT("Release observer and hose ledger agree"),Observer->ReleasedUnits,Hose->TotalReleasedUnits);
		const double Accounted=double(Source->GetStoredUnits())+Target->GetStoredUnits()+Observer->ReleasedUnits;
		TestTrue(TEXT("Source, delivered liquid and destroyed-hose release conserve the initial stock"),FMath::IsNearlyEqual(Accounted,4.0,.0001));
		if (Managed)
		{
			TestEqual(TEXT("Managed destruction creates one canonical spill"),Manager->ReleasedLiquids.Num(),1);
			if (Manager->ReleasedLiquids.Num()==1) TestEqual(TEXT("Canonical spill matches released inventory"),double(Manager->ReleasedLiquids[0].Portions),double(Observer->ReleasedUnits));
		}
		TestEqual(TEXT("Repeated cleanup cannot release the same contents twice"),Hose->PurgeContents(),0.f);
		TestEqual(TEXT("Repeated cleanup leaves one release notification"),Observer->ReleaseCalls,1);
	}
	return true;
}
#endif
