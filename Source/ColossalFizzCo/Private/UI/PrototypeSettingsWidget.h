#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "GenericPlatform/GenericWindow.h"
#include "PrototypeSettingsWidget.generated.h"

class UCheckBox;
class UScrollBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UPrototypeSettingsWidget;

UCLASS()
class UPrototypeBindingButton : public UButton
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UPrototypeSettingsWidget> Owner;
	FName BindingId;

	UFUNCTION()
	void BeginCapture();
};

UCLASS()
class UPrototypeSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	FSimpleDelegate OnBack;

	void Enter();
	void Exit();
	void CaptureBinding(FName BindingId);
	UFUNCTION() void Back();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	enum class EPage { Controls, Graphics, Audio };

	UPROPERTY(Transient) TObjectPtr<UButton> ControlsTab;
	UPROPERTY(Transient) TObjectPtr<UButton> GraphicsTab;
	UPROPERTY(Transient) TObjectPtr<UButton> AudioTab;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroll;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Content;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UPrototypeBindingButton>> BindingButtons;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> QualityPicker;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ResolutionPicker;
	UPROPERTY(Transient) TObjectPtr<USlider> MasterVolumeSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VolumePercent;

	EPage Page = EPage::Controls;
	FName CapturingBinding;
	TArray<FIntPoint> Resolutions;
	FIntPoint Resolution = FIntPoint::ZeroValue;
	EWindowMode::Type WindowMode = EWindowMode::Windowed;
	int32 Quality = INDEX_NONE;
	bool bVSync = false;
	bool bAdjustingVolume = false;
	double ConfirmUntil = 0.0;

	UTextBlock* MakeText(const FText& Label, int32 Size, const FLinearColor& Color);
	UButton* MakeButton(FName Name, const FText& Label, FName Handler, bool bAccent = false);
	void SetStatus(const FText& Message, bool bAccent = false);
	void AddOption(const FText& Label, UWidget* Option);
	void SelectPage(EPage NewPage);
	void BuildPage();
	void BuildControls();
	void BuildGraphics();
	void BuildAudio();
	void RefreshBindingLabels();
	void BindKey(const FKey& Key);
	void ReadGraphics();
	void RefreshResolutions();
	void RevertVideoMode();
	bool UpdateDisplayConfirmation();
	void UpdateVolumePercent(float Volume);

	UFUNCTION() void ShowControls();
	UFUNCTION() void ShowGraphics();
	UFUNCTION() void ShowAudio();
	UFUNCTION() void ResetBindings();
	UFUNCTION() void QualityChanged(FString Value, ESelectInfo::Type Selection);
	UFUNCTION() void WindowModeChanged(FString Value, ESelectInfo::Type Selection);
	UFUNCTION() void ResolutionChanged(FString Value, ESelectInfo::Type Selection);
	UFUNCTION() void VSyncChanged(bool bChecked);
	UFUNCTION() void ApplyGraphics();
	UFUNCTION() void KeepVideoMode();
	UFUNCTION() void UndoVideoMode();
	UFUNCTION() void BeginVolumeAdjustment();
	UFUNCTION() void EndVolumeAdjustment();
	UFUNCTION() void VolumeChanged(float Volume);
};
