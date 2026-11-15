#include "Tests/NormalColaPrototypeTestActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPropActor.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ANormalColaPrototypeTestActor::TickPressureFlight()
{
	if (TestSupplyISIDs.Num()!=3) return;
	const int32 BottleID=TestSupplyISIDs[0], CapID=TestSupplyISIDs[1];
	const bool ReloadShake=FParse::Param(FCommandLine::Get(),TEXT("ReloadBottlePressureTest"));
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID));
	if (Stage>=300 && !bPressureObservationStarted)
	{
		LocalSawPressureFlight=false; LocalPeakPressureHeight=0; bPressureObservationStarted=true; LastSentStage=-1;
	}
	if (Bottle && Stage>=300)
	{
		const bool Powered=Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight();
		if (Powered && !Bottle->RocketComp->GetReplicatedFlightState().bPressurePhysicsFlight && Bottle->PropMesh->IsSimulatingPhysics())
		{ Fail(TEXT("Chaos was enabled while the Rocket Projectile Component owned powered movement")); return; }
		LocalSawPressureFlight|=Powered;
		LocalPeakPressureHeight=FMath::Max(LocalPeakPressureHeight,float(Bottle->GetActorLocation().Z-PressureLaunchOrigin.Z));
		LocalPressureFlight=FString::Printf(TEXT("ISID=%d powered=%d physics=%d state=%d pos=%s velocity=%s pressure=%.3f level=%.3f seal=%.3f cap=%d peak=%.1f"),
			BottleID,Powered,Bottle->PropMesh->IsSimulatingPhysics(),int32(Bottle->RocketComp->GetSimulationState()),
			*Bottle->GetActorLocation().ToCompactString(),*Bottle->PropMesh->GetPhysicsLinearVelocity().ToCompactString(),
			Bottle->GetBottlePressure(),Bottle->GetContentLevel(),Bottle->GetCapSealed(),Bottle->LocalUniqueData.EventData.AttachedCapISID,LocalPeakPressureHeight);
	}
	if (Bottle && Stage>=320 && int32(GetWorld()->GetTimeSeconds()) != LastSentStage)
	{
		LastSentStage=int32(GetWorld()->GetTimeSeconds());
		UE_LOG(LogTemp,Display,TEXT("BOTTLE_PRESSURE_PEER authority=%d stage=%d %s"),HasAuthority(),Stage,*LocalPressureFlight);
	}
	if (!Bottle && Stage>=330)
	{
		const auto* Rest = Manager->FindISIDData(BottleID);
		const auto* Data = Manager->FindUniqueData(BottleID);
		if (Rest && Rest->Representation==EPropRepresentation::ISM && Rest->State==EPropState::AtRest && Data)
			LocalPressureFlight=FString::Printf(TEXT("ISID=%d powered=0 physics=0 state=%d level=%.3f"),BottleID,int32(ERocketSimulationState::AtRest),Data->EventData.ContentLevel);
	}
	if (!HasAuthority() || Stage==400) return;
	if (Stage==200 && Command==3)
	{
		const auto* Data=Manager->FindUniqueData(BottleID);
		if (!Data || Data->EventData.CapSealed<1 || Data->EventData.ContentLevel<=0 || Data->EventData.AttachedCapISID!=CapID)
		{ Fail(TEXT("Pressure flight requires the actual manufactured capped bottle")); return; }
		if (!Bottle) Bottle=Cast<AColaPropActor>(Manager->ConvertISMToASMFromISID(BottleID));
		if (!Bottle || !Bottle->RocketComp) { Fail(TEXT("Manufactured bottle cannot enter pressure validation")); return; }
		// Test setup only: original flight starts upright; reaction-thrust mode
		// starts inverted so its physical nozzle points down. Use real impacts.
		PressureLaunchOrigin=FVector(-2000,-3000,260);
		Bottle->PropMesh->SetSimulatePhysics(false);
		const FRotator LaunchRotation=Bottle->bUseOriginalRocketFlight ? FRotator::ZeroRotator : FRotator(180,0,0);
		Manager->InitializeInstanceTransform(BottleID,FTransform(LaunchRotation,PressureLaunchOrigin,FVector::OneVector));
		if (ReloadShake)
		{
			// Isolated fixture: reproduce an older save with a fallen full bottle
			// and an obsolete cap edge. Never read or overwrite the player's slot.
			const FString Slot=TEXT("ReloadPressureQA_")+FGuid::NewGuid().ToString();
			const int32 Count=Manager->ISIDData.Num();
			if (!Manager->SavePropDatabase(Slot)) { Fail(TEXT("Pressure reload fixture could not save")); return; }
			auto* Saved=UPropDatabaseSaveGame::LoadCompatibleSlot(Slot,0);
			if (!Saved) { Fail(TEXT("Pressure reload fixture could not decode")); return; }
			for (auto& Item:Saved->WorldSaveData.SavedInstances)
			{
				if (Item.ISID==BottleID)
				{
					Item.ActiveTransform=FTransform(FRotator(0,0,90),PressureLaunchOrigin);
					Item.State=EPropState::AtRest; Item.Representation=EPropRepresentation::ISM;
					Item.LinearVelocity=Item.AngularVelocity=FVector::ZeroVector;
					Item.UniqueData.ContinuousData.SealedPressure=.2f;
				}
				if (Item.ISID==CapID) Item.Attachment.RelativeTransform.SetLocation(FVector(0,0,170));
			}
			const bool Loaded=UGameplayStatics::SaveGameToSlot(Saved,Slot,0) && Manager->LoadPropDatabase(Slot);
			UGameplayStatics::DeleteGameInSlot(Slot,0);
			const auto* Restored=Manager->FindUniqueData(BottleID);
			const auto* CapRecord=Manager->FindISIDData(CapID);
			if (!Loaded || Manager->ISIDData.Num()!=Count || !Restored || !CapRecord
				|| !FMath::IsNearlyEqual(Restored->EventData.ContentLevel,1.f)
				|| !FMath::IsNearlyEqual(Restored->ContinuousData.SealedPressure,.2f)
				|| Restored->EventData.AttachedCapISID!=CapID
				|| !CapRecord->Attachment.RelativeTransform.GetLocation().IsNearlyZero(.001f))
			{ Fail(TEXT("Reload changed bottle identity/fill/pressure or left the cap offset")); return; }
			auto* PC=Cast<APlayerController>(GetOwner()); auto* Pawn=PC ? PC->GetPawn().Get() : nullptr;
			PlaceTestPawn(Pawn,PressureLaunchOrigin+FVector(200,0,100));
			if (!Pawn || !Manager->BeginGrab(BottleID,Pawn,Pawn->GetRootComponent()))
			{ Fail(TEXT("Reloaded fallen bottle could not be picked up")); return; }
			Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID));
			if (!Bottle) { Fail(TEXT("Reloaded bottle has no Cola ASM")); return; }
			// Exercise kinematic holders without a scene attachment, the reported
			// path that previously suppressed pressure sampling indefinitely.
			Bottle->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			UE_LOG(LogTemp,Display,TEXT("BOTTLE_RELOAD_PEER_FIXTURE_PASSED records=%d root=%d cap=%d"),Count,BottleID,CapID);
		}
		PressureImpactCount=0; PressureStartedAt=GetWorld()->GetTimeSeconds(); Stage=310; ForceNetUpdate(); return;
	}
	const auto* Record=Manager->FindISIDData(BottleID);
	const bool SettledISM=Stage==330 && !Bottle && Record && Record->State==EPropState::AtRest;
	if (!Bottle && !SettledISM) { Fail(TEXT("Pressure bottle disappeared before physics settlement")); return; }
	if (Stage==310)
	{
		if (ReloadShake)
		{
			++PressureImpactCount;
			const FVector Motion=PressureLaunchOrigin+FVector(40*FMath::Sin(float(PressureImpactCount)*.36f),0,0);
			Manager->UpdateHeldTransform(BottleID,FTransform(FRotator::ZeroRotator,Motion));
		}
		else Bottle->ApplyBottleImpact(FVector(0,0,Bottle->DropImpactThreshold*1.8f),170000+(++PressureImpactCount));
		if (Bottle->RocketComp->IsPoweredFlight())
		{
			const auto* Cap=Manager->FindUniqueData(CapID);
			const bool PhysicsMode=Bottle->RocketComp->GetReplicatedFlightState().bPressurePhysicsFlight;
			const bool CorrectMovement=PhysicsMode ? (Bottle->PropMesh->IsSimulatingPhysics() && Bottle->IsReplicatingMovement()
				&& Bottle->RocketComp->GetPressureThrustDirection().Z>.9) : (!Bottle->PropMesh->IsSimulatingPhysics() && Bottle->bIsInMotion);
			if (!Cap || Cap->EventData.AttachedBottleISID!=0 || Bottle->GetCapSealed()>0 || !CorrectMovement)
			{ Fail(TEXT("Impact launch did not preserve cap identity and the selected rocket-component mode")); return; }
			PressureStartedAt=GetWorld()->GetTimeSeconds(); Stage=320; ForceNetUpdate();
			UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_PRESSURE_FLIGHT_STARTED bottle=%d cap=%d impacts=%d"),BottleID,CapID,PressureImpactCount);
		}
		else if (PressureImpactCount>=(ReloadShake ? 500 : 20)) Fail(TEXT("Real agitation did not launch sealed plastic bottle"));
		return;
	}
	if (GetWorld()->GetTimeSeconds()-PressureStartedAt>45) { Fail(TEXT("Physical pressure flight did not settle within 45 seconds")); return; }
	if (Stage==320 && Bottle && !Bottle->RocketComp->IsPoweredFlight())
	{
		if (!LocalSawPressureFlight || LocalPeakPressureHeight<50 || !FMath::IsNearlyZero(Bottle->GetContentLevel()))
		{ Fail(TEXT("Pressure flight did not produce powered ascent with actual liquid loss")); return; }
		Stage=330; ForceNetUpdate();
	}
	if (Stage==330 && (SettledISM || (Bottle && Bottle->RocketComp->GetSimulationState()==ERocketSimulationState::AtRest)))
	{
		if (Manager->SharedWallet->GetWalletState().BalanceCents!=ExpectedBankCents || Manager->ShopCheckout->RecentReceipts.Num()!=SalesCompleted)
		{ Fail(TEXT("Pressure flight changed unrelated shop accounting")); return; }
		Stage=400; ForceNetUpdate();
		const auto* Data=Manager->FindUniqueData(BottleID);
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_PRESSURE_FLIGHT_PASSED bottle=%d peak=%.1f level=%.3f"),BottleID,LocalPeakPressureHeight,Data ? Data->EventData.ContentLevel : -1.f);
	}
}
