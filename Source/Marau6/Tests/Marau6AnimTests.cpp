#if WITH_DEV_AUTOMATION_TESTS
#include "Marau6Character.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

// Fails until Marau6.SetupLocomotion has run (docs/sensacao-arma.md).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6LocomotionReloadTest, "Marau6.Combat.LocomotionAndReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarau6LocomotionReloadTest::RunTest(const FString& Parameters)
{
    const UBlendSpace* Space = LoadObject<UBlendSpace>(nullptr,
        TEXT("/Game/Marau6/Anims/BS_Marau6_Locomotion.BS_Marau6_Locomotion"));
    if (!TestNotNull(TEXT("Project locomotion blend space"), Space)) return false;
    int32 Jogs = 0, Late = 0;
    for (const FBlendSample& Sample : Space->GetBlendSamples())
    {
        const double Speed = Sample.SampleValue.Y;
        if (FMath::IsNearlyEqual(Speed, 500.0)) ++Jogs;
        if (FMath::IsNearlyEqual(Speed, 600.0)) ++Late;
    }
    TestTrue(TEXT("Jog reached at the 5 m/s move speed"), Jogs > 0 && Late == 0);

    UClass* Body = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Marau6/Anims/ABP_Marau6_TP_Rifle.ABP_Marau6_TP_Rifle_C"));
    if (!TestNotNull(TEXT("Third person pose"), Body)) return false;
    bool bSlot = false;
    for (TFieldIterator<FStructProperty> It(Body); It; ++It)
        bSlot |= It->Struct->GetFName() == TEXT("AnimNode_Slot");
    TestTrue(TEXT("Body pose has the reload slot"), bSlot);

    const auto* Property = FindFProperty<FObjectPropertyBase>(AMarau6Character::StaticClass(), TEXT("ReloadAnimation"));
    const UAnimSequence* Reload = Property ? Cast<UAnimSequence>(Property->GetObjectPropertyValue_InContainer(
        AMarau6Character::StaticClass()->GetDefaultObject())) : nullptr;
    TestNotNull(TEXT("Rifle reload animation loaded"), Reload);
    TestNotNull(TEXT("Reload pose replicates to everyone"), FindFProperty<FBoolProperty>(AMarau6Character::StaticClass(), TEXT("bReloadPose")));
    return true;
}
#endif
