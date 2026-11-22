#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/ColaPropActor.h"
#include "Components/BoxComponent.h"
#include "Components/ASMComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RocketProjectileComponent.h"
#include "Engine/World.h"

namespace ManualEquipment
{
	bool RestoreCradlePose(AManualColaFillingMachine& Machine,APropManager& Manager,int32 BottleID)
	{
		const auto* Record=Manager.FindISIDData(BottleID);
		const auto* Product=Record ? Manager.FindProductDefinition(Record->ProductID) : nullptr;
		const auto* Bottle=Cast<AColaPropActor>(Manager.GetActiveActorFromISID(BottleID));
		if (!Machine.CradlePivot || !Machine.BottleHolderPoint || !Record || !Product || Product->ConsumableProfile.NumOfPortions<=0
			|| Record->State==EPropState::Held || Record->State==EPropState::InMotion || !Bottle || Bottle->bIsHeld || Bottle->bIsInMotion
			|| (Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight())) return false;
		const float Travel=Machine.FillingCradlePitch-Machine.RestCradlePitch;
		if (FMath::IsNearlyZero(Travel)) return false;
		const FTransform Parent=Machine.CradlePivot->GetAttachParent() ? Machine.CradlePivot->GetAttachParent()->GetComponentTransform() : Machine.GetActorTransform();
		const FTransform BottleTransform=Manager.GetTransform(BottleID);
		const FTransform HolderLocal=Machine.BottleHolderPoint->GetComponentTransform().GetRelativeTransform(Machine.CradlePivot->GetComponentTransform());
		const FQuat CradleRotation=Parent.GetRotation().Inverse()*BottleTransform.GetRotation()*HolderLocal.GetRotation().Inverse();
		const FVector CradleUp=CradleRotation.GetUpVector();
		const float Pitch=Machine.RestCradlePitch+FMath::FindDeltaAngleDegrees(Machine.RestCradlePitch,float(FMath::RadiansToDegrees(FMath::Atan2(-CradleUp.X,CradleUp.Z))));
		const float Alpha=(Pitch-Machine.RestCradlePitch)/Travel;
		if (!FMath::IsFinite(Alpha) || Alpha<-.001f || Alpha>1.001f) return false;
		FTransform PivotLocal=Machine.CradlePivot->GetRelativeTransform();
		PivotLocal.SetRotation(FRotator(FMath::Lerp(Machine.RestCradlePitch,Machine.FillingCradlePitch,FMath::Clamp(Alpha,0.f,1.f)),0,0).Quaternion());
		const FTransform ExpectedHolder=HolderLocal*PivotLocal*Parent;
		if (FVector::DistSquared(BottleTransform.GetLocation(),ExpectedHolder.GetLocation())>FMath::Square(8.f)
			|| FVector::DotProduct(BottleTransform.GetUnitAxis(EAxis::Z),ExpectedHolder.GetUnitAxis(EAxis::Z))<.99f) return false;
		// The bottle's saved world pose survives pooling. Recover only a matching cradle pose; never move a nearby loose bottle.
		Machine.CradleAlpha=FMath::Clamp(Alpha,0.f,1.f);
		Machine.CradlePivot->SetRelativeRotation(PivotLocal.GetRotation());
		Machine.ForceNetUpdate();
		return true;
	}

	FPropEventSyncDelta State(const FPropEventUpdateData& Event,int32 Count)
	{
		FPropEventSyncDelta D; D.ExtendedDirtyMask=EPropExtendedEventMask::ManualEquipment|EPropExtendedEventMask::LinkedSupply;
		D.ConnectedSupplyISIDs=Event.ConnectedSupplyISIDs;
		if (D.ConnectedSupplyISIDs.IsEmpty() && Event.LinkedSupplyISID>0) D.ConnectedSupplyISIDs.Add(Event.LinkedSupplyISID);
		D.ConnectedSupplyISIDs.SetNum(Count);
		D.SelectedSupplyPort=FMath::Clamp(Event.SelectedSupplyPort,0,Count-1);
		D.TemporarySealCharges=Event.TemporarySealCharges;
		D.LoadedCapISIDs=Event.LoadedCapISIDs; D.LoadedSealISIDs=Event.LoadedSealISIDs; D.StoredInMachineISID=Event.StoredInMachineISID;
		D.LinkedSupplyISID=D.ConnectedSupplyISIDs[D.SelectedSupplyPort];
		return D;
	}
}

