#pragma once

#include "Components/Widget.h"
#include "FlowControllerIcon.generated.h"

enum class EFlowControllerIcon : uint8
{
	Controller, Inlet, Outlet, Flow, Pressure, Suction, Lever, Hold, Toggle, AutoStop, Automatic,
	Bottle, Coin, Upgrade, Move, Pause, Play, Lock, Check, Close, ArrowRight, Warning, Link, Clock
};

namespace FlowControllerUI
{
	inline const FLinearColor Panel(.017f,.033f,.039f,.985f);
	inline const FLinearColor Surface(.039f,.066f,.071f);
	inline const FLinearColor Inset(.010f,.022f,.026f);
	inline const FLinearColor Edge(.080f,.155f,.164f);
	inline const FLinearColor Text(.94f,.92f,.84f);
	inline const FLinearColor Muted(.46f,.59f,.60f);
	inline const FLinearColor Brass(.96f,.68f,.31f);
	inline const FLinearColor Mint(.36f,.90f,.70f);
	inline const FLinearColor Blue(.39f,.67f,.95f);
	inline const FLinearColor Red(.98f,.28f,.27f);
}

/** Small vector pictograms shared by the Flow Controller readout and upgrade menu. */
UCLASS()
class COLOSSALFIZZCO_API UFlowControllerIcon : public UWidget
{
	GENERATED_BODY()

public:
	UFlowControllerIcon();
	void Configure(EFlowControllerIcon InIcon, const FLinearColor& InTint, FVector2D InSize=FVector2D(24,24));
	void SetIcon(EFlowControllerIcon InIcon);
	void SetTint(const FLinearColor& InTint);
	void SetIconSize(FVector2D InSize);
	EFlowControllerIcon GetIcon() const { return Icon; }
	const FLinearColor& GetTint() const { return Tint; }
	FVector2D GetIconSize() const { return Size; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	EFlowControllerIcon Icon=EFlowControllerIcon::Controller;
	FLinearColor Tint=FlowControllerUI::Text;
	FVector2D Size=FVector2D(24,24);
};
