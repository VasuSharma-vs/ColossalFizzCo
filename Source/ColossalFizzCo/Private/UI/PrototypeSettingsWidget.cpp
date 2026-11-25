#include "UI/PrototypeSettingsWidget.h"

#include "Algo/MinElement.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Containers/Ticker.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "UI/PrototypeMenuStyle.h"
#include "UI/SettingsAudioVideo.h"
#include "UI/SettingsControls.h"

#define LOCTEXT_NAMESPACE "PrototypeSettings"

namespace
{
	constexpr double DisplayConfirmationSeconds = 15.0;
	const EWindowMode::Type WindowModes[] = {
		EWindowMode::Windowed, EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen
	};

	void SetButtonLabel(UButton* Button, const FText& Label)
	{
		CastChecked<UTextBlock>(Button->GetContent())->SetText(Label);
	}
}

void UPrototypeBindingButton::BeginCapture()
{
	if (auto* Settings = Owner.Get())
	{
		Settings->CaptureBinding(BindingId);
	}
}

TSharedRef<SWidget> UPrototypeSettingsWidget::RebuildWidget()
{
	SetIsFocusable(true);
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this);
	}
	if (!WidgetTree->RootWidget)
	{
		auto* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Layout;

		auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
		Layout->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 16));
		auto* TitleSlot = Header->AddChildToHorizontalBox(MakeText(LOCTEXT("Title", "Settings"), 36, PrototypeMenuStyle::Amber));
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		Header->AddChildToHorizontalBox(MakeButton(TEXT("BackFromSettings"), LOCTEXT("Back", "Back"),
			GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, Back)));

		auto* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
		Layout->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0, 0, 0, 16));
		ControlsTab = MakeButton(TEXT("ControlsTab"), LOCTEXT("Controls", "Controls"),
			GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, ShowControls));
		GraphicsTab = MakeButton(TEXT("GraphicsTab"), LOCTEXT("Graphics", "Graphics"),
			GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, ShowGraphics));
		AudioTab = MakeButton(TEXT("AudioTab"), LOCTEXT("Audio", "Audio"),
			GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, ShowAudio));
		for (UButton* Tab : { ControlsTab.Get(), GraphicsTab.Get(), AudioTab.Get() })
		{
			auto* TabSlot = Tabs->AddChildToHorizontalBox(Tab);
			TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TabSlot->SetPadding(FMargin(0, 0, Tab == AudioTab.Get() ? 0.f : 8.f, 0));
		}

		Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
		Layout->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsContent"));
		Scroll->AddChild(Content);
		Status = MakeText(FText::GetEmpty(), 16, PrototypeMenuStyle::Muted);
		auto* StatusArea = WidgetTree->ConstructWidget<USizeBox>();
		StatusArea->SetMinDesiredHeight(24.f);
		StatusArea->AddChild(Status);
		Layout->AddChildToVerticalBox(StatusArea)->SetPadding(FMargin(0, 12, 0, 0));
	}
	return Super::RebuildWidget();
}

void UPrototypeSettingsWidget::Enter()
{
	ReadGraphics();
	SelectPage(EPage::Controls);
	SetKeyboardFocus();
}

void UPrototypeSettingsWidget::Exit()
{
	EndVolumeAdjustment();
	RevertVideoMode();
	CapturingBinding = NAME_None;
}

void UPrototypeSettingsWidget::NativeDestruct()
{
	Exit();
	Super::NativeDestruct();
}

void UPrototypeSettingsWidget::Back()
{
	if (!CapturingBinding.IsNone())
	{
		CapturingBinding = NAME_None;
		RefreshBindingLabels();
		SetStatus(FText::GetEmpty());
		return;
	}
	if (ConfirmUntil > 0.0)
	{
		UndoVideoMode();
		return;
	}
	Exit();
	OnBack.ExecuteIfBound();
}

UTextBlock* UPrototypeSettingsWidget::MakeText(const FText& Label, int32 Size, const FLinearColor& Color)
{
	auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(Label);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle(Size >= 24 ? TEXT("Bold") : TEXT("Regular"), Size));
	Text->SetColorAndOpacity(Color);
	Text->SetAutoWrapText(true);
	return Text;
}

