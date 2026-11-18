#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ASMComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ManualColaFillingMachine.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FManualMachineClientPoolCollisionTest,
	"ColossalFizzCo.Shop.ManualMachineClientPoolCollision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FManualMachineClientPoolCollisionTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	auto* MachineProduct=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_ManualFillingMachine.DA_ManualFillingMachine"));
	auto* CylinderProduct=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_CylinderSpicy.DA_CylinderSpicy"));
	if (!TestNotNull(TEXT("Saved machine product"),MachineProduct) || !TestNotNull(TEXT("Saved supply product"),CylinderProduct)) return false;
	const int32 CylinderPID=Manager->RegisterProductFromDataAsset(CylinderProduct);
	const int32 MachinePID=Manager->RegisterProductFromDataAsset(MachineProduct);
	auto* Machine=World->SpawnActor<AManualColaFillingMachine>(MachineProduct->CustomActiveActorClass,FVector(0,0,1000),FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Fresh saved machine Blueprint"),Machine)) return false;
	Machine->bIsActive=false; Machine->ISID=0; Machine->PropMesh->SetSimulatePhysics(false);
	UStaticMesh* Pedestal=Machine->PropMesh->GetStaticMesh().Get();
	auto* PedestalMaterial=Machine->PropMesh->GetMaterial(0);
	const FVector PedestalScale=Machine->PropMesh->GetRelativeScale3D();
	TestEqual(TEXT("Machine starts with the canonical pedestal"),Pedestal,MachineProduct->StaticMesh.Get());
	TestTrue(TEXT("Cylinder has different visual and actor class"),CylinderProduct->StaticMesh!=Pedestal
		&& CylinderProduct->CustomActiveActorClass!=MachineProduct->CustomActiveActorClass);

	auto CheckPedestal=[&]()
	{
		TestTrue(TEXT("Pool maintenance preserves the externally owned machine actor"),IsValid(Machine));
		TestEqual(TEXT("A remote cylinder slot does not claim the machine's logical identity"),Machine->ISID,0);
		TestEqual(TEXT("Client retains machine pedestal mesh"),Machine->PropMesh->GetStaticMesh().Get(),Pedestal);
		TestEqual(TEXT("Client retains machine pedestal material"),Machine->PropMesh->GetMaterial(0),PedestalMaterial);
		TestTrue(TEXT("Client retains machine pedestal scale"),Machine->PropMesh->GetRelativeScale3D().Equals(PedestalScale));
	};
	for (int32 Route=0;Route<3;++Route)
	{
		const int32 ID=Manager->GenerateProductInstance(CylinderPID,FTransform(FVector(400+Route*200,0,1000)));
		auto* Record=Manager->FindISIDData(ID); if (!TestNotNull(TEXT("Cylinder instance"),Record)) return false;
		Record->ASMID=Machine->ASMID; Record->Representation=EPropRepresentation::ASM; Record->State=EPropState::Unsettled;
		Manager->RegisterProp(Machine); // Deliberately collide the server slot with a local machine slot.
		Manager->SwapRoles();
		if (Route==0) Manager->UpdateClientASM(ID);
		else if (Route==1) Manager->Multicast_NotifyISMConvertedToASM_Implementation(ID,CylinderPID,Machine->ASMID,Record->ActiveTransform,EPropState::Unsettled,EPropActivationReason::Interaction);
		else
		{
			FPropASMConversionEntry Entry; Entry.ISID=ID; Entry.ASMID=Machine->ASMID;
			Manager->Multicast_NotifyBatchISMConvertedToASM_Implementation({Entry});
		}
		Manager->SwapRoles();
		CheckPedestal();
		const auto* Bound=Manager->GetActiveActorFromISID(ID);
		TestTrue(TEXT("Only the product's exact actor class may represent the cylinder"),!Bound || Bound->GetClass()==CylinderProduct->CustomActiveActorClass.Get());
		if (Route>0) TestNotNull(TEXT("Conversion can acquire the correct cylinder class"),Bound);
		TestEqual(TEXT("Original local slot still belongs to the machine"),Manager->GetActiveActorFromASMID(Machine->ASMID),static_cast<AActivePropActor*>(Machine));
		if (Bound)
		{
			TestTrue(TEXT("Replacement uses a different local pool slot"),Bound->ASMID!=Machine->ASMID);
			TestEqual(TEXT("Replacement owns its slot mapping"),Manager->GetActiveActorFromASMID(Bound->ASMID),const_cast<AActivePropActor*>(Bound));
			TestEqual(TEXT("Logical mapping uses replacement's local slot"),Manager->ASMComponent->ISIDToASMID.FindRef(ID),Bound->ASMID);
			TestEqual(TEXT("Inverse slot mapping identifies the cylinder"),Manager->ASMComponent->ASMIDToISID.FindRef(Bound->ASMID),ID);
			const auto* Current=Manager->FindISIDData(ID);
			FPropInstanceDelta Delta; Delta.ISID=ID; Delta.ProductID=CylinderPID; Delta.Version=Current->Version+1;
			Delta.IISMComponentID=Current->IISMComponentID; Delta.ISMI=Current->ISMI;
			Delta.Representation=EPropRepresentation::ASM; Delta.State=Current->State; Delta.Transform=Current->ActiveTransform;
			Delta.CustomFlags=Machine->ASMID<<16;
			Manager->SwapRoles(); Manager->ApplyInstanceDelta(Delta); Manager->SwapRoles();
			TestEqual(TEXT("A repeated server slot hint does not evict the correct local receiver"),Manager->GetActiveActorFromISID(ID),const_cast<AActivePropActor*>(Bound));
		}
	}

	// A same-class slot still cannot steal an actor already representing another ISID.
	const int32 FirstID=Manager->GenerateProductInstance(MachinePID,FTransform(FVector(0,500,1000)));
	AActivePropActor* First=nullptr; Manager->PromoteInstanceToASM(FirstID,EPropActivationReason::Interaction,false,First);
	if (!TestNotNull(TEXT("Existing active machine"),First)) return false;
	const int32 SecondID=Manager->GenerateProductInstance(MachinePID,FTransform(FVector(0,800,1000)));
	auto* Second=Manager->FindISIDData(SecondID); Second->ASMID=First->ASMID; Second->Representation=EPropRepresentation::ASM;
	Manager->SwapRoles(); Manager->UpdateClientASM(SecondID); Manager->SwapRoles();
	TestEqual(TEXT("Occupied matching class keeps its original identity"),First->ISID,FirstID);
	TestTrue(TEXT("Slot collision does not bind one actor to two machines"),Manager->GetActiveActorFromISID(SecondID)!=First);
	const FTransform FirstTransform=First->GetActorTransform();
	const EPropState FirstState=First->State;
	FActivePropPhysicsSync Physics; Physics.ISID=SecondID; Physics.ASMID=First->ASMID;
	Physics.Location=FVector(9000,9000,9000); Physics.State=EPropState::InMotion;
	Manager->SwapRoles();
	Manager->Multicast_SyncActivePhysicsBatch_Implementation({Physics});
	Manager->Multicast_NotifyASMConvertedToISM_Implementation(SecondID,MachinePID,First->ASMID,FTransform(FVector(0,800,1000)),EPropState::AtRest);
	Manager->SwapRoles();
	TestTrue(TEXT("Delayed physics for a reused slot does not move another machine"),First->GetActorTransform().Equals(FirstTransform));
	TestEqual(TEXT("Delayed physics does not change another machine's state"),First->State,FirstState);
	TestTrue(TEXT("Delayed demotion does not pool another machine"),First->bIsActive && First->ISID==FirstID && !First->IsHidden());

	// Delayed product definitions must defer slot reuse, then recover on the snapshot.
	auto* Matching=World->SpawnActor<AManualColaFillingMachine>(MachineProduct->CustomActiveActorClass,FVector(0,800,1000),FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Fresh matching receiving actor"),Matching)) return false;
	Matching->bIsActive=false; Matching->ISID=0; Matching->PropMesh->SetSimulatePhysics(false);
	Second=Manager->FindISIDData(SecondID); Second->ASMID=Matching->ASMID; Second->Representation=EPropRepresentation::ASM;
	const FPropProductDefinition Definition=*Manager->FindProductDefinition(MachinePID);
	Manager->ProductDefinitions.Remove(MachinePID);
	FPropASMConversionEntry Pending; Pending.ISID=SecondID; Pending.ASMID=Matching->ASMID;
	Manager->SwapRoles();
	Manager->Multicast_NotifyBatchISMConvertedToASM_Implementation({Pending});
	TestEqual(TEXT("Unknown definition waits without claiming the actor"),Matching->ISID,0);
	Manager->RegisterProductDefinition(Definition); Manager->UpdateClientASM(SecondID);
	Manager->SwapRoles();
	TestEqual(TEXT("Inactive matching-class machine can be reused after snapshot"),Manager->GetActiveActorFromISID(SecondID),static_cast<AActivePropActor*>(Matching));
	TestEqual(TEXT("Reused machine receives the requested identity"),Matching->ISID,SecondID);
	TestEqual(TEXT("Existing other machine keeps its identity"),First->ISID,FirstID);
	TestEqual(TEXT("Reused machine retains canonical pedestal"),Matching->PropMesh->GetStaticMesh().Get(),MachineProduct->StaticMesh.Get());
	Manager->SwapRoles();
	Manager->Multicast_SyncActivePhysicsBatch_Implementation({Physics});
	TestTrue(TEXT("Matching logical identity still receives physics with a different local slot"),Matching->GetActorLocation().Equals(Physics.Location,.1));
	Manager->Multicast_NotifyASMConvertedToISM_Implementation(SecondID,MachinePID,First->ASMID,FTransform(FVector(0,800,1000)),EPropState::AtRest);
	Manager->SwapRoles();
	TestTrue(TEXT("Matching logical identity still demotes"),!Matching->bIsActive && Matching->ISID==0);
	TestTrue(TEXT("Other machine remains active after matching demotion"),First->bIsActive && First->ISID==FirstID);
	return true;
}
#endif
