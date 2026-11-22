#include "Animation/AnimNode_ColaCustomerArm.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Shop/ColaTableCustomerComponent.h"
#include "TwoBoneIK.h"

void FAnimNode_ColaCustomerArm::PreUpdate(const UAnimInstance* AnimInstance)
{
	Lift = 0.f;
	HeadTilt = 0.f;
	ThrowElapsed = -1.0;
	const USkeletalMeshComponent* Mesh = AnimInstance->GetSkelMeshComponent();
	const AActor* Owner = Mesh ? Mesh->GetOwner() : nullptr;
	const auto* Customer = Owner ? Owner->FindComponentByClass<UColaTableCustomerComponent>() : nullptr;
	if (!Customer) return;

	Customer->GetSipPoseWeights(Lift, HeadTilt);
	ThrowElapsed = Customer->GetThrowElapsed();
	if (Lift <= UE_KINDA_SMALL_NUMBER && ThrowElapsed <= 0.0) return;

	const FTransform& MeshTransform = Mesh->GetComponentTransform();
	BodyForward = MeshTransform.InverseTransformVectorNoScale(Owner->GetActorForwardVector());
	BodyRight = MeshTransform.InverseTransformVectorNoScale(Owner->GetActorRightVector());
	BodyUp = MeshTransform.InverseTransformVectorNoScale(Owner->GetActorUpVector());
	BottleMouthInHand = Customer->GetBottleMouthInHand();
}

void FAnimNode_ColaCustomerArm::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	UpperArm.Initialize(RequiredBones);
	Forearm.Initialize(RequiredBones);
	Hand.Initialize(RequiredBones);
	Head.Initialize(RequiredBones);
}

bool FAnimNode_ColaCustomerArm::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return UpperArm.IsValidToEvaluate() && Forearm.IsValidToEvaluate() && Hand.IsValidToEvaluate() && Head.IsValidToEvaluate();
}

void FAnimNode_ColaCustomerArm::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	const bool bThrowing = ThrowElapsed > 0.0;
	if (Lift <= UE_KINDA_SMALL_NUMBER && !bThrowing) return;

	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex UpperIndex = UpperArm.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex ForearmIndex = Forearm.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex HandIndex = Hand.GetCompactPoseIndex(Bones);
	FTransform Shoulder = Output.Pose.GetComponentSpaceTransform(UpperIndex);
	FTransform Elbow = Output.Pose.GetComponentSpaceTransform(ForearmIndex);
	FTransform Wrist = Output.Pose.GetComponentSpaceTransform(HandIndex);
	const double ArmLength = FVector::Distance(Shoulder.GetLocation(), Elbow.GetLocation())
		+ FVector::Distance(Elbow.GetLocation(), Wrist.GetLocation());

	double Blend = Lift;
	FVector WristTarget;
	FQuat WristTargetRotation;
	if (bThrowing)
	{
		const double Windup = FMath::SmoothStep(0.0, ColaCustomerThrow::WindupEnd, ThrowElapsed);
		const double Swing = FMath::SmoothStep(ColaCustomerThrow::WindupEnd, ColaCustomerThrow::FollowThroughEnd, ThrowElapsed);
		const double Recovery = FMath::SmoothStep(ColaCustomerThrow::FollowThroughEnd, ColaCustomerThrow::RecoveryEnd, ThrowElapsed);
		Blend = Windup * (1.0 - Recovery);

		// The underhand swing passes low before rising into the follow-through.
		const FVector Backswing = -.49 * BodyForward + .20 * BodyRight - .74 * BodyUp;
		const FVector LowPoint = .72 * BodyForward + .26 * BodyRight - 1.05 * BodyUp;
		const FVector FollowThrough = .82 * BodyForward + .20 * BodyRight + .34 * BodyUp;
		const FVector SwingOffset = FMath::Lerp(FMath::Lerp(Backswing, LowPoint, Swing),
			FMath::Lerp(LowPoint, FollowThrough, Swing), Swing);
		WristTarget = Shoulder.GetLocation() + ArmLength * SwingOffset;

		const FVector BottleUp = (BodyUp + FMath::Lerp(-.27, .15, Swing) * BodyForward).GetSafeNormal();
		WristTargetRotation = FRotationMatrix::MakeFromZY(BottleUp, BodyRight).ToQuat()
			* BottleMouthInHand.GetRotation().Inverse();
	}
	else
	{
		const FCompactPoseBoneIndex HeadIndex = Head.GetCompactPoseIndex(Bones);
		FTransform HeadPose = Output.Pose.GetComponentSpaceTransform(HeadIndex);
		constexpr double HeadTiltDegrees = 10.0;
		const FQuat HeadLean(BodyRight, -FMath::DegreesToRadians(HeadTiltDegrees * HeadTilt));
		HeadPose.SetRotation(HeadLean * HeadPose.GetRotation());

		// Rotate the lip offset about the same pivot so the bottle follows the head.
		const FVector Lips = HeadPose.GetLocation() + HeadLean.RotateVector(7.8 * BodyForward - 3.4 * BodyUp);
		const FVector NeckDirection = HeadLean.RotateVector((-BodyForward - .17 * BodyUp).GetSafeNormal());
		WristTargetRotation = FRotationMatrix::MakeFromZY(NeckDirection, BodyRight).ToQuat()
			* BottleMouthInHand.GetRotation().Inverse();
		WristTarget = Lips - WristTargetRotation.RotateVector(BottleMouthInHand.GetLocation() * Wrist.GetScale3D());
		OutBoneTransforms.Emplace(HeadIndex, HeadPose);
	}

	const FVector ElbowTarget = Shoulder.GetLocation() + ArmLength * (.65 * BodyRight - .55 * BodyUp);
	const FVector ElbowPole = FMath::Lerp(Elbow.GetLocation(), ElbowTarget, Blend);
	const FVector Effector = FMath::Lerp(Wrist.GetLocation(), WristTarget, Blend);
	const FQuat WristRotation = FQuat::Slerp(Wrist.GetRotation(), WristTargetRotation, Blend);

	AnimationCore::SolveTwoBoneIK(Shoulder, Elbow, Wrist, ElbowPole, Effector, false, 1.0, 1.0);
	Wrist.SetRotation(WristRotation);
	OutBoneTransforms.Emplace(UpperIndex, Shoulder);
	OutBoneTransforms.Emplace(ForearmIndex, Elbow);
	OutBoneTransforms.Emplace(HandIndex, Wrist);
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
