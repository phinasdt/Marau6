#include "Marau6MovementComponent.h"
#include "GameFramework/Character.h"

namespace
{
class FMarau6SavedMove : public FSavedMove_Character
{
    typedef FSavedMove_Character Super;
public:
    uint8 bSavedWantsToSprint : 1;
    uint8 bSavedWantsToAim : 1;

    virtual void Clear() override
    {
        Super::Clear();
        bSavedWantsToSprint = false;
        bSavedWantsToAim = false;
    }
    virtual uint8 GetCompressedFlags() const override
    {
        uint8 Flags = Super::GetCompressedFlags();
        if (bSavedWantsToSprint) Flags |= FLAG_Custom_0;
        if (bSavedWantsToAim) Flags |= FLAG_Custom_1;
        return Flags;
    }
    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
    {
        const FMarau6SavedMove* Other = static_cast<const FMarau6SavedMove*>(NewMove.Get());
        return bSavedWantsToSprint == Other->bSavedWantsToSprint && bSavedWantsToAim == Other->bSavedWantsToAim
            && Super::CanCombineWith(NewMove, Character, MaxDelta);
    }
    virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
        FNetworkPredictionData_Client_Character& ClientData) override
    {
        Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
        if (const auto* Movement = Cast<UMarau6MovementComponent>(Character->GetCharacterMovement()))
        {
            bSavedWantsToSprint = Movement->bWantsToSprint;
            bSavedWantsToAim = Movement->bWantsToAim;
        }
    }
    virtual void PrepMoveFor(ACharacter* Character) override
    {
        Super::PrepMoveFor(Character);
        if (auto* Movement = Cast<UMarau6MovementComponent>(Character->GetCharacterMovement()))
        {
            Movement->bWantsToSprint = bSavedWantsToSprint;
            Movement->bWantsToAim = bSavedWantsToAim;
        }
    }
};

class FMarau6PredictionData : public FNetworkPredictionData_Client_Character
{
public:
    explicit FMarau6PredictionData(const UCharacterMovementComponent& Movement)
        : FNetworkPredictionData_Client_Character(Movement) {}
    virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FMarau6SavedMove()); }
};
}

UMarau6MovementComponent::UMarau6MovementComponent()
{
    MaxWalkSpeed = 500.f;
}

bool UMarau6MovementComponent::IsSprinting() const
{
    if (!bWantsToSprint || !IsMovingOnGround() || !UpdatedComponent) return false;
    const FVector Direction = GetCurrentAcceleration().GetSafeNormal2D();
    return !Direction.IsZero() && (Direction | UpdatedComponent->GetForwardVector()) >= SprintMinForwardDot;
}

float UMarau6MovementComponent::GetMaxSpeed() const
{
    const float Speed = Super::GetMaxSpeed();
    if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking) return Speed;
    if (IsSprinting()) return Speed * SprintSpeedMultiplier;
    return bWantsToAim ? Speed * AimSpeedMultiplier : Speed;
}

void UMarau6MovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
    bWantsToAim = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UMarau6MovementComponent::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
    {
        UMarau6MovementComponent* Mutable = const_cast<UMarau6MovementComponent*>(this);
        Mutable->ClientPredictionData = new FMarau6PredictionData(*this);
    }
    return ClientPredictionData;
}
