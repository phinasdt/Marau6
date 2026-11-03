#include "Marau6Footsteps.h"
#include "Marau6Audio.h"
#include "Marau6Bot.h"
#include "Marau6Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UMarau6Footsteps::UMarau6Footsteps()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(false);
    static ConstructorHelpers::FObjectFinder<USoundBase> Step1(TEXT("/Game/Marau6/Audio/S_Footstep_01.S_Footstep_01"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Step2(TEXT("/Game/Marau6/Audio/S_Footstep_02.S_Footstep_02"));
    StepSounds[0] = Step1.Object;
    StepSounds[1] = Step2.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Land(TEXT("/Game/Marau6/Audio/S_Land.S_Land"));
    LandSound = Land.Object;
    StepAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("StepAttenuation"));
    StepAttenuation->Attenuation.bAttenuate = true;
    StepAttenuation->Attenuation.bSpatialize = true;
    StepAttenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
    StepAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
    StepAttenuation->Attenuation.AttenuationShapeExtents = FVector(200.f, 0.f, 0.f);
    StepAttenuation->Attenuation.FalloffDistance = 2300.f;
    Marau6Audio::ConfigureDistanceSend(StepAttenuation->Attenuation, .2f, .6f);
    OwnerAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("OwnerAttenuation"));
    Marau6Audio::ConfigureOwnerSend(OwnerAttenuation->Attenuation, .15f);
}

void UMarau6Footsteps::BeginPlay()
{
    Super::BeginPlay();
    // Dedicated servers have no listener; the movement already replicates to clients.
    if (GetNetMode() == NM_DedicatedServer) SetComponentTickEnabled(false);
}

void UMarau6Footsteps::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
    Super::TickComponent(Delta, TickType, ThisTick);
    const ACharacter* Owner = Cast<ACharacter>(GetOwner());
    const UCharacterMovementComponent* Movement = Owner ? Owner->GetCharacterMovement() : nullptr;
    if (!Movement) return;
    const AMarau6Character* Player = Cast<AMarau6Character>(Owner);
    const AMarau6Bot* Bot = Cast<AMarau6Bot>(Owner);
    const bool bAlive = Player ? Player->IsAlive() : !Bot || Bot->IsAlive();
    const float Speed = Owner->GetVelocity().Size2D();
    const bool bGrounded = Movement->IsMovingOnGround();
    const bool bOwnPlayer = Player && Owner->IsLocallyControlled();
    // Only the owner hears their own jump; everyone hears a hard landing.
    if (bAlive && bOwnPlayer && bWasGrounded && !bGrounded && Owner->GetVelocity().Z > 0.f) Play(LandSound, Marau6Audio::JumpVolume, JumpPitch);
    if (bAlive && bGrounded && !bWasGrounded && LastVerticalSpeed < -LandSpeed) Play(LandSound, Marau6Audio::LandVolume, 1.f);
    bWasGrounded = bGrounded;
    LastVerticalSpeed = Owner->GetVelocity().Z;
    if (!bAlive || !bGrounded || Movement->IsCrouching() || Speed < MinSpeed) return;
    Walked += Speed * Delta;
    if (Walked < Stride) return;
    Walked = 0.f;
    Foot ^= 1;
    Play(StepSounds[Foot], bOwnPlayer ? Marau6Audio::StepOwner : Marau6Audio::StepWorld, FMath::FRandRange(.9f, 1.1f));
}

void UMarau6Footsteps::Play(USoundBase* Sound, float Volume, float Pitch) const
{
    const ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Sound || !Owner) return;
    const bool bOwn = Owner->IsLocallyControlled() && Cast<AMarau6Character>(Owner);
    const FVector Feet = Owner->GetActorLocation()
        - FVector(0, 0, Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    UGameplayStatics::PlaySoundAtLocation(this, Sound, Feet, FRotator::ZeroRotator, Volume, Pitch, 0.f,
        bOwn ? OwnerAttenuation : StepAttenuation);
}
