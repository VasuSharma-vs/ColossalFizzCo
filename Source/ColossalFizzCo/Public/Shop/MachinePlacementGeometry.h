#pragma once

#include "CoreMinimal.h"

class APawn;
class AActor;

namespace ShopMachinePlacement
{
	inline constexpr double MaxPreviewDistance = 1400.0;

	// Fills OutPose even when the target is blocked, so the player can see where it would go.
	bool TracePlacement(const AActor& Machine, const APawn& Player,
		const FVector& Eye, const FVector& Direction, float Yaw, FTransform& OutPose);
	bool CanPlace(const AActor& Machine, const APawn& Player, const FTransform& Pose);
}