UButton* UPrototypeSettingsWidget::MakeButton(FName Name, const FText& Label, FName Handler, bool bAccent)
{
	auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	PrototypeMenuStyle::StyleButton(Button, bAccent);
	Button->AddChild(MakeText(Label, 18, bAccent ? PrototypeMenuStyle::Ink : PrototypeMenuStyle::Cream));
	CastChecked<UButtonSlot>(Button->GetContentSlot())->SetPadding(FMargin(16, 8));
	FScriptDelegate Click;
	Click.BindUFunction(this, Handler);
	Button->OnClicked.Add(Click);
	return Button;
}

void UPrototypeSettingsWidget::SetStatus(const FText& Message, bool bAccent)
{
	Status->SetText(Message);
	Status->SetColorAndOpacity(bAccent ? PrototypeMenuStyle::Amber : PrototypeMenuStyle::Muted);
}

void UPrototypeSettingsWidget::AddOption(const FText& Label, UWidget* Option)
{
	auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 3, 12, 3));
	auto* LabelSlot = Row->AddChildToHorizontalBox(MakeText(Label, 18, PrototypeMenuStyle::Cream));
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0, 0, 16, 0));
	auto* Width = WidgetTree->ConstructWidget<USizeBox>();
	Width->SetWidthOverride(300.f);
	Width->AddChild(Option);
	Row->AddChildToHorizontalBox(Width)->SetVerticalAlignment(VAlign_Center);
}

void UPrototypeSettingsWidget::SelectPage(EPage NewPage)
{
	EndVolumeAdjustment();
	RevertVideoMode();
	CapturingBinding = NAME_None;
	Page = NewPage;
	SetStatus(FText::GetEmpty());
	BuildPage();
	Scroll->ScrollToStart();
}

void UPrototypeSettingsWidget::BuildPage()
{
	Content->ClearChildren();
	BindingButtons.Reset();
	ResolutionPicker = nullptr;
	VolumePercent = nullptr;
	for (UButton* Tab : { ControlsTab.Get(), GraphicsTab.Get(), AudioTab.Get() })
	{
		const bool bSelected = Tab == ControlsTab.Get() ? Page == EPage::Controls
			: Tab == GraphicsTab.Get() ? Page == EPage::Graphics : Page == EPage::Audio;
		PrototypeMenuStyle::StyleButton(Tab, bSelected);
		CastChecked<UTextBlock>(Tab->GetContent())->SetColorAndOpacity(bSelected ? PrototypeMenuStyle::Ink : PrototypeMenuStyle::Cream);
	}
	switch (Page)
	{
	case EPage::Controls: BuildControls(); break;
	case EPage::Graphics: BuildGraphics(); break;
	case EPage::Audio: BuildAudio(); break;
	}
}

void UPrototypeSettingsWidget::ShowControls() { SelectPage(EPage::Controls); }
void UPrototypeSettingsWidget::ShowGraphics() { SelectPage(EPage::Graphics); }
void UPrototypeSettingsWidget::ShowAudio() { SelectPage(EPage::Audio); }

void UPrototypeSettingsWidget::BuildControls()
{
	Content->AddChildToVerticalBox(MakeText(LOCTEXT("RebindHelp",
		"Select a first-person control, then press a key or mouse button. Escape cancels."), 16, PrototypeMenuStyle::Muted))
		->SetPadding(FMargin(0, 0, 0, 12));

	for (const auto& Binding : SettingsControls::GetBindings(GetOwningLocalPlayer()))
	{
		auto* Button = WidgetTree->ConstructWidget<UPrototypeBindingButton>();
		Button->BindingId = Binding.Id;
		Button->Owner = this;
		Button->OnClicked.AddDynamic(Button, &UPrototypeBindingButton::BeginCapture);
		PrototypeMenuStyle::StyleButton(Button, false);
		Button->AddChild(MakeText(Binding.Key.GetDisplayName(), 18, PrototypeMenuStyle::Cream));
		CastChecked<UButtonSlot>(Button->GetContentSlot())->SetPadding(FMargin(16, 7));
		BindingButtons.Add(Binding.Id, Button);
		AddOption(Binding.Label, Button);
	}
	AddOption(LOCTEXT("ResetControlsLabel", "Keyboard and mouse defaults"),
		MakeButton(NAME_None, LOCTEXT("ResetControls", "Restore defaults"),
			GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, ResetBindings)));
}

