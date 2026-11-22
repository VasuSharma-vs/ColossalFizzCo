#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/HoseSocketComponent.h"
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
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PropGrabTests { bool FireActionPhase(ACharacter* Player,const FString& ActionName,const FString& Phase); }
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
        auto* Tank=World->SpawnActor<APressurizedColaContainer>(FVector(0,0,1000),FRotator::ZeroRotator);
        Tank->SupplyPort->MaxOuttakePressure=Tank->SupplyPort->MaxOuttakeUnitsPerSecond=100.f;
        return Tank;
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
    UHoseSocketComponent* SignedSocket(FVector Position,float Pressure,float Stock,float SupplyRate)
    {
        auto* Owner=LocalReceiver(); Owner->SetActorLocation(Position);
        auto* Port=NewObject<UHoseSocketComponent>(Owner); Owner->AddInstanceComponent(Port);
        Port->SetupAttachment(Owner->GetRootComponent()); Port->RegisterComponent();
        Port->bUseSignedPressure=true; Port->OperatingPressure=Pressure;
        Port->bCanSupply=Port->bCanReceive=true; Port->SetStoredUnits(Stock);
        Port->MaxSupplyUnitsPerSecond=SupplyRate;
        Port->MaxIntakePressure=Port->MaxIntakeUnitsPerSecond=Port->MaxOuttakePressure=Port->MaxOuttakeUnitsPerSecond=100.f;
        return Port;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFreshHoseMachineFirstTest, "ColossalFizzCo.Hose.ColaContainer.FreshHoseMachineFirst", PressurizedColaTests::Flags)
bool FFreshHoseMachineFirstTest::RunTest(const FString& Parameters)
{
    PressurizedColaTests::FFixture F;
    constexpr int32 ColaID = 9441;
    constexpr int32 OtherColaID = 9442;
    constexpr int32 UnregisteredID = 9443;
    AManualColaFillingMachine* Machine = nullptr;
    AColaPropActor* Bottle = nullptr;
    if (!F.ManualStation(ColaID, Machine, Bottle)) return false;
    Machine->Production->View.bLeverHeld = false;
    auto* Tank = F.Source();
    Tank->SupplyPort->ResourceID = ColaID;
    auto* Inlet = Machine->GetHoseSocket(1);
    if (!TestNotNull(TEXT("The machine has a second unused inlet"), Inlet)) return false;
    TestNull(TEXT("The hose's placeholder content type is absent from the registry"), F.Manager->FindContentDefinition(UnregisteredID));

    for (bool bConnectStartTip : {true, false})
    {
        auto* Hose = F.Hose();
        Hose->Transfer->ResourceID = UnregisteredID;
        const int32 Last = Hose->Cable->NumSegments;
        const int32 MachineTip = bConnectStartTip ? 0 : Last;
        const int32 TankTip = bConnectStartTip ? Last : 0;
        if (!TestTrue(TEXT("Either end of a fresh empty hose can plug into the machine first"), Inlet->AttachHose(Hose, MachineTip))) return false;
        TestTrue(TEXT("The first plug reserves the inlet without requesting flow or assigning the placeholder type"),
            Inlet->ConnectedHose == Hose && Hose->EndPort == Inlet && Hose->GetReceivingConnectorIndex() == MachineTip
            && !Hose->Transfer->HasValidConnection() && !Inlet->bRequestsFlow && Hose->Transfer->ResourceID == UnregisteredID);
        if (!TestTrue(TEXT("Connecting the other end to the tank completes the transfer connection"),
            Tank->AttachSupplyHoseAtConnector(Hose, TankTip))) return false;
        TestTrue(TEXT("The tank identifies the cola and the machine end stays in its original socket"),
            Hose->StartPort == Tank->SupplyPort && Hose->EndPort == Inlet && Hose->GetReceivingConnectorIndex() == MachineTip
            && Hose->Transfer->HasValidConnection() && Machine->GetSupplyHoseForPort(1) == Hose->Transfer
            && Hose->Transfer->ResourceID == ColaID && Inlet->ResourceID == ColaID && !Inlet->bRequestsFlow);
        Inlet->DetachHose();
        Tank->SupplyPort->DetachHose();
    }

    auto* TypedInlet = Machine->GetHoseSocket(2);
    if (!TestNotNull(TEXT("The machine has an inlet to configure for one cola type"), TypedInlet)) return false;
    Machine->HosePortContentIDs[2] = ColaID;
    FPropContentDefinition OtherCola;
    OtherCola.ContentID = OtherColaID;
    F.Manager->RegisterContentDefinition(OtherCola);
    auto* Hose = F.Hose();
    Hose->Transfer->ResourceID = UnregisteredID;
    if (!TestTrue(TEXT("A typed inlet can seat a fresh empty hose"), TypedInlet->AttachHose(Hose, 0))) return false;
    TestEqual(TEXT("The typed inlet gives the hose its required type"), Hose->Transfer->ResourceID, ColaID);
    Tank->SupplyPort->ResourceID = OtherColaID;
    TestFalse(TEXT("A wrong cola type is rejected at the tank"), Tank->AttachSupplyHoseAtConnector(Hose, Hose->Cable->NumSegments));
    TestTrue(TEXT("A rejected tank leaves the original plug and content type alone"), TypedInlet->ConnectedHose == Hose
        && !Hose->StartPort && Hose->EndPort == TypedInlet && Hose->Transfer->ResourceID == ColaID);
    Tank->SupplyPort->ResourceID = ColaID;
    TestTrue(TEXT("A compatible tank can be connected after the rejected attempt"),
        Tank->AttachSupplyHoseAtConnector(Hose, Hose->Cable->NumSegments) && Hose->Transfer->HasValidConnection());
    return true;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedOpenOutletTest,"ColossalFizzCo.Hose.PressurizedSupply.OpenOutlet",PressurizedColaTests::Flags)
bool FPressurizedOpenOutletTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* Tank=F.Source(); if (!Tank) return false;
    Tank->Tick(1.f);
    TestEqual(TEXT("A closed uncovered outlet keeps its contents"),Tank->GetRemainingUnits(),200.f);
    TestEqual(TEXT("A closed outlet has no discharge"),Tank->OpenOutletUnitsPerSecond,0.f);

    Tank->SetValveOpening(.25f);
    Tank->Tick(.5f);
    TestEqual(TEXT("Quarter valve drains one stored unit in half a second"),Tank->GetRemainingUnits(),199.f);
    TestEqual(TEXT("Quarter valve exposes two units per second"),Tank->OpenOutletUnitsPerSecond,2.f);
    TestEqual(TEXT("Quarter valve exposes two pressure units"),Tank->SupplyPort->PushPressure,2.f);
    Tank->SetValveOpening(.75f);
    Tank->Tick(.5f);
    TestEqual(TEXT("Opening farther drains three stored units in half a second"),Tank->GetRemainingUnits(),196.f);
    TestEqual(TEXT("Wider valve increases discharge"),Tank->OpenOutletUnitsPerSecond,6.f);
    TestEqual(TEXT("Wider valve increases pressure"),Tank->SupplyPort->PushPressure,6.f);
    TestEqual(TEXT("The outlet display includes the spilled amount"),Tank->TotalDispensedUnits,4.f);
    TestEqual(TEXT("The outlet display reports the discharge rate"),Tank->CurrentOutletUnitsPerSecond,6.f);

    auto* Hose=F.Hose();
    if (!TestTrue(TEXT("A hose can occupy the open outlet with its far end free"),Tank->AttachSupplyHose(Hose))) return false;
    Tank->Tick(.5f);
    TestEqual(TEXT("A physically occupied outlet sends no free jet"),Tank->OpenOutletUnitsPerSecond,0.f);
    TestEqual(TEXT("A one-ended hose stops the open-air drain"),Tank->GetRemainingUnits(),196.f);
    Tank->SupplyPort->DetachHose();
    Tank->Tick(.5f);
    TestEqual(TEXT("Unplugging an open valve resumes the drain"),Tank->GetRemainingUnits(),193.f);

    auto* Receiver=F.ReceivingPort(F.LocalReceiver(),Tank->SupplyPort->ResourceID,false);
    if (!TestTrue(TEXT("An authored transfer can also occupy the outlet"),Hose->Transfer->ConnectEndpoints(Tank->SupplyPort,Receiver))) return false;
    Tank->Tick(.5f);
    TestEqual(TEXT("A logical connection prevents a second discharge"),Tank->OpenOutletUnitsPerSecond,0.f);
    TestEqual(TEXT("A logical connection does not debit the open-air drain"),Tank->GetRemainingUnits(),193.f);
    Hose->Transfer->DisconnectEndpoints();

    Tank->SetValveOpening(0.f);
    Tank->Tick(1.f);
    TestEqual(TEXT("Closing the valve stops a running discharge"),Tank->GetRemainingUnits(),193.f);
    Tank->SupplyPort->SetStoredUnits(.5f);
    Tank->SetValveOpening(1.f);
    Tank->Tick(.5f);
    TestEqual(TEXT("A nearly empty tank drains only what remains"),Tank->GetRemainingUnits(),0.f);
    TestEqual(TEXT("The final half unit counts once"),Tank->TotalDispensedUnits,7.5f);
    TestEqual(TEXT("The empty tank stops emitting"),Tank->OpenOutletUnitsPerSecond,0.f);
    Tank->Tick(.5f);
    TestEqual(TEXT("An empty tank cannot continue dispensing"),Tank->TotalDispensedUnits,7.5f);
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
        TestTrue(TEXT("The popped end now vents the ongoing supply without filling the disconnected receiver"),
            Hose->Transfer->Discharge.Point==EHoseDischargePoint::EndTip && Hose->Transfer->Discharge.UnitsPerSecond>0.f
            && Source->GetRemainingUnits()<Before && Port->GetStoredUnits()==0.f);
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
    Source->SetValveOpening(1); Hose->Transfer->TransferForDuration(.1f);
    TestTrue(TEXT("High source pressure ruptures a weaker hose"),Hose->Transfer->bBurst);
    TestTrue(TEXT("Ruptured cola hose preserves both fittings"),Hose->StartPort==Source->SupplyPort && Hose->EndPort==Port);
    TestTrue(TEXT("Ruptured cola hose shows the split in its wall"),Hose->Cable->HasVisibleRupture());
    TestTrue(TEXT("Source charges for one tenth of a second before the burst"),FMath::IsNearlyEqual(200-Source->GetRemainingUnits(),.8f,.005f));
    TestTrue(TEXT("Rupture releases exactly the local hot contents trapped at failure"),FMath::IsNearlyEqual(Hose->Transfer->TotalReleasedUnits,.8f,.005f));
    TestEqual(TEXT("Rupture empties the finite hose inventory"),Hose->Transfer->Buffer.Units,0.f);
    TestTrue(TEXT("Source, receiver and spill accounting conserve all contents"),FMath::IsNearlyEqual(Source->GetRemainingUnits()+Port->GetStoredUnits()+Hose->Transfer->Buffer.Units+Hose->Transfer->TotalReleasedUnits,200.f,.005f));
    const float Remaining=Source->GetRemainingUnits(),Released=Hose->Transfer->TotalReleasedUnits;
    TestEqual(TEXT("An already ruptured hose cannot purge the same spill again"),Hose->Transfer->PurgeContents(),0.f);
    for (int32 Step=0;Step<5;++Step) Hose->Transfer->TransferForDuration(.2f);
    TestTrue(TEXT("A ruptured hose keeps leaking at the open valve's actual rate"),
        FMath::IsNearlyEqual(Remaining-Source->GetRemainingUnits(),8.f,.01f)
        && FMath::IsNearlyEqual(Hose->Transfer->TotalReleasedUnits-Released,8.f,.01f)
        && FMath::IsNearlyEqual(Hose->Transfer->Discharge.UnitsPerSecond,8.f,.01f));
    TestTrue(TEXT("The damage diverts the supply to the tear and away from the connected receiver"),
        Hose->Transfer->Discharge.Point==EHoseDischargePoint::Rupture && Port->GetStoredUnits()==0.f);
    Source->SetValveOpening(0);
    const float ClosedUnits=Source->GetRemainingUnits();
    Hose->Transfer->TransferForDuration(.25f);
    TestTrue(TEXT("Closing the tank valve stops the rupture spray immediately"),
        Hose->Transfer->Discharge.UnitsPerSecond==0.f && Source->GetRemainingUnits()==ClosedUnits);
    Source->SupplyPort->SetStoredUnits(.5f); Source->SetValveOpening(.25f);
    const float PreEmpty=Hose->Transfer->TotalReleasedUnits;
    Hose->Transfer->TransferForDuration(.5f); Hose->Transfer->TransferForDuration(.5f);
    TestTrue(TEXT("Reopening the valve feeds the tear again and empties only the remaining liquid"),
        Source->GetRemainingUnits()==0.f && Hose->Transfer->Discharge.UnitsPerSecond==0.f
        && FMath::IsNearlyEqual(Hose->Transfer->TotalReleasedUnits-PreEmpty,.5f,.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseLooseTipFlowTest,"ColossalFizzCo.Hose.PressurizedSupply.LooseTipDischarge",PressurizedColaTests::Flags)
bool FHoseLooseTipFlowTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    for (bool bReversed:{false,true})
    {
        FFixture F; if (!F.World) return false;
        auto* Tank=F.Source(); auto* Hose=F.Hose(); auto* Transfer=Hose->Transfer.Get();
        FPropContentDefinition Cola; Cola.ContentID=Tank->SupplyPort->ResourceID; F.Manager->RegisterContentDefinition(Cola);
        if (!Tank->AttachSupplyHoseAtConnector(Hose,bReversed ? Hose->Cable->NumSegments : 0)) return false;
        Transfer->TransferForDuration(.5f);
        TestTrue(TEXT("The loose outlet stays dry until the supply valve opens"),!Tank->SupplyPort->bSupplyEnabled && Transfer->Discharge.UnitsPerSecond==0.f);
        Tank->SetValveOpening(.25f);
        TestEqual(TEXT("Open air is a spill, not delivery to a receiver"),Transfer->TransferForDuration(.5f),0.f);
        Transfer->TransferForDuration(.5f);
        TestTrue(TEXT("The true physical free connector emits continuously in either mounting direction"),
            Transfer->Discharge.Point==(bReversed ? EHoseDischargePoint::StartTip : EHoseDischargePoint::EndTip)
            && FMath::IsNearlyEqual(Transfer->Discharge.UnitsPerSecond,2.f,.004f) && Transfer->Discharge.Pressure==2.f);
        TestTrue(TEXT("Tank debits match the total discharged volume and remaining hose stock"),
            FMath::IsNearlyEqual(200.f-Tank->GetRemainingUnits(),2.f,.004f)
            && FMath::IsNearlyEqual(Tank->GetRemainingUnits()+Transfer->Buffer.Units+Transfer->TotalReleasedUnits,200.f,.004f));
        if (TestEqual(TEXT("A continuous tip stream creates one release episode"),F.Manager->ReleasedLiquids.Num(),1))
            TestTrue(TEXT("The episode preserves the original cola PID and released amount"),F.Manager->ReleasedLiquids[0].SourceProductID==Cola.ContentID
                && FMath::IsNearlyEqual(F.Manager->ReleasedLiquids[0].Portions,double(Transfer->TotalReleasedUnits),.004));
        const float BeforeClose=Tank->GetRemainingUnits();
        Tank->SetValveOpening(0.f); Transfer->TransferForDuration(.5f);
        TestTrue(TEXT("Closing the supply gate stops the free tip and preserves the remaining stock"),
            !Tank->SupplyPort->bSupplyEnabled && Transfer->Discharge.Point==EHoseDischargePoint::None
            && Tank->GetRemainingUnits()==BeforeClose);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseUnplugDrainTest,"ColossalFizzCo.Hose.PressurizedSupply.UnplugDrainsStoredCola",PressurizedColaTests::Flags)
bool FHoseUnplugDrainTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    for (bool Pressurized:{false,true})
    {
        FFixture F; if (!F.World) return false;
        auto* Tank=F.Source(); auto* Hose=F.Hose(); auto* Transfer=Hose->Transfer.Get();
        const auto ContentID=Tank->SupplyPort->ResourceID;
        auto* Receiver=F.ReceivingPort(F.LocalReceiver(),ContentID,false);
        Receiver->SetFlowRequested(false);
        FPropContentDefinition Cola; Cola.ContentID=ContentID; F.Manager->RegisterContentDefinition(Cola);
        if (!Tank->AttachSupplyHoseAtConnector(Hose,Hose->Cable->NumSegments) || !Hose->ConnectEnd(Receiver)) return false;
        Tank->SetValveOpening(.5f);
        Transfer->TransferForDuration(.5f);
        const float Stored=Transfer->Buffer.Units;
        if (!TestTrue(TEXT("A closed receiver traps two units behind its fitting"),FMath::IsNearlyEqual(Stored,2.f,.002f))) return false;
        if (!Pressurized) { Tank->SetValveOpening(0.f); Transfer->TransferForDuration(.1f); }

        Hose->DisconnectStart();
        if (!Pressurized) Hose->DisconnectEnd();
        const float AfterUnplug=Tank->GetRemainingUnits();
        Transfer->TransferForDuration(.05f);
        const float FirstRate=Transfer->Discharge.UnitsPerSecond;
        TestTrue(TEXT("Stored liquid vents from the physical unplugged connector"),FirstRate>0.f
            && Transfer->Discharge.Point==(Pressurized ? EHoseDischargePoint::EndTip : EHoseDischargePoint::StartTip));
        if (Pressurized)
        {
            TestTrue(TEXT("The detached hose carries its pressure and starts with a strong jet"),Transfer->Discharge.Pressure>3.f && FirstRate>4.f);
            Transfer->TransferForDuration(.05f);
            TestTrue(TEXT("The stream slows as it releases trapped pressure"),Transfer->Discharge.UnitsPerSecond<FirstRate && Transfer->CurrentPressure<3.5f);
            TestTrue(TEXT("Venting leaves the other fitting seated"),Hose->EndPort==Receiver && Hose->Cable->bAttachStart);
        }
        else TestTrue(TEXT("A hose disconnected at both ends still drains at gravity rate"),FMath::IsNearlyEqual(FirstRate,1.f,.002f) && Transfer->Discharge.Pressure==0.f);

        Transfer->TransferForDuration(2.5f);
        Transfer->TransferForDuration(.05f);
        TestTrue(TEXT("The open hose drains completely and stops spraying"),Transfer->Buffer.Units==0.f && Transfer->Discharge.UnitsPerSecond==0.f && Transfer->CurrentPressure==0.f);
        TestTrue(TEXT("Draining credits only the spill and does not debit the unplugged tank or fill the receiver"),
            FMath::IsNearlyEqual(Transfer->TotalReleasedUnits,Stored,.003f) && Tank->GetRemainingUnits()==AfterUnplug && Receiver->GetStoredUnits()==0.f);
        if (TestEqual(TEXT("The reserve is accounted as one spill episode"),F.Manager->ReleasedLiquids.Num(),1))
            TestTrue(TEXT("The spill keeps its catalog identity and exact volume"),F.Manager->ReleasedLiquids[0].SourceProductID==ContentID
                && FMath::IsNearlyEqual(F.Manager->ReleasedLiquids[0].Portions,double(Stored),.003));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseOpenTipRecoilTest,"ColossalFizzCo.Hose.PressurizedSupply.OpenTipRecoil",PressurizedColaTests::Flags)
bool FHoseOpenTipRecoilTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    const auto MeasureTravel=[this](float Opening,float ReactionStrength)
    {
        FFixture F; if (!F.World) return -1.;
        auto* Tank=F.Source(); auto* Hose=F.Hose(); auto* Cable=Hose->Cable.Get();
        Cable->bEnableCollision=false;
        Cable->bEnableSleeping=false;
        Cable->CableGravityScale=0.f;
        Cable->CableLength=240.f;
        Hose->JetReactionStrength=ReactionStrength;
        if (!Tank->AttachSupplyHose(Hose)) return -1.;
        const FVector Mount=Tank->SupplyPort->GetComponentLocation();
        TArray<FVector> Points;
        for (int32 Index=0;Index<=Cable->NumSegments;++Index)
            Points.Add(Mount+FVector(Cable->CableLength*Index/Cable->NumSegments,0,0));
        if (!Cable->SetCableParticleLocations(Points)) return -1.;
        const FVector OriginalTip=Points.Last();
        Tank->SetValveOpening(Opening);
        constexpr float Step=1.f/60;
        double Travel=0.;
        for (int32 Frame=0;Frame<20;++Frame)
        {
            Hose->Transfer->TransferForDuration(Step);
            Hose->Tick(Step);
            Cable->TickComponent(Step,LEVELTICK_All,nullptr);
            Cable->GetCableParticleLocations(Points);
            Travel=FMath::Max(Travel,FVector::Distance(OriginalTip,Points.Last()));
        }
        TestTrue(TEXT("Jet recoil leaves the tank fitting seated"),Hose->StartPort==Tank->SupplyPort && Points[0].Equals(Mount,.02));
        const int32 EndPoint=Cable->NumSegments;
        auto* Holder=F.World->SpawnActor<ACharacter>(Points.Last(),FRotator::ZeroRotator);
        Hose->bCarryConnectorOnPickup=true;
        if (!TestTrue(TEXT("A player can grab the freely spraying end"),Hose->RequestConnectorHandGrip(EndPoint,Holder->GetMesh(),NAME_None))) return -1.;
        Hose->Transfer->TransferForDuration(Step); Hose->Tick(Step); Cable->TickComponent(Step,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Holding the spraying tip keeps the connector pinned to the hand"),Hose->FindHandHeldConnector(Holder)==EndPoint
            && Cable->GetAttachedComponent()==Holder->GetMesh() && Hose->Transfer->Discharge.UnitsPerSecond>0.f);
        return Travel;
    };
    const double Disabled=MeasureTravel(.5f,0.f);
    const double Low=MeasureTravel(.125f,1.f);
    const double High=MeasureTravel(.5f,1.f);
    TestTrue(TEXT("Loose hoses move through the solver and stronger discharge causes more motion"),Disabled>=0. && Disabled<.1 && Low>.5 && High>Low*2.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoseIntakePressureReliefTest,"ColossalFizzCo.Hose.PressurizedSupply.GatedIntakePressureRelief",PressurizedColaTests::Flags)
bool FHoseIntakePressureReliefTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* Tank=F.Source(); auto* Hose=F.Hose(); auto* Transfer=Hose->Transfer.Get();
    auto* Port=F.ReceivingPort(F.LocalReceiver(),Tank->SupplyPort->ResourceID,false);
    Port->MaxReceiveUnitsPerSecond=4.f;
    Port->bEnableIntakeOverload=true;
    Port->MaxIntakeUnitsPerSecond=5.f; Port->MaxIntakePressure=100.f;
    Port->SetFlowRequested(false);
    if (!Tank->AttachSupplyHose(Hose) || !Hose->ConnectEnd(Port)) return false;
    Transfer->BurstPressureThreshold=5.f; Transfer->BurstDelay=.1f; Transfer->BlockedPressureRisePerSecond=1.f;
    Tank->OutletPressure=4.f; Tank->MaxOutletUnitsPerSecond=10.f; Tank->SetValveOpening(.5f);
    Transfer->TransferForDuration(.2f);
    TestTrue(TEXT("Closed receiver keeps the full pressure of two and accepts no cola"),Transfer->CurrentPressure==2.f
        && Port->GetStoredUnits()==0.f && Transfer->Discharge.UnitsPerSecond==0.f);
    const float InitialBuffer=Transfer->Buffer.Units;
    Port->SetFlowRequested(true);
    TestTrue(TEXT("Opening the receiver accepts four of the five units per second"),
        FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),2.f,.003f) && FMath::IsNearlyEqual(Transfer->LastIntakeUnits,2.5f,.003f));
    TestTrue(TEXT("Accepted output relieves 1.6 of the incoming pressure and stores the excess liquid"),
        FMath::IsNearlyEqual(Transfer->CurrentPressure,.4f,.004f) && FMath::IsNearlyEqual(Transfer->Buffer.Units-InitialBuffer,.5f,.004f));
    Port->SetFlowRequested(false); Transfer->TransferForDuration(3.f);
    TestTrue(TEXT("A single inlet holds steady at two as the closed hose fills"),Transfer->CurrentPressure==2.f
        && !Transfer->bBurst && FMath::IsNearlyEqual(Port->GetStoredUnits(),2.f,.004f));
    Tank->OutletPressure=10.f; Tank->SetValveOpening(.5f); Transfer->TransferForDuration(.3f);
    TestTrue(TEXT("The line can hold exactly its rated five units of inlet pressure"),Transfer->CurrentPressure==5.f && !Transfer->bBurst);
    Tank->OutletPressure=12.f; Tank->SetValveOpening(.5f); Port->SetFlowRequested(true); Transfer->TransferForDuration(.3f);
    TestTrue(TEXT("Six units of inlet pressure trip the five-unit hose rating even with an open receiver"),Transfer->bBurst
        && Transfer->FailureMode==EHoseFailureMode::OverpressureBurst && Transfer->Discharge.Point==EHoseDischargePoint::Rupture);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignedHoseFlowTest,"ColossalFizzCo.Hose.SignedPressure.OpposedAndReversibleFlow",PressurizedColaTests::Flags)
