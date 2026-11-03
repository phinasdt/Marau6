#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Marau6Character.h"
#include "Marau6MovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"

namespace
{
AMarau6Character* SpawnPrototypeCharacter(FAutomationTestBase& Test, UWorld* World)
{
    UClass* Class = LoadClass<AMarau6Character>(nullptr,
        TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if (!Test.TestNotNull(TEXT("First person character class"), Class)) return nullptr;
    AMarau6Character* Character = World->SpawnActor<AMarau6Character>(Class);
    Test.TestNotNull(TEXT("Character spawned"), Character);
    return Character;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6SprintMovementTest, "Marau6.Combat.SprintMovement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarau6SprintMovementTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    AMarau6Character* Character = SpawnPrototypeCharacter(*this, World);
    if (!Character) return false;
    UMarau6MovementComponent* Movement = Character->GetMarau6Movement();
    if (!TestNotNull(TEXT("Character uses the Marau6 movement component"), Movement)) return false;
    Movement->SetMovementMode(MOVE_Walking);
    const float Walk = Movement->GetMaxSpeed();
    const FVector Forward = Character->GetActorForwardVector();

    Movement->Acceleration = Forward * 1000.f;
    Movement->bWantsToSprint = true;
    TestTrue(TEXT("Forward sprint is sprinting"), Movement->IsSprinting());
    TestEqual(TEXT("Sprint speed"), Movement->GetMaxSpeed(), Walk * Movement->SprintSpeedMultiplier);

    Movement->Acceleration = Character->GetActorRightVector() * 1000.f;
    TestFalse(TEXT("Strafing does not sprint"), Movement->IsSprinting());
    TestEqual(TEXT("Strafing keeps walking speed"), Movement->GetMaxSpeed(), Walk);

    Movement->Acceleration = Forward * 1000.f;
    Movement->bWantsToAim = true;
    TestEqual(TEXT("Sprint wins over a held aim"), Movement->GetMaxSpeed(), Walk * Movement->SprintSpeedMultiplier);
    Movement->bWantsToSprint = false;
    TestEqual(TEXT("Aiming slows walking"), Movement->GetMaxSpeed(), Walk * Movement->AimSpeedMultiplier);
    TestTrue(TEXT("Aim reported once not sprinting"), Character->IsAiming());

    Movement->SetMovementMode(MOVE_Falling);
    Movement->bWantsToSprint = true;
    TestFalse(TEXT("No sprint in the air"), Movement->IsSprinting());

    // The server rebuilds both wishes from each move's flags.
    Movement->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_Custom_0);
    TestTrue(TEXT("Sprint flag received"), Movement->bWantsToSprint);
    TestFalse(TEXT("Aim flag cleared"), Movement->bWantsToAim);
    Movement->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_Custom_1);
    TestFalse(TEXT("Sprint flag cleared"), Movement->bWantsToSprint);
    TestTrue(TEXT("Aim flag received"), Movement->bWantsToAim);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6RagdollTest, "Marau6.Combat.Ragdoll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarau6RagdollTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    AMarau6Character* Character = SpawnPrototypeCharacter(*this, World);
    if (!Character) return false;
    Character->DispatchBeginPlay();
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    const FTransform Neutral = Character->GetMesh()->GetRelativeTransform();

    FDamageEvent Event;
    Character->TakeDamage(100.f, Event, nullptr, nullptr);
    TestFalse(TEXT("Lethal damage kills"), Character->IsAlive());
    TestTrue(TEXT("Body simulates physics"), Character->GetMesh()->IsSimulatingPhysics());
    TestEqual(TEXT("Body uses the ragdoll profile"), Character->GetMesh()->GetCollisionProfileName(), FName(TEXT("Ragdoll")));
    TestEqual(TEXT("Dead capsule stops blocking"), Character->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Owner's arms hidden"), Character->GetFirstPersonMesh()->IsVisible());

    // A new round revives this same pawn.
    Character->SetRagdoll(false);
    TestFalse(TEXT("Revived body is animated again"), Character->GetMesh()->IsSimulatingPhysics());
    TestTrue(TEXT("Body back on the capsule"), Character->GetMesh()->GetAttachParent() == Character->GetCapsuleComponent());
    TestTrue(TEXT("Body back in place"), Character->GetMesh()->GetRelativeTransform().Equals(Neutral, 1.e-3));
    TestEqual(TEXT("Capsule blocks again"), Character->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
    return true;
}
#endif
