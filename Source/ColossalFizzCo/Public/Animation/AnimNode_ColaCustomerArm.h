#pragma once

#include "Animation/BoneReference.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_ColaCustomerArm.generated.h"

/** Poses the NPC's arm for drinking and throwing over the locomotion pose. */
USTRUCT(BlueprintInternalUseOnly)
struct COLOSSALFIZZCO_API FAnimNode_ColaCustomerArm : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* AnimInstance) override;

protected:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms) override;

private:
	FBoneReference UpperArm{TEXT("upperarm_r")};
	FBoneReference Forearm{TEXT("lowerarm_r")};
	FBoneReference Hand{TEXT("hand_r")};
	FBoneReference Head{TEXT("head")};

	// Game-thread inputs captured before the worker thread evaluates the pose.
	float Lift = 0.f;
	float HeadTilt = 0.f;
	double ThrowElapsed = -1.0;
	FVector BodyForward = FVector::ForwardVector;
	FVector BodyRight = FVector::RightVector;
	FVector BodyUp = FVector::UpVector;
	FTransform BottleMouthInHand = FTransform::Identity;
};