void UPrototypeSettingsWidget::RefreshBindingLabels()
{
	for (const auto& Binding : SettingsControls::GetBindings(GetOwningLocalPlayer()))
	{
		if (const auto* Button = BindingButtons.Find(Binding.Id))
		{
			const bool bCapturing = Binding.Id == CapturingBinding;
			SetButtonLabel(Button->Get(), bCapturing ? LOCTEXT("WaitingForKey", "Press a key…") : Binding.Key.GetDisplayName());
			PrototypeMenuStyle::StyleButton(Button->Get(), bCapturing);
			CastChecked<UTextBlock>((*Button)->GetContent())->SetColorAndOpacity(bCapturing ? PrototypeMenuStyle::Ink : PrototypeMenuStyle::Cream);
		}
	}
}

void UPrototypeSettingsWidget::CaptureBinding(FName BindingId)
{
	CapturingBinding = BindingId;
	RefreshBindingLabels();
	SetStatus(LOCTEXT("CaptureHelp", "Press the new key or mouse button, or press Escape to cancel."), true);
	SetKeyboardFocus();
}

void UPrototypeSettingsWidget::BindKey(const FKey& Key)
{
	FText Error;
	const bool bSaved = SettingsControls::SetBinding(GetOwningLocalPlayer(), CapturingBinding, Key, Error);
	CapturingBinding = NAME_None;
	RefreshBindingLabels();
	SetStatus(bSaved ? LOCTEXT("BindingSaved", "Binding saved.") : Error, !bSaved);
}

void UPrototypeSettingsWidget::ResetBindings()
{
	SettingsControls::ResetBindings(GetOwningLocalPlayer());
	RefreshBindingLabels();
	SetStatus(LOCTEXT("BindingsReset", "Default bindings restored."));
}

FReply UPrototypeSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (!CapturingBinding.IsNone())
	{
		if (!Event.IsRepeat())
		{
			if (Event.GetKey() == EKeys::Escape) Back();
			else BindKey(Event.GetKey());
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

FReply UPrototypeSettingsWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		if (!Event.IsRepeat()) Back();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, Event);
}

FReply UPrototypeSettingsWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!CapturingBinding.IsNone())
	{
		BindKey(Event.GetEffectingButton());
		return FReply::Handled();
	}
	return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}

void UPrototypeSettingsWidget::ReadGraphics()
{
	if (const auto* Settings = SettingsAudioVideo::GetGraphicsSettings())
	{
		Quality = Settings->GetOverallScalabilityLevel();
		WindowMode = Settings->GetFullscreenMode();
		Resolution = Settings->GetScreenResolution();
		bVSync = Settings->IsVSyncEnabled();
	}
}

