#include "Components/ResourcePumpComponent.h"
#include "Net/UnrealNetwork.h"
UResourcePumpComponent::UResourcePumpComponent() { SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=false; }
void UResourcePumpComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UResourcePumpComponent,bRunning); DOREPLIFETIME(UResourcePumpComponent,PushPressure); DOREPLIFETIME(UResourcePumpComponent,PullPressure);
}
