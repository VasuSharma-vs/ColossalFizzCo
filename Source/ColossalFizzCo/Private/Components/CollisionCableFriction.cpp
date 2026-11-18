#include "Components/CollisionCableComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

void UCollisionCableComponent::ApplyContactFriction(float Dt,const TArray<FVector>& Before,TArray<FSurfaceFrictionBudget>& Budgets)
{
    if (!HasCollision() || Dt<=0) return;
    const double Friction=FMath::Clamp(CollisionFriction,0.f,1.f);
    const FVector Acceleration=CableForce+FVector(0,0,GetWorld()->GetGravityZ()*CableGravityScale);
    for (int32 I=0;I<Particles.Num();++I)
    {
        if (IsPinned(I) || !Before.IsValidIndex(I) || (bEnableSleeping && Particles[I].bSleeping)) continue;
        auto& Particle=Particles[I];
        TArray<FVector,TInlineAllocator<3>> Normals;
        auto Apply=[&](const FLineContact& Contact)
        {
            if (!Contact.Component.IsValid() || Contact.Normal.IsNearlyZero()) return;
            const FVector Normal=Contact.Normal.GetSafeNormal();
            // Keep a contact reached during integration even if length/self-contact
            // constraints subsequently lift the point slightly. Velocity still holds
            // the integrated motion here; an intentionally lifted point leaves both tests.
            const double FinalDistance=FVector::DotProduct(Particle.Position-Contact.Point,Normal);
            const double IntegratedDistance=FVector::DotProduct(Before[I]+Particle.Velocity*Dt-Contact.Point,Normal);
            if (FMath::Min(FinalDistance,IntegratedDistance)>CollisionRadius()+.02) return;
            for (const auto& Existing:Normals) if (FVector::DotProduct(Existing,Normal)>.99) return;
            Normals.Add(Normal); Particle.ContactNormal+=Normal;
            if (Friction<=0) return;
            const FVector Recovery=(bSmoothCollisionRecovery ? Particle.ContactCorrectionThisStep : FVector::ZeroVector)+Particle.SelfContactCorrectionThisStep;
            const FVector SurfaceVelocity=Contact.Component->IsSimulatingPhysics()
                ? Contact.Component->GetPhysicsLinearVelocityAtPoint(Particle.Position) : Contact.Component->GetComponentVelocity();
            const FVector RelativeMove=Particle.Position-Before[I]-SurfaceVelocity*Dt;
            const FVector PhysicalMove=RelativeMove-Recovery;
            // Repair along another contact's normal can be tangent to this floor.
            // It must obey this contact's friction too, or stacked spans creep.
            const FVector Tangent=FVector::VectorPlaneProject(RelativeMove,Normal);
            const double Distance=Tangent.Size();
            // This displacement budget is mu*N*dt^2/m. Mass cancels for an
            // acceleration-driven load; constraints/grabs still overcome the budget.
            const double LoadMove=FMath::Max(0.,-FVector::DotProduct(Acceleration,Normal))*Dt*Dt;
            const double Limit=Friction*LoadMove;
            if (Distance>UE_SMALL_NUMBER && Limit>0)
            {
                auto* Budget=Budgets.FindByPredicate([&](const FSurfaceFrictionBudget& Existing)
                    {return Existing.PointIndex==I && FVector::DotProduct(Existing.Normal,Normal)>.99;});
                if(!Budget)
                {
                    Budget=&Budgets.AddDefaulted_GetRef();
                    Budget->PointIndex=I;Budget->Normal=Normal;
                }
                // Both floor passes share one Coulomb cone. Iterating coupled
                // contacts must not multiply friction or resist stronger pulls.
                const FVector Target=(Budget->Applied-Tangent).GetClampedToMaxSize(Limit);
                const FVector Move=ConstrainFullCollisionMotion(I,Target-Budget->Applied);
                Budget->Applied+=Move;
                const double MoveLength=Move.Size();
                const FVector Direction=Move.GetSafeNormal();
                const double PhysicalOpposition=FMath::Clamp(-FVector::DotProduct(PhysicalMove,Direction),0.,MoveLength);
                Particle.Position+=Move;
                // Removing a repair must not create equal-and-opposite velocity.
                // Only the part opposing actual motion changes momentum.
                Particle.SelfContactCorrectionThisStep+=Move-Direction*PhysicalOpposition;
            }
        };
        if (PointLineContacts.IsValidIndex(I)) Apply(PointLineContacts[I]);
        if (SegmentLineContacts.IsValidIndex(I-1)) Apply(SegmentLineContacts[I-1]);
        if (SegmentLineContacts.IsValidIndex(I)) Apply(SegmentLineContacts[I]);
    }
}
