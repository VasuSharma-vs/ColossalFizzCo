#pragma once

#include "UserSettings/EnhancedInputUserSettings.h"
#include "PrototypeInputUserSettings.generated.h"

UCLASS()
class UPrototypeInputUserSettings : public UEnhancedInputUserSettings
{
	GENERATED_BODY()

public:
	virtual void Initialize(ULocalPlayer* LocalPlayer) override;

protected:
	virtual bool RegisterKeyMappingsToProfile(UEnhancedPlayerMappableKeyProfile& Profile, const UInputMappingContext* Context) override;
};
