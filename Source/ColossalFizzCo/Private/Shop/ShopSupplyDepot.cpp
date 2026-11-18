#include "Shop/ShopSupplyDepot.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AShopSupplyDepot::AShopSupplyDepot()
{
	bReplicates=true;
	Counter=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Counter")); SetRootComponent(Counter);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Counter->SetStaticMesh(Cube.Object); Counter->SetRelativeScale3D(FVector(1.4,.7,.9));
	Supplies=CreateDefaultSubobject<UShopProductionStationComponent>(TEXT("SupplyStore"));
	Supplies->bOffersSupplies=true;
	DeliveryPoint=CreateDefaultSubobject<USceneComponent>(TEXT("DeliveryPoint")); DeliveryPoint->SetupAttachment(Counter);
	DeliveryPoint->SetRelativeLocation(FVector(-180,0,60)); Supplies->SupplyOutput=DeliveryPoint;
	auto* Control=CreateDefaultSubobject<USceneComponent>(TEXT("CustomerControls")); Control->SetupAttachment(Counter);
	Control->SetRelativeLocation(FVector(0,-85,60)); Control->ComponentTags.Add(TEXT("ShopInteractionPoint"));
	Sign=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign")); Sign->SetupAttachment(Counter);
	Sign->SetRelativeLocation(FVector(0,-52,85)); Sign->SetRelativeRotation(FRotator(0,-90,0));
	Sign->SetHorizontalAlignment(EHTA_Center); Sign->SetText(FText::FromString(TEXT("SUPPLY DEPOT\nBottles / caps / seals / containers"))); Sign->SetWorldSize(15);
}
