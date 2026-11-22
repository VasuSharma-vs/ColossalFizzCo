#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/BoxComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Engine/StaticMesh.h"
#include "WorldInteractionCarry.h"

namespace ManualEquipment { FPropEventSyncDelta State(const FPropEventUpdateData& Event,int32 Count); }

bool UShopProductionStationComponent::IsTraySupply(int32 ID, bool bSeal, const AActor* Carrier) const
{
	const auto* Manager = APropManager::Get(this);
	const auto* Machine = Cast<AActivePropActor>(GetOwner());
	const auto* Record = Manager ? Manager->FindISIDData(ID) : nullptr;
	const auto* Item = Manager ? Manager->FindUniqueData(ID) : nullptr;
	const auto* Product = Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	if (!Machine || !Item || !Product || Record->Attachment.OwnerISID || Item->EventData.AttachedBottleISID || Item->EventData.Strength <= 0) return false;
	if (Record->State == EPropState::Held && (!Carrier || Record->RuntimeData.InteractionOwner.Get() != Carrier)) return false;
	if (Item->EventData.StoredInMachineISID && Item->EventData.StoredInMachineISID != Machine->ISID) return false;

	FShopStockEntry Stock;
	if (Manager->ShopCheckout && Manager->ShopCheckout->GetStockEntry(ID,Stock) && !Manager->ShopCheckout->IsAvailableShopStock(ID)) return false;
	if (bSeal)
		return Manager->ShopSupplies && Manager->ShopSupplies->IsTemporarySealProduct(Record->ProductID) && !Item->EventData.bTemporarySealUsed;
	return Product->bIsBottleCap;
}

bool UShopProductionStationComponent::SupportsIncomingCap(int32 ID) const
{
	const auto* Layout = Cast<ASocketColaFillingMachine>(GetOwner());
	if (!Layout) return true;
	const auto* Manager = APropManager::Get(this);
	const auto* Cap = Manager ? Manager->FindISIDData(ID) : nullptr;
	const auto* CapDefinition = Cap ? Manager->FindProductDefinition(Cap->ProductID) : nullptr;
	if (!CapDefinition || !Layout->SupportsCapType(CapDefinition->DefaultCapType)) return false;
	const auto* Bottle = Manager->FindISIDData(View.SeatedBottleISID);
	const auto* BottleData = Manager->FindUniqueData(View.SeatedBottleISID);
	return !Bottle || !BottleData || Manager->IsAssemblyCompatible(Bottle->ProductID, BottleData->EventData.ContentID, Cap->ProductID);
}

bool UShopProductionStationComponent::CanLoadCap(int32 ID, const AActor* Carrier) const
{
	const auto* Machine = Cast<AActivePropActor>(GetOwner());
	const auto* Manager = APropManager::Get(this);
	const auto* Equipment = Machine && Manager ? Manager->FindUniqueData(Machine->ISID) : nullptr;
	return Machine && Machine->HasAuthority() && !Machine->bIsHeld && CapInput && Equipment
		&& Equipment->EventData.LoadedCapISIDs.Num() < FMath::Clamp(CapTrayCapacity,1,64)
		&& !Equipment->EventData.LoadedCapISIDs.Contains(ID) && IsTraySupply(ID,false,Carrier) && SupportsIncomingCap(ID);
}

bool UShopProductionStationComponent::CanLoadSeal(int32 ID, const AActor* Carrier) const
{
	const auto* Machine = Cast<AActivePropActor>(GetOwner());
	const auto* Manager = APropManager::Get(this);
	const auto* Equipment = Machine && Manager ? Manager->FindUniqueData(Machine->ISID) : nullptr;
	return Machine && Machine->HasAuthority() && !Machine->bIsHeld && SealInput && Equipment
		&& Equipment->EventData.LoadedSealISIDs.Num() < FMath::Clamp(SealTrayCapacity,1,64)
		&& !Equipment->EventData.LoadedSealISIDs.Contains(ID) && IsTraySupply(ID,true,Carrier);
}

