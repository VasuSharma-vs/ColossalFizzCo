#include "Props/PropDeliveryChannel.h"
#include "Props/InteractablePropManager.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerController.h"

namespace
{
	template<typename T> void WriteDefinition(T& Definition, TArray<uint8>& Bytes)
	{
		Bytes.Reset();
		FMemoryWriter Writer(Bytes);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		T Defaults;
		T::StaticStruct()->SerializeTaggedProperties(Archive, reinterpret_cast<uint8*>(&Definition), T::StaticStruct(), reinterpret_cast<const uint8*>(&Defaults));
	}

	template<typename T> bool ReadDefinition(TArray<uint8>& Bytes, T& Definition)
	{
		FMemoryReader Reader(Bytes);
		FObjectAndNameAsStringProxyArchive Archive(Reader, true);
		T Defaults;
		T::StaticStruct()->SerializeTaggedProperties(Archive, reinterpret_cast<uint8*>(&Definition), T::StaticStruct(), reinterpret_cast<const uint8*>(&Defaults));
		return !Archive.IsError() && Reader.AtEnd();
	}

	int32 Take(TSet<int32>& IDs)
	{
		auto It = IDs.CreateIterator();
		const int32 ID = *It;
		It.RemoveCurrent();
		return ID;
	}
}

APropDeliveryChannel::APropDeliveryChannel()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;
}

void APropDeliveryChannel::Start(APropManager* Source)
{
	Manager = Source;
	Contents.Reset(); Products.Reset(); InitialRecords.Reset(); Records.Reset(); Deleted.Reset(); Released.Reset();
	for (const auto& Pair : Source->ContentDefinitions) Contents.Add(Pair.Key);
	for (const auto& Pair : Source->ProductDefinitions) Products.Add(Pair.Key);
	for (const auto& Pair : Source->ISIDData) InitialRecords.Add(Pair.Key);
	DefinitionBytes.Reset();
	Offset = 0;
	bFirst = true;
	bReady = false;
	FinalSequence = 0;
}

void APropDeliveryChannel::QueueRemoval(int32 ID, bool bDestroy)
{
	InitialRecords.Remove(ID);
	Records.Remove(ID);
	(bDestroy ? Deleted : Released).Add(ID);
}

void APropDeliveryChannel::Pump()
{
	APropManager* Source = Manager.Get();
	UNetConnection* Connection = GetNetConnection();
	if (!Source || !Connection || !Connection->IsNetReady()) return;
	// Never fill a reliable queue while a slow peer is catching up.
	for (int32 Packet = 0; Packet < 2 && Sent - Acked < Window; ++Packet)
	{
		if (Offset == DefinitionBytes.Num())
		{
			DefinitionBytes.Reset(); Offset = 0;
			if (!Contents.IsEmpty())
			{
				bProduct = false;
				if (auto* Def = Source->ContentDefinitions.Find(Take(Contents))) WriteDefinition(*Def, DefinitionBytes);
			}
			else if (!Products.IsEmpty())
			{
				bProduct = true;
				if (auto* Def = Source->ProductDefinitions.Find(Take(Products)))
				{
					// Shared definitions commonly have many zero/default fields. Send a compact
					// delta against struct defaults. Asset paths stay package-stable across peers.
					WriteDefinition(*Def, DefinitionBytes);
				}
			}
		}
		if (Offset < DefinitionBytes.Num())
		{
			const int32 Size = FMath::Min(PayloadLimit, DefinitionBytes.Num() - Offset);
			TArray<uint8> Part(DefinitionBytes.GetData() + Offset, Size);
			ReceiveDefinition(Source, ++Sent, bProduct, Offset == 0, Offset + Size == DefinitionBytes.Num(), Part);
			Offset += Size;
			continue;
		}
		if (Records.IsEmpty() && Released.IsEmpty() && Deleted.IsEmpty() && FinalSequence) break;
		FPropBulkUpdateBatch Batch;
		Batch.bIsFirstInitBatch = bFirst;
		Batch.bIsFullSnapshot = !FinalSequence;
		Batch.SnapshotWorldElapsedSeconds = Source->WorldElapsedSeconds;
		// FPropInstanceDelta contains fixed-size instance fields. Never include definitions here.
		const int32 Limit = FMath::Clamp(Source->MaxBytesPerFrame / int32(sizeof(FPropInstanceDelta)), 1, 2);
		while (!Deleted.IsEmpty() && Batch.DeletedISIDs.Num() < 32) Batch.DeletedISIDs.Add(Take(Deleted));
		while (!Released.IsEmpty() && Batch.ReleasedToEmptyISIDs.Num() < 32) Batch.ReleasedToEmptyISIDs.Add(Take(Released));
		auto& Pending = FinalSequence ? Records : InitialRecords;
		while (!Pending.IsEmpty() && Batch.Records.Num() < Limit)
		{
			if (const auto* Record = Source->FindISIDData(Take(Pending)))
				Batch.Records.Add(Source->BuildDeltaForRecord(*Record));
		}
		Batch.BatchSequence = ++Sent;
		if (!FinalSequence && Products.IsEmpty() && Contents.IsEmpty() && InitialRecords.IsEmpty() && Deleted.IsEmpty() && Released.IsEmpty())
		{
			FinalSequence = Sent;
			Batch.bIsFinalInitBatch = true;
		}
		bFirst = false;
		ReceiveRecords(Source, Sent, Batch);
	}
}

