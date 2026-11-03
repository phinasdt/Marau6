#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Marau6Footsteps.generated.h"
class USoundBase;
class USoundAttenuation;

/** Cosmetic footsteps from replicated movement: alternating clips every stride walked,
 *  a landing thud after a fall and the same thud, pitched up, for the owner's jump. */
UCLASS(NotBlueprintable)
class UMarau6Footsteps : public UActorComponent
{
    GENERATED_BODY()
public:
    UMarau6Footsteps();
    static constexpr float Stride = 160.f;
    static constexpr float MinSpeed = 50.f;
    static constexpr float LandSpeed = 400.f;
    static constexpr float JumpPitch = 1.3f;
    /** Body and gear sounds from the same place as the steps: unpanned for the owner, at the feet for others. */
    void PlayGear(USoundBase* Sound, float Volume) const { Play(Sound, Volume, FMath::FRandRange(.97f, 1.03f)); }
protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;
private:
    friend class FMarau6FootstepsTest;
    UPROPERTY() TObjectPtr<USoundBase> StepSounds[2];
    UPROPERTY() TObjectPtr<USoundBase> LandSound;
    UPROPERTY() TObjectPtr<USoundAttenuation> StepAttenuation;
    UPROPERTY() TObjectPtr<USoundAttenuation> OwnerAttenuation;
    float Walked = 0.f;
    int32 Foot = 0;
    bool bWasGrounded = true;
    float LastVerticalSpeed = 0.f;
    void Play(USoundBase* Sound, float Volume, float Pitch) const;
};