void UShopProductionStationComponent::ReleaseStoredSupply(int32 ID,bool bWakePhysics)
{
	auto* Manager=APropManager::Get(this); auto* Machine=Cast<AActivePropActor>(GetOwner());
	const auto* Equipment=Manager && Machine ? Manager->FindUniqueData(Machine->ISID) : nullptr;
	const auto* Item=Manager ? Manager->FindUniqueData(ID) : nullptr;
	if (!Equipment || !Item || Item->EventData.StoredInMachineISID!=Machine->ISID) return;
	auto MachineDelta=ManualEquipment::State(Equipment->EventData,GetSupportedContentCount()); MachineDelta.ISID=Machine->ISID;
	MachineDelta.LoadedCapISIDs.Remove(ID); MachineDelta.LoadedSealISIDs.Remove(ID);
	auto ItemDelta=ManualEquipment::State(Item->EventData,1); ItemDelta.ISID=ID; ItemDelta.StoredInMachineISID=0;
	if (!Manager->SubmitAtomicInstanceEventUpdates({MachineDelta,ItemDelta})) return;
	if (auto* Active=Manager->GetActiveActorFromISID(ID))
	{
		Active->SetSeatedInFiller(false);
		if (Active->GetAttachParentActor()==Machine) Active->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (bWakePhysics && !Active->bIsHeld)
		{
			if (Active->PhysicsSimComponent) Active->PhysicsSimComponent->SetStateUnsettled();
			else if (Active->PropMesh) Active->PropMesh->SetSimulatePhysics(true);
		}
	}
}

