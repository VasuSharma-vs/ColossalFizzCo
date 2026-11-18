#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Resources/PressurizedColaContainer.h"
#include "Resources/HoseCableActor.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PressurizedColaTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
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
    }
    ~FFixture()
    {
        if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
    }
    APressurizedColaContainer* Source()
    {
        return World->SpawnActor<APressurizedColaContainer>(FVector(0,0,1000),FRotator::ZeroRotator);
    }
    AActivePropActor* ManagedReceiver(int32 ContentID,bool Bottle=false,FVector Location=FVector(150,0,1000))
    {
        FPropContentDefinition Content; Content.ContentID=ContentID; Content.ExpiryProfile.bHasExpiry=false;
        Content.PressureStrengthMultiplier=1; Manager->RegisterContentDefinition(Content);
        FPropProductDefinition Product; Product.ProductID=Bottle?9412:9411;
        Product.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        Product.CustomActiveActorClass=Bottle?AColaPropActor::StaticClass():AActivePropActor::StaticClass();
        Product.bEnableInstanceUniqueData=true; Product.BottleMaterialType=EBottleMaterialType::Plastic;
        Product.ConsumableProfile.NumOfPortions=100; Product.MaxOverfillLevel=1.5f;
        Product.DefaultUniqueData.EventData.ContentID=0; Product.DefaultUniqueData.EventData.ContentLevel=0;
        Product.DefaultUniqueData.EventData.CapSealed=0; Product.DefaultUniqueData.EventData.Strength=1;
        Product.DefaultUniqueData.EventData.bClean=true; Product.SupportedContentIDs={ContentID}; Product.PressureFailureThreshold=2;
        Manager->RegisterProductDefinition(Product);
        const int32 ID=Manager->GenerateProductInstance(Product.ProductID,FTransform(Location));
        AActivePropActor* Active=nullptr;
        if (!Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active)) return nullptr;
        Active->PropMesh->SetSimulatePhysics(false);
        if (auto* Cola=Cast<AColaPropActor>(Active))
        {
            Cola->bUseOriginalRocketFlight=true; Cola->RocketComp->SetAllDebugEnabled(false);
            Cola->RocketComp->TippingSettings.bEnableTipping=Cola->RocketComp->DraggingSettings.bEnableDragging=false;
        }
        return Active;
    }
    UResourceTransferEndpointComponent* ReceivingPort(AActor* Owner,int32 ContentID,bool Managed)
    {
        auto* Port=NewObject<UResourceTransferEndpointComponent>(Owner); Owner->AddInstanceComponent(Port);
        Port->SetupAttachment(Owner->GetRootComponent()); Port->Category=EResourceTransportCategory::Content;
        Port->ResourceID=ContentID; Port->bCanSupply=false; Port->CapacityUnits=1000;
        Port->RegisterComponent(); Port->SetStoredUnits(0);
        if (Managed && !Port->ConfigureManagedContent(Manager,CastChecked<AActivePropActor>(Owner)->ISID,ContentID)) return nullptr;
        return Port;
    }
    AHoseCableActor* Hose()
    {
        auto* Result=World->SpawnActor<AHoseCableActor>(); Result->Transfer->SetComponentTickEnabled(false);
        Result->Transfer->MaxUnitsPerSecond=5; Result->Transfer->MaxDrivenUnitsPerSecond=50;
        Result->Transfer->BufferCapacityUnits=4; Result->Transfer->BurstPressureThreshold=100;
        Result->Transfer->BlockedPressureRisePerSecond=0;
        Result->Cable->SetComponentTickEnabled(false);
        return Result;
    }
    AActor* LocalReceiver()
    {
        auto* Owner=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Root); Owner->AddInstanceComponent(Root); Root->RegisterComponent();
        Owner->SetActorLocation(FVector(150,0,1000)); return Owner;
    }
    double ManagedUnits(const AActivePropActor* Actor) const
    {
        const auto* Data=Manager->FindUniqueData(Actor->ISID);
        return Data?double(Data->EventData.ContentLevel)*100:0;
    }
    void SeedManaged(AActivePropActor* Actor,int32 ContentID,float Units,float Temperature)
    {
        FPropEventSyncDelta Delta; Delta.SetFlag(EPropEventDirtyMask::ContentID); Delta.SetFlag(EPropEventDirtyMask::ContentLevel);
        Delta.ContentID=ContentID; Delta.ContentLevel=Units/100;
        Manager->SubmitInstanceEventUpdate(Actor->ISID,Delta); Manager->SetTemperature(Actor->ISID,Temperature);
    }
    bool ManualStation(int32 ContentID,AManualColaFillingMachine*& Machine,AColaPropActor*& Bottle)
    {
        FPropProductDefinition Equipment; Equipment.ProductID=9413; Equipment.bEnableInstanceUniqueData=true;
        Equipment.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        Equipment.CustomActiveActorClass=AManualColaFillingMachine::StaticClass();
        Equipment.ConsumableProfile.NumOfPortions=0; Equipment.DefaultUniqueData.EventData.Strength=1;
        Manager->RegisterProductDefinition(Equipment);
        const int32 MachineID=Manager->GenerateProductInstance(Equipment.ProductID,FTransform(FVector(0,0,1000)));
        AActivePropActor* ActiveMachine=nullptr;
        if (!Manager->PromoteInstanceToASM(MachineID,EPropActivationReason::Interaction,false,ActiveMachine)) return false;
        Machine=Cast<AManualColaFillingMachine>(ActiveMachine);
        if (!Machine) return false;
        Machine->PropMesh->SetSimulatePhysics(false);
        Bottle=Cast<AColaPropActor>(ManagedReceiver(ContentID,true,Machine->BottleHolderPoint->GetComponentLocation()));
        if (!Bottle) return false;
        if (!Machine->PrepareReceivingHosePort(0,ContentID)) return false;
        Manager->InitializeInstanceTransform(Bottle->ISID,Machine->BottleHolderPoint->GetComponentTransform());
        Bottle->SetSeatedInFiller(true); Bottle->ManualFillingStation=Machine->Production;
        auto* Player=World->SpawnActor<ACharacter>(); Player->SetActorLocation(Machine->GetActorLocation()+FVector(0,-100,100));
        Player->SetActorEnableCollision(false); Player->GetCharacterMovement()->DisableMovement();
        auto* Controller=World->SpawnActor<APlayerController>(); Controller->Possess(Player);
        auto* Actions=NewObject<UShopPlayerInteractionComponent>(Player); Player->AddInstanceComponent(Actions); Actions->RegisterComponent();
        Machine->Production->View.SeatedBottleISID=Bottle->ISID;
        Machine->Production->View.SelectedContentPort=0; Machine->Production->View.ConnectedSourceISID=0;
        Machine->Production->View.LeverOperator=Player; Machine->Production->View.bLeverHeld=true;
        Machine->Production->View.bFlowStopped=true;
        Machine->BottleFiller->LeverOperator=Player; Machine->BottleFiller->LeverExpiresAt=100;
        Machine->UpdateCradleMotion(Machine->CradleTravelSeconds);
        return Machine->Production->IsBottleReadyForFilling(Bottle->ISID);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedValveDeliveryTest,"ColossalFizzCo.Hose.PressurizedSupply.ValveAndManagedDelivery",PressurizedColaTests::Flags)
bool FPressurizedValveDeliveryTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* Source=F.Source(); if (!TestNotNull(TEXT("Native pressurized source spawns"),Source)) return false;
    TestEqual(TEXT("New container starts with its valve closed"),Source->ValveOpening,0.f);
    TestEqual(TEXT("New container starts with two hundred units"),Source->GetRemainingUnits(),200.f);
    const int32 ContentID=Source->SupplyPort->ResourceID;
    auto* Target=F.ManagedReceiver(ContentID); if (!TestNotNull(TEXT("Managed receiver activates"),Target)) return false;
    F.SeedManaged(Target,ContentID,4,20);
    auto* Port=F.ReceivingPort(Target,ContentID,true); auto* Hose=F.Hose();
    if (!TestTrue(TEXT("Closed valve permits a compatible physical source connection"),Hose->ConnectStart(Source->SupplyPort))) return false;
    if (!TestTrue(TEXT("Explicit local content source can connect to managed inventory"),Hose->ConnectEnd(Port))) return false;
    TestEqual(TEXT("Closed valve delivers nothing"),Hose->Transfer->TransferForDuration(.5f),0.f);
    TestEqual(TEXT("Closed valve does not debit local supply"),Source->GetRemainingUnits(),200.f);
    TestTrue(TEXT("Closing a valve preserves the physical and logical connection"),Hose->EndPort==Port && Hose->Transfer->HasValidConnection());
    Source->SupplyPort->ContentCreationTime=20;
    TestTrue(TEXT("Turning the wheel opens a quarter of its configured travel"),Source->RotateValve(Source->ValveTravelDegrees*.25f));
    TestEqual(TEXT("Valve rotation sets the expected opening"),Source->ValveOpening,.25f);
    const float Delivered=Hose->Transfer->TransferForDuration(.5f);
    TestTrue(TEXT("Quarter valve limits actual delivery to one unit in half a second"),FMath::IsNearlyEqual(Delivered,1.f,.002f));
    TestTrue(TEXT("Managed receiver credits precisely the hose-delivered units"),FMath::IsNearlyEqual(F.ManagedUnits(Target),4.0+Delivered,.002));
    TestTrue(TEXT("Local source, canonical receiver and finite hose conserve contents"),FMath::IsNearlyEqual(Source->GetRemainingUnits()+F.ManagedUnits(Target)+Hose->Transfer->Buffer.Units,204.0,.005));
    const auto* Data=F.Manager->FindUniqueData(Target->ISID);
    TestTrue(TEXT("Hot source and existing cold receiver mix by actual received volume"),Data && FMath::IsNearlyEqual(Data->ContinuousData.Temperature,float((4.0*20+Delivered*85)/(4+Delivered)),.002f));
    TestTrue(TEXT("Managed delivery preserves original local liquid age"),Data && Data->EventData.ContentCreationTime==20);
    TestEqual(TEXT("Hose delivery never opens a second direct source transfer session"),F.Manager->FillSessions.Num(),0);
    Source->Tick(.5f);
    TestTrue(TEXT("Container monitor counts actual units supplied to the hose"),FMath::IsNearlyEqual(Source->TotalDispensedUnits,Delivered+Hose->Transfer->Buffer.Units,.002f));
    TestTrue(TEXT("Container monitor reports the actual outlet rate"),FMath::IsNearlyEqual(Source->CurrentOutletUnitsPerSecond,2.f,.002f));
    Source->SetValveOpening(0); const float Remaining=Source->GetRemainingUnits();
    Hose->Transfer->TransferForDuration(.5f);
    Source->Tick(.5f);
    TestEqual(TEXT("Closing the valve stops further source intake"),Source->GetRemainingUnits(),Remaining);
    TestEqual(TEXT("Closed valve reports no new outlet flow"),Source->CurrentOutletUnitsPerSecond,0.f);
    Source->SwapRoles(); const bool ClientOpened=Source->SetValveOpening(1); Source->SwapRoles();
    TestFalse(TEXT("A client cannot open the authoritative valve"),ClientOpened);
    TestEqual(TEXT("Rejected client input leaves the valve closed"),Source->ValveOpening,0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedSocketPopTest,"ColossalFizzCo.Hose.PressurizedSupply.SocketOverload",PressurizedColaTests::Flags)