void APropDeliveryChannel::ReceiveDefinition_Implementation(APropManager* Source, int32 Sequence, bool IsProduct, bool First, bool Last, const TArray<uint8>& Bytes)
{
	if (!Source || Bytes.IsEmpty() || Bytes.Num() > PayloadLimit) return;
	Source->LocalDeliveryChannel = this;
	if (First) { ReceivedBytes.Reset(); bReceivingProduct = IsProduct; }
	if (IsProduct != bReceivingProduct) return;
	ReceivedBytes.Append(Bytes);
	if (Last)
	{
		if (IsProduct)
		{
			FPropProductDefinition Def;
			if (!ReadDefinition(ReceivedBytes, Def) || !Source->RegisterProductDefinition(Def)) return;
		}
		else
		{
			FPropContentDefinition Def;
			if (!ReadDefinition(ReceivedBytes, Def) || !Def.IsValidDefinition()) return;
			Source->StoreContentDefinition(MoveTemp(Def));
		}
		ReceivedBytes.Reset();
	}
	Confirm(Sequence);
}

void APropDeliveryChannel::ReceiveRecords_Implementation(APropManager* Source, int32 Sequence, const FPropBulkUpdateBatch& Batch)
{
	if (!Source) return;
	Source->LocalDeliveryChannel = this;
	for (int32 ID : Batch.ReleasedToEmptyISIDs) Source->ReleaseInstanceToEmpty(ID);
	for (int32 ID : Batch.DeletedISIDs) Source->DestroyPropInstance(ID);
	Source->ApplyPropInitializationBatch(Batch);
	Confirm(Sequence);
}

void APropDeliveryChannel::Confirm_Implementation(int32 Sequence)
{
	if (Sequence <= Acked || Sequence > Sent) return;
	Acked = Sequence;
	if (FinalSequence && Acked >= FinalSequence && !bReady)
	{
		bReady = true;
		UE_LOG(LogTemp, Display, TEXT("Prop snapshot acknowledged by %s through sequence %d (window %d, definition payload %d)"),
			*GetNameSafe(GetOwner()), FinalSequence, Window, PayloadLimit);
	}
}

void APropDeliveryChannel::RequestPromotion_Implementation(const TArray<int32>& IDs, EPropState State, EPropActivationReason Reason, bool bSimulate)
{
	if (auto* Source = Manager.Get(); Source && IDs.Num() > 0 && IDs.Num() <= 32
		&& StaticEnum<EPropState>()->IsValidEnumValue(static_cast<int64>(State))
		&& StaticEnum<EPropActivationReason>()->IsValidEnumValue(static_cast<int64>(Reason)))
		Source->BatchMakeISMToASM(IDs, State, Reason, bSimulate);
}