int32 UShopProductionStationComponent::GetSupportedContentCount() const
{
	const auto* Machine=Cast<AManualColaFillingMachine>(GetOwner());
	return Machine ? FMath::Clamp(Machine->SupportedContentCount,1,32) : 1;
}

void UShopProductionStationComponent::ResetEquipmentSession()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	EndManualLever(); ReleaseSeatedBottle();
	if (Filler) Filler->StopFilling();
	ResetManualCapping(); View=FShopProductionView(); View.SessionID=FGuid::NewGuid();
	GetOwner()->ForceNetUpdate();
}

bool UShopProductionStationComponent::IsBottleAligned(int32 ID) const
{
	const auto* Manager=APropManager::Get(this);
	const auto* Record=Manager ? Manager->FindISIDData(ID) : nullptr;
	const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	if (!Record || !Product || Product->ConsumableProfile.NumOfPortions<=0 || Record->State==EPropState::Held || Record->State==EPropState::InMotion || !BottleHolder) return false;
	const auto* Active=Manager->GetActiveActorFromISID(ID);
	if (View.SeatedBottleISID==ID && (!Active || !Active->IsSeatedInFiller())) return false;
	if (Active)
	{
		if (Active->bIsHeld || Active->bIsInMotion) return false;
		if (const auto* Bottle=Cast<AColaPropActor>(Active)) if (Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight()) return false;
	}
	const FTransform Transform=Manager->GetTransform(ID);
	return FVector::DistSquared(Transform.GetLocation(),BottleHolder->GetComponentLocation())<=FMath::Square(8.f)
		&& FVector::DotProduct(Transform.GetUnitAxis(EAxis::Z),BottleHolder->GetUpVector())>=.99f;
}

bool UShopProductionStationComponent::IsBottleReadyForFilling(int32 ID) const
{
	const auto* Machine=Cast<AManualColaFillingMachine>(GetOwner());
	return IsBottleAligned(ID) && (!Machine || Machine->IsCradleInFillPosition());
}

bool UShopProductionStationComponent::CanSeatBottle(int32 ID,const AActor* Carrier) const
{
	const auto* Machine = Cast<AActivePropActor>(GetOwner());
	const auto* Manager = APropManager::Get(this);
	if (!Machine || !Machine->HasAuthority() || Machine->ISID <= 0 || Machine->bIsHeld || !Manager || !BottleHolder
		|| !Filler || Filler->bIsFilling || View.bLeverHeld || (View.SeatedBottleISID && View.SeatedBottleISID != ID)) return false;
	if (const auto* Layout = Cast<ASocketColaFillingMachine>(Machine); Layout && Layout->IsBottleHolderReserved(ID)) return false;

	const auto* Record = Manager->FindISIDData(ID);
	const auto* Definition = Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	if (!Definition || Definition->bIsBottleCap || Definition->ConsumableProfile.NumOfPortions <= 0
		|| Record->Attachment.OwnerISID || Record->State == EPropState::InMotion) return false;
	if (Record->State == EPropState::Held && (!Carrier || Record->RuntimeData.InteractionOwner.Get() != Carrier)) return false;

	FShopStockEntry Stock;
	if (Manager->ShopCheckout && Manager->ShopCheckout->GetStockEntry(ID,Stock) && !Manager->ShopCheckout->IsAvailableShopStock(ID)) return false;
	if (const auto* Active = Manager->GetActiveActorFromISID(ID))
	{
		const auto* Bottle = Cast<AColaPropActor>(Active);
		if (!Bottle) return false;
		if (Bottle->bIsInMotion || (Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight())) return false;
		if (Bottle->IsSeatedInFiller() && Bottle->ManualFillingStation.IsValid() && Bottle->ManualFillingStation.Get() != this) return false;
		return true;
	}
	const UClass* ActiveClass = Manager->ASMComponent ? Manager->ASMComponent->GetEffectiveActivePropActorClass(Record->ProductID).Get() : nullptr;
	return ActiveClass && ActiveClass->IsChildOf(AColaPropActor::StaticClass());
}

