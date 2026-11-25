#include "Shop/ShopTableUpgradeTerminal.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AShopTableUpgradeTerminal::AShopTableUpgradeTerminal()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	auto* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(Root);
	InteractionBox->SetRelativeLocation(FVector(0, 0, 61));
	InteractionBox->InitBoxExtent(FVector(30, 38, 61));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionObjectType(ECC_WorldStatic);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetGenerateOverlapEvents(false);
	InteractionBox->SetCanEverAffectNavigation(false);
	InteractionBox->ComponentTags.Add(TEXT("ShopInteractionPoint"));

	static const ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Housing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Housing"));
	Housing->SetupAttachment(Root);
	Housing->SetRelativeLocation(FVector(0, 0, 51));
	Housing->SetRelativeScale3D(FVector(.40, .56, 1.02));
	Housing->SetStaticMesh(Cube.Object);
	Housing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Housing->SetCanEverAffectNavigation(false);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
	Panel->SetRelativeLocation(FVector(17, 0, 99));
	Panel->SetRelativeScale3D(FVector(.20, .74, .42));
	Panel->SetStaticMesh(Cube.Object);
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->SetCanEverAffectNavigation(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(27.2, 0, 99));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(FColor(12, 32, 44));
	Label->SetWorldSize(7.5f);
	Label->SetText(NSLOCTEXT("ShopTableUpgradeTerminal", "Label", "TABLE UPGRADES\nINTERACT TO OPEN"));
}
