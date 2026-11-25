#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/FillingMachineUpgradeTypes.h"
#include "Shop/FlowControllerUpgradeTypes.h"
#include "Shop/ShopCheckoutTypes.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopTableOfferComponent.h"
#include "Components/CableFeatureTypes.h"
#include "ShopPlayerInteractionComponent.generated.h"

class UShopScannerComponent;
class UShopATMComponent;
class UShopTerminalWidget;
class UShopBalanceWidget;
class UShopBottleDispenserWidget;
class SWidget;
class ULocalPlayer;
class UCollisionCableComponent;
class UHoseSocketComponent;
class AHoseCableActor;
class APressurizedColaContainer;
class AColaFlowController;
class ASocketColaFillingMachine;
class AShopMachinePlacementPreview;
class AShopBottleDispenser;
class AShopTableUpgradeTerminal;
class UShopTablePurchaseWidget;
class AController;
class APlayerController;
class APawn;
class UPrimitiveComponent;
class UInputComponent;
class UEnhancedInputComponent;
class UInputMappingContext;

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
	UFUNCTION(BlueprintCallable, Category = "Shop") void OpenPlayerUpgrades();
	/** Local E interaction: trace from the real camera using the station reach budget. */
	UFUNCTION(BlueprintCallable, Category="Shop") bool TryOpenTerminalFromView();
	UFUNCTION(BlueprintCallable, Category="Flow Controller") void RequestFlowControllerPause(bool bPaused);
	bool RequestFlowControllerUpgrade(AColaFlowController* Flow, EFlowControllerUpgradeType Upgrade, int32 ExpectedRank, FGuid OperationID);
	bool RequestFillingMachineUpgrade(ASocketColaFillingMachine* Table, EFillingMachineUpgradeType Upgrade, int32 ExpectedRank, FGuid OperationID);
	/** Returns a reachable station and optionally reports the first blocking hit for passive readouts. */
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
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
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
	bool OpenTableUpgradePicker(APlayerController& Controller, AShopTableUpgradeTerminal& Station);
	void CloseTableUpgradePicker();
	bool IsTableUpgradePickerOpen() const;
	bool RequestTablePurchase(FName OfferID, int64 ExpectedPriceCents, FGuid OperationID);
	EShopTablePurchaseResult ExecuteTablePurchase(AShopTableUpgradeTerminal* Station, FName OfferID, int64 ExpectedPriceCents);
	UFUNCTION(Server, Reliable) void Server_PurchaseTable(AShopTableUpgradeTerminal* Station, FName OfferID, int64 ExpectedPriceCents, FGuid OperationID);
	UFUNCTION(Client, Reliable) void Client_TablePurchaseResult(FGuid OperationID, EShopTablePurchaseResult Result);
	bool TryUseSocketMachineControl(APawn& Pawn, const FHitResult& Hit);
	UFUNCTION(Server, Reliable) void Server_UseSocketMachineControl(int32 MachineISID, FName ControlName);
	AShopBottleDispenser* FindAimedBottleDispenser(const APawn& Pawn, const FHitResult& Hit, const FVector& Eye,
		const FVector& Direction, bool& bAimingAtLever) const;
	UFUNCTION(Server, Reliable) void Server_UseBottleDispenser(AShopBottleDispenser* Dispenser);
	UFUNCTION(Server, Reliable) void Server_UseFlowSelector(AColaFlowController* FlowController);
	UFUNCTION(Server, Reliable) void Server_SetFlowLever(AColaFlowController* FlowController, bool bPressed);
	UFUNCTION(Server, Reliable) void Server_SetFlowPaused(AColaFlowController* FlowController, bool bPaused);
	UFUNCTION(Server, Reliable) void Server_UpgradeFlowController(AColaFlowController* FlowController, EFlowControllerUpgradeType Upgrade, int32 ExpectedRank, FGuid OperationID);
	UFUNCTION(Client, Reliable) void Client_FlowControllerUpgradeResult(AColaFlowController* FlowController, FGuid OperationID, EFlowControllerPurchaseResult Result);
	UFUNCTION(Server, Reliable) void Server_UpgradeFillingMachine(ASocketColaFillingMachine* Table, EFillingMachineUpgradeType Upgrade, int32 ExpectedRank, FGuid OperationID);
	UFUNCTION(Client, Reliable) void Client_FillingMachineUpgradeResult(FGuid OperationID, EFillingMachinePurchaseResult Result);
	UPROPERTY(Transient, Replicated) TObjectPtr<AColaFlowController> ActiveFlowLever;
	TWeakObjectPtr<AColaFlowController> PressedFlowLever;
	UPROPERTY(Transient) TObjectPtr<UInputComponent> FlowControlInput;
	AColaFlowController* GetLocalFlowLever() const;
	bool BeginFlowLeverInput(APawn& Pawn, AColaFlowController& FlowController);
	void EndFlowLeverInput();
	void CleanupFlowLeverInput();
	void UpdateStationReadoutsFromView();
	void OpenAimedFillingMachineUpgrades();
	void EnsureShopBalanceWidget(APlayerController& PC);
	void UpdateBottleDispenserReadout(APlayerController& PC, const AShopBottleDispenser* Dispenser, bool bAimingAtLever);
	void CloseBottleDispenserReadout();
	void ShowTerminal(AActor* Station, bool bManualOpen, bool bOpenUpgrades=false);
	void CloseFlowReadout();
	void CloseFillingTableReadout();
	FTimerHandle StationReadoutTimer;
	TWeakObjectPtr<ASocketColaFillingMachine> PinnedFillingTable;
	TWeakObjectPtr<ASocketColaFillingMachine> DismissedFillingTable;
	UPROPERTY(Transient) TObjectPtr<UShopTerminalWidget> FlowReadoutWidget;
	UPROPERTY(Transient) TObjectPtr<UShopTerminalWidget> TerminalWidget;
	UPROPERTY(Transient) TObjectPtr<UShopTablePurchaseWidget> TableUpgradePicker;
	TWeakObjectPtr<AShopTableUpgradeTerminal> TableUpgradeTerminal;
	UPROPERTY(Transient) TObjectPtr<UShopBalanceWidget> ShopBalanceWidget;
	UPROPERTY(Transient) TObjectPtr<UShopBottleDispenserWidget> BottleDispenserWidget;
	UPROPERTY(Transient) TObjectPtr<UInputComponent> FillingTableInput;
	UFUNCTION() void OnFillingPawnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
	void BindFillingTableInput(APlayerController* PC);
	void UnbindFillingTableInput();
	APlayerController* GetMachinePlacementController() const;
	AActor* FindMachineForPlacement(const APlayerController& Controller) const;
	void PressMachinePlacementKey();
	void ReleaseMachinePlacementKey();
	void ClearMachinePlacementHold();
	void UpdateMachinePlacementHold();
	void UpdateMachinePlacementPreview();
	void CommitMachinePlacementInput();
	void CancelMachinePlacementInput();
	void StartMachinePlacementRotation();
	void StopMachinePlacementRotation();
	bool BindMachinePlacementInput(APlayerController& Controller);
	void CloseMachinePlacementPreview();
	void ReleaseMachinePlacementInputWhenReady();
	void UnbindMachinePlacementInput();
	void CleanupMachinePlacement();
	UFUNCTION(Server, Reliable) void Server_BeginMachinePlacement(AActor* Machine, int64 Sequence);
	UFUNCTION(Server, Reliable) void Server_FinishMachinePlacement(AActor* Machine, int64 Sequence, FTransform Pose);
	UFUNCTION(Server, Reliable) void Server_CancelMachinePlacement(AActor* Machine, int64 Sequence);
	UFUNCTION(Client, Reliable) void Client_MachinePlacementState(AActor* Machine, int64 Sequence, bool bActive);
	FTimerHandle MachinePlacementHoldTimer;
	FTimerHandle MachinePlacementPreviewTimer;
	FTimerHandle MachinePlacementInputReleaseTimer;
	TWeakObjectPtr<AActor> PendingPlacementMachine;
	TWeakObjectPtr<AActor> LocalPlacementMachine;
	TWeakObjectPtr<AActor> ServerPlacementMachine;
	TWeakObjectPtr<APawn> ServerPlacementPawn;
	UPROPERTY(Transient) TObjectPtr<AShopMachinePlacementPreview> MachinePlacementPreview;
	UPROPERTY(Transient) TObjectPtr<UEnhancedInputComponent> MachinePlacementInput;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> MachinePlacementMapping;
	int64 LocalPlacementSequence = 0;
	int64 ServerPlacementSequence = 0;
	double MachinePlacementHoldTime = 0;
	float MachinePlacementYaw = 0;
	bool bMachinePlacementKeyDown = false;
	bool bMachinePlacementRotating = false;
	bool bMachinePlacementCommitPending = false;
	friend class FShopTerminalViewTest;
	bool IsFillingTableReachable(const ASocketColaFillingMachine* Table) const;
	ASocketColaFillingMachine* FindAimedFillingTable(const FHitResult& Hit) const;
	ASocketColaFillingMachine* FindNearbyFillingTable(bool bPreferAimedCapInput=false) const;
	void ToggleFillingTableReadout();
	void UseFillingTableControl(EShopProductionAction Action);
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
