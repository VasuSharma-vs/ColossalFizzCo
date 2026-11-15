#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopProductionStationComponent.generated.h"

class UBoxComponent;
class USceneComponent;
class UBottleFillingComponent;

UENUM(BlueprintType)
enum class EShopProductionAction : uint8 { Fill, StopFilling, AttachCap, CloseCap, OpenCap, HoldCapRole, RotateCapRole, PressCap, TurnCap, LeaveCapRole, ConnectSupply, AlignBottle, HoldFillingLever, ReleaseFillingLever, ApplyTemporarySeal };

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopProductionItem
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 ISID=0;
	UPROPERTY(BlueprintReadOnly) FText Name;
	UPROPERTY(BlueprintReadOnly) float ContentLevel=0;
	UPROPERTY(BlueprintReadOnly) float CapSealed=0;
	UPROPERTY(BlueprintReadOnly) int32 AttachedCapISID=0;
	UPROPERTY(BlueprintReadOnly) float ResidualPressure=0;
	UPROPERTY(BlueprintReadOnly) float TemporarySealSeconds=0;
	UPROPERTY(BlueprintReadOnly) bool bTemporarySealUsed=false;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopProductionView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid SessionID;
	UPROPERTY(BlueprintReadOnly) TArray<FShopProductionItem> Bottles;
	UPROPERTY(BlueprintReadOnly) TArray<FShopProductionItem> Cylinders;
	UPROPERTY(BlueprintReadOnly) TArray<FShopProductionItem> Caps;
	UPROPERTY(BlueprintReadOnly) bool bFilling=false;
	UPROPERTY(BlueprintReadOnly) int32 SeatedBottleISID=0;
	UPROPERTY(BlueprintReadOnly) int32 ConnectedSourceISID=0;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> LeverOperator;
	UPROPERTY(BlueprintReadOnly) int32 ManualBottleISID=0;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> CapHolder;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> CapRotator;
	UPROPERTY(BlueprintReadOnly) float HoldingForce=0;
	UPROPERTY(BlueprintReadOnly) float HoldingEfficiency=1;
	UPROPERTY(BlueprintReadOnly) float RotationProgress=0;
};

/** Thin equipment coordinator. Prop Manager owns every product, liquid and cap mutation. */
UCLASS(ClassGroup=(Shop),meta=(BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UShopProductionStationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopProductionStationComponent();
	/** Server-only identities from the most recent successful supply delivery at this station. */
	TArray<int32> LastDeliveredSupplies;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station") TObjectPtr<UBoxComponent> BottleInput;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station") TObjectPtr<UBoxComponent> CylinderInput;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station") TObjectPtr<UBoxComponent> CapInput;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station") TObjectPtr<USceneComponent> SupplyOutput;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station") TObjectPtr<UBottleFillingComponent> Filler;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station") bool bOffersSupplies=true;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Filling") bool bManualLeverMode=false;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Filling") bool bAllowSoloCap=false;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station|Manual Filling") TObjectPtr<USceneComponent> BottleHolder;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station|Manual Filling",meta=(ClampMin="0.1")) float TemporarySealDuration=3.f;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station|Manual Filling",meta=(ClampMin="0")) float SafeResidualPressure=.1f;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station|Manual Filling",meta=(ClampMin="0.1")) float LeverLeaseSeconds=.4f;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Station|Manual Filling",meta=(ClampMin="0.1")) float FillPressurePerLevel=.6f;
	bool IsBottleAligned(int32 BottleISID) const;
	void UpdateManualEquipment();
	void ResetEquipmentSession();
	void ReleaseSeatedBottle();
	EShopCheckoutResult ManualEquipmentAction(EShopProductionAction Action,int32 BottleISID,int32 OtherISID,AActor* Player);
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap") bool bRequiresCooperativeCap=false;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap",meta=(ClampMin="0.01")) float HoldingForcePerPress=1.25f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap",meta=(ClampMin="0.01")) float MaximumHoldingForce=2.5f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap",meta=(ClampMin="0.01")) float HoldingForceDecayPerSecond=1.f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap",meta=(ClampMin="0.001")) float HoldingEfficiencyDecayPerSecond=.025f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap",meta=(ClampMin="0.01",ClampMax="1")) float RotationPerTurn=.1f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap",meta=(ClampMin="0.02")) float MinimumInputInterval=.08f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Station|Manual Cap",meta=(ClampMin="0")) float LeakageLevelPerSecond=.05f;
	UPROPERTY(Replicated,BlueprintReadOnly,Category="Station") FShopProductionView View;
	EShopCheckoutResult Purchase(FGuid Session, FGuid OperationID, FName Offer, int32 Quantity);
	EShopCheckoutResult Execute(FGuid Session, EShopProductionAction Action, int32 BottleISID, int32 OtherISID);
	EShopCheckoutResult ExecuteForPlayer(FGuid Session, EShopProductionAction Action, int32 BottleISID, int32 OtherISID,AActor* Player);
	void RefreshInputs();
	void UpdateManualCapping(float DeltaSeconds);
	bool HasSufficientCapHold(int32 BottleISID) const;
private:
	FTimerHandle RefreshTimer;
	FGuid LiquidReleaseEpisode;
	double LastManualUpdate=-1, LastHoldingPress=-1, LastRotationTurn=-1;
	void ResetManualCapping();
	float RequiredHoldingForce(int32 BottleISID) const;
	bool OperatorInRange(const AActor* Player) const;
	EShopCheckoutResult ManualCapAction(EShopProductionAction Action,int32 BottleISID,AActor* Player);
};
