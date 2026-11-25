#pragma once

#include "Components/Widget.h"
#include "FlowControllerPressureDial.generated.h"

/** A compact pressure instrument. The text next to the dial exposes the exact reading. */
UCLASS()
class UFlowControllerPressureDial : public UWidget
{
	GENERATED_BODY()

public:
	void SetReading(float Value, float Limit);
	float GetFraction() const { return Fraction; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	float Fraction = 0.f;
};