bool FSignedHoseFlowTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* A=F.SignedSocket(FVector(0,0,1000),2.f,80.f,5.f);
    auto* B=F.SignedSocket(FVector(150,0,1000),.3f,20.f,.75f);
    auto* Hose=F.Hose(); auto* Transfer=Hose->Transfer.Get();
    if (!TestTrue(TEXT("Both bidirectional sockets seat in either physical order"),A->AttachHose(Hose,Hose->Cable->NumSegments) && B->AttachHose(Hose,0))) return false;
    TestTrue(TEXT("Opposing pumps deliver 4.25 units per second at 2.3 line pressure"),
        FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),2.125f,.002f)
        && FMath::IsNearlyEqual(Transfer->CurrentPressure,2.3f,.001f));
    TestTrue(TEXT("Port telemetry follows actual flow"),Transfer->GetLastPortFlowUnits(A)>0.f && Transfer->GetLastPortFlowUnits(B)<0.f);
    B->MaxSupplyUnitsPerSecond=10.f;
    TestTrue(TEXT("The weaker socket still receives when its rated flow is greater"),
        FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),2.125f,.002f)
        && A->IsSupplyRequested() && !B->IsSupplyRequested() && B->GetSignedFlowUnitsPerSecond()<0.f);
    B->SetOperatingPressure(0.f);
    TestTrue(TEXT("A passive receiver accepts the full five units per second and relieves the two units of pressure"),
        FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),2.5f,.002f) && FMath::Abs(Transfer->CurrentPressure)<.002f
        && B->LastIncomingPressure==2.f);
    A->OperatingPressure=.3f; A->MaxSupplyUnitsPerSecond=.75f;
    B->OperatingPressure=2.f; B->MaxSupplyUnitsPerSecond=5.f;
    TestTrue(TEXT("Changing the pressure reverses delivery without reconnecting"),
        FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),2.125f,.002f)
        && Transfer->GetLastPortFlowUnits(B)>0.f && Transfer->GetLastPortFlowUnits(A)<0.f);
    A->OperatingPressure=B->OperatingPressure=0.f;
    TestEqual(TEXT("Two explicitly passive ports do not pump"),Transfer->TransferForDuration(.5f),0.f);
    TestTrue(TEXT("Reversing cannot duplicate liquid"),FMath::IsNearlyEqual(A->GetStoredUnits()+B->GetStoredUnits()+Transfer->Buffer.Units,100.f,.002f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBidirectionalColaTankTest,"ColossalFizzCo.Hose.SignedPressure.SavedTankBackflow",PressurizedColaTests::Flags)
bool FBidirectionalColaTankTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* TankClass=LoadClass<APressurizedColaContainer>(nullptr,TEXT("/Game/Assest/Blueprints/Cola_Container/BP_ColaCointainer.BP_ColaCointainer_C"));
    auto* HoseClass=LoadClass<AHoseCableActor>(nullptr,TEXT("/Game/Resources/Hoses/BP_ContentHose.BP_ContentHose_C"));
    if (!TankClass || !HoseClass) return false;
    auto* A=F.World->SpawnActor<APressurizedColaContainer>(TankClass,FVector(0,0,1000),FRotator::ZeroRotator);
    auto* B=F.World->SpawnActor<APressurizedColaContainer>(TankClass,FVector(150,0,1000),FRotator::ZeroRotator);
    auto* Hose=F.World->SpawnActor<AHoseCableActor>(HoseClass);
    if (!A || !B || !Hose) return false;
    auto* Transfer=Hose->Transfer.Get();
    Transfer->SetComponentTickEnabled(false);
    if (!TestTrue(TEXT("The actual content hose accepts tanks on both physical ends"),
        A->AttachSupplyHoseAtConnector(Hose,Hose->Cable->NumSegments) && B->AttachSupplyHoseAtConnector(Hose,0))) return false;
    TestTrue(TEXT("Both saved tanks receive through their valve socket"),A->SupplyPort->bCanReceive && B->SupplyPort->bCanReceive);
    A->SupplyPort->SetStoredUnits(80.f); B->SupplyPort->SetStoredUnits(20.f);
    A->OutletPressure=2.f; A->MaxOutletUnitsPerSecond=5.f;
    B->OutletPressure=1.f; B->MaxOutletUnitsPerSecond=8.f;
    A->SetValveOpening(1.f); B->SetValveOpening(1.f);
    TestTrue(TEXT("The winning pressure fills the other tank's storage and reports signed flow to both actors"),
        FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),1.25f,.004f)
        && FMath::IsNearlyEqual(A->GetRemainingUnits(),78.75f,.004f) && FMath::IsNearlyEqual(B->GetRemainingUnits(),21.25f,.004f)
        && A->SupplyPort->GetSignedFlowUnitsPerSecond()>0.f && B->SupplyPort->GetSignedFlowUnitsPerSecond()<0.f
        && FMath::IsNearlyEqual(B->SupplyPort->TotalReceivedUnits,1.25,.004));

    A->OutletPressure=-1.f; B->MaxOutletUnitsPerSecond=5.f;
    A->RefreshSupplySettings(); B->RefreshSupplySettings();
    TestTrue(TEXT("Negative pressure reverses the flow into the first tank while the connectors stay seated"),
        A->SupplyPort->GetOperatingPressure()==-1.f && FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),2.5f,.004f)
        && FMath::IsNearlyEqual(A->GetRemainingUnits(),81.25f,.004f) && A->SupplyPort->GetSignedFlowUnitsPerSecond()<0.f
        && Hose->Cable->GetAttachedComponent()==A->SupplyPort && Hose->Cable->GetStartAttachmentComponent()==B->SupplyPort
        && Transfer->GetOtherEndpoint(A->SupplyPort)==B->SupplyPort);

    A->SetValveOpening(0.f);
    TestTrue(TEXT("Closing the receiving valve pauses backflow and leaves the liquid in the line"),
        Transfer->TransferForDuration(.1f)==0.f && !A->SupplyPort->bRequestsFlow && Transfer->Buffer.Units>0.f);
    const float Trapped=Transfer->Buffer.Units;
    const float BeforeRecovery=A->GetRemainingUnits();
    if (!B->SupplyPort->DetachHose()) return false;
    A->SetValveOpening(1.f);
    TestTrue(TEXT("An accepting suction socket recovers the trapped cola with the other tip loose"),
        FMath::IsNearlyEqual(Transfer->TransferForDuration(.5f),Trapped,.004f)
        && FMath::IsNearlyEqual(A->GetRemainingUnits(),BeforeRecovery+Trapped,.004f) && Transfer->TotalReleasedUnits==0.f);
    TestTrue(TEXT("Direction changes and recovery conserve the original liquid"),
        FMath::IsNearlyEqual(A->GetRemainingUnits()+B->GetRemainingUnits()+Transfer->Buffer.Units,100.f,.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignedHoseConnectorTest,"ColossalFizzCo.Hose.SignedPressure.ConnectorPressureAndFlowLimits",PressurizedColaTests::Flags)
bool FSignedHoseConnectorTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    for (bool bSource:{true,false}) for (bool bPressure:{true,false})
    {
        FFixture F; if (!F.World) return false;
        auto* A=F.SignedSocket(FVector(0,0,1000),2.f,80.f,5.f);
        auto* B=F.SignedSocket(FVector(150,0,1000),0.f,20.f,0.f);
        auto* Hose=F.Hose(); auto* Transfer=Hose->Transfer.Get();
        const int32 SourcePoint=bPressure ? 0 : Hose->Cable->NumSegments;
        const int32 OtherPoint=SourcePoint==0 ? Hose->Cable->NumSegments : 0;
        if (!A->AttachHose(Hose,SourcePoint) || !B->AttachHose(Hose,OtherPoint)) return false;
        auto* Popped=bSource ? A : B;
        auto* Safe=bSource ? B : A;
        if (bSource) (bPressure ? A->MaxOuttakePressure : A->MaxOuttakeUnitsPerSecond)=1.f;
        else (bPressure ? B->MaxIntakePressure : B->MaxIntakeUnitsPerSecond)=1.f;
        TestEqual(TEXT("An overloaded fitting trips before any debit or credit"),Transfer->TransferForDuration(.1f),0.f);
        TestTrue(TEXT("Only the overloaded socket drops its connector"),Popped->bIsBroken && !Popped->ConnectedHose && Safe->ConnectedHose==Hose);
        const bool bAtPhysicalStart=(bSource ? SourcePoint : OtherPoint)==0;
        TestEqual(TEXT("The hose reports the physical connector that popped"),Transfer->FailureMode,
            bAtPhysicalStart ? EHoseFailureMode::StartConnectorPopped : EHoseFailureMode::EndConnectorPopped);
        TestTrue(TEXT("Connector pop retains the other anchor and does not split the hose"),
            !Transfer->bBurst && !Hose->Cable->HasVisibleRupture() && Hose->Cable->bAttachStart!=Hose->Cable->bAttachEnd);
        TestEqual(TEXT("After a pop nothing can reach the unseated receiver"),Transfer->TransferForDuration(.2f),0.f);
        TestTrue(TEXT("Only a still-seated source can feed a popped hose"),bSource ? A->GetStoredUnits()==80.f && Transfer->Discharge.UnitsPerSecond==0.f
            : FMath::IsNearlyEqual(Transfer->TotalReleasedUnits,1.f,.002f) && B->GetStoredUnits()==20.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignedHosePressureBurstTest,"ColossalFizzCo.Hose.SignedPressure.ChargeBurstAndCatalogRelease",PressurizedColaTests::Flags)
bool FSignedHosePressureBurstTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
	FFixture F; if (!F.World) return false;
    auto* Tank=F.Source(); auto* Hose=F.Hose(); auto* Transfer=Hose->Transfer.Get();
    FPropContentDefinition Cola; Cola.ContentID=Tank->SupplyPort->ResourceID; F.Manager->RegisterContentDefinition(Cola);
    if (!Tank->AttachSupplyHose(Hose)) return false;
    Tank->SetValveOpening(.25f);
    Transfer->bDrainEnabled=false;
    TestEqual(TEXT("An open tank charges a hose with its discharge valve closed"),Transfer->TransferForDuration(.5f),0.f);
    TestTrue(TEXT("Only the units held by the hose are debited and tagged with the catalog PID"),
        FMath::IsNearlyEqual(Transfer->Buffer.Units,1.f,.001f) && Transfer->Buffer.SourceProductID==Cola.ContentID
        && FMath::IsNearlyEqual(Tank->GetRemainingUnits(),199.f,.001f));

    auto* Other=F.SignedSocket(FVector(150,0,1000),2.f,10.f,5.f);
    if (!Other->AttachHose(Hose)) return false;
    Transfer->BlockedPressureRisePerSecond=1.f;
    Transfer->BurstPressureThreshold=4.5f; Transfer->BurstDelay=.15f;
    Transfer->TransferForDuration(.3f); Hose->Tick(.016f);
    TestTrue(TEXT("Two positive drives charge the hose and expand its rendered diameter"),
        Transfer->CurrentPressure==4.f && !Transfer->bBurst && Hose->Cable->GetRenderingDiameter()>Hose->Cable->CableWidth);
    Transfer->TransferForDuration(1.f);
    TestTrue(TEXT("Dead-headed pressure ruptures the hose wall"),Transfer->bBurst && Transfer->FailureMode==EHoseFailureMode::OverpressureBurst
        && Hose->Cable->HasVisibleRupture());
    TestTrue(TEXT("Both connector meshes remain mounted at their sockets"),Tank->SupplyPort->ConnectedHose==Hose && Other->ConnectedHose==Hose
        && Hose->Cable->GetStartAttachmentComponent()==Tank->SupplyPort && Hose->Cable->GetAttachedComponent()==Other);
    if (TestEqual(TEXT("Burst records exactly one cataloged spill"),F.Manager->ReleasedLiquids.Num(),1))
    {
        const auto& Release=F.Manager->ReleasedLiquids[0];
        TestTrue(TEXT("World spill preserves the catalog PID and coalesces continued leakage from both pumps"),Release.SourceProductID==Cola.ContentID
            && Release.Portions>4. && FMath::IsNearlyEqual(Release.Portions,double(Transfer->TotalReleasedUnits),.005));
    }
    TestTrue(TEXT("Source stocks and spilled liquid conserve the starting supply"),
        FMath::IsNearlyEqual(Tank->GetRemainingUnits()+Other->GetStoredUnits()+Transfer->TotalReleasedUnits,210.f,.005f));
    TestEqual(TEXT("The accounted spill leaves nothing to purge twice"),Transfer->PurgeContents(),0.f);
    Tank->SetValveOpening(.125f); Other->OperatingPressure=0.f;
    TestTrue(TEXT("Repair restores the existing seated circuit"),Transfer->RepairHose() && Transfer->HasValidConnection());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSignedHoseVacuumTest,"ColossalFizzCo.Hose.SignedPressure.VacuumCollapseKeepsConnectors",PressurizedColaTests::Flags)
bool FSignedHoseVacuumTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F; if (!F.World) return false;
    auto* A=F.SignedSocket(FVector(0,0,1000),1.f,20.f,4.f);
    auto* B=F.SignedSocket(FVector(150,0,1000),0.f,20.f,0.f);
    auto* Hose=F.Hose(); auto* Transfer=Hose->Transfer.Get();
    if (!A->AttachHose(Hose,0) || !B->AttachHose(Hose,Hose->Cable->NumSegments)) return false;
    B->SetFlowRequested(false); Transfer->TransferForDuration(.1f);
    const float Trapped=Transfer->Buffer.Units;
    A->OperatingPressure=-1.f; B->OperatingPressure=-2.f;
    B->SetFlowRequested(true); Transfer->VacuumCollapseThreshold=2.5f; Transfer->BurstDelay=.3f;
    Transfer->TransferForDuration(.05f); Hose->Tick(.016f);
    TestTrue(TEXT("Vacuum compresses the hose before failure"),Transfer->CurrentPressure==-3.f
        && Hose->Cable->GetRenderingDiameter()<Hose->Cable->CableWidth && !Transfer->bBurst);
    TestTrue(TEXT("The lower-pressure socket draws out the remaining content"),Transfer->GetLastPortFlowUnits(A)==0.f && Transfer->GetLastPortFlowUnits(B)<0.f);
    Transfer->TransferForDuration(.5f);
    TestTrue(TEXT("Sustained vacuum fails the dry hose with both ends seated"),Transfer->FailureMode==EHoseFailureMode::VacuumCollapse
        && Transfer->bBurst && Hose->Cable->HasVisibleRupture() && A->ConnectedHose==Hose && B->ConnectedHose==Hose);
    TestEqual(TEXT("A drained hose has no liquid to spill"),Transfer->TotalReleasedUnits,0.f);
    TestTrue(TEXT("Suction credits exactly the trapped liquid to the lower-pressure end"),FMath::IsNearlyEqual(B->GetStoredUnits(),20.f+Trapped,.002f));
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
        const bool MachineFirst=Opening>.2f;
        const bool Connected=MachineFirst ? Hose->ConnectStart(Port) && Hose->ConnectEnd(Source->SupplyPort)
            : Hose->ConnectStart(Source->SupplyPort) && Hose->ConnectEnd(Port);
        if (!TestTrue(TEXT("A closed source and parked machine can occupy either logical hose slot"),Connected)) return false;
        TestEqual(TEXT("Selected machine port resolves the real connected hose"),Machine->GetSupplyHoseForPort(0),Hose->Transfer.Get());
        Filler->PortionsPerSecond=.01f; // Hose-delivered rate owns this route, independent of the old direct-fill setting.
        Source->SetValveOpening(Opening);
        TestTrue(TEXT("The parked socket waits behind the open tank valve"),Source->SupplyPort->bSupplyEnabled && !Port->bRequestsFlow);
        if (!TestTrue(TEXT("Held lever starts external-hose filling without a managed source ISID"),Filler->StartFilling(0,Bottle->ISID))) return false;
        TestTrue(TEXT("Manual filler selects hose delivery"),Filler->IsHoseFilling());
        TestTrue(TEXT("Starting the filling stroke opens the selected socket"),Port->bRequestsFlow);
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
        const float BottleBeforeStop=F.ManagedUnits(Bottle);
        Filler->StopFilling(); Hose->Transfer->TransferForDuration(.5f);
        TestFalse(TEXT("Stopping the filling stroke closes the selected socket"),Port->bRequestsFlow);
        TestEqual(TEXT("Stopped lever demand prevents further bottle filling"),float(F.ManagedUnits(Bottle)),BottleBeforeStop);
        TestTrue(TEXT("An open valve charges the hose behind the closed machine inlet"),Source->GetRemainingUnits()<BeforeStop);
        TestTrue(TEXT("Stopping flow keeps the physical hose available at the socket"),(MachineFirst ? Hose->StartPort : Hose->EndPort)==Port);
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
    const double FailedBottleUnits=F.ManagedUnits(Bottle);
    Filler->TickComponent(1,LEVELTICK_All,nullptr); Hose->Transfer->TransferForDuration(1);
    TestTrue(TEXT("Failed bottle receives none of the charge behind the closed inlet"),F.ManagedUnits(Bottle)==FailedBottleUnits);
    TestTrue(TEXT("Closed inlet can only add the remaining stock to the finite hose"),FMath::IsNearlyEqual(Remaining-Source->GetRemainingUnits(),Hose->Transfer->Buffer.Units,.005f));
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
    TestTrue(TEXT("Saved tank uses its Hose Socket for supply and return"), Container->SupplyPort->bCanSupply
        && Container->SupplyPort->bCanReceive && Container->SupplyPort->GetAttachParent()==Container->ContainerRoot
        && UHoseSocketComponent::GetSocketFromHitComponent(Container->SupplyPort->GetTraceVolume())==Container->SupplyPort);
    TestTrue(TEXT("Saved tank has a Niagara jet at the outlet"), Container->OutletJet && Container->OutletJet->GetAttachParent()==Container->SupplyPort
        && GetNameSafe(Container->OutletJet->GetAsset())==TEXT("NS_ColaOutletJet") && !Container->OutletJet->IsActive());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressurizedContainerSocketTest,"ColossalFizzCo.Hose.PressurizedSupply.SocketWorldInteraction",PressurizedColaTests::Flags)
