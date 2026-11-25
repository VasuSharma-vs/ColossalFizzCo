#pragma once

#include "CoreMinimal.h"

class ACharacter;

namespace ColaCustomerMovement
{
	struct FSidestep
	{
		FVector Away;
		TOptional<FVector> PastBlocker;
	};

	bool FindSidestep(const ACharacter& Customer, const ACharacter& Blocker, const FVector& Intent,
		const FVector* BlockerSidestep, FSidestep& OutStep);
}