void UShopProductionStationComponent::RefreshSupplyTrays(int32 PreferredCapISID,int32 PreferredSealISID)
{
	auto* Manager=APropManager::Get(this); auto* Machine=Cast<AActivePropActor>(GetOwner());
	if (!Manager || !Machine || !Machine->HasAuthority() || Machine->ISID<=0 || Machine->bIsHeld) return;
	const auto* Equipment=Manager->FindUniqueData(Machine->ISID); if (!Equipment) return;
	// Convert previously purchased abstract stock into physical deliveries once.
	// Keep any undelivered balance if creation fails; tray limits still apply.
	if (Equipment->EventData.TemporarySealCharges>0 && SupplyOutput && Manager->ShopSupplies)
	{
		const int32 Quantity=FMath::Min(8,Equipment->EventData.TemporarySealCharges);
		const auto* Catalog=Manager->ShopSupplies->Catalog.LoadSynchronous(); const auto* Offer=Catalog ? Catalog->FindOffer(TEXT("TemporarySeal")) : nullptr;
		auto* Product=Offer ? Offer->Product.LoadSynchronous() : nullptr;
		if (Product)
		{
			const int32 PID=Manager->RegisterProductFromDataAsset(Product); TArray<int32> Delivered;
			for (int32 I=0; I<Quantity; ++I)
			{
				const int32 ID=Manager->GenerateProductInstance(PID,FTransform(SupplyOutput->GetComponentQuat(),SupplyOutput->GetComponentLocation()+FVector(0,I*40,20)));
				if (ID<=0) break;
				AActivePropActor* Active=nullptr;
				if (!Manager->PromoteInstanceToASM(ID,EPropActivationReason::Drop,true,Active)) { Manager->ReleaseInstanceToEmpty(ID); break; }
				Delivered.Add(ID);
			}
			Equipment=Manager->FindUniqueData(Machine->ISID); if (!Equipment) return;
			auto Legacy=ManualEquipment::State(Equipment->EventData,GetSupportedContentCount()); Legacy.TemporarySealCharges-=Delivered.Num();
			if (!Manager->SubmitInstanceEventUpdate(Machine->ISID,Legacy)) for (int32 ID:Delivered) Manager->ReleaseInstanceToEmpty(ID);
		}
		Equipment=Manager->FindUniqueData(Machine->ISID); if (!Equipment) return;
	}
	auto Update=ManualEquipment::State(Equipment->EventData,GetSupportedContentCount()); Update.ISID=Machine->ISID;
	TArray<FPropEventSyncDelta> ItemUpdates;
	TArray<int32> ToRelease,Rejected;
	auto* SocketMachine = Cast<ASocketColaFillingMachine>(Machine);
	auto Tray=[&](UBoxComponent* Volume,bool Seal,int32 Limit,TArray<int32>& Loaded,int32 PreferredISID)
	{
		Limit=FMath::Clamp(Limit,1,64);
		const auto Previous=Loaded; Loaded.Reset();
		for (int32 ID:Previous)
		{
			if (Volume && IsTraySupply(ID,Seal) && Loaded.Num()<Limit) Loaded.AddUnique(ID);
			else ToRelease.AddUnique(ID);
		}
		if (!Volume) return;
		auto Candidates=ShopInstanceVolumeQuery::Query(Volume).Array(); Candidates.Sort();
		if (PreferredISID > 0)
		{
			Candidates.Remove(PreferredISID);
			Candidates.Insert(PreferredISID,0);
		}
		for (int32 ID:Candidates)
		{
			if (Loaded.Contains(ID) || !IsTraySupply(ID,Seal) || (!Seal && !SupportsIncomingCap(ID))) continue;
			const int32 Pending = SocketMachine ? SocketMachine->GetPendingInputCount(Volume) : 0;
			if (Loaded.Num() + Pending >= Limit) { Rejected.AddUnique(ID); continue; }
			if (SocketMachine && ID != PreferredISID)
			{
				SocketMachine->BeginInputPlacement(*Manager, ID, *Volume);
				continue;
			}
			if (ItemUpdates.Num()>=63) break; // Leave room for the machine record in the atomic batch.
			AActivePropActor* Active=nullptr;
			if (!Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active) || !Active) continue;
			const auto* Item=Manager->FindUniqueData(ID); if (!Item) continue;
			auto ItemDelta=ManualEquipment::State(Item->EventData,1); ItemDelta.ISID=ID; ItemDelta.StoredInMachineISID=Machine->ISID;
			ItemUpdates.Add(ItemDelta); Loaded.Add(ID);
		}
	};
	Tray(CapInput,false,CapTrayCapacity,Update.LoadedCapISIDs,PreferredCapISID);
	Tray(SealInput,true,SealTrayCapacity,Update.LoadedSealISIDs,PreferredSealISID);
	Equipment=Manager->FindUniqueData(Machine->ISID); if (!Equipment) return;
	const bool Changed=Update.LoadedCapISIDs!=Equipment->EventData.LoadedCapISIDs || Update.LoadedSealISIDs!=Equipment->EventData.LoadedSealISIDs;
	if (Changed)
	{
		ItemUpdates.Add(Update);
		if (!Manager->SubmitAtomicInstanceEventUpdates(ItemUpdates)) return;
	}
	for (int32 ID:ToRelease)
	{
		const auto* Item=Manager->FindUniqueData(ID); if (!Item || Item->EventData.StoredInMachineISID!=Machine->ISID) continue;
		auto D=ManualEquipment::State(Item->EventData,1); D.ISID=ID; D.StoredInMachineISID=0; Manager->SubmitInstanceEventUpdate(ID,D);
		if (auto* Active=Manager->GetActiveActorFromISID(ID))
		{
			Active->SetSeatedInFiller(false);
			if (Active->GetAttachParentActor()==Machine) { Active->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); Rejected.AddUnique(ID); }
		}
	}
	PlaceTraySupplies(*Manager,Update.LoadedCapISIDs,Update.LoadedSealISIDs);
	const bool bSocketMachine = SocketMachine != nullptr;
	for (int32 I=0;I<Rejected.Num();++I)
	{
		const int32 ID=Rejected[I]; auto* Active=Manager->GetActiveActorFromISID(ID);
		if (!Active) Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active);
		const auto* Record=Manager->FindISIDData(ID);
		if (!Active || !Record || !Active->PropMesh || !Active->PropMesh->GetStaticMesh()) continue;
		UBoxComponent* Volume=Manager->ShopSupplies && Manager->ShopSupplies->IsTemporarySealProduct(Record->ProductID) ? SealInput : CapInput;
		if (!Volume) continue;
		const FBox Bounds = Active->PropMesh->GetStaticMesh()->GetBoundingBox();
		const float Radius=Bounds.GetExtent().GetMax()*Active->GetActorScale3D().GetAbsMax();
		const bool bMagazine = bSocketMachine && Volume == CapInput;
		const FVector Exit = bMagazine ? Volume->GetRightVector() : Volume->GetForwardVector();
		const double Extent = bMagazine ? Volume->GetScaledBoxExtent().Y : Volume->GetScaledBoxExtent().X;
		const FVector Center = Active->GetActorQuat().RotateVector(Bounds.GetCenter()*Active->GetActorScale3D());
		const FVector Location = Volume->GetComponentLocation()+Exit*(Extent+Radius+15+I*10)-Center;
		Manager->InitializeInstanceTransform(ID,FTransform(Active->GetActorQuat(),Location,Active->GetActorScale3D()));
		Active->SetSeatedInFiller(false); if (Active->PhysicsSimComponent) Active->PhysicsSimComponent->SetStateUnsettled();
	}
	View.CapTrayCount=Update.LoadedCapISIDs.Num(); View.SealTrayCount=Update.LoadedSealISIDs.Num(); View.TemporarySealCharges=View.SealTrayCount;
	UpdateSupplyTrayCollision();
}

