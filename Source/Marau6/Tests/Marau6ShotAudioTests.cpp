#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/ReverbEffect.h"
#include "Marau6PlayerController.h"
#include "Marau6Audio.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6ShotAudioTest, "Marau6.Combat.ShotAudio",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarau6ShotAudioTest::RunTest(const FString& Parameters)
{
    UClass* Class = LoadClass<AActor>(nullptr, TEXT("/Script/Marau6.Marau6ShotEffect"));
    if (!TestNotNull(TEXT("Shot effect class"), Class)) return false;
    const auto* SoundProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("ShotSound"));
    const auto* AttenuationProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("ShotAttenuation"));
    if (!TestNotNull(TEXT("Shot audio referenced by effect"), SoundProperty)
        || !TestNotNull(TEXT("Remote sound attenuation configured"), AttenuationProperty)) return false;
    const UObject* Defaults = Class->GetDefaultObject();
    USoundBase* Sound = Cast<USoundBase>(SoundProperty->GetObjectPropertyValue_InContainer(Defaults));
    USoundAttenuation* Attenuation = Cast<USoundAttenuation>(AttenuationProperty->GetObjectPropertyValue_InContainer(Defaults));
    if (!TestNotNull(TEXT("Sound asset loaded"), Sound) || !TestNotNull(TEXT("Attenuation object"), Attenuation)) return false;
    TestTrue(TEXT("Shot has a short finite duration"), Sound->GetDuration() > 0.f && Sound->GetDuration() < 5.f);
    TestFalse(TEXT("Shot does not loop"), Sound->IsLooping());
    TestTrue(TEXT("Other player's sound is spatialized"), !!Attenuation->Attenuation.bSpatialize);
    TestTrue(TEXT("Distance attenuation enabled"), !!Attenuation->Attenuation.bAttenuate);
    const FTransform Origin = FTransform::Identity;
    const float Near = Attenuation->Attenuation.Evaluate(Origin, FVector(100,0,0));
    const float Far = Attenuation->Attenuation.Evaluate(Origin, FVector(6000,0,0));
    TestTrue(TEXT("Distant rifle quieter than nearby rifle"), Near > Far && Far > 0.f);
    TestTrue(TEXT("Beyond range sound inaudible"), Attenuation->Attenuation.Evaluate(Origin, FVector(9000,0,0)) <= KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Background gunfire at 20 m is well below full volume"), Attenuation->Attenuation.Evaluate(Origin, FVector(2000,0,0)) < .35f);
    TestTrue(TEXT("Shot is the rifle fire clip"), Sound->GetName() == TEXT("S_RifleFire"));
    // The clip is ~3 dB under the old -18 LUFS layers the footsteps were balanced against.
    TestTrue(TEXT("Own footsteps not buried under own shots"),
        Marau6Audio::StepOwner >= FMath::Pow(10.f, -2.8f / 20.f) * Marau6Audio::ShotOwner * .35f);
    TestTrue(TEXT("Distant shots are wetter"), !!Attenuation->Attenuation.bEnableReverbSend
        && Attenuation->Attenuation.ReverbWetLevelMax > Attenuation->Attenuation.ReverbWetLevelMin);
    const auto* OwnerProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("OwnerShotAttenuation"));
    const USoundAttenuation* Owner = OwnerProperty
        ? Cast<USoundAttenuation>(OwnerProperty->GetObjectPropertyValue_InContainer(Defaults)) : nullptr;
    if (!TestNotNull(TEXT("Owner shot attenuation"), Owner)) return false;
    TestFalse(TEXT("Owner shot is not panned"), !!Owner->Attenuation.bSpatialize);
    TestFalse(TEXT("Owner shot is not distance attenuated"), !!Owner->Attenuation.bAttenuate);
    TestTrue(TEXT("Owner shot feeds the reverb"), Owner->Attenuation.bEnableReverbSend
        && Owner->Attenuation.ReverbSendMethod == EReverbSendMethod::Manual && Owner->Attenuation.ManualReverbSendLevel > 0.f);
    const auto* ReverbProperty = FindFProperty<FObjectPropertyBase>(AMarau6PlayerController::StaticClass(), TEXT("AmbientReverb"));
    const UReverbEffect* Reverb = ReverbProperty ? Cast<UReverbEffect>(ReverbProperty->GetObjectPropertyValue_InContainer(
        AMarau6PlayerController::StaticClass()->GetDefaultObject())) : nullptr;
    if (!TestNotNull(TEXT("Player controller carries the ambient reverb"), Reverb)) return false;
    TestTrue(TEXT("Ambient tail is short"), Reverb->DecayTime > .3f && Reverb->DecayTime < 2.f);
    TestTrue(TEXT("Ambient reverb audible"), Reverb->Gain > 0.f && !Reverb->bBypassLateReflections);
    return true;
}
#endif
