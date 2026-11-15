#include "UI/PrototypeMenuGameMode.h"
#include "Components/SceneComponent.h"

APrototypeMenuPawn::APrototypeMenuPawn()
{
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("MenuRoot")));
 bReplicates=true;SetReplicateMovement(false);PrimaryActorTick.bCanEverTick=false;
}

APrototypeMenuGameMode::APrototypeMenuGameMode()
{
 // Retain normal player possession through lobby-to-shop seamless travel.
 DefaultPawnClass=APrototypeMenuPawn::StaticClass();
 bUseSeamlessTravel=true;
}
const TCHAR* APrototypeMenuGameMode::MenuMap(){return TEXT("/Game/NormalColaPrototype/Lvl_PrototypeMainMenu");}
const TCHAR* APrototypeMenuGameMode::ShopMap(){return TEXT("/Game/NormalColaPrototype/Lvl_NormalColaPrototype");}
