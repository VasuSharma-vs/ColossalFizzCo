#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "PrototypeMenuGameMode.generated.h"

/** Invisible, collision-free lobby possession; replaced by the shop character. */
UCLASS()
class COLOSSALFIZZCO_API APrototypeMenuPawn : public APawn
{
 GENERATED_BODY()
public:
 APrototypeMenuPawn();
};

/** UI-only lobby. The shop map retains its existing game mode and character. */
UCLASS()
class COLOSSALFIZZCO_API APrototypeMenuGameMode : public AGameModeBase
{
 GENERATED_BODY()
public:
 APrototypeMenuGameMode();
 static const TCHAR* MenuMap();
 static const TCHAR* ShopMap();
};