bool UShopProductionStationComponent::SeatBottle(int32 ID)
{
	if (!CanSeatBottle(ID)) return false;
	if (View.SeatedBottleISID == ID && IsBottleAligned(ID)) return true;

	auto* Manager = APropManager::Get(this);
	AActivePropActor* Active = nullptr;
	if (!Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Active)) return false;
	auto* Bottle = Cast<AColaPropActor>(Active);
	if (!Bottle) return false;

	Bottle->SetSeatedInFiller(true);
	const FTransform Pose(BottleHolder->GetComponentQuat(),BottleHolder->GetComponentLocation(),Bottle->GetActorScale3D());
	if (!Manager->InitializeInstanceTransform(ID,Pose))
	{
		Bottle->SetSeatedInFiller(false);
		return false;
	}
	View.SeatedBottleISID = ID;
	Bottle->ManualFillingStation = this;
	Manager->MarkInstanceDirty(ID, EPropDirtyFlags::Transform);
	UpdateBottleInputTrace();
	GetOwner()->ForceNetUpdate();
	return true;
}

void UShopProductionStationComponent::UpdateBottleInputTrace()
{
	if (BottleInput && GetOwner()->IsA<ASocketColaFillingMachine>())
		BottleInput->SetCollisionResponseToChannel(ECC_Visibility, View.SeatedBottleISID>0 ? ECR_Ignore : ECR_Block);
}

bool UShopProductionStationComponent::IsLeverHoldingBottle(int32 ID) const
{
	const auto* Machine=Cast<AActivePropActor>(GetOwner());
	return View.bLeverHeld && View.SeatedBottleISID==ID && Filler && GetWorld()
		&& GetWorld()->GetTimeSeconds()<Filler->LeverExpiresAt && OperatorInRange(View.LeverOperator)
		&& (!Machine || !Machine->bIsHeld) && IsBottleAligned(ID);
}

bool UShopProductionStationComponent::UseTemporarySeal(int32 ID)
{
	auto* Manager=APropManager::Get(this); auto* Machine=Cast<AActivePropActor>(GetOwner());
	if (!Machine || !Machine->HasAuthority()) return false;
	const auto* Equipment=Manager ? Manager->FindUniqueData(Machine->ISID) : nullptr;
	auto* Bottle=Manager ? Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID)) : nullptr;
	if (!Equipment || !Bottle || Equipment->EventData.LoadedSealISIDs.IsEmpty() || !Bottle->CanApplyManufacturingSeal()) return false;
	const int32 SealID=Equipment->EventData.LoadedSealISIDs[0];
	const auto* Supply=Manager->FindUniqueData(SealID);
	if (!Supply || Supply->EventData.bTemporarySealUsed || Supply->EventData.StoredInMachineISID!=Machine->ISID) return false;
	const auto* SealRecord = Manager->FindISIDData(SealID);
	const auto* SealProduct = SealRecord ? Manager->FindProductDefinition(SealRecord->ProductID) : nullptr;
	const auto* SealAsset = SealProduct ? SealProduct->SourceDataAsset.Get() : nullptr;
	const float BaseDuration = SealAsset && SealAsset->bIsTemporaryPressureSeal ? static_cast<float>(SealAsset->NumOfPortions) : 0.0f;
	const float Duration = BaseDuration > 0.0f ? FMath::FRandRange(FMath::Max(.1f, BaseDuration - 1.0f), BaseDuration + 1.0f) : 0.0f;
	auto D=ManualEquipment::State(Equipment->EventData,GetSupportedContentCount()); D.ISID=Machine->ISID; D.LoadedSealISIDs.RemoveAt(0);
	FPropEventSyncDelta Seal;
	if (!Bottle->BuildManufacturingSealDelta(Seal, Duration)) return false;
	Seal.ISID=ID;
	FPropEventSyncDelta Burn; Burn.ISID=SealID; Burn.ExtendedDirtyMask=EPropExtendedEventMask::TemporarySeal; Burn.bTemporarySealUsed=true;
	if (!Manager->SubmitAtomicInstanceEventUpdates({Seal,D,Burn})) return false;
	ReleaseStoredSupply(SealID,false); Manager->ReleaseInstanceToEmpty(SealID);
	View.TemporarySealCharges=D.LoadedSealISIDs.Num(); View.SealTrayCount=View.TemporarySealCharges;
	return true;
}

