#include "UI/SettingsControls.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "PlayerMappableKeySettings.h"
#include "UI/PrototypeInputUserSettings.h"
#include "UObject/UnrealType.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#define LOCTEXT_NAMESPACE "SettingsControls"

namespace SettingsControls
{
	namespace
	{
		constexpr auto FirstSlot = EPlayerMappableKeySlot::First;
		const TCHAR* DefaultContextPath = TEXT("/Game/Input/IMC_Default.IMC_Default");
		const FName InteractId(TEXT("CFC.Interact"));
		const FName GrabId(TEXT("CFC.Grab"));

		struct FDefaultBinding : FBinding
		{
			FName ActionName;
		};

		const TArray<FDefaultBinding>& DefaultBindings()
		{
			static const TArray<FDefaultBinding> Bindings = {
				{{TEXT("CFC.MoveForward"), LOCTEXT("MoveForward", "Move forward"), EKeys::W}, TEXT("IA_Move")},
				{{TEXT("CFC.MoveBackward"), LOCTEXT("MoveBackward", "Move backward"), EKeys::S}, TEXT("IA_Move")},
				{{TEXT("CFC.MoveLeft"), LOCTEXT("MoveLeft", "Move left"), EKeys::A}, TEXT("IA_Move")},
				{{TEXT("CFC.MoveRight"), LOCTEXT("MoveRight", "Move right"), EKeys::D}, TEXT("IA_Move")},
				{{TEXT("CFC.Jump"), LOCTEXT("Jump", "Jump"), EKeys::SpaceBar}, TEXT("IA_Jump")},
				{{InteractId, LOCTEXT("Interact", "Interact"), EKeys::E}, TEXT("IA_Intract")},
				{{GrabId, LOCTEXT("Grab", "Grab / drop"), EKeys::G}, TEXT("IA_Grab")},
				{{TEXT("CFC.Attack"), LOCTEXT("Attack", "Attack"), EKeys::LeftMouseButton}, TEXT("IA_Attack")}
			};
			return Bindings;
		}

		void ConfigureMappingNames(UInputMappingContext& Context)
		{
			// The per-key override fields are reflected, with no public Enhanced Input setters.
			const UScriptStruct* MappingStruct = FEnhancedActionKeyMapping::StaticStruct();
			const auto* Behavior = FindFProperty<FEnumProperty>(MappingStruct, TEXT("SettingBehavior"));
			const auto* Settings = FindFProperty<FObjectPropertyBase>(MappingStruct, TEXT("PlayerMappableKeySettings"));
			if (!ensure(Behavior && Settings)) return;

			for (int32 Index = 0; Index < Context.GetMappings().Num(); ++Index)
			{
				auto& Mapping = Context.GetMapping(Index);
				const auto* Binding = DefaultBindings().FindByPredicate([&Mapping](const FDefaultBinding& Entry)
				{
					return Mapping.Action && Mapping.Action->GetFName() == Entry.ActionName && Mapping.Key == Entry.Key;
				});
				if (!Binding || Mapping.GetMappingName() == Binding->Id) continue;

				auto* KeySettings = NewObject<UPlayerMappableKeySettings>(&Context);
				KeySettings->Name = Binding->Id;
				KeySettings->DisplayName = Binding->Label;
				Settings->SetObjectPropertyValue_InContainer(&Mapping, KeySettings);
				Behavior->GetUnderlyingProperty()->SetIntPropertyValue(Behavior->ContainerPtrToValuePtr<void>(&Mapping),
					static_cast<uint64>(EPlayerMappableKeySettingBehaviors::OverrideSettings));
			}
		}

		UEnhancedInputUserSettings* GetUserSettings(const ULocalPlayer* Player)
		{
			const auto* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player);
			return Input ? Input->GetUserSettings() : nullptr;
		}

		FKey GetKey(const UEnhancedInputUserSettings* Settings, FName Id, const FKey& DefaultKey)
		{
			const auto* Mapping = Settings ? Settings->FindCurrentMappingForSlot(Id, FirstSlot) : nullptr;
			return Mapping && Mapping->GetCurrentKey().IsValid() ? Mapping->GetCurrentKey() : DefaultKey;
		}