bool FPressurizedContainerSocketTest::RunTest(const FString& Parameters)
{
    using namespace PressurizedColaTests;
    FFixture F;
    if (!F.World) return false;
    auto* TankClass=LoadClass<APressurizedColaContainer>(nullptr,TEXT("/Game/Assest/Blueprints/Cola_Container/BP_ColaCointainer.BP_ColaCointainer_C"));
    auto* HoseClass=LoadClass<AHoseCableActor>(nullptr,TEXT("/Game/Resources/Hoses/BP_ContentHose.BP_ContentHose_C"));
    auto* MasterClass=LoadClass<AHoseCableActor>(nullptr,TEXT("/Game/Resources/Hoses/BP_HoseCable.BP_HoseCable_C"));
    auto* PlayerClass=LoadClass<ACharacter>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if (!TestTrue(TEXT("Saved tank, hose and character presets are available"),TankClass && HoseClass && MasterClass && PlayerClass)) return false;
    auto* Tank=F.World->SpawnActor<APressurizedColaContainer>(TankClass,FVector(0,0,1000),FRotator(0,35,0));
    auto* Hose=F.World->SpawnActor<AHoseCableActor>(HoseClass);
    auto* Player=F.World->SpawnActor<ACharacter>(PlayerClass);
    auto* Controller=F.World->SpawnActor<APlayerController>();
    if (!Tank || !Hose || !Player || !Controller) return false;
    TestTrue(TEXT("Tank socket is authored for the BP_ContentHose class"),Tank->SupplyPort->CompatibleHoseClass.Get()==HoseClass);
    auto* GenericHose=F.World->SpawnActor<AHoseCableActor>(MasterClass);
    if (!GenericHose) return false;
    TestFalse(TEXT("The generic parent hose cannot attach despite its default content category"),Tank->AttachSupplyHose(GenericHose));
    TestFalse(TEXT("Hose-side calls still respect the container's socket class"),GenericHose->ConnectStart(Tank->SupplyPort));
    GenericHose->Destroy();
    auto* Actions=Player->FindComponentByClass<UShopPlayerInteractionComponent>();
    auto* TickHit=FindFProperty<FStructProperty>(PlayerClass,TEXT("Hit Result On tick"));
    if (!TestTrue(TEXT("Saved player exposes the World Interaction trace and actions"),Actions && TickHit && TickHit->Struct==FHitResult::StaticStruct())) return false;
    Player->SetActorEnableCollision(false);
    Player->GetCharacterMovement()->DisableMovement();
    Controller->Possess(Player);
    auto* LocalPlayer=NewObject<ULocalPlayer>(GEngine);
    LocalPlayer->PlayerController=Controller;
    Controller->Player=LocalPlayer;
    Controller->SetViewTarget(Player);
    auto UseManualSimulation = [](AHoseCableActor& TestHose)
    {
        TestHose.Cable->bEnableCollision=false;
        TestHose.Cable->bEnableSleeping=false;
        TestHose.Cable->CableGravityScale=0;
        TestHose.Cable->SetComponentTickEnabled(false);
        TestHose.Transfer->SetComponentTickEnabled(false);
    };
    UseManualSimulation(*Hose);

    auto* TankTrace=Tank->SupplyPort->GetTraceVolume();
    if (!TestNotNull(TEXT("The tank outlet generates a reusable socket trace volume"),TankTrace)) return false;
    const FVector Port=Tank->SupplyPort->GetComponentLocation();
    const FVector Outward=Tank->SupplyPort->GetUpVector();
    TestTrue(TEXT("The outlet and valve use separate query controls"),TankTrace->GetAttachParent()==Tank->SupplyPort
        && Tank->IsHoseSocket(TankTrace) && Tank->IsValveControl(Tank->ValveVolume) && !Tank->IsHoseSocket(Tank->ValveVolume));
    Player->SetActorLocation(Port+Outward*155-FVector(0,0,Player->BaseEyeHeight));
    FVector Eye;
    FRotator Aim;
    const int32 LastPoint = Hose->Cable->NumSegments;
    auto GrabConnector = [&](int32 PointIndex)
    {
        Controller->SetControlRotation(Outward.Rotation());
        Controller->UpdateCameraManager(0);
        Controller->GetPlayerViewPoint(Eye,Aim);
        TArray<FVector> CablePoints;
        for (int32 Index=0; Index<=LastPoint; ++Index)
            CablePoints.Add(Eye+Aim.Vector()*85+Aim.RotateVector(FVector(0,5*(PointIndex==0 ? Index : LastPoint-Index),0)));
        Hose->Cable->SetCableParticleLocations(CablePoints);
        Hose->Tick(0);
        return Actions->TryToggleCableGrabFromView() && Hose->FindHandHeldConnector(Player)==PointIndex;
    };
    if (!TestTrue(TEXT("The player grabs physical connector zero"),GrabConnector(0))) return false;

    auto PressInteract = [&](UPrimitiveComponent* Target)
    {
        Player->GetActorEyesViewPoint(Eye,Aim);
        const FVector Direction=(Target->Bounds.Origin-Eye).GetSafeNormal();
        Controller->SetControlRotation(Direction.Rotation());
        Controller->UpdateCameraManager(0);
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ColaContainerSocketTest),true,Player);
        F.World->LineTraceSingleByChannel(Hit,Eye,Eye+Direction*250,ECC_Visibility,Query);
        if (!TestEqual(TEXT("The player's real trace hits the requested control volume"),Hit.GetComponent(),Target)) return false;
        *TickHit->ContainerPtrToValuePtr<FHitResult>(Player)=Hit;
        auto* View=F.World->SpawnActor<ACameraActor>(Eye,Direction.Rotation());
        if (!View) return false;
        Controller->SetViewTarget(View);
        Controller->UpdateCameraManager(0);
        const bool bHandled=PropGrabTests::FireActionPhase(Player,TEXT("IA_Intract"),TEXT("Started"))
            && PropGrabTests::FireActionPhase(Player,TEXT("IA_Intract"),TEXT("Completed"));
        Controller->SetViewTarget(Player);
        Controller->UpdateCameraManager(0);
        View->Destroy();
        return bHandled;
    };
    auto MountOnTank = [&](int32 ConnectorIndex)
    {
        TArray<FVector> Before, After;
        const FTransform CableFrame = Hose->Cable->GetComponentTransform();
        Hose->Cable->GetCableParticleLocations(Before);
        if (!PressInteract(TankTrace)) return false;
        Hose->Cable->GetCableParticleLocations(After);
        if (!TestEqual(TEXT("Mounting preserves the cable particle count"),After.Num(),Before.Num())) return false;
        TestTrue(TEXT("The selected cable point seats immediately at the outlet"),After[ConnectorIndex].Equals(Port,.001));
        for (int32 Index=0; Index<Before.Num(); ++Index)
            if (Index!=ConnectorIndex && !TestTrue(TEXT("Mounting preserves the loose cable path"),After[Index].Equals(Before[Index],.001))) return false;
        return TestTrue(TEXT("Mounting a rotated socket preserves the hose's render frame"),Hose->Cable->GetAttachParent()==Hose->SceneRoot
            && Hose->Cable->GetComponentTransform().Equals(CableFrame));
    };

    Hose->Transfer->Category=EResourceTransportCategory::Fuel;
    if (!TestTrue(TEXT("E reaches tank socket through the saved interaction graph"),PressInteract(TankTrace))) return false;
    TestTrue(TEXT("An incompatible hose stays in the player's hand"),!Hose->StartPort && Actions->IsCableGrabActive()
        && Hose->FindHandHeldConnector(Player)==0);
    Hose->Transfer->Category=EResourceTransportCategory::Content;
    if (!TestTrue(TEXT("E seats the compatible inlet and releases the hand"),MountOnTank(0)
        && Hose->StartPort==Tank->SupplyPort && Tank->SupplyPort->ConnectedHose==Hose
        && Hose->FindHandHeldConnector(Player)==INDEX_NONE && !Actions->IsCableGrabActive())) return false;
    Hose->Tick(0);
    TestTrue(TEXT("The seated connector follows the socket pose, including rotation"),Hose->StartEndpointMesh->GetComponentLocation().Equals(Port,.02)
        && Hose->StartEndpointMesh->GetUpVector().Equals(Outward,.002));
    auto* OtherHose=F.World->SpawnActor<AHoseCableActor>(HoseClass);
    if (!OtherHose) return false;
    TestFalse(TEXT("An occupied socket refuses another hose"),OtherHose->ConnectStart(Tank->SupplyPort));

    auto* Receiver=F.LocalReceiver();
    Receiver->SetActorLocation(Port+Outward*280);
    auto* ReceiverPort=F.ReceivingPort(Receiver,Tank->SupplyPort->ResourceID,false);
    if (!TestTrue(TEXT("The hose can connect to a compatible receiving port"),Hose->ConnectEnd(ReceiverPort))) return false;
    TestEqual(TEXT("The closed tank sends no liquid"),Hose->Transfer->TransferForDuration(.5f),0.f);
    if (!TestTrue(TEXT("E hits the valve volume and opens the tank"),PressInteract(Tank->ValveVolume) && Tank->ValveOpening>0)) return false;
    Tank->Tick(.4f);
    TestFalse(TEXT("The visible valve rotor turns"),Tank->ValveRotor->GetRelativeRotation().IsNearlyZero());
    Hose->Transfer->TransferForDuration(.5f);
    TestTrue(TEXT("The open valve sends liquid through the connected hose"),ReceiverPort->GetStoredUnits()>0 && Tank->GetRemainingUnits()<200);

    Hose->DisconnectStart();
    TestFalse(TEXT("The tank cannot reuse a connector already mounted to a receiver"),Tank->AttachSupplyHoseAtConnector(Hose,LastPoint));
    TestTrue(TEXT("A rejected hookup leaves the receiver mounted"),Hose->Cable->bAttachEnd && Hose->Cable->GetAttachedComponent()==ReceiverPort);
    Hose->DisconnectEnd();
    // The delivery above can leave buffered cola bound to the first receiver's transport context.
    Swap(Hose,OtherHose);
    UseManualSimulation(*Hose);
    if (!TestTrue(TEXT("The player can also grab the last physical connector"),GrabConnector(LastPoint))) return false;
    if (!TestTrue(TEXT("E seats the last physical connector on the tank"),MountOnTank(LastPoint)
        && Hose->StartPort==Tank->SupplyPort && !Actions->IsCableGrabActive())) return false;
    Hose->Tick(0);
    TestTrue(TEXT("The mounted end follows the arrow and the other connector remains free"),Hose->Cable->bAttachEnd
        && Hose->Cable->GetAttachedComponent()==Tank->SupplyPort && Hose->EndEndpointMesh->GetComponentLocation().Equals(Port,.02)
        && Hose->EndEndpointMesh->GetUpVector().Equals(Outward,.002) && Hose->CanGrabConnector(0));

    AManualColaFillingMachine* Machine=nullptr;
    AColaPropActor* Bottle=nullptr;
    if (!TestTrue(TEXT("An active test filling machine is available"),F.ManualStation(Tank->SupplyPort->ResourceID,Machine,Bottle))) return false;
    Machine->Production->View.bLeverHeld=false;
    Machine->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Machine->SetActorLocation(Machine->GetActorLocation()+Port+Outward*275-Machine->GetHosePortTransform(0).GetLocation());
    if (!TestTrue(TEXT("The player grabs the remaining physical connector"),GrabConnector(0))) return false;
    // A stationary view makes the receiving socket targetable without the animated camera.
    Player->GetActorEyesViewPoint(Eye,Aim);
    const FRotator MachineAim=(Machine->GetHosePortTransform(0).GetLocation()-Eye).Rotation();
    auto* SocketView=F.World->SpawnActor<ACameraActor>(Eye,MachineAim);
    if (!SocketView) return false;
    Controller->SetViewTarget(SocketView);
    Controller->UpdateCameraManager(0);
    auto* MachineSocket = Machine->GetHoseSocket(0);
    FHitResult SocketHit;
    FCollisionQueryParams SocketQuery(SCENE_QUERY_STAT(FillingMachineSocketTest),false,Player);
    SocketQuery.AddIgnoredActor(Hose);
    F.World->LineTraceSingleByChannel(SocketHit,Eye,Eye+MachineAim.Vector()*Actions->MaxInteractionDistance,ECC_Visibility,SocketQuery);
    if (!TestEqual(TEXT("The live trace hits the reusable machine socket"), SocketHit.GetComponent(),
        static_cast<UPrimitiveComponent*>(MachineSocket->GetTraceVolume()))) return false;
    *TickHit->ContainerPtrToValuePtr<FHitResult>(Player)=SocketHit;
    Machine->HosePortContentIDs[0] = Tank->SupplyPort->ResourceID+1;
    TestTrue(TEXT("A machine socket with a different content type leaves the connector in hand"), Actions->TryOpenTerminalFromView()
        && Actions->IsCableGrabActive() && !MachineSocket->ConnectedHose);
    Machine->HosePortContentIDs[0] = 0;
    const bool bConnected = Actions->TryOpenTerminalFromView();
    if (!TestTrue(TEXT("The player's socket interaction connects the remaining end to the filling machine"),bConnected
        && Hose->EndPort==Machine->GetReceivingHosePort(0) && !Actions->IsCableGrabActive()))
    {
        FHitResult ViewHit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(FillingMachineSocketTest),false,Player);
        Query.AddIgnoredActor(Hose);
        F.World->LineTraceSingleByChannel(ViewHit,Eye,Eye+MachineAim.Vector()*Actions->MaxInteractionDistance,ECC_Visibility,Query);
        AddInfo(FString::Printf(TEXT("Machine hookup view hit %s.%s, held=%d, reachable=%d, resource=%d, buffer=%.3f"),
            *GetNameSafe(ViewHit.GetActor()),*GetNameSafe(ViewHit.GetComponent()),Actions->IsCableGrabActive(),
            Actions->IsStationReachable(Machine),Hose->Transfer->ResourceID,Hose->Transfer->Buffer.Units));
        return false;
    }
    Hose->Tick(0);
    TestTrue(TEXT("The reversed hose still supplies the filling machine from the tank"),Hose->Transfer->HasValidConnection()
        && Machine->GetSupplyHoseForPort(0)==Hose->Transfer && Hose->Cable->GetStartAttachmentComponent()==Hose->EndPort);
    auto* HoldingComponent=FindFProperty<FObjectPropertyBase>(PlayerClass,TEXT("Holding Component"));
    if (!TestNotNull(TEXT("The saved character has its legacy holding component"),HoldingComponent)) return false;

    const float OpenValve=Tank->ValveOpening;
    SocketView->SetActorRotation((TankTrace->Bounds.Origin-Eye).Rotation());
    Controller->UpdateCameraManager(0);
    TestTrue(TEXT("Tapping E on the occupied supply socket keeps the hose plugged in"), Actions->TryOpenTerminalFromView()
        && Hose->StartPort==Tank->SupplyPort && Tank->SupplyPort->ConnectedHose==Hose);
    TestTrue(TEXT("Empty-handed grab takes the reversed supply connector into the hand"), Actions->TryGrabSocketHoseFromView(true)
        && Actions->IsCableGrabActive() && Hose->FindHandHeldConnector(Player)==LastPoint
        && !Hose->StartPort && !Tank->SupplyPort->ConnectedHose && Hose->EndPort==MachineSocket && Tank->ValveOpening==OpenValve);
    TestTrue(TEXT("E reconnects the tank without changing the receiving socket or valve"), Actions->TryOpenTerminalFromView()
        && Hose->StartPort==Tank->SupplyPort && Tank->SupplyPort->ConnectedHose==Hose && !Actions->IsCableGrabActive()
        && Hose->EndPort==MachineSocket && Hose->Transfer->HasValidConnection() && Tank->ValveOpening==OpenValve);
    HoldingComponent->SetObjectPropertyValue_InContainer(Player,Bottle->PropMesh);
    TestTrue(TEXT("Grab with full hands drops the tank end and keeps the carried component"),
        Actions->TryGrabSocketHoseFromView(false) && !Hose->StartPort && !Tank->SupplyPort->ConnectedHose
        && Hose->EndPort==MachineSocket && !Actions->IsCableGrabActive() && Tank->ValveOpening==OpenValve
        && HoldingComponent->GetObjectPropertyValue_InContainer(Player)==Bottle->PropMesh);
    HoldingComponent->SetObjectPropertyValue_InContainer(Player,nullptr);
    if (!TestTrue(TEXT("The tank socket can be reconnected after removal"),Tank->AttachSupplyHoseAtConnector(Hose,LastPoint))) return false;

    SocketView->SetActorRotation(MachineAim);
    Controller->UpdateCameraManager(0);
    TestTrue(TEXT("A tap keeps the machine end plugged in"), Actions->TryOpenTerminalFromView()
        && MachineSocket->ConnectedHose==Hose && Hose->EndPort==MachineSocket && !Actions->IsCableGrabActive());
    TestTrue(TEXT("Grabbing the occupied socket takes the machine end into the player's hand"), Actions->TryGrabSocketHoseFromView(true)
        && !MachineSocket->ConnectedHose && !Hose->EndPort && !MachineSocket->bRequestsFlow
        && Actions->IsCableGrabActive() && Hose->FindHandHeldConnector(Player)==0
        && Hose->StartPort==Tank->SupplyPort && Hose->Cable->GetAttachedComponent()==Tank->SupplyPort);
    TestTrue(TEXT("E reconnects the nozzle from the player's hand"), Actions->TryOpenTerminalFromView()
        && !Actions->IsCableGrabActive()
        && Hose->EndPort==MachineSocket && MachineSocket->ConnectedHose==Hose && Hose->Transfer->HasValidConnection());
    HoldingComponent->SetObjectPropertyValue_InContainer(Player,Bottle->PropMesh);
    TestTrue(TEXT("Grab with occupied hands drops the machine end and preserves the carried component"),
        Actions->TryGrabSocketHoseFromView(false) && !Hose->EndPort && !Actions->IsCableGrabActive()
        && Hose->StartPort==Tank->SupplyPort && HoldingComponent->GetObjectPropertyValue_InContainer(Player)==Bottle->PropMesh);
    HoldingComponent->SetObjectPropertyValue_InContainer(Player,nullptr);
    Controller->SetViewTarget(Player);
    Controller->UpdateCameraManager(0);
    Hose->DisconnectStart();
    Hose->DisconnectEnd();

    auto* LegacyConnector=OtherHose->EndEndpointMesh.Get();
    if (!LegacyConnector->AttachToComponent(Player->GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale)) return false;
    HoldingComponent->SetObjectPropertyValue_InContainer(Player,LegacyConnector);
    LegacyConnector->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    if (!TestTrue(TEXT("The tank accepts the other end through the legacy Holding Component"),PressInteract(TankTrace)
        && OtherHose->StartPort==Tank->SupplyPort)) return false;
    OtherHose->Tick(0);
    TestNull(TEXT("The socket clears the character's legacy hand reference"),HoldingComponent->GetObjectPropertyValue_InContainer(Player));
    TestTrue(TEXT("Legacy handoff also locks the connector to the arrow"),LegacyConnector->GetComponentLocation().Equals(Port,.02)
        && LegacyConnector->GetUpVector().Equals(Outward,.002));

    OtherHose->DisconnectStart();
    auto* HoldingActor=FindFProperty<FObjectPropertyBase>(PlayerClass,TEXT("Holding Actor"));
    if (!TestNotNull(TEXT("The saved character has a held actor reference"),HoldingActor)) return false;
    if (!OtherHose->AttachToComponent(Player->GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale)) return false;
    HoldingActor->SetObjectPropertyValue_InContainer(Player,OtherHose);
    if (!TestTrue(TEXT("The tank can connect a compatible hose carried as Holding Actor"),PressInteract(TankTrace)
        && OtherHose->StartPort==Tank->SupplyPort)) return false;
    TestNull(TEXT("The connected hose clears the hand's actor reference"),HoldingActor->GetObjectPropertyValue_InContainer(Player));
    TestTrue(TEXT("The container keeps the hose attached through the socket after clearing the hand"),
        OtherHose->GetAttachParentActor()!=Player && OtherHose->Cable->GetAttachedComponent()==Tank->SupplyPort);

    Hose->Destroy();
    OtherHose->Destroy();
    Hose = F.World->SpawnActor<AHoseCableActor>(HoseClass);
    if (!TestNotNull(TEXT("A new instance of the saved hose is available"), Hose)) return false;
    UseManualSimulation(*Hose);
    if (!TestTrue(TEXT("The player can take the end of a hose that has never been connected"), GrabConnector(0))) return false;
    if (!TestTrue(TEXT("E plugs the fresh hose into the filling machine before the tank"),
        PressInteract(MachineSocket->GetTraceVolume()) && Hose->EndPort == MachineSocket
        && !Hose->StartPort && !Actions->IsCableGrabActive())) return false;
    if (!TestTrue(TEXT("The opposite end can then be carried to the tank"), GrabConnector(LastPoint))) return false;
    TestTrue(TEXT("E completes the connection while keeping the original machine plug in place"),
        MountOnTank(LastPoint) && Hose->StartPort == Tank->SupplyPort && Hose->EndPort == MachineSocket
        && Hose->Transfer->HasValidConnection() && !Actions->IsCableGrabActive());
    return true;
}
#endif