void UPrototypeSettingsWidget::BuildGraphics()
{
	if (!SettingsAudioVideo::GetGraphicsSettings())
	{
		SetStatus(LOCTEXT("GraphicsUnavailable", "Graphics settings are unavailable."), true);
		return;
	}
	if (ConfirmUntil > 0.0)
	{
		Content->AddChildToVerticalBox(MakeText(LOCTEXT("ConfirmDisplay", "Keep these display settings?"), 25, PrototypeMenuStyle::Cream))
			->SetPadding(FMargin(0, 0, 0, 12));
		AddOption(LOCTEXT("KeepLabel", "Save the new display mode"),
			MakeButton(NAME_None, LOCTEXT("KeepChanges", "Keep changes"),
				GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, KeepVideoMode), true));
		AddOption(LOCTEXT("RevertLabel", "Restore the previous display mode"),
			MakeButton(NAME_None, LOCTEXT("Revert", "Revert"),
				GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, UndoVideoMode)));
		return;
	}

	if (!QualityPicker)
	{
		QualityPicker = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("GraphicsQuality"));
		for (const auto& Label : {
			LOCTEXT("Low", "Low"), LOCTEXT("Medium", "Medium"), LOCTEXT("High", "High"),
			LOCTEXT("Epic", "Epic"), LOCTEXT("Cinematic", "Cinematic")
		})
		{
			QualityPicker->AddOption(Label.ToString());
		}
		QualityPicker->OnSelectionChanged.AddDynamic(this, &UPrototypeSettingsWidget::QualityChanged);
	}
	const FString CustomQuality = LOCTEXT("Custom", "Custom").ToString();
	const int32 CustomIndex = QualityPicker->FindOptionIndex(CustomQuality);
	if (Quality == INDEX_NONE && CustomIndex == INDEX_NONE)
	{
		QualityPicker->AddOption(CustomQuality);
	}
	else if (Quality != INDEX_NONE && CustomIndex != INDEX_NONE)
	{
		QualityPicker->RemoveOption(CustomQuality);
	}
	QualityPicker->SetSelectedIndex(Quality == INDEX_NONE ? 5 : Quality);
	AddOption(LOCTEXT("Quality", "Graphics quality"), QualityPicker);

	auto* ModePicker = WidgetTree->ConstructWidget<UComboBoxString>();
	ModePicker->AddOption(LOCTEXT("Windowed", "Windowed").ToString());
	ModePicker->AddOption(LOCTEXT("Borderless", "Borderless fullscreen").ToString());
	ModePicker->AddOption(LOCTEXT("Fullscreen", "Fullscreen").ToString());
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(WindowModes); ++Index)
	{
		if (WindowModes[Index] == WindowMode) ModePicker->SetSelectedIndex(Index);
	}
	ModePicker->OnSelectionChanged.AddDynamic(this, &UPrototypeSettingsWidget::WindowModeChanged);
	AddOption(LOCTEXT("DisplayMode", "Display mode"), ModePicker);

	ResolutionPicker = WidgetTree->ConstructWidget<UComboBoxString>();
	ResolutionPicker->SetMaxListHeight(240.f);
	RefreshResolutions();
	ResolutionPicker->OnSelectionChanged.AddDynamic(this, &UPrototypeSettingsWidget::ResolutionChanged);
	AddOption(LOCTEXT("Resolution", "Resolution"), ResolutionPicker);

	auto* VSync = WidgetTree->ConstructWidget<UCheckBox>();
	VSync->SetIsChecked(bVSync);
	VSync->OnCheckStateChanged.AddDynamic(this, &UPrototypeSettingsWidget::VSyncChanged);
	AddOption(LOCTEXT("VSync", "VSync"), VSync);
	AddOption(FText::GetEmpty(), MakeButton(NAME_None, LOCTEXT("ApplyGraphics", "Apply graphics settings"),
		GET_FUNCTION_NAME_CHECKED(UPrototypeSettingsWidget, ApplyGraphics), true));
}

void UPrototypeSettingsWidget::RefreshResolutions()
{
	ResolutionPicker->ClearOptions();
	if (WindowMode == EWindowMode::WindowedFullscreen)
	{
		ResolutionPicker->AddOption(LOCTEXT("DesktopResolution", "Desktop resolution").ToString());
		ResolutionPicker->SetSelectedIndex(0);
		ResolutionPicker->SetIsEnabled(false);
		return;
	}
	Resolutions = SettingsAudioVideo::GetSupportedResolutions(WindowMode);
	for (const FIntPoint& Size : Resolutions)
	{
		ResolutionPicker->AddOption(FString::Printf(TEXT("%d × %d"), Size.X, Size.Y));
	}
	int32 SelectedIndex = Resolutions.IndexOfByKey(Resolution);
	if (SelectedIndex == INDEX_NONE && !Resolutions.IsEmpty())
	{
		const FIntPoint Target = Resolution.X > 0 && Resolution.Y > 0 ? Resolution : FIntPoint(1280, 720);
		const auto* Closest = Algo::MinElementBy(Resolutions, [Target](FIntPoint Size)
		{
			return FMath::Abs(Size.X - Target.X) + FMath::Abs(Size.Y - Target.Y);
		});
		SelectedIndex = static_cast<int32>(Closest - Resolutions.GetData());
		Resolution = *Closest;
	}
	ResolutionPicker->SetSelectedIndex(SelectedIndex);
	ResolutionPicker->SetIsEnabled(!Resolutions.IsEmpty());
}

