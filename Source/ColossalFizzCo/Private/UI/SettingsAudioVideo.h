#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"

class UGameUserSettings;

namespace SettingsAudioVideo
{
	void Initialize();
	void Shutdown();

	UGameUserSettings* GetGraphicsSettings();
	TArray<FIntPoint> GetSupportedResolutions(EWindowMode::Type WindowMode);
	void ApplyGraphics(int32 Quality, EWindowMode::Type WindowMode, FIntPoint Resolution, bool bVSync);

	float GetMasterVolume();
	void SetMasterVolume(float Volume, bool bSave);
}
