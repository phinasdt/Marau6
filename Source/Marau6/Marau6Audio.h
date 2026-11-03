#pragma once
#include "CoreMinimal.h"
class UReverbEffect;
struct FSoundAttenuationSettings;

/** Shared mix for the prototype: one soft room reverb and how each sound feeds it. */
namespace Marau6Audio
{
    inline const FName AmbientReverbTag(TEXT("Marau6Ambient"));
    static constexpr float AmbientReverbVolume = .5f;

    // Mix. The rifle fire clip (S_RifleFire, -21 LUFS) is the only gunfire sound. It
    // is raised to the level of the old Uzi + report pair (two -18 LUFS layers at
    // .45/.54) that the footsteps were balanced against.
    // Footsteps are ~15 dB quieter at the source than gunfire, so they sit near full volume.
    // Raised on request, twice (.88/1.05, then 1.8/2.1): gunfire should dominate.
    static constexpr float ShotOwner = 3.0f, ShotWorld = 3.5f;
    static constexpr float GunfireInner = 300.f, GunfireFalloff = 7700.f;
    static constexpr float StepOwner = .8f, StepWorld = 1.4f;
    static constexpr float LandVolume = .8f, JumpVolume = .6f;
    // Reload gear sounds are authored near footstep loudness (about -20 LUFS).
    static constexpr float ReloadVolume = 1.f;
    // Decompression gas on raid entry, pitched up a little for a tighter hiss.
    static constexpr float GasVolume = .5f, GasPitch = 1.15f;

    /** Short, warm room tail: grounds dry clips without washing out gunfire. */
    void ConfigureAmbientReverb(UReverbEffect& Reverb);

    /** Sounds the owner hears at their own head: no panning or distance, constant send. */
    void ConfigureOwnerSend(FSoundAttenuationSettings& Settings, float Send);

    /** Distant gunfire drops in dB with distance instead of staying loud until the far edge. */
    void ConfigureGunfireFalloff(FSoundAttenuationSettings& Settings, float InnerRadius, float Falloff);

    /** World sounds get wetter with distance, between the attenuation's inner radius and its silence. */
    void ConfigureDistanceSend(FSoundAttenuationSettings& Settings, float NearSend, float FarSend);
}