void UPrototypeSettingsWidget::QualityChanged(FString Value, ESelectInfo::Type Selection)
{
	if (Selection != ESelectInfo::Direct)
	{
		const int32 Index = QualityPicker->FindOptionIndex(Value);
		Quality = Index >= 0 && Index <= 4 ? Index : INDEX_NONE;
		SetStatus(FText::GetEmpty());
	}
}

void UPrototypeSettingsWidget::WindowModeChanged(FString Value, ESelectInfo::Type Selection)
{
	if (Selection == ESelectInfo::Direct) return;
	if (Value == LOCTEXT("Windowed", "Windowed").ToString()) WindowMode = EWindowMode::Windowed;
	else if (Value == LOCTEXT("Borderless", "Borderless fullscreen").ToString()) WindowMode = EWindowMode::WindowedFullscreen;
	else WindowMode = EWindowMode::Fullscreen;
	RefreshResolutions();
	SetStatus(FText::GetEmpty());
}

void UPrototypeSettingsWidget::ResolutionChanged(FString Value, ESelectInfo::Type Selection)
{
	const int32 Index = ResolutionPicker->FindOptionIndex(Value);
	if (Selection != ESelectInfo::Direct && Resolutions.IsValidIndex(Index))
	{
		Resolution = Resolutions[Index];
		SetStatus(FText::GetEmpty());
	}
}

void UPrototypeSettingsWidget::VSyncChanged(bool bChecked)
{
	bVSync = bChecked;
	SetStatus(FText::GetEmpty());
}

void UPrototypeSettingsWidget::ApplyGraphics()
{
	auto* Settings = SettingsAudioVideo::GetGraphicsSettings();
	if (!Settings) return;
	if (WindowMode == EWindowMode::WindowedFullscreen)
	{
		Resolution = Settings->GetScreenResolution();
	}

	if (WindowMode != Settings->GetFullscreenMode() || Resolution != Settings->GetScreenResolution())
	{
		Settings->SetFullscreenMode(WindowMode);
		Settings->SetScreenResolution(Resolution);
		Settings->ApplyResolutionSettings(false);
		ConfirmUntil = FPlatformTime::Seconds() + DisplayConfirmationSeconds;
		BuildPage();
		SetKeyboardFocus();
		UpdateDisplayConfirmation();
		FTSTicker::GetCoreTicker().AddTicker(TEXT("PrototypeDisplayConfirmation"), 1.f,
			[Widget = MakeWeakObjectPtr(this), Deadline = ConfirmUntil](float)
			{
				auto* Panel = Widget.Get();
				return Panel && Panel->ConfirmUntil == Deadline && Panel->UpdateDisplayConfirmation();
			});
		return;
	}
	KeepVideoMode();
}

void UPrototypeSettingsWidget::KeepVideoMode()
{
	const bool bWasPreviewing = ConfirmUntil > 0.0;
	const bool bUsedCustomQuality = QualityPicker && QualityPicker->GetOptionCount() > 5;
	ConfirmUntil = 0.0;
	SettingsAudioVideo::ApplyGraphics(Quality, WindowMode, Resolution, bVSync);
	ReadGraphics();
	if (bWasPreviewing || (bUsedCustomQuality && Quality != INDEX_NONE))
	{
		BuildPage();
		SetKeyboardFocus();
	}
	SetStatus(LOCTEXT("GraphicsSaved", "Graphics settings saved."));
}

