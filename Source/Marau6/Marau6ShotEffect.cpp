#include "Marau6ShotEffect.h"
#include "Marau6Audio.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AMarau6ShotEffect::AMarau6ShotEffect()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    static ConstructorHelpers::FObjectFinder<USoundBase> Sound(
        TEXT("/Game/Marau6/Audio/S_RifleFire.S_RifleFire"));
    ShotSound = Sound.Object;
    ShotAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("ShotAttenuation"));
    Marau6Audio::ConfigureGunfireFalloff(ShotAttenuation->Attenuation, Marau6Audio::GunfireInner, Marau6Audio::GunfireFalloff);
    Marau6Audio::ConfigureDistanceSend(ShotAttenuation->Attenuation, .3f, .8f);
    OwnerShotAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("OwnerShotAttenuation"));
    Marau6Audio::ConfigureOwnerSend(OwnerShotAttenuation->Attenuation, .3f);
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AMarau6ShotEffect::Present(USkeletalMeshComponent* Rifle, bool bFirstPerson,
    const FVector& /*Point*/, const FVector& /*Normal*/, bool /*bBlockingHit*/, bool /*bHitCharacter*/)
{
    if (!GetWorld() || GetNetMode() == NM_DedicatedServer) { Destroy(); return; }
    // Fire-and-forget audio outlives this actor. Only the owning listener gets
    // unpanned playback; other clients hear the world-space rifle. Both feed the
    // ambient reverb, distant shots more.
    if (ShotSound && Rifle)
    {
        const float Pitch = FMath::FRandRange(.95f, 1.05f);
        if (bFirstPerson)
        {
            UGameplayStatics::PlaySoundAtLocation(this, ShotSound, Rifle->GetComponentLocation(),
                FRotator::ZeroRotator, Marau6Audio::ShotOwner, Pitch, 0.f, OwnerShotAttenuation);
        }
        else
        {
            UGameplayStatics::PlaySoundAtLocation(this, ShotSound,
                Rifle->GetSocketLocation(TEXT("Muzzle")), FRotator::ZeroRotator,
                Marau6Audio::ShotWorld, Pitch, 0.f, ShotAttenuation);
        }
    }
    SetLifeSpan(.22f);
}
