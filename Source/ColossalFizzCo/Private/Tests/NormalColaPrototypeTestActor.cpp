#include "Tests/NormalColaPrototypeTestActor.h"
#include "Shop/ShopCustomerFlow.h"
#include "Shop/ShopCustomerVisitComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/ColaPropActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "CollisionShape.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "Engine/NetDriver.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Components/RocketProjectileComponent.h"

ANormalColaPrototypeTestActor::ANormalColaPrototypeTestActor()
{
	bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.2f;
}
void ANormalColaPrototypeTestActor::PlaceTestPawn(APawn* Pawn,const FVector& Location)
{
	if (!IsValid(Pawn)) return;
	if (auto* Character=Cast<ACharacter>(Pawn)) Character->GetCharacterMovement()->StopMovementImmediately();
	// TeleportTo notifies character movement and its network prediction/floor cache.
	Pawn->TeleportTo(Location,Pawn->GetActorRotation(),false,true);
	Pawn->ForceNetUpdate();
}
bool ANormalColaPrototypeTestActor::PlaceCashierAtControls(APawn* Pawn)
{
	const auto* Character=Cast<ACharacter>(Pawn);
	if (!Character || !Scanner.IsValid()) return false;
	const auto* Capsule=Character->GetCapsuleComponent();
	TArray<USceneComponent*> Points; Scanner->GetOwner()->GetComponents(Points);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeCashierSupport),false,Pawn);
	for (auto* Point:Points) if (Point->ComponentHasTag(TEXT("ShopInteractionPoint")))
		for (const FVector2D Offset:{FVector2D(-100,0),FVector2D(100,0),FVector2D(0,-100),FVector2D(0,100),FVector2D::ZeroVector})
		{
			const FVector Target=Point->GetComponentLocation()+FVector(Offset,0);
			FHitResult Surface;
			if (!GetWorld()->LineTraceSingleByChannel(Surface,Target+FVector(0,0,250),Target-FVector(0,0,600),ECC_Visibility,Query)
				|| Surface.ImpactNormal.Z<.7 || Cast<AActivePropActor>(Surface.GetActor())
				|| (Surface.GetComponent() && Surface.GetComponent()->IsSimulatingPhysics())) continue;
			const FVector Location(Target.X,Target.Y,Surface.ImpactPoint.Z+Capsule->GetScaledCapsuleHalfHeight()+2);
			if (ShopInteractionGeometry::DistanceToStation(Scanner->GetOwner(),Location)>300
				|| GetWorld()->OverlapBlockingTestByChannel(Location,Capsule->GetComponentQuat(),ECC_Pawn,
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-.5f,Capsule->GetScaledCapsuleHalfHeight()-.5f),Query)) continue;
			PlaceTestPawn(Pawn,Location);
			return true;
		}
	return false;
}
void ANormalColaPrototypeTestActor::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bCommandLinePressureTest=FParse::Param(FCommandLine::Get(),TEXT("NormalColaPressureTest"));
	if (FParse::Param(FCommandLine::Get(),TEXT("ReloadBottlePressureTest"))) PrimaryActorTick.TickInterval=.02f;
	bManufacturingOnly=FParse::Param(FCommandLine::Get(),TEXT("ManualColaManufacturingTest"));
	int32 ExitAfter=0;
	if (FParse::Value(FCommandLine::Get(),TEXT("NormalColaTestExitAfter="),ExitAfter))
	{
		// A world timer disappears if a client loses its server and browses to another map.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { FPlatformMisc::RequestExit(false); return false; }),float(FMath::Clamp(ExitAfter,30,600)));
	}
	if (bManufacturingOnly || bCommandLinePressureTest || FParse::Param(FCommandLine::Get(),TEXT("NormalColaReinvestmentTest")))
	{
		const auto* Shell=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Assest/meshs/PlasticBottle/SM_PlasticBottle.SM_PlasticBottle"));
		FSoftObjectPath LegacyPath(TEXT("/Game/Assest/meshs/Cola1.Cola1"));
		LegacyPath.FixupCoreRedirects();
		if (!Shell || LegacyPath.TryLoad()!=Shell)
		{
			Fail(TEXT("Legacy bottle mesh path does not resolve to the supplied plastic shell"));
			return;
		}
#if !WITH_EDITOR
		// Exercise the actual GameplayStatics save reader, not just soft loading.
		// The tiny fixture object has no mesh geometry and never enters gameplay.
		auto* LegacyMesh=NewObject<UStaticMesh>(CreatePackage(TEXT("/Game/Assest/meshs/Cola1")),TEXT("Cola1"),RF_Public);
		auto* Save=NewObject<UPropDatabaseSaveGame>();
		FPropProductDefinition LegacyDef; LegacyDef.ProductID=734; LegacyDef.StaticMesh=LegacyMesh;
		Save->WorldSaveData.ProductDefinitions.Add(LegacyDef);
		TArray<uint8> Bytes;
		const bool bWritten=UGameplayStatics::SaveGameToMemory(Save,Bytes);
		LegacyMesh->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
		const auto* Restored=bWritten ? UPropDatabaseSaveGame::LoadCompatibleMemory(Bytes) : nullptr;
		if (!Restored || Restored->WorldSaveData.ProductDefinitions.Num()!=1 || Restored->WorldSaveData.ProductDefinitions[0].StaticMesh!=Shell)
		{
			Fail(TEXT("Legacy saved mesh reference did not resolve through the cooked save reader"));
			return;
		}
		UE_LOG(LogTemp,Display,TEXT("COLA1_COOKED_SAVE_READER_PASSED authority=%d"),HasAuthority());
#endif
		UE_LOG(LogTemp,Display,TEXT("COLA1_COOKED_ALIAS_PASSED authority=%d"),HasAuthority());
	}
	if (HasAuthority() && (bManufacturingOnly || bCommandLinePressureTest || FParse::Param(FCommandLine::Get(),TEXT("NormalColaReinvestmentTest")))) Command=2;
	UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_TEST_BOOT authority=%d command=%d world=%s"),HasAuthority(),Command,*GetWorld()->GetName());
