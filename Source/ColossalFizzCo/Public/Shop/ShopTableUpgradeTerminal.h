#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopTableUpgradeTerminal.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(Blueprintable, meta=(DisplayName="Shop Table Upgrade Terminal"))
class COLOSSALFIZZCO_API AShopTableUpgradeTerminal : public AActor
{
	GENERATED_BODY()

public:
	AShopTableUpgradeTerminal();

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Terminal", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBoxComponent> InteractionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Terminal", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Housing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Terminal", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> Panel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Terminal", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UTextRenderComponent> Label;
};
