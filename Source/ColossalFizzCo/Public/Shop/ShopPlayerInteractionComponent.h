#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Components/CableFeatureTypes.h"
#include "ShopPlayerInteractionComponent.generated.h"

class UShopScannerComponent;
class UShopATMComponent;
class UShopTerminalWidget;
class SWidget;
class ULocalPlayer;
class UCollisionCableComponent;
class UHoseSocketComponent;
class AHoseCableActor;
class APressurizedColaContainer;
class APawn;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EShopScannerAction : uint8 { Save, RemoveItem, Clear, LockBill, Complete, CashCompensation, ProvideReplacement };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShopPlayerActionResult, int64, Sequence, EShopCheckoutResult, Result);

/** Owned-player RPC boundary. Shared stations never accept client-specified prices, totals or customer IDs. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UShopPlayerInteractionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopPlayerInteractionComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop") float MaxInteractionDistance = 350;
	UPROPERTY(BlueprintAssignable, Category = "Shop") FOnShopPlayerActionResult OnActionResult;
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void RequestScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ItemISID, FGuid CaseID);
	UFUNCTION(BlueprintCallable, Category = "Shop") void RequestATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Denominations);
	UFUNCTION(BlueprintCallable, Category = "Shop") void OpenTerminal(AActor* Station);
	/** Local E interaction: trace from the real camera using the station reach budget. */
	UFUNCTION(BlueprintCallable, Category="Shop") bool TryOpenTerminalFromView();
	/** Shared query used by the input path and collision/reach regression tests. */
	AActor* FindTerminalInView(const FVector& ViewLocation, const FVector& ViewDirection, FHitResult* StationHit=nullptr) const;
	UFUNCTION(BlueprintPure, Category="Shop") bool IsTerminalOpen() const;
	/** Use the selected cable's Default Grab Settings. Disable to configure a stronger/weaker participant here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cable|Grabbing") bool bUseCableDefaultGrabSettings = true;
	/** Server-owned participant settings; disable Use Cable Default Grab Settings and set Movement Rate to 15 cm/s for a vehicle, for example. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cable|Grabbing", meta=(EditCondition="!bUseCableDefaultGrabSettings")) FCableGrabSettings CableGrabSettings;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cable|Grabbing", meta=(ClampMin="1", Units="cm")) float CableGrabTraceRadius = 15;
	UFUNCTION(BlueprintCallable, Category="Cable|Grabbing") bool TryToggleCableGrabFromView();
	UFUNCTION(BlueprintCallable, Category="Cable|Grabbing") bool RequestCableGrab(UCollisionCableComponent* Cable, FVector HitPosition, FVector Target);
	UFUNCTION(BlueprintCallable, Category="Cable|Grabbing") void RequestCableRelease();
	void RequestCableDrop();
	UFUNCTION(BlueprintPure, Category="Cable|Grabbing") bool IsCableGrabActive() const;
	UCollisionCableComponent* GetGrabbedCable() const { return LocalCable.Get(); }
	bool ExecuteCableGrab(UCollisionCableComponent* Cable, FVector HitPosition, FVector Target, int64 Sequence,
		int32 ConnectorIndex = INDEX_NONE, bool bAnimateConnector = false);
	bool TryConnectSupplyHoseFromView();
	bool TryGrabSocketHoseFromView(bool bTakeInHand, bool bAnimateConnector = false);
	bool TryGrabLooseHoseFromView(bool bAnimateConnector = false);
	bool TryGrabCableOnHoldFromView();
	bool FinishHosePickupPress();
	bool ExecuteConnectSupplyHose(UCollisionCableComponent* Cable,class AManualColaFillingMachine* Machine,int32 PortIndex,int64 Sequence);
	bool ExecuteConnectResourceHose(UCollisionCableComponent* Cable,AActor* SocketActor,int64 Sequence);
	/** Clears the authoritative hand after an interaction takes the item. */
	void CompleteCarryHandoff(AActor* TakenActor, UPrimitiveComponent* TakenComponent = nullptr);
	UFUNCTION(BlueprintCallable,Category="Cola Container") bool TryTurnColaContainerValveFromView(bool bOpen=true);
	bool ExecuteColaContainerInteraction(APressurizedColaContainer* Container,UPrimitiveComponent* Control);
	bool ExecuteTurnColaContainerValve(class APressurizedColaContainer* Container,bool bOpen,int64 Sequence);
	virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
	UFUNCTION(BlueprintCallable, Category="Shop") void RequestSupplyPurchase(UShopProductionStationComponent* Station, FGuid SessionID, FName OfferID, int32 Quantity=1);
	UFUNCTION(BlueprintCallable, Category="Shop") void RequestProductionAction(UShopProductionStationComponent* Station, FGuid SessionID, EShopProductionAction Action, int32 BottleISID, int32 OtherISID=0);
	EShopCheckoutResult ExecuteSupplyPurchase(UShopProductionStationComponent* Station, FGuid SessionID, FName OfferID, int32 Quantity, FGuid OperationID, int64 Sequence);
	EShopCheckoutResult ExecuteProductionAction(UShopProductionStationComponent* Station, FGuid SessionID, EShopProductionAction Action, int32 BottleISID, int32 OtherISID, int64 Sequence);
	void ShowProductionResult(EShopCheckoutResult Result);
	void ClearProductionNotice();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool IsStationReachable(const AActor* Station) const { return CanReach(Station); }
	EShopCheckoutResult ExecuteATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Denominations, FGuid OperationID, int64 Sequence);
	/** Server/test entry uses the same access, ordering, session and case checks as the owned RPC. */
	EShopCheckoutResult ExecuteScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ItemISID, FGuid CaseID, int64 Sequence);
