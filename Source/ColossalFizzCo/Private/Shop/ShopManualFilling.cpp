#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPropActor.h"
#include "Components/BoxComponent.h"
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
		const FTransform HolderLocal=Machine.BottleHolderPoint->GetRelativeTransform();
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
	if (const auto* Active=Manager->GetActiveActorFromISID(ID))
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
	const auto* Equipment=Manager && Machine ? Manager->FindUniqueData(Machine->ISID) : nullptr;
	auto* Bottle=Manager ? Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID)) : nullptr;
	if (!Equipment || !Bottle || Equipment->EventData.LoadedSealISIDs.IsEmpty() || !Bottle->CanApplyManufacturingSeal()) return false;
	const int32 SealID=Equipment->EventData.LoadedSealISIDs[0];
	const auto* Supply=Manager->FindUniqueData(SealID);
	if (!Supply || Supply->EventData.bTemporarySealUsed || Supply->EventData.StoredInMachineISID!=Machine->ISID) return false;
	auto D=ManualEquipment::State(Equipment->EventData,GetSupportedContentCount()); D.ISID=Machine->ISID; D.LoadedSealISIDs.RemoveAt(0);
	FPropEventSyncDelta Seal;
	if (!Bottle->BuildManufacturingSealDelta(Seal)) return false;
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
	if (!GetOwner()->HasAuthority()) return;
	View.bLeverHeld=false; View.bFlowStopped=true; View.LeverOperator=nullptr; View.bFilling=false;
	if (Filler) { Filler->StopFilling(); Filler->LeverExpiresAt=0; Filler->LeverOperator.Reset(); }
	ReleaseSeatedBottle(); GetOwner()->ForceNetUpdate();
}

void UShopProductionStationComponent::ReleaseSeatedBottle()
{
	auto* Manager=APropManager::Get(this);
	auto* Previous=Manager ? Manager->GetActiveActorFromISID(View.SeatedBottleISID) : nullptr;
	View.SeatedBottleISID=0;
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
				Port.bInRange=FVector::DistSquared(GetOwner()->GetActorLocation(),Manager->GetTransform(Port.SourceISID).GetLocation())<=FMath::Square(Filler->MaxConnectionDistance);
				if (const auto* Equipment=Cast<AManualColaFillingMachine>(Machine)) Port.bInRange &= Equipment->HasSupplyNozzle(I,Port.SourceISID);
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
		if (!IsBottleAligned(Bottle.ISID))
		{
			auto* Equipment=Cast<AManualColaFillingMachine>(Machine);
			if (!Equipment || !ManualEquipment::RestoreCradlePose(*Equipment,*Manager,Bottle.ISID)) continue;
		}
		View.SeatedBottleISID=Bottle.ISID;
		// An interrupted saved fill resumes with its valve closed. Explicit V is
		// required to add more liquid, including after a saved seal expires.
		if (const auto* Restored=Manager->FindUniqueData(Bottle.ISID)) View.bFlowStopped=Restored->EventData.bManualFillPressure && Restored->EventData.ContentLevel>0;
		break;
	}
	if (View.SeatedBottleISID)
	{
		if (auto* Active=Manager->GetActiveActorFromISID(View.SeatedBottleISID))
		{
			Active->SetSeatedInFiller(true);
			if (auto* Bottle=Cast<AColaPropActor>(Active)) Bottle->ManualFillingStation=this;
		}
	}
	if (Filler->bIsFilling && (!View.bLeverHeld || Filler->TargetISID!=View.SeatedBottleISID || Filler->SourceISID!=View.ConnectedSourceISID)) Filler->StopFilling();
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(View.SeatedBottleISID));
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
			if (!Source || !Manager->ShopSupplies || !Manager->ShopSupplies->IsCylinderProduct(Source->ProductID)
				|| FVector::DistSquared(GetOwner()->GetActorLocation(),Manager->GetTransform(OtherID).GetLocation())>FMath::Square(Filler->MaxConnectionDistance)) return EShopCheckoutResult::Unavailable;
			const auto* SourceData=Manager->FindUniqueData(OtherID);
			const auto* EquipmentActor=Cast<AManualColaFillingMachine>(Machine);
			if (!SourceData || !EquipmentActor || !EquipmentActor->HasSupplyNozzle(BottleID,OtherID)
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
		if (!BottleHolder || View.bLeverHeld || Filler->bIsFilling || !ShopInstanceVolumeQuery::Query(BottleInput).Contains(BottleID)) return EShopCheckoutResult::Unavailable;
		const auto* Record=Manager->FindISIDData(BottleID);
		const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (!Product || Product->ConsumableProfile.NumOfPortions<=0
			|| (View.SeatedBottleISID && View.SeatedBottleISID!=BottleID)) return EShopCheckoutResult::OperationConflict;
		AActivePropActor* Bottle=nullptr;
		if (!Manager->PromoteInstanceToASM(BottleID,EPropActivationReason::Interaction,false,Bottle) || !Bottle || Bottle->bIsHeld || Bottle->bIsInMotion) return EShopCheckoutResult::Unavailable;
		if (const auto* Cola=Cast<AColaPropActor>(Bottle)) if (Cola->RocketComp && Cola->RocketComp->IsPoweredFlight()) return EShopCheckoutResult::Unavailable;
		Manager->InitializeInstanceTransform(BottleID,FTransform(BottleHolder->GetComponentQuat(),BottleHolder->GetComponentLocation(),Manager->GetTransform(BottleID).GetScale3D()));
		Bottle->SetSeatedInFiller(true); View.SeatedBottleISID=BottleID; View.bFlowStopped=false;
		if (auto* Cola=Cast<AColaPropActor>(Bottle)) Cola->ManualFillingStation=this;
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
