#include "Marau6Ragdoll.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

void Marau6Ragdoll::Start(ACharacter* Character, const FVector& Impulse)
{
    if (!Character) return;
    Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (Character->GetNetMode() == NM_DedicatedServer) return;
    USkeletalMeshComponent* Body = Character->GetMesh();
    if (!Body || !Body->GetPhysicsAsset()) return;
    Body->SetCollisionProfileName(TEXT("Ragdoll"));
    Body->SetAllBodiesSimulatePhysics(true);
    Body->SetSimulatePhysics(true);
    Body->WakeAllRigidBodies();
    if (!Impulse.IsNearlyZero()) Body->AddImpulse(Impulse, TEXT("spine_03"), true);
}

void Marau6Ragdoll::Stop(ACharacter* Character, const FTransform& MeshRelative, FName MeshCollisionProfile)
{
    if (!Character) return;
    Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    USkeletalMeshComponent* Body = Character->GetMesh();
    if (!Body || !Body->IsSimulatingPhysics()) return;
    Body->SetSimulatePhysics(false);
    Body->SetAllBodiesSimulatePhysics(false);
    Body->SetCollisionProfileName(MeshCollisionProfile);
    Body->AttachToComponent(Character->GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    Body->SetRelativeTransform(MeshRelative);
}