private:
	int64 LocalSequence = 0;
	int64 LastServerSequence = 0;
	UFUNCTION(Server, Reliable) void Server_ScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ItemISID, FGuid CaseID, int64 Sequence);
	UFUNCTION(Client, Reliable) void Client_ActionResult(int64 Sequence, EShopCheckoutResult Result);
	UFUNCTION(Server, Reliable) void Server_ATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Denominations, FGuid OperationID, int64 Sequence);
	UFUNCTION(Server, Reliable) void Server_SupplyPurchase(UShopProductionStationComponent* Station, FGuid SessionID, FName OfferID, int32 Quantity, FGuid OperationID, int64 Sequence);
	UFUNCTION(Server, Reliable) void Server_ProductionAction(UShopProductionStationComponent* Station, FGuid SessionID, EShopProductionAction Action, int32 BottleISID, int32 OtherISID, int64 Sequence);
	UFUNCTION(Client, Reliable) void Client_OpenTerminal(AActor* Station);
	bool TryUseSocketMachineControl(APawn& Pawn, const FHitResult& Hit);
	UFUNCTION(Server, Reliable) void Server_UseSocketMachineControl(int32 MachineISID, FName ControlName);
	UPROPERTY(Transient) TObjectPtr<UShopTerminalWidget> TerminalWidget;
	bool CanReach(const AActor* Station) const;
	TSharedPtr<SWidget> ProductionNotice;
	TWeakObjectPtr<ULocalPlayer> NoticePlayer;
	FTimerHandle NoticeTimer;
	TWeakObjectPtr<UCollisionCableComponent> LocalCable;
	TWeakObjectPtr<UCollisionCableComponent> ServerCable;
	FVector LocalCableViewOffset = FVector::ZeroVector;
	float CableTargetAccumulator = 0;
	float LastCableTargetTime = -1;
	int64 LocalCableSequence = 0;
	int64 LastCableSequence = 0;
	int64 LocalCableGrabSequence = 0;
	int64 LocalHosePickupPress = 0;
	int64 ServerHosePickupSequence = 0;
	void CleanupCableGrab();
	void SubmitCableRelease(bool bToss);
	void ReleaseServerCable(AActor* Participant, bool bToss = false);
	bool GrabCableFromView(bool bLooseHosesOnly, bool bAnimateConnector = false);
	bool RequestCableGrabFromHit(UCollisionCableComponent* Cable, FVector Hit, FVector Target, int32 ConnectorIndex,
		bool bAnimateConnector = false);
	bool BeginHoseGrip(AHoseCableActor& Hose, int32 Point, AActor* Participant, int64 Sequence, bool bAnimateConnector);
	void UpdateHosePickup();
	UFUNCTION(Server,Reliable) void Server_CableGrab(UCollisionCableComponent* Cable,FVector_NetQuantize10 HitPosition,FVector_NetQuantize10 Target,
		int64 Sequence,int32 ConnectorIndex,bool bAnimateConnector);
	UFUNCTION(Server,Unreliable) void Server_CableTarget(UCollisionCableComponent* Cable,FVector_NetQuantize10 Target,int64 Sequence);
	UFUNCTION(Server,Reliable) void Server_CableRelease(int64 Sequence, bool bToss);
	UFUNCTION(Server,Reliable) void Server_EndHosePickup(int64 GrabSequence);
	UFUNCTION(Client,Reliable) void Client_CableGrabResult(UCollisionCableComponent* Cable,bool bAccepted,int64 GrabSequence = 0);
	UFUNCTION(Server,Reliable) void Server_ConnectSupplyHose(UCollisionCableComponent* Cable,class AManualColaFillingMachine* Machine,int32 PortIndex,int64 Sequence);
	UFUNCTION(Server,Reliable) void Server_ConnectResourceHose(UCollisionCableComponent* Cable,AActor* SocketActor,int64 Sequence);
	UFUNCTION(Server,Reliable) void Server_ConnectHoseSocket(UCollisionCableComponent* Cable, UHoseSocketComponent* Socket, int64 Sequence);
	bool ExecuteConnectHoseSocket(UCollisionCableComponent* Cable, UHoseSocketComponent* Socket, int64 Sequence);
	UHoseSocketComponent* FindHoseSocketInView() const;
	bool BeginSocketUnplugFromView();
	void UpdateSocketUnplugHold();
	void CancelSocketUnplugHold();
	bool RemoveSocketHoseFromView(UHoseSocketComponent* Socket, AHoseCableActor* Hose, bool bGrab, bool bAnimateConnector = false);
	bool ExecuteRemoveSocketHose(UHoseSocketComponent* Socket, AHoseCableActor* ExpectedHose, bool bGrab, int64 Sequence, bool bAnimateConnector);
	UFUNCTION(Server,Reliable) void Server_RemoveSocketHose(UHoseSocketComponent* Socket, AHoseCableActor* ExpectedHose, bool bGrab, int64 Sequence, bool bAnimateConnector);
	FTimerHandle SocketUnplugTimer;
	TWeakObjectPtr<UHoseSocketComponent> PendingUnplugSocket;
	TWeakObjectPtr<AHoseCableActor> PendingUnplugHose;
	UPrimitiveComponent* FindColaContainerWorldControl(const APawn& Pawn) const;
	bool RequestTurnColaContainerValve(APressurizedColaContainer* Container,bool bOpen);
	bool TurnColaContainerValve(APressurizedColaContainer* Container,const UPrimitiveComponent* Control,bool bOpen);
	bool CanUseColaContainerControl(const UPrimitiveComponent* Control,const AActor* HeldObject=nullptr) const;
	UFUNCTION(Server,Reliable) void Server_TurnColaContainerValve(class APressurizedColaContainer* Container,bool bOpen,int64 Sequence);
	int64 LocalValveSequence=0;
	int64 LastValveSequence=0;
	double LastValveInteractionTime=-1;
};
