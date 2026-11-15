#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Props/InteractablePropDatabase.h"
#include "PropDeliveryChannel.generated.h"

class APropManager;

UCLASS(NotPlaceable, Transient)
class COLOSSALFIZZCO_API APropDeliveryChannel : public AActor
{
	GENERATED_BODY()
public:
	APropDeliveryChannel();
	void Start(APropManager* Source);
	void Pump();
	void QueueProduct(int32 ID) { Products.Add(ID); }
	void QueueContent(int32 ID) { Contents.Add(ID); }
	void QueueRecord(int32 ID) { if (!InitialRecords.Contains(ID)) Records.Add(ID); }
	void QueueRemoval(int32 ID, bool bDestroy);
	UFUNCTION(Server, Reliable) void RequestPromotion(const TArray<int32>& IDs, EPropState State, EPropActivationReason Reason, bool bSimulate);
	bool IsSnapshotReady() const { return bReady; }
	static constexpr int32 PayloadLimit = 896;
	static constexpr int32 Window = 8;

private:
	TWeakObjectPtr<APropManager> Manager;
	TSet<int32> Products, Contents, InitialRecords, Records, Released, Deleted;
	TArray<uint8> DefinitionBytes, ReceivedBytes;
	int32 Offset = 0, Sent = 0, Acked = 0, FinalSequence = 0;
	bool bProduct = false, bFirst = true, bReady = false;
	bool bReceivingProduct = false;
	UFUNCTION(Client, Reliable) void ReceiveDefinition(APropManager* Source, int32 Sequence, bool IsProduct, bool First, bool Last, const TArray<uint8>& Bytes);
	UFUNCTION(Client, Reliable) void ReceiveRecords(APropManager* Source, int32 Sequence, const FPropBulkUpdateBatch& Batch);
	UFUNCTION(Server, Reliable) void Confirm(int32 Sequence);
};
