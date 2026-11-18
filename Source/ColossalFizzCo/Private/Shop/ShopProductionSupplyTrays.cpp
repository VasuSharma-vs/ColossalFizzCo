#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Engine/StaticMesh.h"

namespace ManualEquipment { FPropEventSyncDelta State(const FPropEventUpdateData& Event,int32 Count); }

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

void UShopProductionStationComponent::RefreshSupplyTrays()
{
	auto* Manager=APropManager::Get(this); auto* Machine=Cast<AActivePropActor>(GetOwner());
	if (!Manager || !Machine || !Machine->HasAuthority() || Machine->ISID<=0) return;
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
	auto IsKind=[&](int32 ID,bool Seal)
	{
		const auto* Record=Manager->FindISIDData(ID); const auto* Item=Manager->FindUniqueData(ID);
		const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (!Record || !Item || !Product || Record->State==EPropState::Held || Item->EventData.AttachedBottleISID || Item->EventData.Strength<=0) return false;
		if (Item->EventData.StoredInMachineISID && Item->EventData.StoredInMachineISID!=Machine->ISID) return false;
		FShopStockEntry Stock;
		if (Manager->ShopCheckout && Manager->ShopCheckout->GetStockEntry(ID,Stock) && !Manager->ShopCheckout->IsAvailableShopStock(ID)) return false;
		return Seal ? Manager->ShopSupplies && Manager->ShopSupplies->IsTemporarySealProduct(Record->ProductID) && !Item->EventData.bTemporarySealUsed : Product->bIsBottleCap;
	};
	auto Tray=[&](UBoxComponent* Volume,bool Seal,int32 Limit,TArray<int32>& Loaded)
	{
		Limit=FMath::Clamp(Limit,1,64);
		const auto Previous=Loaded; Loaded.Reset();
		for (int32 ID:Previous)
		{
			if (Volume && IsKind(ID,Seal) && Loaded.Num()<Limit) Loaded.AddUnique(ID);
			else ToRelease.AddUnique(ID);
		}
		if (!Volume) return;
		auto Candidates=ShopInstanceVolumeQuery::Query(Volume).Array(); Candidates.Sort();
		for (int32 ID:Candidates)
		{
			if (Loaded.Contains(ID) || !IsKind(ID,Seal)) continue;
			if (Loaded.Num()>=Limit) { Rejected.AddUnique(ID); continue; }
			if (ItemUpdates.Num()>=63) break; // Leave room for the machine record in the atomic batch.
			AActivePropActor* Active=nullptr;
			if (!Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active) || !Active) continue;
			const auto* Item=Manager->FindUniqueData(ID); if (!Item) continue;
			auto ItemDelta=ManualEquipment::State(Item->EventData,1); ItemDelta.ISID=ID; ItemDelta.StoredInMachineISID=Machine->ISID;
			ItemUpdates.Add(ItemDelta); Loaded.Add(ID);
		}
	};
	Tray(CapInput,false,CapTrayCapacity,Update.LoadedCapISIDs);
	Tray(SealInput,true,SealTrayCapacity,Update.LoadedSealISIDs);
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
	auto Place=[&](UBoxComponent* Volume,const TArray<int32>& IDs,int32 Limit)
	{
		if (!Volume) return;
		const int32 Columns=FMath::Max(1,FMath::CeilToInt(FMath::Sqrt(float(FMath::Clamp(Limit,1,64)))));
		const int32 Rows=FMath::DivideAndRoundUp(FMath::Clamp(Limit,1,64),Columns);
		for (int32 I=0;I<IDs.Num();++I)
		{
			auto* Active=Manager->GetActiveActorFromISID(IDs[I]);
			if (!Active) Manager->PromoteInstanceToASM(IDs[I],EPropActivationReason::LoadRestore,false,Active);
			if (!Active || !Active->PropMesh || !Active->PropMesh->GetStaticMesh()) continue;
			const FVector Extent=Volume->GetUnscaledBoxExtent();
			const FVector Slot((float(I%Columns)+.5f)/Columns*Extent.X*2.f-Extent.X,(float(I/Columns)+.5f)/Rows*Extent.Y*2.f-Extent.Y,0);
			const FVector Scale=Active->GetActorScale3D();
			const FVector Center=Active->PropMesh->GetStaticMesh()->GetBoundingBox().GetCenter()*Scale;
			const FVector Location=Volume->GetComponentTransform().TransformPosition(Slot)-Volume->GetComponentQuat().RotateVector(Center);
			Active->SetSeatedInFiller(true);
			if (!Active->GetActorLocation().Equals(Location,.05f)) Manager->InitializeInstanceTransform(IDs[I],FTransform(Volume->GetComponentQuat(),Location,Scale));
			if (Active->GetAttachParentActor()!=Machine) Active->AttachToComponent(Volume,FAttachmentTransformRules::KeepWorldTransform);
		}
	};
	Place(CapInput,Update.LoadedCapISIDs,CapTrayCapacity); Place(SealInput,Update.LoadedSealISIDs,SealTrayCapacity);
	for (int32 I=0;I<Rejected.Num();++I)
	{
		const int32 ID=Rejected[I]; auto* Active=Manager->GetActiveActorFromISID(ID);
		if (!Active) Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active);
		const auto* Record=Manager->FindISIDData(ID);
		if (!Active || !Record || !Active->PropMesh || !Active->PropMesh->GetStaticMesh()) continue;
		UBoxComponent* Volume=Manager->ShopSupplies && Manager->ShopSupplies->IsTemporarySealProduct(Record->ProductID) ? SealInput : CapInput;
		if (!Volume) continue;
		const float Radius=Active->PropMesh->GetStaticMesh()->GetBoundingBox().GetExtent().GetMax()*Active->GetActorScale3D().GetAbsMax();
		const FVector Location=Volume->GetComponentLocation()+Volume->GetForwardVector()*(Volume->GetScaledBoxExtent().X+Radius+15)+Volume->GetRightVector()*I*10;
		Manager->InitializeInstanceTransform(ID,FTransform(Active->GetActorQuat(),Location,Active->GetActorScale3D()));
		Active->SetSeatedInFiller(false); if (Active->PhysicsSimComponent) Active->PhysicsSimComponent->SetStateUnsettled();
	}
	View.CapTrayCount=Update.LoadedCapISIDs.Num(); View.SealTrayCount=Update.LoadedSealISIDs.Num(); View.TemporarySealCharges=View.SealTrayCount;
	UpdateSupplyTrayCollision();
}

void UShopProductionStationComponent::OnRep_View() { UpdateSupplyTrayCollision(); }

void UShopProductionStationComponent::UpdateSupplyTrayCollision()
{
	if (!bManualLeverMode) return;
	auto SetFullCollision=[](UBoxComponent* Volume,bool Full)
	{
		if (!Volume) return;
		Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
		if (Full)
		{
			Volume->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Block);
			Volume->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
		}
		Volume->SetCollisionEnabled(Full ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	};
	SetFullCollision(CapInput,View.CapTrayCount>=FMath::Clamp(CapTrayCapacity,1,64));
	SetFullCollision(SealInput,View.SealTrayCount>=FMath::Clamp(SealTrayCapacity,1,64));
}
