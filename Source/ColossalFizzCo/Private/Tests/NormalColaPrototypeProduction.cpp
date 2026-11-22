#include "Tests/NormalColaPrototypeTestActor.h"
#include "Shop/ShopATMComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopSupplyDepot.h"
#include "Shop/ColaSupplyContainer.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Components/HoseSocketComponent.h"
#include "Shop/ShopShelfStockComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/ColaPropActor.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
bool FindSupportedShelfPlacement(APropManager* Manager,UShopShelfStockComponent* Shelf,int32 ISID,FTransform& Result)
{
	const auto* Record=Manager->FindISIDData(ISID);
	const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	const auto* Data=Manager->FindUniqueData(ISID);
	if (!Product || !Product->StaticMesh || !Data) return false;
	TArray<FBox> Occupied;
	for (const auto& Pair:Manager->ISIDData)
	{
		if (Manager->ResolveRootISID(Pair.Key)==ISID) continue;
		const auto* Other=Manager->FindProductDefinition(Pair.Value.ProductID);
		if (Other && Other->StaticMesh) Occupied.Add(Other->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Pair.Key)));
	}
	const FBox LocalBounds=Product->StaticMesh->GetBoundingBox();
	Result=Manager->GetTransform(ISID); Result.SetRotation(FQuat::Identity);
	const FBox BoundsAtOrigin=LocalBounds.TransformBy(FTransform(FQuat::Identity,FVector::ZeroVector,Result.GetScale3D()));
	TInlineComponentArray<UStaticMeshComponent*> Slabs(Shelf->GetOwner());
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeRestockSpace),false);
	if (auto* Active=Manager->GetActiveActorFromISID(ISID)) Query.AddIgnoredActor(Active);
	for (auto* Slab:Slabs)
	{
		if (!Slab->IsCollisionEnabled() || !Slab->GetStaticMesh()) continue;
		const FBox Surface=Slab->Bounds.GetBox();
		if (Surface.GetExtent().Z>25 || Surface.GetSize().X<BoundsAtOrigin.GetSize().X || Surface.GetSize().Y<BoundsAtOrigin.GetSize().Y) continue;
		for (float X=Surface.Min.X-BoundsAtOrigin.Min.X+1;X<=Surface.Max.X-BoundsAtOrigin.Max.X-1;X+=15)
		for (float Y=Surface.Min.Y-BoundsAtOrigin.Min.Y+1;Y<=Surface.Max.Y-BoundsAtOrigin.Max.Y-1;Y+=15)
		{
			Result.SetLocation(FVector(X,Y,Surface.Max.Z-BoundsAtOrigin.Min.Z+1));
			const FBox Interior=LocalBounds.TransformBy(Result).ExpandBy(-.5f);
			if (Occupied.ContainsByPredicate([&](const auto& Other){return Interior.Intersect(Other);})) continue;
			if (Manager->GetWorld()->OverlapBlockingTestByChannel(Interior.GetCenter(),FQuat::Identity,ECC_PhysicsBody,
				FCollisionShape::MakeBox(Interior.GetExtent()),Query)) continue;
			return true;
		}
	}
	return false;
}
}