void UShopProductionStationComponent::EndManualLever()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	const bool WasHeld=View.bLeverHeld;
	const int32 BottleID=View.SeatedBottleISID;
	View.bLeverHeld=false; View.bDepressurizing=false; View.LeverOperator=nullptr;
	View.bFilling=false; View.bFlowStopped=false;
	if (Filler) { Filler->StopFilling(); Filler->LeverOperator.Reset(); Filler->LeverExpiresAt=0; }
	if (!WasHeld) return;
	auto* Manager=APropManager::Get(this);
	if (auto* Bottle=Manager ? Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID)) : nullptr)
	{
		// A stocked seal is fitted before releasing nozzle restraint. The bottle chooses its duration/failure policy.
		if (!Bottle->IsManufacturingSafeToRelease() && !Bottle->HasTemporaryManufacturingSeal()) UseTemporarySeal(BottleID);
		Bottle->NotifyManufacturingLeverReleased();
	}
	GetOwner()->ForceNetUpdate();
}

void UShopProductionStationComponent::HandleManualOverfill(int32 BottleID)
{
	if (!GetOwner()->HasAuthority() || BottleID<=0 || View.SeatedBottleISID!=BottleID) return;
	View.bLeverHeld=false; View.LeverOperator=nullptr; View.bFilling=false;
	View.bFlowStopped=false; View.bDepressurizing=false;
	if (Filler) { Filler->StopFilling(); Filler->LeverExpiresAt=0; Filler->LeverOperator.Reset(); }
	ReleaseSeatedBottle(); GetOwner()->ForceNetUpdate();
}

void UShopProductionStationComponent::ReleaseSeatedBottle()
{
	if (!View.SeatedBottleISID) return;
	auto* Manager=APropManager::Get(this);
	auto* Previous=Manager ? Manager->GetActiveActorFromISID(View.SeatedBottleISID) : nullptr;
	View.SeatedBottleISID=0;
	UpdateBottleInputTrace();
	GetOwner()->ForceNetUpdate();
	if (auto* Bottle=Cast<AColaPropActor>(Previous)) if (Bottle->ManualFillingStation.Get()==this) Bottle->ManualFillingStation.Reset();
	if (!Previous || !Previous->IsSeatedInFiller()) return;
	Previous->SetSeatedInFiller(false);
	const auto* Bottle=Cast<AColaPropActor>(Previous);
	if (Previous->bIsHeld || Previous->bIsInMotion || (Bottle && Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight())) return;
	if (Previous->PhysicsSimComponent) Previous->PhysicsSimComponent->SetStateUnsettled();
	else if (Previous->PropMesh) Previous->PropMesh->SetSimulatePhysics(true);
}

