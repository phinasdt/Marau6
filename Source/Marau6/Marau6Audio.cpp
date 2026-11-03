#include "Marau6Audio.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundAttenuation.h"

void Marau6Audio::ConfigureAmbientReverb(UReverbEffect& Reverb)
{
    Reverb.bBypassEarlyReflections = false;
    Reverb.ReflectionsDelay = .01f;
    Reverb.ReflectionsGain = .3f;
    Reverb.GainHF = .55f;
    Reverb.bBypassLateReflections = false;
    Reverb.LateDelay = .015f;
    Reverb.DecayTime = 1.1f;
    Reverb.Density = .85f;
    Reverb.Diffusion = .9f;
    Reverb.AirAbsorptionGainHF = .99f;
    Reverb.DecayHFRatio = .6f;
    Reverb.LateGain = 1.1f;
    Reverb.Gain = .32f;
}

void Marau6Audio::ConfigureGunfireFalloff(FSoundAttenuationSettings& Settings, float InnerRadius, float Falloff)
{
    Settings.bAttenuate = true;
    Settings.bSpatialize = true;
    Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
    Settings.dBAttenuationAtMax = -60.f;
    Settings.FalloffMode = ENaturalSoundFalloffMode::Silent;
    Settings.AttenuationShape = EAttenuationShape::Sphere;
    Settings.AttenuationShapeExtents = FVector(InnerRadius, 0.f, 0.f);
    Settings.FalloffDistance = Falloff;
}

void Marau6Audio::ConfigureOwnerSend(FSoundAttenuationSettings& Settings, float Send)
{
    Settings.bAttenuate = false;
    Settings.bSpatialize = false;
    Settings.bEnableReverbSend = true;
    Settings.ReverbSendMethod = EReverbSendMethod::Manual;
    Settings.ManualReverbSendLevel = Send;
}

void Marau6Audio::ConfigureDistanceSend(FSoundAttenuationSettings& Settings, float NearSend, float FarSend)
{
    Settings.bEnableReverbSend = true;
    Settings.ReverbSendMethod = EReverbSendMethod::Linear;
    Settings.ReverbWetLevelMin = NearSend;
    Settings.ReverbWetLevelMax = FarSend;
    Settings.ReverbDistanceMin = UE_REAL_TO_FLOAT(Settings.AttenuationShapeExtents.X);
    Settings.ReverbDistanceMax = Settings.ReverbDistanceMin + Settings.FalloffDistance;
}
