#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Marau6MovementComponent.generated.h"

/**
 * Sprint and aim speeds travel with each saved move, so the server applies the
 * same speed the owning client predicted and never trusts a client speed value.
 */
UCLASS()
class MARAU6_API UMarau6MovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()

    friend class FMarau6SprintMovementTest;

public:
    UMarau6MovementComponent();

    UPROPERTY(EditDefaultsOnly, Category = "Marau6|Movement")
    float SprintSpeedMultiplier = 1.3f;

    UPROPERTY(EditDefaultsOnly, Category = "Marau6|Movement")
    float AimSpeedMultiplier = 0.6f;

    /** Accepts input mostly forward; strafing or backpedalling falls back to walking. */
    UPROPERTY(EditDefaultsOnly, Category = "Marau6|Movement")
    float SprintMinForwardDot = 0.7f;

    bool bWantsToSprint = false;
    bool bWantsToAim = false;

    bool IsSprinting() const;

    virtual float GetMaxSpeed() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};
