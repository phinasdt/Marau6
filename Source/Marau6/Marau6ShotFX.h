#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Marau6ShotFX.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialBillboardComponent;

/**
 * Cosmetic, client-only presentation of one accepted shot, layered on top of
 * AMarau6ShotEffect (which plays the shot sound): a textured muzzle flash and light,
 * puffs of muzzle smoke, a tracer and a smoke trail along the server's hit line, and
 * an impact that is dust, sparks and a bullet hole on surfaces or blood on characters.
 */
UCLASS()
class AMarau6ShotFX : public AActor
{
    GENERATED_BODY()

public:
    AMarau6ShotFX();

    /**
     * For the owner, ViewOrigin and FirstPersonScale map the first person muzzle
     * to where it is rendered, so world-space trails leave the visible barrel.
     */
    void Present(USkeletalMeshComponent* Rifle, bool bFirstPerson, const FVector& End, const FVector& Normal,
        bool bBlockingHit, bool bHitCharacter, const FVector& ViewOrigin, float FirstPersonScale);

    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY() TObjectPtr<USceneComponent> FlashRoot;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FlashPlanes;
    UPROPERTY() TObjectPtr<UPointLightComponent> FlashLight;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Tracer;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Smoke;
    UPROPERTY() TObjectPtr<UMaterialBillboardComponent> Puff;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Debris;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SmokeMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PuffMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DropMaterial;
    UPROPERTY() TArray<TObjectPtr<UMaterialBillboardComponent>> MuzzleSmoke;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> MuzzleSmokeMaterials;

    FVector TrailStart = FVector::ZeroVector;
    FVector TrailDirection = FVector::ForwardVector;
    float TrailLength = 0.f;
    FVector ImpactPoint = FVector::ZeroVector;
    FVector ImpactNormal = FVector::UpVector;
    TArray<FVector> DebrisStart;
    TArray<FVector> DebrisVelocity;
    struct FMuzzlePuff { FVector Start, Velocity; float Radius, Seconds; };
    TArray<FMuzzlePuff> MuzzlePuffs;
    float MuzzleSmokeOpacity = 0.f;
    float MuzzleSmokeRise = 0.f;
    float PuffMaxRadius = 1.f;
    float Age = 0.f;
    bool bFlash = false;
    bool bImpact = false;
    bool bBlood = false;
    bool bDecals = false;

    void SpawnDecals();
    static void PlaceTube(UStaticMeshComponent* Tube, const FVector& From, const FVector& To, float Radius);
};