void UShopProductionStationComponent::RefreshClientSupplyPlacement(APropManager& Manager)
{
	if (!bManualLeverMode || GetNetMode()!=NM_Client || (!View.CapTrayCount && !View.SealTrayCount)) return;
	const auto* Machine=Cast<AActivePropActor>(GetOwner());
	const auto* Equipment=Machine ? Manager.FindUniqueData(Machine->ISID) : nullptr;
	if (Equipment) PlaceTraySupplies(Manager,Equipment->EventData.LoadedCapISIDs,Equipment->EventData.LoadedSealISIDs);
}

FTransform UShopProductionStationComponent::GetTraySupplyPose(const AActivePropActor& Item, const UBoxComponent& Volume, int32 Index) const
{
	const FVector Scale = Item.GetActorScale3D();
	if (const auto* Machine = Cast<ASocketColaFillingMachine>(GetOwner()); Machine && &Volume == CapInput && Machine->CapSocket)
	{
		const auto& Socket = Machine->CapSocket->GetComponentTransform();
		return FTransform(Socket.GetRotation(), Socket.TransformPosition(FVector(0, 0, Index * Machine->CapStackSpacing)), Scale);
	}

	const bool bSealTray = &Volume == SealInput;
	const int32 Limit = FMath::Clamp(bSealTray ? SealTrayCapacity : CapTrayCapacity, 1, 64);
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(float(Limit)));
	const int32 Rows = FMath::DivideAndRoundUp(Limit, Columns);
	const FVector Extent = Volume.GetUnscaledBoxExtent();
	const FTransform& Tray = Volume.GetComponentTransform();
	const FBox Bounds = Item.PropMesh->GetStaticMesh()->GetBoundingBox();
	FVector Slot((float(Index % Columns) + .5f) / Columns * Extent.X * 2.f - Extent.X,
		(float(Index / Columns) + .5f) / Rows * Extent.Y * 2.f - Extent.Y, 0);
	if (bSealTray)
	{
		double Floor = -Extent.Z;
		const auto* Platform = Cast<UStaticMeshComponent>(GetOwner()->GetDefaultSubobjectByName(TEXT("SealTrayPlatform")));
		if (Platform && Platform->GetAttachParent() == &Volume && Platform->GetStaticMesh())
			Floor = FMath::Max(Floor, Platform->GetStaticMesh()->GetBoundingBox().TransformBy(Platform->GetRelativeTransform()).Max.Z);
		Slot.Z = Floor + Bounds.GetExtent().Z * FMath::Abs(Scale.Z) / FMath::Max(UE_SMALL_NUMBER, FMath::Abs(Tray.GetScale3D().Z));
	}
	return FTransform(Tray.GetRotation(), Tray.TransformPosition(Slot) - Tray.GetRotation().RotateVector(Bounds.GetCenter() * Scale), Scale);
}