void UPrototypeSettingsWidget::RevertVideoMode()
{
	if (ConfirmUntil <= 0.0) return;
	ConfirmUntil = 0.0;
	if (auto* Settings = SettingsAudioVideo::GetGraphicsSettings())
	{
		Settings->RevertVideoMode();
		Settings->ApplyResolutionSettings(false);
	}
	ReadGraphics();
}

void UPrototypeSettingsWidget::UndoVideoMode()
{
	RevertVideoMode();
	BuildPage();
	SetKeyboardFocus();
	SetStatus(LOCTEXT("DisplayRestored", "Previous display settings restored."));
}

bool UPrototypeSettingsWidget::UpdateDisplayConfirmation()
{
	const double Remaining = ConfirmUntil - FPlatformTime::Seconds();
	if (Remaining <= 0.0)
	{
		UndoVideoMode();
		return false;
	}
	SetStatus(FText::Format(LOCTEXT("DisplayCountdown", "Reverting in {0} seconds."), FMath::CeilToInt(Remaining)), true);
	return true;
}

void UPrototypeSettingsWidget::BuildAudio()
{
	auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (!MasterVolumeSlider)
	{
		MasterVolumeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("MasterVolume"));
		MasterVolumeSlider->SetSliderBarColor(PrototypeMenuStyle::Muted);
		MasterVolumeSlider->SetSliderHandleColor(PrototypeMenuStyle::Amber);
		MasterVolumeSlider->SetStepSize(.01f);
		MasterVolumeSlider->OnValueChanged.AddDynamic(this, &UPrototypeSettingsWidget::VolumeChanged);
		MasterVolumeSlider->OnMouseCaptureBegin.AddDynamic(this, &UPrototypeSettingsWidget::BeginVolumeAdjustment);
		MasterVolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UPrototypeSettingsWidget::EndVolumeAdjustment);
		MasterVolumeSlider->OnControllerCaptureBegin.AddDynamic(this, &UPrototypeSettingsWidget::BeginVolumeAdjustment);
		MasterVolumeSlider->OnControllerCaptureEnd.AddDynamic(this, &UPrototypeSettingsWidget::EndVolumeAdjustment);
	}
	const float MasterVolume = SettingsAudioVideo::GetMasterVolume();
	MasterVolumeSlider->SetValue(MasterVolume);
	auto* SliderSlot = Row->AddChildToHorizontalBox(MasterVolumeSlider);
	SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SliderSlot->SetVerticalAlignment(VAlign_Center);
	VolumePercent = MakeText(FText::GetEmpty(), 18, PrototypeMenuStyle::Cream);
	VolumePercent->SetJustification(ETextJustify::Right);
	auto* PercentWidth = WidgetTree->ConstructWidget<USizeBox>();
	PercentWidth->SetWidthOverride(64.f);
	PercentWidth->AddChild(VolumePercent);
	Row->AddChildToHorizontalBox(PercentWidth);
	UpdateVolumePercent(MasterVolume);
	AddOption(LOCTEXT("MasterVolume", "Master volume"), Row);
}

void UPrototypeSettingsWidget::UpdateVolumePercent(float Volume)
{
	if (VolumePercent)
	{
		VolumePercent->SetText(FText::AsPercent(Volume));
	}
}

void UPrototypeSettingsWidget::BeginVolumeAdjustment()
{
	bAdjustingVolume = true;
}

void UPrototypeSettingsWidget::EndVolumeAdjustment()
{
	if (bAdjustingVolume)
	{
		bAdjustingVolume = false;
		SettingsAudioVideo::SetMasterVolume(SettingsAudioVideo::GetMasterVolume(), true);
	}
}

void UPrototypeSettingsWidget::VolumeChanged(float Volume)
{
	SettingsAudioVideo::SetMasterVolume(Volume, !bAdjustingVolume);
	UpdateVolumePercent(Volume);
}

#undef LOCTEXT_NAMESPACE
