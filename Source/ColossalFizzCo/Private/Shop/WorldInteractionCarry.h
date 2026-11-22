#pragma once

#include "CoreMinimal.h"
#include "UObject/Script.h"

class AActor;
class APawn;

namespace WorldInteractionCarry
{
    UObject* GetCarryReference(const APawn& Player,FName PropertyName);
    bool PublishHeldActor(APawn& Player, AActor& Item);
    void ClearHeldActor(APawn& Player, const AActor& Item);
    bool InvokeCarryRelease(APawn& Player,const TCHAR* FunctionName,UObject* HeldObject,EFunctionFlags NetRole=FUNC_NetServer);
    void ClearLocalMountedItem(AActor& Item);
}
