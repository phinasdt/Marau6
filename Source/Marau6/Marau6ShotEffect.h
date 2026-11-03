#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Marau6ShotEffect.generated.h"
class USkeletalMeshComponent;
class USoundBase;
class USoundAttenuation;

/**
 * Local presentation only: the shot sound. Never participates in hit detection or
 * replication. The flash, light, tracer and impact are drawn by AMarau6ShotFX.
 */
UCLASS(NotBlueprintable)
class AMarau6ShotEffect : public AActor
{
    GENERATED_BODY()
public:
    AMarau6ShotEffect();
    void Present(USkeletalMeshComponent* Rifle, bool bFirstPerson,
        const FVector& Point, const FVector& Normal, bool bBlockingHit, bool bHitCharacter);
private:
    UPROPERTY() TObjectPtr<USoundBase> ShotSound;
    UPROPERTY() TObjectPtr<USoundAttenuation> ShotAttenuation;
    UPROPERTY() TObjectPtr<USoundAttenuation> OwnerShotAttenuation;
};