void UShopProductionStationComponent::UpdateManualEquipment()
{
	auto* Manager=APropManager::Get(this); if (!Manager || !Filler) return;
	auto* Machine=Cast<AActivePropActor>(GetOwner());
	if (auto* Equipment=Cast<AManualColaFillingMachine>(Machine)) Equipment->RestoreSupplyNozzles();
	const auto* State=Machine ? Manager->FindUniqueData(Machine->ISID) : nullptr;
	View.SupplyPorts.Reset();
	if (State)
	{
		const auto D=ManualEquipment::State(State->EventData,GetSupportedContentCount());
		View.SelectedContentPort=D.SelectedSupplyPort; View.ConnectedSourceISID=D.LinkedSupplyISID;
		View.CapTrayCount=D.LoadedCapISIDs.Num(); View.SealTrayCount=D.LoadedSealISIDs.Num(); View.TemporarySealCharges=View.SealTrayCount;
		for (int32 I=0; I<D.ConnectedSupplyISIDs.Num(); ++I)
		{
			FShopProductionPort Port; Port.PortIndex=I; Port.SourceISID=D.ConnectedSupplyISIDs[I];
			if (const auto* Source=Manager->FindUniqueData(Port.SourceISID))
			{
				Port.ContentID=Source->EventData.ContentID;
				if (const auto* Content=Manager->FindContentDefinition(Port.ContentID)) Port.ContentName=Content->DisplayName;
				const auto* Equipment=Cast<AManualColaFillingMachine>(Machine);
				const FVector Origin=Equipment ? Equipment->GetSupplyConnectionOrigin() : GetOwner()->GetActorLocation();
				Port.bInRange=FVector::DistSquared(Origin,Manager->GetTransform(Port.SourceISID).GetLocation())<=FMath::Square(Filler->MaxConnectionDistance);
				if (Equipment) Port.bInRange &= Equipment->HasSupplyNozzle(I,Port.SourceISID);
			}
			if (const auto* Equipment=Cast<AManualColaFillingMachine>(Machine); Equipment && Equipment->GetSupplyHoseForPort(I))
			{
				Port.ContentID=Equipment->GetSupplyContentID(I); Port.bInRange=Equipment->IsSupplyInRange(I);
				if (const auto* Content=Manager->FindContentDefinition(Port.ContentID)) Port.ContentName=Content->DisplayName;
			}
			View.SupplyPorts.Add(Port);
		}
	}
	const bool MovingMachine=Machine && Machine->bIsHeld;
	if (View.bLeverHeld && !IsLeverHoldingBottle(View.SeatedBottleISID)) EndManualLever();
	if (MovingMachine || !IsBottleAligned(View.SeatedBottleISID)) ReleaseSeatedBottle();
	if (MovingMachine) { Filler->StopFilling(); return; }
	if (!View.SeatedBottleISID) for (const auto& Bottle:View.Bottles)
	{
		if (!CanSeatBottle(Bottle.ISID)) continue;
		if (!IsBottleAligned(Bottle.ISID))
		{
			auto* Equipment=Cast<AManualColaFillingMachine>(Machine);
			if (!Equipment || !ManualEquipment::RestoreCradlePose(*Equipment,*Manager,Bottle.ISID)) continue;
		}
		if (SeatBottle(Bottle.ISID)) break;
	}
	if (auto* Layout = Cast<ASocketColaFillingMachine>(Machine); !View.SeatedBottleISID && Layout && BottleInput)
	{
		// Poll the actual collision volume. Settled instanced props and props that
		// were held when overlap began need no new physics overlap event to seat.
		for (const FShopProductionItem& Item : View.Bottles)
			if (CanSeatBottle(Item.ISID) && Layout->BeginInputPlacement(*Manager, Item.ISID, *BottleInput)) break;
	}
	if (Filler->bIsFilling && (!View.bLeverHeld || Filler->TargetISID!=View.SeatedBottleISID || Filler->SourceISID!=View.ConnectedSourceISID)) Filler->StopFilling();
	auto* Seated=Manager->GetActiveActorFromISID(View.SeatedBottleISID);
	auto* Bottle=Cast<AColaPropActor>(Seated);
	if (Seated) Seated->SetSeatedInFiller(true);
	if (Bottle) Bottle->ManualFillingStation=this;
	if (View.bLeverHeld && !View.bFlowStopped && !Filler->bIsFilling && Bottle && IsBottleReadyForFilling(Bottle->ISID))
	{
		const auto* Source=Manager->FindUniqueData(View.ConnectedSourceISID);
		const auto* Equipment=Cast<AManualColaFillingMachine>(Machine);
		const int32 ContentID=Equipment ? Equipment->GetSupplyContentID(View.SelectedContentPort) : (Source ? Source->EventData.ContentID : 0);
		if (Bottle->CanAcceptManufacturingContent(ContentID))
			Filler->StartFilling(View.ConnectedSourceISID,Bottle->ISID);
	}
	View.bSafeToRelease=!Bottle || Bottle->IsManufacturingSafeToRelease();
	View.bFilling=Filler->bIsFilling && (!Filler->IsHoseFilling() || Filler->DeliveredPortionsPerSecond>UE_SMALL_NUMBER);
	View.bDepressurizing=View.bLeverHeld && !View.bFilling && !View.bSafeToRelease;
}

