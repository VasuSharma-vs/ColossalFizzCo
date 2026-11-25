#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ManualFillingPressureGauge.generated.h"

/** Vector instrument, so the pressure needle remains sharp at any UI scale. */
UCLASS()
class COLOSSALFIZZCO_API UManualFillingPressureGauge : public UWidget
{
	GENERATED_BODY()
public:
	void SetReading(float InPressure, float InSafePressure);
	float Pressure=0;
	float SafePressure=.1f;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
