#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Marau6Character.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/OutputDevice.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6WeaponVisualTest, "Marau6.Combat.WeaponVisuals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarau6WeaponVisualTest::RunTest(const FString& Parameters)
{
    struct FNullReferenceCapture : FOutputDevice
    {
        FThreadSafeCounter Count;
        virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type, const FName&) override
        {
            if (FCString::Strstr(Message, TEXT("Accessed None"))) Count.Increment();
        }
    } Capture;
    GLog->AddOutputDevice(&Capture);
    ON_SCOPE_EXIT { GLog->RemoveOutputDevice(&Capture); };
    UClass* Class = LoadClass<AMarau6Character>(nullptr,
        TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if (!TestNotNull(TEXT("First person character class"), Class)) return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        if (World->IsRooted()) World->RemoveFromRoot();
    };
    AMarau6Character* Character = World->SpawnActor<AMarau6Character>(Class);
    if (!TestNotNull(TEXT("Character"), Character)) return false;
    Character->DispatchBeginPlay();
    USkeletalMeshComponent* FP = FindObject<USkeletalMeshComponent>(Character, TEXT("FirstPersonRifle"));
    USkeletalMeshComponent* TP = FindObject<USkeletalMeshComponent>(Character, TEXT("ThirdPersonRifle"));
    if (!TestNotNull(TEXT("First person rifle exists"), FP)
        || !TestNotNull(TEXT("Third person rifle exists"), TP)) return false;
    TestTrue(TEXT("First rifle attached to owner's first person body"), FP->GetAttachParent() == Character->GetFirstPersonMesh());
    TestTrue(TEXT("Third rifle attached to world body"), TP->GetAttachParent() == Character->GetMesh());
    TestTrue(TEXT("First person body follows the camera"),
        Character->GetFirstPersonMesh()->GetAttachParent() == Character->GetFirstPersonCameraComponent());
    TestEqual(TEXT("First person grip socket"), FP->GetAttachSocketName(), FName("HandGrip_R"));
    TestEqual(TEXT("Third person grip socket"), TP->GetAttachSocketName(), FName("HandGrip_R"));
    TestTrue(TEXT("First body contains grip socket"), Character->GetFirstPersonMesh()->DoesSocketExist(TEXT("HandGrip_R")));
    TestTrue(TEXT("World body contains grip socket"), Character->GetMesh()->DoesSocketExist(TEXT("HandGrip_R")));
    TestTrue(TEXT("First rifle only visible to owner"), !!FP->bOnlyOwnerSee);
    TestTrue(TEXT("Third rifle hidden from owner"), !!TP->bOwnerNoSee);
    TestFalse(TEXT("Third rifle is not owner-only"), !!TP->bOnlyOwnerSee);
    TestEqual(TEXT("First rifle has no collision"), FP->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestEqual(TEXT("Third rifle has no collision"), TP->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    if (TestNotNull(TEXT("First rifle mesh"), FP->GetSkeletalMeshAsset()))
    {
        TestEqual(TEXT("Lasgun model used"), FP->GetSkeletalMeshAsset()->GetPathName(),
            FString(TEXT("/Game/Weapons/Lasgun/Meshes/SKM_Lasgun.SKM_Lasgun")));
    }
    TestTrue(TEXT("Both perspectives use same rifle model"), FP->GetSkeletalMeshAsset() == TP->GetSkeletalMeshAsset());
    TestNotNull(TEXT("First person animation initialized"), Character->GetFirstPersonMesh()->GetAnimInstance());
    TestNotNull(TEXT("Third person animation initialized"), Character->GetMesh()->GetAnimInstance());
    TestEqual(TEXT("First pose class"), Character->GetFirstPersonMesh()->GetAnimClass()->GetPathName(),
        FString(TEXT("/Game/Marau6/Anims/ABP_Marau6_FP_Rifle.ABP_Marau6_FP_Rifle_C")));
    TestEqual(TEXT("Third pose class"), Character->GetMesh()->GetAnimClass()->GetPathName(),
        FString(TEXT("/Game/Marau6/Anims/ABP_Marau6_TP_Rifle.ABP_Marau6_TP_Rifle_C")));
    for (int32 Frame = 0; Frame < 3; ++Frame)
    {
        ++GFrameCounter;
        World->Tick(LEVELTICK_All, 1.f / 30.f);
    }
    const FQuat FlatGrip = Character->GetMesh()->GetSocketQuaternion(TEXT("HandGrip_R"));
    TestNull(TEXT("Remote-pose fixture has no controller"), Character->GetController());
    Character->SetRemoteViewPitch(40.0);
    for (int32 Frame = 0; Frame < 6; ++Frame)
    {
        ++GFrameCounter;
        World->Tick(LEVELTICK_All, 1.f / 30.f);
    }
    const FQuat RaisedGrip = Character->GetMesh()->GetSocketQuaternion(TEXT("HandGrip_R"));
    AddInfo(FString::Printf(TEXT("Remote aim=%s grip delta=%.3f"),
        *Character->GetBaseAimRotation().ToString(), FlatGrip.AngularDistance(RaisedGrip)));
    UAnimInstance* Pose = Character->GetMesh()->GetAnimInstance();
    for (TFieldIterator<FProperty> It(Pose->GetClass()); It; ++It)
    {
        if (!It->GetName().Contains(TEXT("Pitch")) && !It->GetName().Contains(TEXT("Aim"))) continue;
        if (const FDoubleProperty* Value = CastField<FDoubleProperty>(*It))
            AddInfo(FString::Printf(TEXT("%s=%.3f"), *It->GetName(), Value->GetPropertyValue_InContainer(Pose)));
        if (const FFloatProperty* Value = CastField<FFloatProperty>(*It))
            AddInfo(FString::Printf(TEXT("%s=%.3f"), *It->GetName(), Value->GetPropertyValue_InContainer(Pose)));
    }
    TestTrue(TEXT("Remote aim pitch changes rifle grip without a controller"),
        FlatGrip.AngularDistance(RaisedGrip) > 0.1);
    TestEqual(TEXT("Poses evaluate without null character/camera/controller references"), Capture.Count.GetValue(), 0);
    return true;
}
#endif
