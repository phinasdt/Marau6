#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Marau6Character.h"
#include "Marau6ShotFX.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6ShotEffectsTest, "Marau6.Combat.ShotEffects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarau6ShotEffectsTest::RunTest(const FString& Parameters)
{
    UClass* EffectClass = LoadClass<AActor>(nullptr, TEXT("/Script/Marau6.Marau6ShotEffect"));
    if (!TestNotNull(TEXT("Cosmetic shot effect exists"), EffectClass)) return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        if (World->IsRooted()) World->RemoveFromRoot(); };
    UClass* PawnClass = LoadClass<AMarau6Character>(nullptr,
        TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Shooter = World->SpawnActor<AMarau6Character>(PawnClass, FVector(0,0,200), FRotator::ZeroRotator, Spawn);
    auto* Target = World->SpawnActor<AMarau6Character>(PawnClass, FVector(500,0,200), FRotator::ZeroRotator, Spawn);
    auto* PC = World->SpawnActor<APlayerController>();
    if (!Shooter || !Target || !PC) return false;
    PC->Possess(Shooter); PC->SetControlRotation(FRotator::ZeroRotator);
    auto Effects = [&]() { TArray<AActor*> Items; for (TActorIterator<AActor> It(World, EffectClass); It; ++It) Items.Add(*It); return Items; };
    auto Fire = [&]() { Shooter->ProcessEvent(Shooter->FindFunctionChecked(TEXT("ServerFire")), nullptr); };
    ++GFrameCounter; World->GetTimerManager().Tick(0.f);
    Fire(); auto Items = Effects();
    if (!TestEqual(TEXT("Accepted shot creates exactly one local effect"), Items.Num(), 1)) return false;
    AActor* Effect = Items[0];
    TestFalse(TEXT("Cosmetic actor does not replicate"), Effect->GetIsReplicated());
    TestTrue(TEXT("Effect has bounded lifespan"), Effect->GetLifeSpan() > 0 && Effect->GetLifeSpan() < 1);
    // The sound actor draws nothing: the flash and impact belong to AMarau6ShotFX.
    TArray<UStaticMeshComponent*> Meshes;
    Effect->GetComponents(Meshes);
    TestEqual(TEXT("Shot sound actor draws no mesh (no glow sphere)"), Meshes.Num(), 0);
    AMarau6ShotFX* FX = nullptr;
    for (TActorIterator<AMarau6ShotFX> It(World); It; ++It) FX = *It;
    if (!TestNotNull(TEXT("Accepted shot creates the shot FX"), FX)) return false;
    FX->GetComponents(Meshes);
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    TestFalse(TEXT("No shot FX part is a sphere"), Meshes.ContainsByPredicate(
        [Sphere](const UStaticMeshComponent* Mesh) { return Mesh->GetStaticMesh() == Sphere; }));
    // The flash is textured blades only: the crossed side blades, the star, and the
    // larger second star layer, all on the muzzle.
    auto* Blade = FindObject<UStaticMeshComponent>(FX, TEXT("FlashBlade0"));
    auto* Layer = FindObject<UStaticMeshComponent>(FX, TEXT("FlashBlade3"));
    if (!TestNotNull(TEXT("Flash blade"), Blade) || !TestNotNull(TEXT("Second textured flash layer"), Layer)) return false;
    TestTrue(TEXT("Second layer rides the flash at the muzzle"), Layer->GetAttachParent() == Blade->GetAttachParent()
        && Layer->GetAttachParent()->GetAttachSocketName() == FName("Muzzle"));
    TestEqual(TEXT("Second layer drawn in the same perspective as the blades"),
        Layer->FirstPersonPrimitiveType, Blade->FirstPersonPrimitiveType);
    TestNull(TEXT("No procedural glow layer"), FindObject<UStaticMeshComponent>(FX, TEXT("MuzzleGlow")));
    Fire(); TestEqual(TEXT("Cooldown rejection creates no effect"), Effects().Num(), 1);
    ++GFrameCounter; World->Tick(LEVELTICK_TimeOnly, .3f); World->GetTimerManager().Tick(.3f);
    TestEqual(TEXT("Transient effects are cleaned up"), Effects().Num(), 0);
    PC->SetControlRotation(FRotator(0,90,0)); Fire(); Items = Effects();
    if (!TestEqual(TEXT("Air shot creates flash"), Items.Num(), 1)) return false;
    ++GFrameCounter; World->Tick(LEVELTICK_TimeOnly, .3f); World->GetTimerManager().Tick(.3f);
    Shooter->ProcessEvent(Shooter->FindFunctionChecked(TEXT("ServerReload")), nullptr);
    Fire(); TestEqual(TEXT("Reload rejection creates no effect"), Effects().Num(), 0);
    FDamageEvent Damage; Shooter->TakeDamage(100, Damage, PC, Target);
    Fire(); TestEqual(TEXT("Dead shooter creates no effect"), Effects().Num(), 0);
    return true;
}
#endif
