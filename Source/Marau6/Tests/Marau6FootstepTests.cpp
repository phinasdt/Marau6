#if WITH_DEV_AUTOMATION_TESTS
#include "Marau6Bot.h"
#include "Marau6Character.h"
#include "Marau6Footsteps.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6FootstepsTest, "Marau6.Combat.Footsteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarau6FootstepsTest::RunTest(const FString& Parameters)
{
    const UMarau6Footsteps* Steps = GetDefault<UMarau6Footsteps>();
    for (int32 Foot = 0; Foot < 2; ++Foot)
    {
        const USoundBase* Sound = Steps->StepSounds[Foot];
        if (!TestNotNull(*FString::Printf(TEXT("Footstep %d loaded"), Foot + 1), Sound)) return false;
        TestTrue(TEXT("Footstep is short"), Sound->GetDuration() > 0.f && Sound->GetDuration() < 1.f);
        TestFalse(TEXT("Footstep does not loop"), Sound->IsLooping());
    }
    TestTrue(TEXT("Alternating clips differ"), Steps->StepSounds[0] != Steps->StepSounds[1]);
    if (!TestNotNull(TEXT("Land sound loaded"), Steps->LandSound.Get())) return false;
    TestTrue(TEXT("Land is short"), Steps->LandSound->GetDuration() > 0.f && Steps->LandSound->GetDuration() < 2.f);
    TestFalse(TEXT("Land does not loop"), Steps->LandSound->IsLooping());
    TestTrue(TEXT("Jump reuses the land thud pitched up"), UMarau6Footsteps::JumpPitch > 1.f);
    const USoundAttenuation* Attenuation = Steps->StepAttenuation;
    if (!TestNotNull(TEXT("Footstep attenuation"), Attenuation)) return false;
    TestTrue(TEXT("Other players' steps are spatialized"), !!Attenuation->Attenuation.bSpatialize);
    const FTransform Origin = FTransform::Identity;
    TestTrue(TEXT("Distant steps quieter"), Attenuation->Attenuation.Evaluate(Origin, FVector(100,0,0))
        > Attenuation->Attenuation.Evaluate(Origin, FVector(1500,0,0)));
    TestTrue(TEXT("Steps inaudible past 25 m"), Attenuation->Attenuation.Evaluate(Origin, FVector(2600,0,0)) <= KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Distant steps are wetter"), !!Attenuation->Attenuation.bEnableReverbSend
        && Attenuation->Attenuation.ReverbWetLevelMax > Attenuation->Attenuation.ReverbWetLevelMin);
    const USoundAttenuation* Own = Steps->OwnerAttenuation;
    if (!TestNotNull(TEXT("Owner step attenuation"), Own)) return false;
    TestTrue(TEXT("Own steps unpanned, undamped and in the reverb"), !Own->Attenuation.bSpatialize && !Own->Attenuation.bAttenuate
        && Own->Attenuation.bEnableReverbSend && Own->Attenuation.ManualReverbSendLevel > 0.f);
    for (UClass* Class : { AMarau6Character::StaticClass(), AMarau6Bot::StaticClass() })
    {
        const auto* Property = FindFProperty<FObjectPropertyBase>(Class, TEXT("Footsteps"));
        TestTrue(*FString::Printf(TEXT("%s walks with footsteps"), *Class->GetName()), Property
            && Cast<UMarau6Footsteps>(Property->GetObjectPropertyValue_InContainer(Class->GetDefaultObject())));
    }
    return true;
}
#endif
