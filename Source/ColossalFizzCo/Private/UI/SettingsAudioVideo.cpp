#include "UI/SettingsAudioVideo.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* AudioSettingsSection = TEXT("ColossalFizzCo.Audio");
	const TCHAR* MasterVolumeKey = TEXT("MasterVolume");
	TOptional<float> MasterVolume;
	FDelegateHandle WorldInitializedHandle;

	float SanitizeVolume(float Volume)
	{
		return FMath::IsFinite(Volume) ? FMath::Clamp(Volume, 0.0f, 1.0f) : 1.0f;
	}

	void ApplyMasterVolume(UWorld* World, float Volume)
	{
		if (World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer)
		{
			const FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
			if (FAudioDevice* Device = AudioDevice.GetAudioDevice())
			{
				Device->SetTransientPrimaryVolume(Volume);
			}
		}
	}

	bool IsValidResolution(FIntPoint Resolution)
	{
		return Resolution.X > 0 && Resolution.Y > 0;
	}
}

void SettingsAudioVideo::Initialize()
{
	if (!IsRunningDedicatedServer() && !IsRunningCommandlet())
	{
		WorldInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddLambda([](const FActorsInitializedParams& Params)
		{
			ApplyMasterVolume(Params.World, GetMasterVolume());
		});
	}
}

void SettingsAudioVideo::Shutdown()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitializedHandle);
}

UGameUserSettings* SettingsAudioVideo::GetGraphicsSettings()
{
	return GEngine ? GEngine->GetGameUserSettings() : nullptr;
}

TArray<FIntPoint> SettingsAudioVideo::GetSupportedResolutions(EWindowMode::Type WindowMode)
{
	TArray<FIntPoint> Resolutions;
	if (WindowMode == EWindowMode::Fullscreen)
	{
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	}
	else if (WindowMode == EWindowMode::Windowed)
	{
		UKismetSystemLibrary::GetConvenientWindowedResolutions(Resolutions);
	}

	Resolutions.RemoveAll([](FIntPoint Resolution) { return !IsValidResolution(Resolution); });
	if (const UGameUserSettings* Settings = GetGraphicsSettings())
	{
		const FIntPoint Current = Settings->GetScreenResolution();
		const FIntPoint Desktop = Settings->GetDesktopResolution();
		if (WindowMode != EWindowMode::WindowedFullscreen && Settings->GetFullscreenMode() == WindowMode && IsValidResolution(Current))
		{
			Resolutions.AddUnique(Current);
		}
		if (Resolutions.IsEmpty() && IsValidResolution(Desktop))
		{
			Resolutions.Add(Desktop);
		}
		if (Resolutions.IsEmpty() && IsValidResolution(Current))
		{
			Resolutions.Add(Current);
		}
	}
	if (Resolutions.IsEmpty())
	{
		Resolutions.Add(FIntPoint(1280, 720));
	}
	Resolutions.Sort([](FIntPoint A, FIntPoint B)
	{
		return A.X == B.X ? A.Y < B.Y : A.X < B.X;
	});
	return Resolutions;
}

void SettingsAudioVideo::ApplyGraphics(int32 Quality, EWindowMode::Type WindowMode, FIntPoint Resolution, bool bVSync)
{
	if (UGameUserSettings* Settings = GetGraphicsSettings())
	{
		Settings->SetFullscreenMode(WindowMode);
		if (IsValidResolution(Resolution))
		{
			Settings->SetScreenResolution(Resolution);
		}
		if (Quality >= 0)
		{
			Settings->SetOverallScalabilityLevel(Quality);
		}
		Settings->SetVSyncEnabled(bVSync);
		Settings->ConfirmVideoMode();
		Settings->ApplySettings(false);
	}
}

float SettingsAudioVideo::GetMasterVolume()
{
	if (!MasterVolume.IsSet())
	{
		float SavedVolume = 1.0f;
		if (GConfig && !GGameUserSettingsIni.IsEmpty())
		{
			GConfig->GetFloat(AudioSettingsSection, MasterVolumeKey, SavedVolume, GGameUserSettingsIni);
			MasterVolume = SanitizeVolume(SavedVolume);
		}
		return MasterVolume.Get(1.0f);
	}
	return MasterVolume.GetValue();
}

void SettingsAudioVideo::SetMasterVolume(float Volume, bool bSave)
{
	MasterVolume = SanitizeVolume(Volume);
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			ApplyMasterVolume(Context.World(), MasterVolume.GetValue());
		}
	}
	if (bSave && GConfig && !GGameUserSettingsIni.IsEmpty())
	{
		GConfig->SetFloat(AudioSettingsSection, MasterVolumeKey, MasterVolume.GetValue(), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}