bool FPressurizedSocketPopTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    for (bool PressureLimit:{false,true})
    {
        FFixture F; if (!F.World) return false;
        auto* Source=F.Source(); auto* Port=F.ReceivingPort(F.LocalReceiver(),Source->SupplyPort->ResourceID,false);
        auto* Hose=F.Hose(); Port->bEnableIntakeOverload=true;
        Port->MaxIntakePressure=PressureLimit?2.f:100.f; Port->MaxIntakeUnitsPerSecond=PressureLimit?100.f:1.f;
        if (!Hose->ConnectStart(Source->SupplyPort) || !Hose->ConnectEnd(Port)) return false;
        Source->SetValveOpening(.1f); Hose->Transfer->bDrainEnabled=false; Hose->Transfer->TransferForDuration(.5f);
        const float Before=Source->GetRemainingUnits(),Trapped=Hose->Transfer->Buffer.Units;
        if (!TestTrue(TEXT("A safe partial valve can charge the finite hose"),Trapped>0)) return false;
        Source->SetValveOpening(1); Hose->Transfer->bDrainEnabled=true;
        TestEqual(TEXT("Overloaded socket rejects the incoming transfer before delivery"),Hose->Transfer->TransferForDuration(.5f),0.f);
        TestFalse(TEXT("Socket overload physically releases the receiving connector"),IsValid(Hose->EndPort));
        TestFalse(TEXT("Socket overload removes the end cable attachment"),Hose->Cable->bAttachEnd);
        TestTrue(TEXT("Socket pop retains the source connector"),Hose->StartPort==Source->SupplyPort);
        TestFalse(TEXT("A stronger hose remains intact after socket overload"),Hose->Transfer->bBurst);
        TestEqual(TEXT("Socket pop cannot debit additional supply"),Source->GetRemainingUnits(),Before);
        TestEqual(TEXT("Socket pop retains trapped contents for later draining"),Hose->Transfer->Buffer.Units,Trapped);
        TestTrue(TEXT("Popped connection conserves original local contents"),FMath::IsNearlyEqual(Source->GetRemainingUnits()+Port->GetStoredUnits()+Hose->Transfer->Buffer.Units,200.f,.005f));
        Hose->Transfer->TransferForDuration(1);
        TestEqual(TEXT("Popped hose cannot resume intake on later ticks"),Source->GetRemainingUnits(),Before);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedSingleOutletTest,"ColossalFizzCo.Hose.PressurizedSupply.SingleOutlet",PressurizedColaTests::Flags)
bool FPressurizedSingleOutletTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* Source=F.Source(); auto* First=F.Hose(); auto* Second=F.Hose();
    auto* Port=F.ReceivingPort(F.LocalReceiver(),Source->SupplyPort->ResourceID,false);
    TestEqual(TEXT("Pressurized container has one physical outlet"),Source->SupplyPort->MaxConnectedHoses,1);
    if (!TestTrue(TEXT("First one-ended hose occupies the outlet"),First->ConnectStart(Source->SupplyPort))) return false;
    TestFalse(TEXT("A second one-ended hose cannot share the occupied outlet"),Second->ConnectStart(Source->SupplyPort));
    TestTrue(TEXT("Completing the first hose does not count its own endpoint twice"),First->ConnectEnd(Port));
    TestFalse(TEXT("Logical connection and physical attachment still represent one occupied outlet"),Second->ConnectStart(Source->SupplyPort));
    First->DisconnectStart();
    TestTrue(TEXT("Explicit removal frees the outlet for another hose"),Second->ConnectStart(Source->SupplyPort));
    TestEqual(TEXT("Connecting and replacing hoses cannot consume supply"),Source->GetRemainingUnits(),200.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedHoseBurstTest,"ColossalFizzCo.Hose.PressurizedSupply.HoseRupture",PressurizedColaTests::Flags)
bool FPressurizedHoseBurstTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* Source=F.Source(); auto* Port=F.ReceivingPort(F.LocalReceiver(),Source->SupplyPort->ResourceID,false);
    auto* Hose=F.Hose(); Port->bEnableIntakeOverload=true; Port->MaxIntakePressure=Port->MaxIntakeUnitsPerSecond=100;
    Hose->Transfer->BurstPressureThreshold=4; Hose->Transfer->BurstDelay=.1f; Hose->Transfer->bDrainEnabled=false;
    if (!Hose->ConnectStart(Source->SupplyPort) || !Hose->ConnectEnd(Port)) return false;
    Source->SetValveOpening(1); Hose->Transfer->TransferForDuration(1);
    TestTrue(TEXT("High source pressure ruptures a weaker hose"),Hose->Transfer->bBurst);
    TestFalse(TEXT("Ruptured hose releases the source connector"),IsValid(Hose->StartPort));
    TestFalse(TEXT("Ruptured hose releases the destination connector"),IsValid(Hose->EndPort));
    TestTrue(TEXT("Rupture deadline permits only one tenth second of source intake"),FMath::IsNearlyEqual(200-Source->GetRemainingUnits(),.8f,.005f));
    TestTrue(TEXT("Rupture releases exactly the local hot contents trapped at failure"),FMath::IsNearlyEqual(Hose->Transfer->TotalReleasedUnits,.8f,.005f));
    TestEqual(TEXT("Rupture empties the finite hose inventory"),Hose->Transfer->Buffer.Units,0.f);
    TestTrue(TEXT("Source, receiver and spill accounting conserve all contents"),FMath::IsNearlyEqual(Source->GetRemainingUnits()+Port->GetStoredUnits()+Hose->Transfer->Buffer.Units+Hose->Transfer->TotalReleasedUnits,200.f,.005f));
    const float Remaining=Source->GetRemainingUnits(),Released=Hose->Transfer->TotalReleasedUnits;
    TestEqual(TEXT("An already ruptured hose cannot purge the same spill again"),Hose->Transfer->PurgeContents(),0.f);
    Hose->Transfer->TransferForDuration(1);
    TestEqual(TEXT("Rupture stops subsequent source intake"),Source->GetRemainingUnits(),Remaining);
    TestEqual(TEXT("Later ticks cannot duplicate released contents"),Hose->Transfer->TotalReleasedUnits,Released);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedManualDeliveryTest,"ColossalFizzCo.Hose.PressurizedSupply.ManualDeliveredRate",PressurizedColaTests::Flags)
bool FPressurizedManualDeliveryTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    double LowDelivery=0,HighDelivery=0;
    for (float Opening:{.1f,.25f})
    {
        FFixture F; if (!F.World) return false;
        auto* Source=F.Source(); AManualColaFillingMachine* Machine=nullptr; AColaPropActor* Bottle=nullptr;
        if (!TestTrue(TEXT("Manual cradle holds a compatible receiving bottle"),F.ManualStation(Source->SupplyPort->ResourceID,Machine,Bottle))) return false;
        auto* Port=Machine->GetReceivingHosePort(0); auto* Hose=F.Hose(); auto* Filler=Machine->BottleFiller.Get();
        if (!TestTrue(TEXT("Closed source connects to a parked machine socket"),Hose->ConnectStart(Source->SupplyPort) && Hose->ConnectEnd(Port))) return false;
        TestEqual(TEXT("Selected machine port resolves the real connected hose"),Machine->GetSupplyHoseForPort(0),Hose->Transfer.Get());
        Filler->PortionsPerSecond=.01f; // Hose-delivered rate owns this route, independent of the old direct-fill setting.
        Source->SetValveOpening(Opening);
        if (!TestTrue(TEXT("Held lever starts external-hose filling without a managed source ISID"),Filler->StartFilling(0,Bottle->ISID))) return false;
        TestTrue(TEXT("Manual filler selects hose delivery"),Filler->IsHoseFilling());
        for (int32 I=0;I<20 && Filler->bIsFilling;++I) Filler->TickComponent(.05f,LEVELTICK_All,nullptr);
        const double Actual=F.ManagedUnits(Bottle),Expected=8.0*Opening;
        AddInfo(FString::Printf(TEXT("Manual valve %.2f received %.5f expected %.5f telemetry %.5f pressure %.5f source %.5f buffer %.5f"),Opening,Actual,Expected,Filler->DeliveredPortionsPerSecond,Filler->DeliveredSupplyPressure,Source->GetRemainingUnits(),Hose->Transfer->Buffer.Units));
        TestTrue(TEXT("Actual one-second bottle fill follows the source and hose delivered rate"),FMath::IsNearlyEqual(Actual,Expected,.005));
        TestTrue(TEXT("Filler telemetry reports actual received units per second"),FMath::IsNearlyEqual(Filler->DeliveredPortionsPerSecond,float(Expected),.005f));
        TestTrue(TEXT("Filler telemetry reports actual incoming pressure"),FMath::IsNearlyEqual(Filler->DeliveredSupplyPressure,8*Opening,.005f));
        TestTrue(TEXT("Manual filling conserves source, bottle and trapped hose contents"),FMath::IsNearlyEqual(Source->GetRemainingUnits()+Actual+Hose->Transfer->Buffer.Units,200.0,.01));
        TestEqual(TEXT("Hose-driven manual filling creates no duplicate direct transfer session"),F.Manager->FillSessions.Num(),0);
        TestFalse(TEXT("Safe supply pressure leaves the bottle controlled by the station"),Bottle->RocketComp->IsPoweredFlight());
        TestTrue(TEXT("Delivered hot liquid reaches the canonical bottle temperature"),FMath::IsNearlyEqual(F.Manager->FindUniqueData(Bottle->ISID)->ContinuousData.Temperature,85.f,.005f));
        const float BeforeStop=Source->GetRemainingUnits();
        Filler->StopFilling(); Hose->Transfer->TransferForDuration(.5f);
        TestEqual(TEXT("Stopped lever demand prevents subsequent source intake"),Source->GetRemainingUnits(),BeforeStop);
        TestTrue(TEXT("Stopping flow keeps the physical hose available at the socket"),Hose->EndPort==Port);
        if (Opening<.2f) LowDelivery=Actual; else HighDelivery=Actual;
    }
    TestTrue(TEXT("Opening the valve farther produces meaningfully faster actual bottle filling"),HighDelivery>LowDelivery*2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedBottleFailureTest,"ColossalFizzCo.Hose.PressurizedSupply.ExtremePressureRocket",PressurizedColaTests::Flags)
bool FPressurizedBottleFailureTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* Source=F.Source(); AManualColaFillingMachine* Machine=nullptr; AColaPropActor* Bottle=nullptr;
    if (!TestTrue(TEXT("Extreme-pressure fixture seats a normal plastic bottle"),F.ManualStation(Source->SupplyPort->ResourceID,Machine,Bottle))) return false;
    auto* Port=Machine->GetReceivingHosePort(0); auto* Hose=F.Hose(); auto* Filler=Machine->BottleFiller.Get();
    // Reinforced hose/socket allow the bottle's own pressure limit to decide the failure.
    Port->MaxIntakePressure=Port->MaxIntakeUnitsPerSecond=100;
    if (!Hose->ConnectStart(Source->SupplyPort) || !Hose->ConnectEnd(Port)) return false;
    Source->SetValveOpening(1);
    if (!TestTrue(TEXT("Reinforced supply route opens into the bottle"),Filler->StartFilling(0,Bottle->ISID))) return false;
    Filler->TickComponent(.2f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Extreme delivered pressure triggers the bottle's existing rocket failure"),Bottle->RocketComp->IsPoweredFlight());
    TestFalse(TEXT("Bottle failure releases the cradle restraint"),Bottle->IsSeatedInFiller());
    TestFalse(TEXT("Bottle failure closes the filling route in the same tick"),Filler->bIsFilling);
    TestFalse(TEXT("Reinforced hose itself remains intact"),Hose->Transfer->bBurst);
    const float Remaining=Source->GetRemainingUnits();
    TestTrue(TEXT("Only the triggering fixed step takes liquid from supply"),FMath::IsNearlyEqual(200-Remaining,.4f,.005f));
    double Released=0; for (const auto& Liquid:F.Manager->ReleasedLiquids) Released+=Liquid.Portions;
    TestTrue(TEXT("Rocket failure conserves delivered liquid across bottle, hose and release ledger"),FMath::IsNearlyEqual(Remaining+F.ManagedUnits(Bottle)+Hose->Transfer->Buffer.Units+Released,200.0,.01));
    Filler->TickComponent(1,LEVELTICK_All,nullptr); Hose->Transfer->TransferForDuration(1);
    TestEqual(TEXT("Failed bottle cannot continue draining the source"),Source->GetRemainingUnits(),Remaining);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedSavedPresetTest,"ColossalFizzCo.Hose.PressurizedSupply.SavedPresets",PressurizedColaTests::Flags)
bool FPressurizedSavedPresetTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* Class=LoadClass<AActor>(nullptr,TEXT("/Game/Assest/Blueprints/Cola_Container/BP_ColaCointainer.BP_ColaCointainer_C"));
    if (!TestTrue(TEXT("Existing tank Blueprint inherits the native pressurized container"),Class && Class->IsChildOf(APressurizedColaContainer::StaticClass()))) return false;
    auto* Container=F.World->SpawnActor<APressurizedColaContainer>(Class,FVector(0,0,1000),FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Saved tank preset spawns"),Container)) return false;
    TestEqual(TEXT("Saved tank valve starts closed"),Container->ValveOpening,0.f);
    TestEqual(TEXT("Saved tank starts with two hundred units"),Container->GetRemainingUnits(),200.f);
    TestEqual(TEXT("Saved tank preserves the chosen hot temperature"),Container->OutletTemperature,85.f);
    TestEqual(TEXT("Saved tank preserves the chosen outlet pressure"),Container->OutletPressure,8.f);
    TestEqual(TEXT("Saved tank preserves the chosen outlet rate"),Container->MaxOutletUnitsPerSecond,8.f);
    TestEqual(TEXT("Saved tank exposes one physical outlet"),Container->SupplyPort->MaxConnectedHoses,1);
    bool PreservedTankMesh=false;
    TArray<UStaticMeshComponent*> Meshes; Container->GetComponents(Meshes);
    for (const auto* Mesh:Meshes)
        if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetPathName()==TEXT("/Game/Assest/meshs/Cointaners/ContentTankers.ContentTankers"))
            PreservedTankMesh|=Mesh->GetRelativeScale3D().Equals(FVector(5),.0001);
    TestTrue(TEXT("Original ContentTankers art retains its authored scale of five"),PreservedTankMesh);
    auto* HoseClass=LoadClass<AHoseCableActor>(nullptr,TEXT("/Game/Assest/Blueprints/Cola_Container/BP_ColaHose.BP_ColaHose_C"));
    if (!TestNotNull(TEXT("Matching saved cola hose preset exists"),HoseClass)) return false;
    const auto* Hose=HoseClass->GetDefaultObject<AHoseCableActor>();
    TestEqual(TEXT("Saved hose carries the tank's cola identity"),Hose->Transfer->ResourceID,Container->SupplyPort->ResourceID);
    TestEqual(TEXT("Saved hose has two units of finite capacity"),Hose->Transfer->BufferCapacityUnits,2.f);
    TestEqual(TEXT("Saved hose has the selected nominal flow rate"),Hose->Transfer->MaxUnitsPerSecond,5.f);
    TestEqual(TEXT("Saved hose has the selected pressure threshold"),Hose->Transfer->BurstPressureThreshold,4.f);
    TestEqual(TEXT("Saved hose has the selected rupture delay"),Hose->Transfer->BurstDelay,.25f);
    TestEqual(TEXT("Saved hose retains the configured reach"),Hose->Cable->CableLength,1200.f);
    return true;
}
#endif