		bool IsReserved(const FKey& Key)
		{
			static const TSet<FKey> Reserved = {
				EKeys::Escape, EKeys::F11, EKeys::Tilde, EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right,
				EKeys::Four, EKeys::NumPadFour, EKeys::F, EKeys::H, EKeys::M, EKeys::R, EKeys::T,
				EKeys::U, EKeys::V, EKeys::X, EKeys::RightMouseButton, EKeys::LeftShift, EKeys::RightShift
			};
			return Reserved.Contains(Key);
		}
	}

	TArray<FBinding> GetBindings(const ULocalPlayer* Player)
	{
		const auto* Settings = GetUserSettings(Player);
		TArray<FBinding> Bindings;
		Bindings.Reserve(DefaultBindings().Num());
		for (const FDefaultBinding& Binding : DefaultBindings())
		{
			Bindings.Add({Binding.Id, Binding.Label, GetKey(Settings, Binding.Id, Binding.Key)});
		}
		return Bindings;
	}

	bool SetBinding(ULocalPlayer* Player, FName Id, const FKey& Key, FText& OutError)
	{
		if (!Key.IsValid() || Key == EKeys::AnyKey || !Key.IsBindableInBlueprints() || Key.IsAnalog()
			|| Key.IsGamepadKey() || Key.IsTouch() || Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown)
		{
			OutError = LOCTEXT("InvalidKey", "Choose a keyboard key or mouse button.");
			return false;
		}
		if (IsReserved(Key))
		{
			OutError = LOCTEXT("ReservedKey", "That key is reserved for another control.");
			return false;
		}

		if (const auto Bindings = GetBindings(Player); Bindings.ContainsByPredicate([&](const FBinding& Binding)
			{ return Binding.Id != Id && Binding.Key == Key; }))
		{
			OutError = LOCTEXT("DuplicateKey", "That key is already assigned.");
			return false;
		}

		auto* Settings = GetUserSettings(Player);
		const auto* Mapping = Settings ? Settings->FindCurrentMappingForSlot(Id, FirstSlot) : nullptr;
		if (!Mapping || !DefaultBindings().ContainsByPredicate([Id](const FBinding& Binding) { return Binding.Id == Id; }))
		{
			OutError = LOCTEXT("Unavailable", "This control is currently unavailable.");
			return false;
		}
		if (Mapping->GetCurrentKey() == Key)
			return true;

		FMapPlayerKeyArgs Args;
		Args.MappingName = Id;
		Args.Slot = FirstSlot;
		Args.NewKey = Key;
		Args.HardwareDeviceId = Mapping->GetHardwareDeviceId().HardwareDeviceIdentifier;
		Args.bCreateMatchingSlotIfNeeded = false;
		FGameplayTagContainer FailureReason;
		Settings->MapPlayerKey(Args, FailureReason);
		if (!FailureReason.IsEmpty())
		{
			OutError = LOCTEXT("CannotChangeKey", "Could not change this control.");
			return false;
		}
		Settings->SaveSettings();
		return true;
	}

	void ResetBindings(ULocalPlayer* Player)
	{
		if (auto* Settings = GetUserSettings(Player))
		{
			for (const FBinding& Binding : DefaultBindings())
			{
				FMapPlayerKeyArgs Args;
				Args.MappingName = Binding.Id;
				FGameplayTagContainer FailureReason;
				Settings->ResetAllPlayerKeysInRow(Args, FailureReason);
			}
			Settings->SaveSettings();
		}
	}

	FKey InteractKey(const ULocalPlayer* Player)
	{
		return GetKey(GetUserSettings(Player), InteractId, EKeys::E);
	}

	FKey GrabKey(const ULocalPlayer* Player)
	{
		return GetKey(GetUserSettings(Player), GrabId, EKeys::G);
	}
}

void UPrototypeInputUserSettings::Initialize(ULocalPlayer* LocalPlayer)
{
	Super::Initialize(LocalPlayer);
	if (auto* Context = LoadObject<UInputMappingContext>(nullptr, SettingsControls::DefaultContextPath))
	{
		RegisterInputMappingContext(Context);
	}
}

bool UPrototypeInputUserSettings::RegisterKeyMappingsToProfile(UEnhancedPlayerMappableKeyProfile& Profile, const UInputMappingContext* Context)
{
	if (Context && Context->GetPathName() == SettingsControls::DefaultContextPath)
	{
		// Keep the existing context and actions used by the first-person and vehicle Blueprints.
		// Supply names before Enhanced Input registers the mappings or reloads them from a saved profile.
		SettingsControls::ConfigureMappingNames(*const_cast<UInputMappingContext*>(Context));
	}
	return Super::RegisterKeyMappingsToProfile(Profile, Context);
}

#undef LOCTEXT_NAMESPACE