#if DO_ENABLE_NET_TEST
	if (const auto* Driver=GetWorld()->GetNetDriver())
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_NET_PROFILE authority=%d lag_ms=%d loss_pct=%d"),HasAuthority(),Driver->PacketSimulationSettings.PktLag,Driver->PacketSimulationSettings.PktLoss);
#endif
#endif
}
void ANormalColaPrototypeTestActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,Stage); DOREPLIFETIME(ANormalColaPrototypeTestActor,SalesCompleted);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,ExpectedCashCents); DOREPLIFETIME(ANormalColaPrototypeTestActor,LastFailure);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,TestSession); DOREPLIFETIME(ANormalColaPrototypeTestActor,TestCustomerID); DOREPLIFETIME(ANormalColaPrototypeTestActor,TestItems);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,TestSupplyISIDs); DOREPLIFETIME(ANormalColaPrototypeTestActor,ExpectedBankCents);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,PressureLaunchOrigin);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,bManualCappingInput);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,bManufacturingOnly);
}
void ANormalColaPrototypeTestActor::Fail(const FString& Message)
{
	LastFailure=Message; Stage=-1; ForceNetUpdate(); UE_LOG(LogTemp,Error,TEXT("NORMAL_COLA_PLAYTEST_FAILED: %s"),*Message);
}
void ANormalColaPrototypeTestActor::Tick(float Dt)
{
	Super::Tick(Dt);
	CaptureBottleEvidence();
	if (!Manager.IsValid()) Manager=APropManager::Get(this);
	if (!Manager.IsValid() || !Manager->SharedWallet || !Manager->ShopCheckout) return;
	if (!Scanner.IsValid()) for (TActorIterator<AShopCustomerFlow> It(GetWorld()); It; ++It) { Scanner=It->GetScanner(); break; }
	if (HasAuthority() && FParse::Param(FCommandLine::Get(),TEXT("NormalColaReinvestmentTest")))
		for (TActorIterator<AColaPropActor> It(GetWorld());It;++It) if (It->bIsActive) It->bDebugBottleImpacts=true;
	LocalBankCents=Manager->SharedWallet->GetWalletState().BalanceCents;
	LocalCashCents=Manager->ShopCheckout->TeamCashCents; LocalReceiptCount=Manager->ShopCheckout->RecentReceipts.Num();
	if (Stage!=ObservedStage) { ObservedStage=Stage; ObservedStageAt=GetWorld()->GetTimeSeconds(); }
	if (TestSupplyISIDs.Num()==3)
	{
		const auto* Bottle=Manager->FindUniqueData(TestSupplyISIDs[0]);
		const auto* Cylinder=Manager->FindUniqueData(TestSupplyISIDs[2]);
		if (Bottle && Cylinder) LocalManufacturedBottle=FString::Printf(TEXT("content=%d level=%.3f sealed=%.3f cap=%d source=%.3f"),
			Bottle->EventData.ContentID,Bottle->EventData.ContentLevel,Bottle->EventData.CapSealed,Bottle->EventData.AttachedCapISID,Cylinder->EventData.ContentLevel);
	}
	const bool PressureSnapshotReady=Stage!=400 || LocalPressureFlight.Contains(FString::Printf(TEXT("state=%d "),int32(ERocketSimulationState::AtRest)));
	if ((Stage==200 || Stage==400) && PressureSnapshotReady && LoggedStage!=Stage && GetWorld()->GetTimeSeconds()-ObservedStageAt>.75
		&& LocalBankCents==ExpectedBankCents && LocalCashCents==0 && LocalReceiptCount>=(bManufacturingOnly ? 0 : 2))
	{
		if (Stage == 200)
		{
			if (TestSupplyISIDs.Num() != 3) return;
			const int32 Root = TestSupplyISIDs[0], Cap = TestSupplyISIDs[1];
			const auto* State = Manager->FindUniqueData(Root);
			if (!State || State->EventData.CapSealed < 1 || State->EventData.AttachedCapISID != Cap || Manager->ResolveRootISID(Cap) != Root) return;
			int32 Liquid = 0;
			for (int32 Child : Manager->GetChildISIDs(Root))
			{
				const auto* R = Manager->FindISIDData(Child);
				const auto* D = R ? Manager->FindProductDefinition(R->ProductID) : nullptr;
				if (D && D->bLiquidVisualOnly && Manager->ResolveRootISID(Child) == Root) Liquid = Child;
			}
			if (!Liquid || !LocalManufacturedBottle.Contains(TEXT("level=1.000 sealed=1.000")) || !LocalManufacturedBottle.Contains(TEXT("source=0.900"))) return;
			UE_LOG(LogTemp,Display,TEXT("MODULAR_BOTTLE_PEER root=%d cap=%d liquid=%d"),Root,Cap,Liquid);
		}
		LoggedStage=Stage;
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_PEER_ACCEPTANCE authority=%d stage=%d bank=%lld cash=%lld receipts=%d bottle=[%s] pressure=[%s] peak=%.2f"),
			HasAuthority(),Stage,LocalBankCents,LocalCashCents,LocalReceiptCount,*LocalManufacturedBottle,*LocalPressureFlight,LocalPeakPressureHeight);
	}
	if (HasAuthority() && bCommandLinePressureTest && Stage==200 && LoggedStage==200 && GetWorld()->GetTimeSeconds()-ObservedStageAt>1.5) Command=3;
	LocalPresentPurchases=0; for (int32 ID:TestItems) if (Manager->FindISIDData(ID)) ++LocalPresentPurchases;
	LocalPlayerCount=GetWorld()->GetGameState() ? GetWorld()->GetGameState()->PlayerArray.Num() : 0;
	LocalVisitStates.Empty();
	if (auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		const auto& Agents=Nav->GetSupportedAgents();
		const auto* Data=Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
		LocalNavigationState=FString::Printf(TEXT("agents=%d radius=%.1f height=%.1f data=%s building=%d bounds=%d activeTiles=%d worldTime=%.1f"),Agents.Num(),
			Agents.IsEmpty() ? -1.f : Agents[0].AgentRadius,Agents.IsEmpty() ? -1.f : Agents[0].AgentHeight,
			*GetNameSafe(Data),UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()),Nav->GetNavigationBounds().Num(),Data ? Data->GetNumActiveTiles() : 0,GetWorld()->GetTimeSeconds());
		if (HasAuthority() && Command==4 && LocalNavigationProbe.IsEmpty())
		{
			LocalNavigationProbe=FString::Printf(TEXT("mode=%d initialized=%d gameWorld=%d; "),int32(Nav->GetRunMode()),Nav->IsInitialized(),GetWorld()->IsGameWorld());
			for (const auto& Bounds:Nav->GetNavigationBounds()) LocalNavigationProbe+=TEXT("bounds=")+Bounds.AreaBox.ToString()+TEXT("; ");
			FHitResult Floor; const FVector Entrance(0,-2700,2);
			GetWorld()->LineTraceSingleByChannel(Floor,Entrance+FVector(0,0,1400),Entrance-FVector(0,0,200),ECC_WorldStatic);
			LocalNavigationProbe+=FString::Printf(TEXT("entranceFloor=%s component=%s point=%s; grid="),*GetNameSafe(Floor.GetActor()),*GetNameSafe(Floor.GetComponent()),*Floor.ImpactPoint.ToCompactString());
			for (int32 X=-3000;X<=3000;X+=500) for (int32 Y=-3500;Y<=2000;Y+=500)
			{
				FNavLocation Point;
				if (Nav->ProjectPointToNavigation(FVector(X,Y,2),Point,FVector(50,50,150)))
					LocalNavigationProbe+=Point.Location.ToCompactString()+TEXT(" ");
			}
			UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_NAVIGATION_PROBE %s"),*LocalNavigationProbe);
		}
	}
	else LocalNavigationState=TEXT("No local navigation system");
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It) if (const auto* Visit=It->FindComponentByClass<UShopCustomerVisitComponent>())
		LocalVisitStates.Add(GetNameSafe(*It)+TEXT(": ")+Visit->Status+TEXT(" ")+Visit->DescribeRoute());
	if (HasAuthority() && Command && GetWorld()->GetTimeSeconds()>=NextProgressLogAt)
	{
		NextProgressLogAt=GetWorld()->GetTimeSeconds()+10;
		FString Arrival;
		for (TActorIterator<AShopCustomerFlow> It(GetWorld());It;++It) { Arrival=It->ArrivalStatus; break; }
		int32 Bottles=0,Sealed=0,Registered=0;
		for (const auto& Pair:Manager->ISIDData)
		{
			const auto* Data=Manager->FindUniqueData(Pair.Key);
			if (!Data || !Manager->ShopSupplies || !Manager->ShopSupplies->IsNormalContent(Data->EventData.ContentID)) continue;
			++Bottles; Sealed+=Data->EventData.CapSealed>=1;
			FShopStockEntry Entry; Registered+=Manager->ShopCheckout->GetStockEntry(Pair.Key,Entry);
		}
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_TEST_PROGRESS stage=%d players=%d bank=%lld cash=%lld receipts=%d bottles=%d sealed=%d registered=%d available=%d arrival=[%s] visits=[%s] navigation=[%s]"),
			Stage,LocalPlayerCount,LocalBankCents,LocalCashCents,LocalReceiptCount,Bottles,Sealed,Registered,Manager->ShopCheckout->GetAvailableColaStockISIDs().Num(),
			*Arrival,*FString::Join(LocalVisitStates,TEXT(" | ")),*LocalNavigationState);
	}
	if (HasAuthority() && bManufacturingOnly && Stage==0)
	{
		// Explicit validation-map mode isolates manufacturing from random NPC routes.
		// It never runs in the player-facing map, which contains no test driver.
		for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
			if (auto* Remote=It->Get(); Remote && !Remote->IsLocalController() && Remote->GetPawn())
			{
				ExpectedCashCents=10000; Manager->SharedWallet->Credit(FGuid::NewGuid(),ExpectedCashCents,TEXT("Isolated manufacturing validation funds"));
				SetOwner(Remote); Stage=100; ForceNetUpdate(); break;
			}
		if (Stage==0) return; // Wait for a real remote pawn before choosing a test path.
	}
	if (!HasAuthority() && Scanner.IsValid() && GetWorld()->GetTimeSeconds()>=NextProgressLogAt
		&& FParse::Param(FCommandLine::Get(),TEXT("NormalColaReinvestmentTest")))
	{
		NextProgressLogAt=GetWorld()->GetTimeSeconds()+10;
		const auto* PC=Cast<APlayerController>(GetOwner());
		const auto View=Scanner->GetScannerState();
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_CLIENT_PROGRESS stage=%d owner=%s local=%d pawn=%s phase=%d session=%s expected=%s sent=%d requests=%d paused=%d"),
			Stage,*GetNameSafe(PC),PC && PC->IsLocalController(),*GetNameSafe(PC ? PC->GetPawn().Get() : nullptr),
			int32(View.Phase),*View.SessionID.ToString(),*TestSession.ToString(),LastSentStage,LocalRequestsSent,GetWorld()->IsPaused());
	}
	if (!Scanner.IsValid() || Stage<0) return;
	if (Stage>=300 || (Stage==200 && Command==3)) { TickPressureFlight(); return; }
	if (Stage>=100) { TickReinvestment(); return; }
	if (!HasAuthority())
	{
		const auto* PC=Cast<APlayerController>(GetOwner()); APawn* Pawn=PC ? PC->GetPawn().Get() : nullptr;
		auto* Actions=Pawn ? Pawn->FindComponentByClass<UShopPlayerInteractionComponent>() : nullptr;
		const auto View=Scanner->GetScannerState();
		if (!PC || !PC->IsLocalController() || !Actions || LastSentStage==Stage || View.SessionID!=TestSession) return;
		if ((Stage==10 && View.Phase==EShopScannerPhase::Scanning) || (Stage==20 && View.Phase==EShopScannerPhase::Checkout))
		{
			Actions->RequestScannerAction(Scanner.Get(),TestSession,Stage==10 ? EShopScannerAction::LockBill : EShopScannerAction::Complete,0,FGuid());
			LastSentStage=Stage; ++LocalRequestsSent;
		}
		return;
	}
	if (Stage==0 && (Command==1 || Command==2)) { Stage=1; StartedAt=GetWorld()->GetTimeSeconds(); }
	int32 VisitDeadline=180; FParse::Value(FCommandLine::Get(),TEXT("NormalColaVisitDeadline="),VisitDeadline);
	if (Stage>0 && Stage<100 && GetWorld()->GetTimeSeconds()-StartedAt>FMath::Clamp(VisitDeadline,60,600)) { Fail(TEXT("Customer/remote-checkout/departure loop exceeded configured deadline")); return; }
	if (Stage==1)
	{
		for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
			if (It->Get() && It->Get()->IsLocalController()) It->Get()->SetControlRotation(FRotator(0,90,0));
		APlayerController* Cashier=nullptr;
		for (auto It=GetWorld()->GetPlayerControllerIterator(); It; ++It) if (It->Get() && !It->Get()->IsLocalController() && It->Get()->GetPawn()) { Cashier=It->Get(); break; }
		if (!Cashier) return; // This test deliberately requires a real remote player.
		for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
		{
			const auto* Visit=It->FindComponentByClass<UShopCustomerVisitComponent>(); const auto* Cash=It->FindComponentByClass<UShopCustomerCashComponent>();
			if (!Visit || !Cash || Visit->Phase!=EShopVisitPhase::WaitingForCashier) continue;
			Customer=*It; TestCustomerID=Cash->CustomerID; TestItems=Visit->GetCollectedISIDs();
			PlaceTestPawn(Cashier->GetPawn(),Scanner->GetOwner()->GetActorLocation()+FVector(-110,-160,100));
			for (int32 I=0; I<TestItems.Num(); ++I)
			{
				PlaceTestPawn(Cashier->GetPawn(),Manager->GetTransform(TestItems[I]).GetLocation()+FVector(0,-140,100));
				if (!Manager->ValidateGrab(TestItems[I],Cashier->GetPawn())) { Fail(TEXT("Cashier could not reach a customer bottle")); return; }
				auto* Prop=Manager->GetActiveActorFromISID(TestItems[I]);
				if (!Prop) Prop=Manager->ConvertISMToASMFromISID(TestItems[I]);
				if (!Prop) { Fail(TEXT("Customer bottle could not be promoted for scanning")); return; }
				if (!Prop->AttachForInteraction(Cashier->GetPawn()->GetRootComponent())) { Fail(TEXT("Cashier could not take a customer bottle")); return; }
				const UStaticMesh* Mesh=Prop->PropMesh ? Prop->PropMesh->GetStaticMesh().Get() : nullptr;
				if (!Mesh) { Fail(TEXT("Scanned bottle has no physical mesh")); return; }
				const FVector Scale=Prop->GetActorScale3D();
				const FBox Bounds=Mesh->GetBoundingBox().TransformBy(FTransform(FQuat::Identity,FVector::ZeroVector,Scale));
				FTransform Transform=ShopInteractionGeometry::GridItemTransform(
					FTransform(Scanner->ScanVolume->GetComponentQuat(),Scanner->ScanVolume->GetComponentLocation()),
					ShopInteractionGeometry::MeshGridSpacing(Mesh,Scale),I,TestItems.Num(),Scale,2);
				FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeScanSupport),false);
				for (int32 ID:TestItems) if (auto* Active=Manager->GetActiveActorFromISID(ID)) Query.AddIgnoredActor(Active);
				for (TActorIterator<ACharacter> Character(GetWorld());Character;++Character) Query.AddIgnoredActor(*Character);
				FHitResult Surface;
				const FBox Volume=Scanner->ScanVolume->Bounds.GetBox();
				const FVector XY=Transform.GetLocation();
				if (!GetWorld()->LineTraceSingleByChannel(Surface,FVector(XY.X,XY.Y,Volume.Max.Z+10),
					FVector(XY.X,XY.Y,Volume.Min.Z-10),ECC_Visibility,Query))
				{ Fail(TEXT("Scanner has no supporting surface for a physical bottle")); return; }
				Transform.SetLocation(FVector(XY.X,XY.Y,Surface.ImpactPoint.Z-Bounds.Min.Z+2));
				PlaceTestPawn(Cashier->GetPawn(),Transform.GetLocation()+FVector(0,-160,100));
				Manager->UpdateHeldTransform(TestItems[I],Transform); Prop->DetachForInteraction();
			}
			if (!PlaceCashierAtControls(Cashier->GetPawn())) { Fail(TEXT("No supported reachable standing position at checkout controls")); return; }
			Scanner->RefreshLiveProducts();
			if (Scanner->GetScannerState().Checkout.Items.Num()!=TestItems.Num()) { Fail(TEXT("Physical scanner did not detect every customer bottle")); return; }
			TestSession=Scanner->GetScannerState().SessionID; SetOwner(Cashier); Stage=10; LastSentStage=0; ForceNetUpdate(); break;
		}
	}
	else if (Stage==10 && Scanner->GetScannerState().Phase==EShopScannerPhase::Checkout)
	{
		CheckoutID=Scanner->GetScannerState().Checkout.CheckoutID;
		if (auto* Register=Scanner->GetOwner()->FindComponentByClass<UShopCashRegisterComponent>()) Register->RefreshPayment();
		const auto Bill=Scanner->GetScannerState().Checkout;
		if (Bill.TenderCents>Bill.TotalCents && Bill.CustomerID==TestCustomerID)
		{
			auto* Register=Scanner->GetOwner()->FindComponentByClass<UShopCashRegisterComponent>();
			const int64 Dollars=(Bill.TenderCents-Bill.TotalCents)/100;
			if (Register && Register->ChangeVolume && Dollars<=100)
			{
				TArray<TArray<int32>> Plan; Plan.SetNum(int32(Dollars)+1); TArray<bool> Reachable; Reachable.Init(false,int32(Dollars)+1); Reachable[0]=true;
				for (int32 ID:UShopCashRegisterComponent::QueryCoinsInVolume(Register->TenderVolume))
				{
					FShopCashCoin Coin; if (!Manager->ShopCheckout->GetCoin(ID,Coin) || Coin.CustomerOwner.IsValid() || Coin.CheckoutID.IsValid()) continue;
					for (int32 Value=int32(Dollars); Value>=Coin.Dollars; --Value) if (!Reachable[Value] && Reachable[Value-Coin.Dollars])
					{ Reachable[Value]=true; Plan[Value]=Plan[Value-Coin.Dollars]; Plan[Value].Add(ID); }
				}
				if (Reachable.Last()) for (int32 ID:Plan.Last()) Manager->InitializeInstanceTransform(ID,FTransform(Register->ChangeVolume->GetComponentLocation()));
				Register->RefreshPayment();
			}
		}
		const auto Paid=Scanner->GetScannerState().Checkout;
		if (Paid.TenderCents>=Paid.TotalCents && Paid.ChangeCents==Paid.TenderCents-Paid.TotalCents && Paid.CustomerID==TestCustomerID) { Stage=20; ForceNetUpdate(); }
	}
	else if (Stage==20)
	{
		FCompletedCheckoutReceipt Receipt;
		if (Manager->ShopCheckout->GetCompletedReceipt(CheckoutID,Receipt))
		{
			ExpectedCashCents+=Receipt.TotalCents; ++SalesCompleted;
			if (Receipt.CustomerID!=TestCustomerID || Manager->SharedWallet->GetWalletState().BalanceCents!=0 || Manager->ShopCheckout->GetTeamCashCents()!=ExpectedCashCents)
			{ Fail(TEXT("Completed sale violated customer or cash/bank accounting")); return; }
			Stage=30; ForceNetUpdate();
		}
	}
	else if (Stage==30 && Customer.IsValid())
	{
		const auto* Cash=Customer->FindComponentByClass<UShopCustomerCashComponent>();
		if (Cash && Cash->CustomerID!=TestCustomerID)
		{
			for (int32 ID:TestItems) if (Manager->FindISIDData(ID)) { Fail(TEXT("Departed customer purchase was not retired")); return; }
			FCompletedCheckoutReceipt Receipt;
			if (!Manager->ShopCheckout->GetCompletedReceipt(CheckoutID,Receipt)) { Fail(TEXT("Departure deleted completed receipt")); return; }
			int32 RequiredSales=2; FParse::Value(FCommandLine::Get(),TEXT("NormalColaTestSales="),RequiredSales);
			Stage=SalesCompleted>=FMath::Clamp(RequiredSales,2,8) ? 100 : 1; CheckoutID.Invalidate(); ForceNetUpdate();
			if (Stage==100) UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_REMOTE_LOOP_PASSED sales=%d cash=%lld bank=0"),SalesCompleted,ExpectedCashCents);
		}
	}
}