EShopCheckoutResult UShopProductionStationComponent::ManualEquipmentAction(EShopProductionAction Action,int32 BottleID,int32 OtherID,AActor* Player)
{
	if (!OperatorInRange(Player) || !Filler) return EShopCheckoutResult::Unavailable;
	auto* Manager=APropManager::Get(this); if (!Manager) return EShopCheckoutResult::Unavailable;
	auto* Machine=Cast<AActivePropActor>(GetOwner());
	if (Machine && Machine->bIsHeld) return EShopCheckoutResult::Unavailable;
	UpdateManualEquipment();
	if (Action==EShopProductionAction::ConnectSupply || Action==EShopProductionAction::DisconnectSupply || Action==EShopProductionAction::SelectContentPort)
	{
		const auto* Equipment=Machine ? Manager->FindUniqueData(Machine->ISID) : nullptr;
		if (!Equipment || View.bLeverHeld || Filler->bIsFilling || BottleID<0 || BottleID>=GetSupportedContentCount()) return EShopCheckoutResult::Unavailable;
		auto D=ManualEquipment::State(Equipment->EventData,GetSupportedContentCount());
		if (Action==EShopProductionAction::ConnectSupply)
		{
			const auto* Source=Manager->FindISIDData(OtherID);
			const auto* EquipmentActor=Cast<AManualColaFillingMachine>(Machine);
			if (!Source || !EquipmentActor || !Manager->ShopSupplies || !Manager->ShopSupplies->IsCylinderProduct(Source->ProductID)
				|| FVector::DistSquared(EquipmentActor->GetSupplyConnectionOrigin(),Manager->GetTransform(OtherID).GetLocation())>FMath::Square(Filler->MaxConnectionDistance)) return EShopCheckoutResult::Unavailable;
			const auto* SourceData=Manager->FindUniqueData(OtherID);
			if (!SourceData || !EquipmentActor->HasSupplyNozzle(BottleID,OtherID)
				|| (SourceData->EventData.StoredInMachineISID && SourceData->EventData.StoredInMachineISID!=Machine->ISID)) return EShopCheckoutResult::Unavailable;
			D.ConnectedSupplyISIDs[BottleID]=OtherID;
		}
		else if (Action==EShopProductionAction::DisconnectSupply) D.ConnectedSupplyISIDs[BottleID]=0;
		else D.SelectedSupplyPort=BottleID;
		D.LinkedSupplyISID=D.ConnectedSupplyISIDs[D.SelectedSupplyPort];
		TArray<FPropEventSyncDelta> Updates; D.ISID=Machine->ISID; Updates.Add(D);
		int32 SourceID=0;
		if (Action==EShopProductionAction::ConnectSupply) SourceID=OtherID;
		else if (Action==EShopProductionAction::DisconnectSupply) SourceID=Equipment->EventData.ConnectedSupplyISIDs.IsValidIndex(BottleID) ? Equipment->EventData.ConnectedSupplyISIDs[BottleID] : Equipment->EventData.LinkedSupplyISID;
		if (const auto* SourceData=Manager->FindUniqueData(SourceID))
		{
			auto SourceUpdate=ManualEquipment::State(SourceData->EventData,1); SourceUpdate.ISID=SourceID;
			SourceUpdate.StoredInMachineISID=Action==EShopProductionAction::ConnectSupply ? Machine->ISID : 0; Updates.Add(SourceUpdate);
		}
		if (!Manager->SubmitAtomicInstanceEventUpdates(Updates)) return EShopCheckoutResult::Unavailable;
		if (Action==EShopProductionAction::DisconnectSupply) if (auto* EquipmentActor=Cast<AManualColaFillingMachine>(Machine)) EquipmentActor->DisconnectSupplyNozzle(BottleID);
	}
	else if (Action==EShopProductionAction::AlignBottle)
	{
		if (View.SeatedBottleISID && View.SeatedBottleISID!=BottleID) return EShopCheckoutResult::OperationConflict;
		if (View.SeatedBottleISID != BottleID && !ShopInstanceVolumeQuery::Query(BottleInput).Contains(BottleID)) return EShopCheckoutResult::Unavailable;
		if (!SeatBottle(BottleID)) return EShopCheckoutResult::Unavailable;
	}
	else if (Action==EShopProductionAction::HoldFillingLever)
	{
		if (View.SeatedBottleISID!=BottleID || !IsBottleAligned(BottleID)) return EShopCheckoutResult::Unavailable;
		if (const auto* EquipmentActor=Cast<AManualColaFillingMachine>(Machine); EquipmentActor && !EquipmentActor->HasSupplyNozzle(View.SelectedContentPort,View.ConnectedSourceISID)) return EShopCheckoutResult::Unavailable;
		if (View.bLeverHeld && View.LeverOperator!=Player) return EShopCheckoutResult::OperationConflict;
		const auto* B=Manager->FindISIDData(BottleID); const auto* Data=Manager->FindUniqueData(BottleID); const auto* S=Manager->FindUniqueData(View.ConnectedSourceISID);
		if (!B || !Data || Data->EventData.AttachedCapISID>0) return EShopCheckoutResult::Unavailable;
		auto* Cola=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID));
		const auto* EquipmentActor=Cast<AManualColaFillingMachine>(Machine);
		const int32 ContentID=EquipmentActor ? EquipmentActor->GetSupplyContentID(View.SelectedContentPort) : (S ? S->EventData.ContentID : 0);
		if (!Cola || ContentID<=0 || (!View.bFlowStopped && !Cola->HasTemporaryManufacturingSeal() && !Cola->CanAcceptManufacturingContent(ContentID))) return EShopCheckoutResult::ContentNotSupported;
		Filler->LeverOperator=Player; Filler->LeverExpiresAt=GetWorld()->GetTimeSeconds()+FMath::Max(.15f,LeverLeaseSeconds);
		View.LeverOperator=Player; View.bLeverHeld=true;
		// Flow starts in UpdateManualEquipment only after the cradle reaches its filling angle.

	}
	else if (Action==EShopProductionAction::StopFilling || Action==EShopProductionAction::ResumeFilling)
	{
		if (!View.bLeverHeld || View.LeverOperator!=Player) return EShopCheckoutResult::OperationConflict;
		View.bFlowStopped=Action==EShopProductionAction::StopFilling;
		if (View.bFlowStopped) Filler->StopFilling();
		else return ManualEquipmentAction(EShopProductionAction::HoldFillingLever,View.SeatedBottleISID,0,Player);
	}
	else if (Action==EShopProductionAction::ReleaseFillingLever)
	{
		if (View.LeverOperator && View.LeverOperator!=Player) return EShopCheckoutResult::OperationConflict;
		EndManualLever();
	}
	else if (Action==EShopProductionAction::ApplyTemporarySeal)
	{
		if (!IsBottleAligned(BottleID) || !UseTemporarySeal(BottleID)) return EShopCheckoutResult::Unavailable;
	}
	else return EShopCheckoutResult::InvalidRequest;
	UpdateManualEquipment(); GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success;
}
