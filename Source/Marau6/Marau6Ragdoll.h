#pragma once

#include "CoreMinimal.h"

class ACharacter;

/**
 * Death presentation shared by players and bots. The capsule stops blocking
 * on every machine; only clients simulate the body, since nothing on the
 * server depends on where a corpse lies.
 */
namespace Marau6Ragdoll
{
    /** Hands the body to physics, pushed by Impulse (a velocity change, cm/s). */
    void Start(ACharacter* Character, const FVector& Impulse);

    /** Returns the body to the capsule, animated, as it was before Start. */
    void Stop(ACharacter* Character, const FTransform& MeshRelative, FName MeshCollisionProfile);
}
