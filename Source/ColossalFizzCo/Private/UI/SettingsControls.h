#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class ULocalPlayer;

namespace SettingsControls
{
	struct FBinding
	{
		FName Id;
		FText Label;
		FKey Key;
	};

	TArray<FBinding> GetBindings(const ULocalPlayer* Player);
	bool SetBinding(ULocalPlayer* Player, FName Id, const FKey& Key, FText& OutError);
	void ResetBindings(ULocalPlayer* Player);
	FKey InteractKey(const ULocalPlayer* Player);
	FKey GrabKey(const ULocalPlayer* Player);
}