void UShopProductionStationComponent::PlaceTraySupplies(APropManager& Manager,const TArray<int32>& Caps,const TArray<int32>& Seals)
{
	auto* Machine=CastChecked<AActivePropActor>(GetOwner());
	const auto* SocketMachine=Cast<ASocketColaFillingMachine>(Machine);
	const bool bClient=GetNetMode()==NM_Client;
	auto Place=[&](UBoxComponent* Volume,const TArray<int32>& IDs,USceneComponent* StackSocket=nullptr)
	{
		if (!Volume || IDs.IsEmpty()) return;
		USceneComponent* Mount = StackSocket ? StackSocket : Volume;
		for (int32 I=0;I<IDs.Num();++I)
		{
			const int32 ID=IDs[I];
			auto* Active=Manager.GetActiveActorFromISID(ID);
			if (!Active && !bClient) Manager.PromoteInstanceToASM(ID,EPropActivationReason::LoadRestore,false,Active);
			if (!IsValid(Active) || !Active->bIsActive || Active->ISID!=ID || !Active->PropMesh || !Active->PropMesh->GetStaticMesh()) continue;
			if (bClient)
			{
				const auto* Item=Manager.FindUniqueData(ID);
				if (!Item || Item->EventData.StoredInMachineISID!=Machine->ISID) continue;
				if (Active->bIsHeld) Active->DetachForInteraction();
				else if (const auto* Parent=Active->GetAttachParentActor(); Parent && Parent!=Machine)
					Active->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
				if (Active->PropMesh->IsSimulatingPhysics()) Active->PropMesh->SetSimulatePhysics(false);
				if (Active->PropMesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)
					Active->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				WorldInteractionCarry::ClearLocalMountedItem(*Active);
			}
			const FTransform Pose = GetTraySupplyPose(*Active, *Volume, I);
			if (!bClient) Active->SetSeatedInFiller(true);
			if (!Active->GetActorLocation().Equals(Pose.GetLocation(), .05f) || !Active->GetActorQuat().Equals(Pose.GetRotation(), .001f))
			{
				if (bClient) Active->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);
				else Manager.InitializeInstanceTransform(ID,Pose);
			}
			if (!bClient && Active->GetRootComponent()->GetAttachParent()!=Mount)
				Active->AttachToComponent(Mount,FAttachmentTransformRules::KeepWorldTransform);
		}
	};
	Place(CapInput,Caps,SocketMachine ? SocketMachine->CapSocket.Get() : nullptr);
	Place(SealInput,Seals);
}

void UShopProductionStationComponent::OnRep_View()
{
	UpdateBottleInputTrace();
	UpdateSupplyTrayCollision();
}

void UShopProductionStationComponent::UpdateSupplyTrayCollision()
{
	if (!bManualLeverMode) return;
	auto SetFullCollision=[](UBoxComponent* Volume,bool Full,bool bTraceable)
	{
		if (!Volume) return;
		Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
		if (bTraceable) Volume->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
		if (Full)
		{
			Volume->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Block);
			Volume->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
		}
		Volume->SetCollisionEnabled(Full ? ECollisionEnabled::QueryAndPhysics : (bTraceable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision));
	};
	const bool bTraceable=GetOwner()->IsA<ASocketColaFillingMachine>();
	SetFullCollision(CapInput,View.CapTrayCount>=FMath::Clamp(CapTrayCapacity,1,64),bTraceable);
	SetFullCollision(SealInput,View.SealTrayCount>=FMath::Clamp(SealTrayCapacity,1,64),bTraceable);
}
