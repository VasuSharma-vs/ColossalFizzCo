#pragma once

#include "Animation/AnimNode_ColaCustomerArm.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AnimGraphNode_ColaCustomerArm.generated.h"

UCLASS()
class UAnimGraphNode_ColaCustomerArm : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_ColaCustomerArm Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

protected:
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
