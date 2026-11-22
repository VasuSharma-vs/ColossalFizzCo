#include "AnimGraphNode_ColaCustomerArm.h"

#define LOCTEXT_NAMESPACE "ColaCustomerArm"

FText UAnimGraphNode_ColaCustomerArm::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

FText UAnimGraphNode_ColaCustomerArm::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Poses the NPC for sipping from its bottle and throwing it into the bin.");
}

FText UAnimGraphNode_ColaCustomerArm::GetControllerDescription() const
{
	return LOCTEXT("Title", "Cola Customer Sip");
}

#undef LOCTEXT_NAMESPACE
