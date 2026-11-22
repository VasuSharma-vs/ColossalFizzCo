#pragma once

#include "CoreMinimal.h"
#include "ResourceTransferTypes.generated.h"

/** A connector transports exactly one resource category; categories are never interchangeable. */
UENUM(BlueprintType)
enum class EResourceTransportCategory : uint8
{
	Content,
	Fuel,
	Electricity
};

UENUM(BlueprintType)
enum class EHoseFailureMode : uint8
{
	None,
	StartConnectorPopped,
	EndConnectorPopped,
	OverpressureBurst,
	VacuumCollapse
};

UENUM(BlueprintType)
enum class EHoseDischargePoint : uint8
{
	None,
	StartTip,
	EndTip,
	Rupture
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FHoseDischargeState
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) EHoseDischargePoint Point=EHoseDischargePoint::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float UnitsPerSecond=0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Pressure=0.f;
};

/** Actual resources trapped inside a hose, including managed liquid provenance. */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FHoseResourceBuffer
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame) EResourceTransportCategory Category=EResourceTransportCategory::Content;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame) int32 ResourceID=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame) float Units=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame) float ContentTemperature=20.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame) double ContentCreationTime=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame) int32 SourceISID=0;
	/** Source product for managed liquid, or catalog PID for liquid supplied by a local tank. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame) int32 SourceProductID=0;
};
