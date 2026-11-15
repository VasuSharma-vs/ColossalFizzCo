#pragma once
#include "CoreMinimal.h"
#include "PropProductReference.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UMaterialInterface;
class UFXSystemAsset;

UENUM(BlueprintType)
enum class EPropProductCategory : uint8 { Bottle, Content, Cap, Other };

/** Stable identity only. Names and category filters are supplied by the catalog editor. */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPropProductReference
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Product") int32 PID=0;
 FPropProductReference()=default;
 explicit FPropProductReference(int32 Value):PID(Value){}
 bool IsSet() const{return PID>0;}
 bool operator==(const FPropProductReference& Other)const{return PID==Other.PID;}
};

/** One content product can use different geometry in each supported bottle. */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPropBottleContentVisual
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ProductCategory="Bottle")) FPropProductReference Bottle;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<USkeletalMesh> ActiveMesh;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<TObjectPtr<UMaterialInterface>> Materials;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UFXSystemAsset> Effects;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform Attachment;
};