void ANormalColaPrototypeTestActor::CaptureBottleEvidence()
{
#if !UE_BUILD_SHIPPING
	FString Directory;
	if (HasAuthority() || !FParse::Value(FCommandLine::Get(),TEXT("BottleVisualEvidence="),Directory) || !Manager.IsValid() || TestSupplyISIDs.Num()!=3) return;
	auto* PC=Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController()) return;
	const auto* Data=Manager->FindUniqueData(TestSupplyISIDs[0]);
	if (!Data) return;
	const bool Active=Stage==140 && Data->EventData.ContentLevel>=.4f && Data->EventData.ContentLevel<=.7f && !bCapturedActive;
	const bool Resting=Stage==200 && !bCapturedResting;
	if (!Active && !Resting) return;
	const FVector Target=Manager->GetTransform(TestSupplyISIDs[0]).TransformPosition(FVector(0,0,95));
	if (!EvidenceCamera.IsValid()) EvidenceCamera=GetWorld()->SpawnActor<ACameraActor>();
	if (!EvidenceCamera.IsValid()) return;
	const FVector Location=Target+FVector(500,0,100);
	const bool NeedsCameraFrame=PC->GetViewTarget()!=EvidenceCamera.Get() || !EvidenceCamera->GetActorLocation().Equals(Location,1.f);
	EvidenceCamera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());
	EvidenceCamera->GetCameraComponent()->SetFieldOfView(40);
	PC->SetViewTarget(EvidenceCamera.Get());
	if (NeedsCameraFrame) return; // Let the viewport consume the new camera pose before capture.
	const FString Name=Active ? TEXT("Client_ASM.png") : TEXT("Client_ISM.png");
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,Name),false,false);
	if (Active) bCapturedActive=true; else bCapturedResting=true;
	UE_LOG(LogTemp,Display,TEXT("BOTTLE_VISUAL_CAPTURE requested=%s fill=%.3f"),*Name,Data->EventData.ContentLevel);
#endif
}
