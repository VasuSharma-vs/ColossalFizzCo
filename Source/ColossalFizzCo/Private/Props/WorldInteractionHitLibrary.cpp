#include "Interfaces/WorldInteractionHitLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/PropAssemblyMesh.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/Actor.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
	bool ValidWorldHit(const FWorldInteractionHit& Data)
	{
		return IsValid(Data.SourceActor) && Data.SourceActor->HasAuthority() && Data.SourceActor->GetNetMode()!=NM_Client
			&& IsValid(Data.TargetActor) && IsValid(Data.TargetComponent) && Data.TargetComponent->GetOwner()==Data.TargetActor
			&& Data.SourceActor!=Data.TargetActor && Data.SourceActor->GetWorld()==Data.TargetActor->GetWorld()
			&& Data.EventID.IsValid() && !Data.Velocity.ContainsNaN() && !Data.RelativeVelocity.ContainsNaN() && !Data.Impulse.ContainsNaN()
			&& FMath::IsFinite(Data.MassKg) && Data.MassKg>=0;
	}
	bool SendLegacyHit(UObject* Receiver,const FWorldInteractionHit& Data)
	{
		// Preserve the existing Blueprint interface signature without adding a hard native dependency on its generated class.
		static TWeakObjectPtr<UClass> Interface;
		if (!Interface.IsValid()) Interface=LoadObject<UClass>(nullptr,TEXT("/Game/WorldInteractionSystem/BPI_World_Intractions_System.BPI_World_Intractions_System_C"));
		if (!Interface.IsValid() || !Receiver->GetClass()->ImplementsInterface(Interface.Get())) return false;
		UFunction* Declaration=nullptr;
		for (TFieldIterator<UFunction> It(Interface.Get());It;++It)
			if (It->GetName().Replace(TEXT(" "),TEXT("")).Replace(TEXT("_"),TEXT("")).Equals(TEXT("HitActor"),ESearchCase::IgnoreCase))
			{ Declaration=*It; break; }
		if (!Declaration) return false;
		UFunction* Function=Receiver->FindFunction(Declaration->GetFName());
		// Blueprint interfaces permit an unimplemented void event. As with an interface Message node, that is an accepted no-op.
		if (!Function) return true;
		FStructOnScope Parameters(Function);
		for (TFieldIterator<FProperty> It(Function);It && It->HasAnyPropertyFlags(CPF_Parm);++It)
		{
			FProperty* Property=*It;
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
			FString Name=Property->GetName().Replace(TEXT(" "),TEXT("")).Replace(TEXT("_"),TEXT("")).ToLower();
			void* Value=Property->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory());
			if (auto* Object=CastField<FObjectPropertyBase>(Property); Object && Name==TEXT("newparam") && Data.TargetComponent->IsA(Object->PropertyClass))
				Object->SetObjectPropertyValue(Value,Data.TargetComponent);
			else if (auto* Vector=CastField<FStructProperty>(Property); Vector && Vector->Struct==TBaseStructure<FVector>::Get()
				&& (Name==TEXT("hitposition") || Name==TEXT("hitnormal")))
				*static_cast<FVector*>(Value)=Name==TEXT("hitposition") ? FVector(Data.Hit.ImpactPoint) : FVector(Data.Hit.ImpactNormal);
			else if (auto* Number=CastField<FNumericProperty>(Property); Number && Number->IsFloatingPoint() && Name==TEXT("force"))
				Number->SetFloatingPointPropertyValue(Value,Data.Impulse.Size());
			else return false;
		}
		Receiver->ProcessEvent(Function,Parameters.GetStructMemory());
		return true;
	}
}

bool UWorldInteractionHitLibrary::SendWorldHitToObject(UObject* Receiver,const FWorldInteractionHit& Data)
{
	if (!IsValid(Receiver) || !ValidWorldHit(Data)) return false;
	if (Receiver->GetClass()->ImplementsInterface(UWorldInteractionHitInterface::StaticClass()))
	{
		IWorldInteractionHitInterface::Execute_ReceiveWorldHit(Receiver,Data);
		return true;
	}
	return SendLegacyHit(Receiver,Data);
}

bool UWorldInteractionHitLibrary::DispatchWorldHit(const FWorldInteractionHit& Data)
{
	if (!ValidWorldHit(Data)) return false;
	// A shared ISM actor represents many objects. The component resolves the one stable identity before notifying its ASM.
	if (Data.TargetComponent->IsA<UInteractableISMComponent>()) return SendWorldHitToObject(Data.TargetComponent,Data);
	if (const auto* Piece=Cast<UPropAssemblyMesh>(Data.TargetComponent))
		if (auto* Manager=APropManager::Get(Data.TargetActor)) return Manager->ReceiveWorldHitByISID(Piece->ISID,Data);
	bool Delivered=SendWorldHitToObject(Data.TargetComponent,Data);
	Delivered=SendWorldHitToObject(Data.TargetActor,Data) || Delivered;
	TInlineComponentArray<UActorComponent*> Components(Data.TargetActor);
	for (auto* Component:Components)
		if (IsValid(Data.TargetActor) && IsValid(Component) && Component!=Data.TargetComponent
			&& Component->GetClass()->ImplementsInterface(UWorldInteractionHitInterface::StaticClass()))
			Delivered=SendWorldHitToObject(Component,Data) || Delivered;
	return Delivered;
}

bool UWorldInteractionHitLibrary::ApplyBoundedWorldHitImpulse(UPrimitiveComponent* Target,const FWorldInteractionHit& Data,float Scale,float MaxSpeedChange)
{
	if (!Data.bApplyHitForce || !IsValid(Target) || !Target->GetOwner() || !Target->GetOwner()->HasAuthority() || Target->GetNetMode()==NM_Client
		|| !Target->IsSimulatingPhysics() || Data.Impulse.ContainsNaN() || !FMath::IsFinite(Scale) || !FMath::IsFinite(MaxSpeedChange) || Scale<=0 || MaxSpeedChange<=0) return false;
	const float Mass=Target->GetMass();
	if (!FMath::IsFinite(Mass) || Mass<=0) return false;
	const FVector Impulse=(Data.Impulse*Scale).GetClampedToMaxSize(Mass*MaxSpeedChange);
	if (Impulse.ContainsNaN() || Impulse.IsNearlyZero()) return false;
	Target->AddImpulse(Impulse,NAME_None,false);
	return true;
}