void ANormalColaPrototypeTestActor::TickReinvestment()
{
	UShopProductionStationComponent* Bench=nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (const auto* Prop=Cast<AActivePropActor>(*It)) if (!Prop->bIsActive || Prop->ISID<=0) continue;
		if (auto* Candidate=It->FindComponentByClass<UShopProductionStationComponent>(); Candidate && Candidate->Filler) { Bench=Candidate; break; }
	}
	if (!Bench) return;
	UShopProductionStationComponent* Supplier=Bench;
	if (Bench->bManualLeverMode)
	{
		Supplier=nullptr;
		for (TActorIterator<AShopSupplyDepot> It(GetWorld());It;++It) { Supplier=It->Supplies; break; }
		if (!Supplier) return;
	}
	// A slower opt-in validation fixture allows several rendered frames at partial fill.
	if (HasAuthority() && Stage==140 && Bench->Filler && FParse::Param(FCommandLine::Get(),TEXT("BottleVisualTest")))
		Bench->Filler->PortionsPerSecond=FMath::Min(Bench->Filler->PortionsPerSecond,1.f);
	auto* PC=Cast<APlayerController>(GetOwner()); auto* Pawn=PC ? PC->GetPawn().Get() : nullptr;
	auto* Actions=Pawn ? Pawn->FindComponentByClass<UShopPlayerInteractionComponent>() : nullptr;
	if (!HasAuthority())
	{
		if (Stage==160 && bManualCappingInput)
		{
			if (PC && PC->IsLocalController() && Actions && LastSentStage!=Stage)
			{ Actions->OpenTerminal(Bench->GetOwner()); LastSentStage=Stage; }
			return;
		}
		if (Stage==140 && Bench->bManualLeverMode && Actions && TestSupplyISIDs.Num()==3)
		{
			if (LastSentStage!=Stage || Bench->View.ConnectedSourceISID!=TestSupplyISIDs[2] || Bench->View.SeatedBottleISID!=TestSupplyISIDs[0])
			{
				Actions->RequestProductionAction(Bench,Bench->View.SessionID,EShopProductionAction::AlignBottle,TestSupplyISIDs[0],0);
				LastSentStage=Stage;
			}
			const auto* Data=Manager->FindUniqueData(TestSupplyISIDs[0]);
			if (Data && Data->EventData.ContentLevel>=1 && Bench->View.bFilling) Actions->RequestProductionAction(Bench,Bench->View.SessionID,EShopProductionAction::StopFilling,TestSupplyISIDs[0],0);
			const bool Safe=Data && Data->EventData.ContentLevel>=1 && Data->ContinuousData.SealedPressure<=Bench->SafeResidualPressure;
			Actions->RequestProductionAction(Bench,Bench->View.SessionID,Safe ? EShopProductionAction::ReleaseFillingLever : EShopProductionAction::HoldFillingLever,TestSupplyISIDs[0],0);
			++LocalRequestsSent; return;
		}
		if (Stage==160 && Bench->bAllowSoloCap && Actions && TestSupplyISIDs.Num()==3)
		{
			const auto* Data=Manager->FindUniqueData(TestSupplyISIDs[0]);
			if (Data && Data->ContinuousData.SealedPressure<=Bench->SafeResidualPressure)
			{ Actions->RequestProductionAction(Bench,Bench->View.SessionID,EShopProductionAction::TurnCap,TestSupplyISIDs[0],0); ++LocalRequestsSent; }
			return;
		}
		if (Stage==160 && PC && PC->IsLocalController() && Actions && TestSupplyISIDs.Num()==3 && Bench->bRequiresCooperativeCap)
		{
			Actions->RequestProductionAction(Bench,Bench->View.SessionID,EShopProductionAction::PressCap,TestSupplyISIDs[0],0);
			++LocalRequestsSent; return;
		}
		if (!PC || !PC->IsLocalController() || !Actions || LastSentStage==Stage || !Bench->View.SessionID.IsValid()) return;
		if (Stage==110 || Stage==120 || Stage==130)
		{
			const FName Offer=Stage==110 ? TEXT("EmptyBottle") : Stage==120 ? TEXT("TwistCap") : TEXT("CylinderMint");
			Actions->RequestSupplyPurchase(Supplier,Supplier->View.SessionID,Offer,1);
		}
		else if (TestSupplyISIDs.Num()==3 && Stage>=140 && Stage<=160)
		{
			const auto Action=Stage==140 ? EShopProductionAction::Fill : Stage==150 ? EShopProductionAction::AttachCap : EShopProductionAction::CloseCap;
			Actions->RequestProductionAction(Bench,Bench->View.SessionID,Action,TestSupplyISIDs[0],Stage==140 ? TestSupplyISIDs[2] : TestSupplyISIDs[1]);
		}
		else if (Stage==200) Actions->OpenTerminal(Bench->GetOwner());
		else return;
		LastSentStage=Stage; ++LocalRequestsSent; return;
	}
	if (Stage==100)
	{
		if (Command!=2 || !Pawn || !Bench->BottleInput || !Bench->CapInput || !Bench->CylinderInput) return;
		UShopATMComponent* ATM=nullptr;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It) if ((ATM=It->FindComponentByClass<UShopATMComponent>())) break;
		if (!ATM || !ATM->DepositVolume) { Fail(TEXT("Validation map has no physical ATM deposit tray")); return; }
		// The fixture stages existing coins. The real ATM still discovers and validates them.
		for (const auto& Coin:Manager->ShopCheckout->ExportCommittedState().Cash.Coins)
		{
			if (Coin.CustomerOwner.IsValid() || Coin.CheckoutID.IsValid()) continue;
			if (auto* Prop=Manager->GetActiveActorFromISID(Coin.ISID)) Prop->PropMesh->SetSimulatePhysics(false);
			Manager->InitializeInstanceTransform(Coin.ISID,FTransform(ATM->DepositVolume->GetComponentLocation()));
		}
		ATM->CollectDeposits(); ExpectedBankCents=ExpectedCashCents;
		if (Manager->SharedWallet->GetWalletState().BalanceCents!=ExpectedBankCents || Manager->ShopCheckout->GetTeamCashCents()!=0)
		{ Fail(TEXT("Banking sale proceeds did not conserve physical cash")); return; }
		PlaceTestPawn(Pawn,Supplier->GetOwner()->GetActorLocation()+FVector(0,-180,100));
		for (const auto& Pair:Manager->ISIDData) KnownInstances.Add(Pair.Key);
		TestSupplyISIDs.Init(0,3); ProductionStartedAt=GetWorld()->GetTimeSeconds(); Stage=110; ForceNetUpdate(); return;
	}
	if (Stage==200) return;
	if (Stage==160 && Bench->bRequiresCooperativeCap)
	{
		for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
		{
			auto* Host=It->Get(); auto* Partner=Host && Host->IsLocalController() ? Host->GetPawn().Get() : nullptr;
			auto* PartnerActions=Partner ? Partner->FindComponentByClass<UShopPlayerInteractionComponent>() : nullptr;
			if (!PartnerActions || Partner==Pawn) continue;
			PlaceTestPawn(Partner,Bench->GetOwner()->GetActorLocation()+FVector(100,-100,100));
			if (bManualCappingInput)
			{
				if (LastSentStage!=Stage) { PartnerActions->OpenTerminal(Bench->GetOwner()); LastSentStage=Stage; }
			}
			else if (Bench->View.CapHolder) PartnerActions->RequestProductionAction(Bench,Bench->View.SessionID,EShopProductionAction::TurnCap,TestSupplyISIDs[0],0);
			break;
		}
	}
	if (GetWorld()->GetTimeSeconds()-ProductionStartedAt>(bManualCappingInput ? 600 : 60)) { Fail(TEXT("Remote supply/fill/cap/restock sequence exceeded its acceptance timeout")); return; }
	if (Stage==110 || Stage==120 || Stage==130)
	{
		const FName OfferID=Stage==110 ? TEXT("EmptyBottle") : Stage==120 ? TEXT("TwistCap") : TEXT("CylinderMint");
		const auto* Catalog=Manager->ShopSupplies->Catalog.Get(); const auto* Offer=Catalog ? Catalog->FindOffer(OfferID) : nullptr;
		if (!Offer) { Fail(TEXT("Missing configured prototype supply offer")); return; }
		int32 NewID=0;
		for (int32 ID : Supplier->LastDeliveredSupplies)
		{
			const auto* R=Manager->FindISIDData(ID);
			const auto* Product=R ? Manager->FindProductDefinition(R->ProductID) : nullptr;
			if (!KnownInstances.Contains(ID) && Product && Product->SourceDataAsset.Get()==Offer->Product.Get()) { NewID=ID; break; }
		}
		if (!NewID) return;
		ExpectedBankCents-=Offer->UnitCostCents;
		if (Manager->SharedWallet->GetWalletState().BalanceCents!=ExpectedBankCents) { Fail(TEXT("Remote purchase charged a non-catalog amount")); return; }
		const int32 Index=(Stage-110)/10; TestSupplyISIDs[Index]=NewID; KnownInstances.Add(NewID);
		if (auto* Prop=Manager->GetActiveActorFromISID(NewID)) Prop->PropMesh->SetSimulatePhysics(false);
		const auto* Input=Index==0 ? Bench->BottleInput.Get() : Index==1 ? Bench->CapInput.Get() : Bench->CylinderInput.Get();
		const auto* Definition=Manager->FindProductDefinition(Manager->FindISIDData(NewID)->ProductID);
		const FVector Scale=Manager->GetTransform(NewID).GetScale3D();
		const FVector Offset=Index==1 && Definition && Definition->StaticMesh ? Definition->StaticMesh->GetBoundingBox().GetCenter()*Scale : FVector::ZeroVector;
		Manager->InitializeInstanceTransform(NewID,FTransform(FRotator::ZeroRotator,Input->GetComponentLocation()-Offset,Scale));
		if (Index==2)
		{
			PlaceTestPawn(Pawn,Bench->GetOwner()->GetActorLocation()+FVector(0,-180,100));
			if (auto* Machine=Cast<AManualColaFillingMachine>(Bench->GetOwner()))
			{
				auto* Container=Cast<AColaSupplyContainer>(Manager->GetActiveActorFromISID(NewID));
				if (!Container || !Machine->AttachSupplyNozzle(FHitResult(Machine,Machine->GetHoseSocket(0)->GetTraceVolume(),Machine->GetHosePortTransform(0).GetLocation(),FVector::UpVector),Container->SupplyNozzle,NewID,Pawn))
				{ Fail(TEXT("Physical supply hose failed to attach")); return; }
			}
		}
		Stage+=10; ForceNetUpdate(); return;
	}
	if (TestSupplyISIDs.Num()!=3) return;
	const auto* Bottle=Manager->FindUniqueData(TestSupplyISIDs[0]); const auto* Cylinder=Manager->FindUniqueData(TestSupplyISIDs[2]);
	if (!Bottle || !Cylinder) { Fail(TEXT("Purchased production inputs lost their identities")); return; }
	if (Stage>=140 && Stage<=160) if (auto* Active=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(TestSupplyISIDs[0]))) Active->bDebugBottleImpacts=true;
	auto TraceBottle=[&](const TCHAR* Operation)
	{
		const auto* Data=Manager->FindUniqueData(TestSupplyISIDs[0]);
		const auto* Active=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(TestSupplyISIDs[0]));
		if (Data) UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_PRODUCTION_TRACE op=%s stage=%d id=%d content=%d level=%.4f sealed=%.3f strength=%.3f pressure=%.3f actorLevel=%.4f hold=%.3f efficiency=%.3f rotation=%.3f"),
			Operation,Stage,TestSupplyISIDs[0],Data->EventData.ContentID,Data->EventData.ContentLevel,Data->EventData.CapSealed,
			Data->EventData.Strength,Data->ContinuousData.SealedPressure,Active ? Active->ColaLevel : -1.f,
			Bench->View.HoldingForce,Bench->View.HoldingEfficiency,Bench->View.RotationProgress);
		if (Data && Bench->bManualLeverMode) UE_LOG(LogTemp,Display,TEXT("MANUAL_FILLING_TRACE session=%s seat=%d source=%d filling=%d aligned=%d bottle=%s holder=%s operator=%s"),*Bench->View.SessionID.ToString(),Bench->View.SeatedBottleISID,Bench->View.ConnectedSourceISID,Bench->Filler->bIsFilling,Bench->IsBottleAligned(TestSupplyISIDs[0]),*Manager->GetTransform(TestSupplyISIDs[0]).GetLocation().ToCompactString(),Bench->BottleHolder ? *Bench->BottleHolder->GetComponentLocation().ToCompactString() : TEXT("none"),Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("none"));
	};
	if (Stage>=140 && Stage<=160) TraceBottle(TEXT("production"));
	if (Stage==140 && Bottle->EventData.ContentLevel>=1 && Bench->Filler && !Bench->Filler->bIsFilling
		&& (!Bench->bManualLeverMode || (!Bench->View.bLeverHeld && Bottle->ContinuousData.SealedPressure<=Bench->SafeResidualPressure)))
	{
		const auto* BDef=Manager->FindProductDefinition(Manager->FindISIDData(TestSupplyISIDs[0])->ProductID);
		const auto* CDef=Manager->FindProductDefinition(Manager->FindISIDData(TestSupplyISIDs[2])->ProductID);
		const float Expected=1.f-Bottle->EventData.ContentLevel*BDef->ConsumableProfile.NumOfPortions/CDef->ConsumableProfile.NumOfPortions;
		if (!FMath::IsNearlyEqual(Cylinder->EventData.ContentLevel,Expected,1.e-5f)) { Fail(TEXT("Normal fill did not conserve cylinder portions")); return; }
		Stage=150; ForceNetUpdate();
	}
	else if (Stage==150 && Bottle->EventData.AttachedCapISID==TestSupplyISIDs[1]) { Stage=160; ForceNetUpdate(); }
	else if (Stage==160 && Bottle->EventData.CapSealed>=1)
	{
		TraceBottle(TEXT("before-restock"));
		UShopShelfStockComponent* Shelf=nullptr;
		FTransform Transform;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			auto* Candidate=It->FindComponentByClass<UShopShelfStockComponent>();
			if (Candidate && Candidate->StockVolume && FindSupportedShelfPlacement(Manager.Get(),Candidate,TestSupplyISIDs[0],Transform))
			{ Shelf=Candidate; break; }
		}
		if (!Shelf || !Pawn) { Fail(TEXT("No clear supported position on any configured shelf for manufactured bottle")); return; }
		const FVector Destination=Transform.GetLocation();
		PlaceTestPawn(Pawn,Destination+FVector(0,-200,0));
		if (!Manager->BeginGrab(TestSupplyISIDs[0],Pawn,Pawn->GetRootComponent())) { Fail(TEXT("Finished bottle could not be carried to shelf")); return; }
		TraceBottle(TEXT("after-grab"));
		Manager->UpdateHeldTransform(TestSupplyISIDs[0],Transform); Manager->ReleaseGrab(TestSupplyISIDs[0],Pawn);
		TraceBottle(TEXT("after-release"));
		if (!Manager->ConvertASMToISM(TestSupplyISIDs[0],EPropState::AtRest,true,true)) { Fail(TEXT("Manufactured cap assembly did not settle")); return; }
		TraceBottle(TEXT("after-demotion"));
		Shelf->RefreshStock();
		if (!Manager->ShopCheckout->IsAvailableColaStockISID(TestSupplyISIDs[0])) { Fail(TEXT("Remote manufactured bottle did not become customer stock")); return; }
		PlaceTestPawn(Pawn,Bench->GetOwner()->GetActorLocation()+FVector(0,-180,100));
		Stage=200; ForceNetUpdate();
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_REMOTE_REINVESTMENT_PASSED bank=%lld bottle=%d cap=%d cylinder=%d"),ExpectedBankCents,TestSupplyISIDs[0],TestSupplyISIDs[1],TestSupplyISIDs[2]);
	}
}
