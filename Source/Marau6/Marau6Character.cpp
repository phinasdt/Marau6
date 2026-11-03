// Copyright Epic Games, Inc. All Rights Reserved.

#include "Marau6Character.h"
#include "Marau6DestructibleSection.h"
#include "Marau6HealingComponent.h"
#include "Marau6Footsteps.h"
#include "Sound/SoundBase.h"
#include "Marau6ItemLedger.h"
#include "Marau6Audio.h"
#include "Marau6PlayerController.h"
#include "Marau6Bot.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "Marau6Persistence.h"
#include "Marau6InventoryComponent.h"
#include "Marau6LootActor.h"
#include "Marau6ExtractionZone.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Marau6.h"
#include "Net/UnrealNetwork.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Marau6ShotEffect.h"
#include "Marau6ShotFX.h"
#include "Marau6Ragdoll.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/ConstructorHelpers.h"
#include "Marau6MovementComponent.h"
#include "HAL/IConsoleManager.h"

// Inspection aid for Marau6.CaptureViewModel: 1 forces the aim pose, 2 the sprint pose.
static TAutoConsoleVariable<int32> CVarViewModelPose(TEXT("Marau6.ViewModelPose"), 0,
    TEXT("Force the owner's view model pose: 0 live, 1 aiming, 2 sprinting."));

AMarau6Character::AMarau6Character(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UMarau6MovementComponent>(
        ACharacter::CharacterMovementComponentName))
{
	bReplicates = true;
    Inventory=CreateDefaultSubobject<UMarau6InventoryComponent>(TEXT("Inventory"));
    Healing=CreateDefaultSubobject<UMarau6HealingComponent>(TEXT("Healing"));
    Footsteps=CreateDefaultSubobject<UMarau6Footsteps>(TEXT("Footsteps"));
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, BaseEyeHeight), FRotator::ZeroRotator);
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

    ViewModelAim = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewModelAim"));
    ViewModelAim->SetupAttachment(FirstPersonCameraComponent);
    ViewModelAim->bAutoActivate = false;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;

    FirstPersonRifle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonRifle"));
    FirstPersonRifle->SetupAttachment(FirstPersonMesh, TEXT("HandGrip_R"));
    FirstPersonRifle->SetCollisionProfileName(TEXT("NoCollision"));
    FirstPersonRifle->SetGenerateOverlapEvents(false);
    FirstPersonRifle->SetOnlyOwnerSee(true);
    FirstPersonRifle->SetCastShadow(false);
    FirstPersonRifle->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;

    ThirdPersonRifle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ThirdPersonRifle"));
    ThirdPersonRifle->SetupAttachment(GetMesh(), TEXT("HandGrip_R"));
    ThirdPersonRifle->SetCollisionProfileName(TEXT("NoCollision"));
    ThirdPersonRifle->SetGenerateOverlapEvents(false);
    ThirdPersonRifle->SetOwnerNoSee(true);
    ThirdPersonRifle->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

    static ConstructorHelpers::FObjectFinder<UAnimSequence> Reload(
        TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload"));
    ReloadAnimation = Reload.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> MagOut(TEXT("/Game/Marau6/Audio/S_Reload_MagOut.S_Reload_MagOut"));
    static ConstructorHelpers::FObjectFinder<USoundBase> MagIn(TEXT("/Game/Marau6/Audio/S_Reload_MagIn.S_Reload_MagIn"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Charge(TEXT("/Game/Marau6/Audio/S_Reload_Charge.S_Reload_Charge"));
    ReloadMagOut = MagOut.Object; ReloadMagIn = MagIn.Object; ReloadCharge = Charge.Object;
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Rifle(
        TEXT("/Game/Weapons/Lasgun/Meshes/SKM_Lasgun.SKM_Lasgun"));
    if (Rifle.Succeeded())
    {
        FirstPersonRifle->SetSkeletalMesh(Rifle.Object);
        ThirdPersonRifle->SetSkeletalMesh(Rifle.Object);
    }

    // These graphs refer back to character Blueprints. Defer loading until
    // after native class defaults have finished constructing.
    PrototypeFirstPersonPose = FSoftObjectPath(
        TEXT("/Game/Marau6/Anims/ABP_Marau6_FP_Rifle.ABP_Marau6_FP_Rifle_C"));
    PrototypeThirdPersonPose = FSoftObjectPath(
        TEXT("/Game/Marau6/Anims/ABP_Marau6_TP_Rifle.ABP_Marau6_TP_Rifle_C"));

}

void AMarau6Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{

        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
            {
                if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                    ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
                {
                    PrototypeFireAction = NewObject<UInputAction>(this);
                    PrototypeFireAction->ValueType = EInputActionValueType::Boolean;
                    PrototypeFireMapping = NewObject<UInputMappingContext>(this);
                    PrototypeFireMapping->MapKey(PrototypeFireAction, EKeys::LeftMouseButton);
                    PrototypeReloadAction = NewObject<UInputAction>(this);
                    PrototypeReloadAction->ValueType = EInputActionValueType::Boolean;
                    PrototypeFireMapping->MapKey(PrototypeReloadAction, EKeys::R);
                    EnhancedInputComponent->BindAction(PrototypeReloadAction, ETriggerEvent::Started,
                        this, &AMarau6Character::DoReload);
                    auto BindInventoryKey = [&](const TCHAR* Name,FKey Key,void (AMarau6Character::*Handler)())
                    {
                        UInputAction* Action=NewObject<UInputAction>(this,FName(Name));
                        Action->ValueType=EInputActionValueType::Boolean;
                        PrototypeFireMapping->MapKey(Action,Key);
                        EnhancedInputComponent->BindAction(Action,ETriggerEvent::Started,this,Handler);
                    };
                    BindInventoryKey(TEXT("InventoryToggle"),EKeys::Tab,&AMarau6Character::ToggleInventory);
                    BindInventoryKey(TEXT("InventoryNext"),EKeys::Down,&AMarau6Character::NextInventorySlot);
                    BindInventoryKey(TEXT("InventoryPrevious"),EKeys::Up,&AMarau6Character::PreviousInventorySlot);
                    BindInventoryKey(TEXT("LootPickup"),EKeys::E,&AMarau6Character::PickupLoot);
                    BindInventoryKey(TEXT("ItemDrop"),EKeys::G,&AMarau6Character::DropSelectedItem);
                    BindInventoryKey(TEXT("ItemEquip"),EKeys::F,&AMarau6Character::EquipSelectedItem);
                    BindInventoryKey(TEXT("ItemProtect"),EKeys::P,&AMarau6Character::ProtectSelectedItem);
                    UInputAction* HealAction=NewObject<UInputAction>(this,FName(TEXT("ItemHeal")));HealAction->ValueType=EInputActionValueType::Boolean;
                    PrototypeFireMapping->MapKey(HealAction,EKeys::H);EnhancedInputComponent->BindAction(HealAction,ETriggerEvent::Started,Healing.Get(),&UMarau6HealingComponent::UseSelected);
                    auto BindHoldKey = [&](const TCHAR* Name,FKey Key,void (AMarau6Character::*Press)(),void (AMarau6Character::*Release)())
                    {
                        UInputAction* Action=NewObject<UInputAction>(this,FName(Name));
                        Action->ValueType=EInputActionValueType::Boolean;
                        PrototypeFireMapping->MapKey(Action,Key);
                        EnhancedInputComponent->BindAction(Action,ETriggerEvent::Started,this,Press);
                        EnhancedInputComponent->BindAction(Action,ETriggerEvent::Completed,this,Release);
                        EnhancedInputComponent->BindAction(Action,ETriggerEvent::Canceled,this,Release);
                    };
                    BindHoldKey(TEXT("Sprint"),EKeys::LeftShift,&AMarau6Character::StartSprint,&AMarau6Character::StopSprint);
                    BindHoldKey(TEXT("AimDownSights"),EKeys::RightMouseButton,&AMarau6Character::StartAim,&AMarau6Character::StopAim);
                    Subsystem->AddMappingContext(PrototypeFireMapping, 1);
                    EnhancedInputComponent->BindAction(PrototypeFireAction,ETriggerEvent::Started,this,&AMarau6Character::StartAutomaticFire);
                    EnhancedInputComponent->BindAction(PrototypeFireAction,ETriggerEvent::Triggered,this,&AMarau6Character::DoFire);
                    EnhancedInputComponent->BindAction(PrototypeFireAction,ETriggerEvent::Completed,this,&AMarau6Character::StopAutomaticFire);
                    EnhancedInputComponent->BindAction(PrototypeFireAction,ETriggerEvent::Canceled,this,&AMarau6Character::StopAutomaticFire);
                }
            }
        }

		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AMarau6Character::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMarau6Character::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMarau6Character::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMarau6Character::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMarau6Character::LookInput);
	}
	else
	{
		UE_LOG(LogMarau6, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void AMarau6Character::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AMarau6Character::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AMarau6Character::DoAim(float Yaw, float Pitch)
{
	if (GetController() && !bInventoryOpen && !bAtBase)
	{
		// Keep the same on-screen sensitivity while the aim zoom narrows the view.
		const float Zoom = FirstPersonCameraComponent->FieldOfView / FMath::Max(DefaultFieldOfView, 1.f);
		// pass the rotation inputs
		AddControllerYawInput(Yaw * Zoom);
		AddControllerPitchInput(Pitch * Zoom);
	}
}

void AMarau6Character::DoMove(float Right, float Forward)
{
	if (CanParticipate() && !bInventoryOpen && GetController())
	{
		// pass the move inputs
		if(!FMath::IsNearlyZero(Right) || !FMath::IsNearlyZero(Forward))Healing->CancelFromMovement(); AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AMarau6Character::DoJumpStart()
{
	if (!CanParticipate() || bInventoryOpen) return;
	Healing->CancelFromMovement();
	// pass Jump to the character
	Jump();
}

void AMarau6Character::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

void AMarau6Character::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarau6Character,bAtBase);

	DOREPLIFETIME(AMarau6Character, Health);
    DOREPLIFETIME(AMarau6Character, LastHitDirection);
    DOREPLIFETIME(AMarau6Character, RaidStatus);
    DOREPLIFETIME_CONDITION(AMarau6Character, AmmoState, COND_OwnerOnly);
    DOREPLIFETIME(AMarau6Character, bReloadPose);
}

void AMarau6Character::OnRep_Health()
{
    if (!IsAlive())
    {
        StopJumping();
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
    }
    SetRagdoll(!IsAlive());

	UE_LOG(LogMarau6, Log, TEXT("%s: vida atual = %.0f"),
		*GetName(), Health);
}


float AMarau6Character::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (!HasAuthority() || !CanBeDamaged() || !CanParticipate()
        || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0.0f)
    {
        return 0.0f;
    }

    Healing->Cancel(TEXT("Dano interrompeu a cura: suprimento preservado.")); const float PreviousHealth=Health;
    if (DamageCauser) LastHitDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
    const auto PreviousRaid=RaidStatus;
    const float AppliedDamage = FMath::Min(DamageAmount, Health);
    Health = FMath::Clamp(Health - AppliedDamage, 0.0f, 100.0f);

    if (!IsAlive())
    {
        CancelReload();
        RaidStatus.State = EMarau6RaidState::Eliminated;
        if(!Inventory->FinalizeRaid(false) && UMarau6Persistence::UsesPersistentBase(GetWorld())){Health=PreviousHealth;RaidStatus=PreviousRaid;return 0.f;}
        RaidStatus.ExtractionEndServerTime = 0.0;
        ExtractionZone.Reset();
        OnRep_RaidStatus();
    }

    // RepNotify runs on receiving clients, so also update the server explicitly.
    OnRep_Health();
    ForceNetUpdate();
    Super::TakeDamage(AppliedDamage, DamageEvent, EventInstigator, DamageCauser);
    return AppliedDamage;
}


void AMarau6Character::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Healing->Cancel(TEXT("Cura interrompida ao encerrar a sessao."));
    CancelReload();
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
            {
                if (PrototypeFireMapping)
                {
                    Subsystem->RemoveMappingContext(PrototypeFireMapping);
                }
            }
        }
    }
    Super::EndPlay(EndPlayReason);
}

void AMarau6Character::StartAutomaticFire()
{
    bFireNeedsRelease=bInventoryOpen || !CanParticipate() || Healing->IsHealing();
    DoFire();
}
void AMarau6Character::StopAutomaticFire(){bFireNeedsRelease=false;}
void AMarau6Character::DoFire()
{
    if(bInventoryOpen || !CanParticipate() || Healing->IsHealing()){bFireNeedsRelease=true;return;}
    if(bFireNeedsRelease || !GetWorld() || GetWorld()->GetTimeSeconds()<NextLocalFireTime)return;
    if (IsLocallyControlled() && CanParticipate() && HasEquippedRifle() && !Healing->IsHealing() && !bInventoryOpen && !IsReloading() && !IsSprinting() && SprintAlpha < .2f && GetMagazineAmmo() > 0 && GetRifleDurability() > 0)
    {
        NextLocalFireTime=GetWorld()->GetTimeSeconds()+PrototypeFireInterval;
        // Send pending moves first so the server sees the current aim and sprint state.
        GetCharacterMovement()->FlushServerMoves();
        ServerFire();
        AddRecoil();
    }
}

void AMarau6Character::AddRecoil()
{
    // The server fires along the replicated control rotation, so the kick moves
    // real aim and the player pulls down to compensate. Aiming steadies it.
    const float Steady = FMath::Lerp(1.f, .7f, AimAlpha);
    PendingRecoil.Pitch += RecoilKick.X * Steady;
    PendingRecoil.Yaw += FMath::FRandRange(-RecoilKick.Y, RecoilKick.Y) * Steady;
    RecoilRecovery += RecoilKick.X * Steady * RecoilSettle;
    LastShotTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    ShotKick = 1.f;
}

void AMarau6Character::StartSprint()
{
    if (!GetMarau6Movement() || IsReloading()) return;
    GetMarau6Movement()->bWantsToSprint = true;
    StopAim();
}
void AMarau6Character::StopSprint() { if (GetMarau6Movement()) GetMarau6Movement()->bWantsToSprint = false; }
void AMarau6Character::StartAim()
{
    // Held while sprinting, aim takes over once the sprint ends.
    if (GetMarau6Movement() && !bInventoryOpen) GetMarau6Movement()->bWantsToAim = true;
}
void AMarau6Character::StopAim() { if (GetMarau6Movement()) GetMarau6Movement()->bWantsToAim = false; }
UMarau6MovementComponent* AMarau6Character::GetMarau6Movement() const { return Cast<UMarau6MovementComponent>(GetCharacterMovement()); }
bool AMarau6Character::IsSprinting() const { return GetMarau6Movement() && GetMarau6Movement()->IsSprinting(); }
bool AMarau6Character::IsAiming() const { return GetMarau6Movement() && GetMarau6Movement()->bWantsToAim && !IsSprinting(); }

void AMarau6Character::ServerFire_Implementation()
{
    if (!HasAuthority() || !CanParticipate() || Healing->IsHealing() || !GetController() || !GetWorld()
        || !HasEquippedRifle() || AmmoState.bReloading || AmmoState.Magazine <= 0 || GetRifleDurability() <= 0
        || IsSprinting())
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();
    if (LastServerFireTime >= 0.0 && Now - LastServerFireTime < PrototypeFireInterval)
    {
        return;
    }

    // Neither origin, target, nor damage is supplied by the firing client.
    const FVector Start = GetPawnViewLocation();
    const FRotator Aim = GetControlRotation();
    if (Start.ContainsNaN() || Aim.ContainsNaN())
    {
        return;
    }
    if(!Inventory->SaveEquippedShot()) return;
    LastServerFireTime = Now;
    // The ledger is the sole writer; copy the committed round count, never spend twice.
    const auto* FiredRifle=Inventory->ItemAt(Marau6Slots::Weapon);
    AmmoState.Magazine=FiredRifle->Magazine;AmmoState.Reserve=FiredRifle->Reserve;
    ForceNetUpdate();
    const FVector End = Start + Aim.Vector() * PrototypeRange;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(Marau6PrototypeFire), false, this);
    FHitResult Hit;
    // Pawn capsules and solid world surfaces block this channel. The capsule
    // intentionally gives uniform whole-body damage in this first prototype.
    const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Query);
    AMarau6Character* Target = bBlockingHit ? Cast<AMarau6Character>(Hit.GetActor()) : nullptr;
    AMarau6Bot* Bot = bBlockingHit ? Cast<AMarau6Bot>(Hit.GetActor()) : nullptr;
    const bool bHitCharacter = (Target && Target->CanParticipate()) || (Bot && Bot->IsAlive());
    if (bHitCharacter || (bBlockingHit && Cast<AMarau6DestructibleSection>(Hit.GetActor())))
    {
        FDamageEvent DamageEvent;
        Hit.GetActor()->TakeDamage(PrototypeDamage, DamageEvent, GetController(), this);
    }
    MulticastShot(bBlockingHit ? Hit.ImpactPoint : End,
        bBlockingHit ? Hit.ImpactNormal : FVector::ZeroVector, bBlockingHit, bHitCharacter);
}

void AMarau6Character::MulticastShot_Implementation(
    FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal, bool bBlockingHit, bool bHitCharacter)
{
    if (!GetWorld() || GetNetMode() == NM_DedicatedServer) return;
    // Only an accepted server shot reaches this cosmetic multicast.
    USkeletalMeshComponent* Rifle = IsLocallyControlled() ? FirstPersonRifle : ThirdPersonRifle;
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (AMarau6ShotEffect* Effect = GetWorld()->SpawnActor<AMarau6ShotEffect>(
        AMarau6ShotEffect::StaticClass(), FVector(Point), FRotator::ZeroRotator, Spawn))
    {
        Effect->Present(Rifle, IsLocallyControlled(), Point, Normal, bBlockingHit, bHitCharacter);
    }
    if (AMarau6ShotFX* FX = GetWorld()->SpawnActor<AMarau6ShotFX>(
        AMarau6ShotFX::StaticClass(), FVector(Point), FRotator::ZeroRotator, Spawn))
    {
        FX->Present(Rifle, IsLocallyControlled(), Point, Normal, bBlockingHit, bHitCharacter,
            FirstPersonCameraComponent->GetComponentLocation(), FirstPersonCameraComponent->FirstPersonScale);
    }
}

void AMarau6Character::DoReload()
{
    if (IsLocallyControlled() && CanParticipate() && HasEquippedRifle() && !Healing->IsHealing())
    {
        StopSprint();
        ServerReload();
    }
}

void AMarau6Character::ServerReload_Implementation()
{
    // R reloads straight from the backpack: open as many 20-round boxes into the
    // reserve as the magazine needs, through the same durable path as dropping a
    // box on the rifle.
    if (HasAuthority() && CanParticipate() && GetController() && HasEquippedRifle() && !AmmoState.bReloading
        && !Healing->IsHealing() && AmmoState.Magazine < PrototypeMagazineCapacity)
        if (auto* Ledger = GetWorld()->GetSubsystem<UMarau6ItemLedger>())
            for (int32 Slot = 0; Slot < Marau6Slots::Weapon && AmmoState.Reserve < PrototypeMagazineCapacity - AmmoState.Magazine; ++Slot)
                if (const FMarau6Item* Box = Inventory->ItemAt(Slot); Box && Box->IsAmmo())
                    Ledger->Move(Inventory, Box->Id, Marau6Slots::Weapon);
    if (!HasAuthority() || !CanParticipate() || Healing->IsHealing() || !GetController() || !GetWorld()
        || !HasEquippedRifle() || AmmoState.bReloading || AmmoState.Magazine >= PrototypeMagazineCapacity
        || AmmoState.Reserve <= 0)
    {
        return;
    }

    AmmoState.bReloading = true;
    SetReloadPose(true);
    GetWorldTimerManager().SetTimer(ReloadTimer, this,
        &AMarau6Character::FinishReload, PrototypeReloadDuration, false);
    ForceNetUpdate();
}

void AMarau6Character::FinishReload()
{
    if (!HasAuthority() || !AmmoState.bReloading)
    {
        return;
    }
    if (!CanParticipate() || !HasEquippedRifle() || !GetController())
    {
        CancelReload();
        return;
    }

    const int32 Needed = FMath::Max(0, PrototypeMagazineCapacity - AmmoState.Magazine);
    const int32 Transferred = FMath::Min(Needed, AmmoState.Reserve);
    if(!Inventory->SaveEquippedAmmo(AmmoState.Magazine+Transferred,AmmoState.Reserve-Transferred)){CancelReload();return;}
    AmmoState.Magazine += Transferred;
    AmmoState.Reserve -= Transferred;
    AmmoState.bReloading = false;
    SetReloadPose(false);
    ForceNetUpdate();
}

void AMarau6Character::CancelReload()
{
    if (!HasAuthority())
    {
        return;
    }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReloadTimer);
    }
    AmmoState.bReloading = false;
    SetReloadPose(false);
}

#if WITH_EDITOR
void AMarau6Character::DebugReload()
{
    if (!HasAuthority()) return;
    if (GetController() && HasEquippedRifle())
    {
        AmmoState.Magazine = FMath::Min(AmmoState.Magazine, PrototypeMagazineCapacity / 2);
        AmmoState.Reserve = FMath::Max(AmmoState.Reserve, PrototypeMagazineCapacity);
        ServerReload_Implementation();
        return;
    }
    SetReloadPose(true);
    FTimerHandle Handle;
    GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() { SetReloadPose(false); }),
        PrototypeReloadDuration, false);
}
#endif

void AMarau6Character::SetReloadPose(bool bValue)
{
    if (!HasAuthority() || bReloadPose == bValue) return;
    bReloadPose = bValue;
    OnRep_ReloadPose();
}

void AMarau6Character::OnRep_ReloadPose()
{
    if (GetNetMode() == NM_DedicatedServer) return;
    ReloadPoseStart = bReloadPose && GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
    // Sounds follow the owner's staged reload (fractions of the 2 s reload); a
    // cancelled reload stops the ones still pending.
    for (FTimerHandle& Timer : ReloadSoundTimers) GetWorldTimerManager().ClearTimer(Timer);
    ReloadSoundTimers.Reset();
    if (bReloadPose)
    {
        const TPair<float, USoundBase*> Cues[] = { {.24f, ReloadMagOut}, {.53f, ReloadMagIn}, {.70f, ReloadCharge} };
        for (const auto& [At, Sound] : Cues)
        {
            if (!Sound) continue;
            FTimerHandle& Timer = ReloadSoundTimers.AddDefaulted_GetRef();
            GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, Sound = Sound]()
                { Footsteps->PlayGear(Sound, Marau6Audio::ReloadVolume); }), FMath::Max(At * PrototypeReloadDuration, .01f), false);
        }
    }
    if (!ReloadAnimation) return;
    // Others see the montage on the body. The owner's arms are attached to the camera,
    // so their copy-pose node no longer finds the body: play the clip on them directly,
    // which moves the left hand while the reload releases the hand rig.
    for (USkeletalMeshComponent* Body : { GetMesh(), IsLocallyControlled() ? FirstPersonMesh : nullptr })
    {
        UAnimInstance* Pose = Body ? Body->GetAnimInstance() : nullptr;
        if (!Pose) continue;
        if (bReloadPose)
            Pose->PlaySlotAnimationAsDynamicMontage(ReloadAnimation, ReloadSlot, .15f, .2f,
                ReloadAnimation->GetPlayLength() / PrototypeReloadDuration);
        else Pose->StopSlotAnimation(.2f, ReloadSlot);
    }
}


void AMarau6Character::BeginPlay()
{
    Super::BeginPlay();

    // The template pose initializes its camera when the owning actor has begun play.
    if (UClass* ThirdPose = PrototypeThirdPersonPose.LoadSynchronous())
    {
        GetMesh()->SetAnimInstanceClass(ThirdPose);
    }
    if (UClass* FirstPose = PrototypeFirstPersonPose.LoadSynchronous())
    {
        FirstPersonMesh->SetAnimInstanceClass(FirstPose);
    }

    // The first person graph copies the world body pose even when it is hidden
    // from the owning camera. Always refresh bones so the hand socket stays current.
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    FirstPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    FirstPersonMesh->AddTickPrerequisiteComponent(GetMesh());
    // The first person mesh is the whole soldier; its helmet and gas mask sit in
    // front of the camera, so the owner's view drops the head and what hangs on it.
    FirstPersonMesh->HideBoneByName(TEXT("head"), EPhysBodyOp::PBO_None);
    HideFirstPersonKit();
    BodyNeutral = GetMesh()->GetRelativeTransform();
    BodyCollisionProfile = GetMesh()->GetCollisionProfileName();

    // The camera stays on the capsule so the crosshair matches the server's shot
    // origin. Carry the arms rigidly with it, keeping their neutral placement, so
    // the rifle holds its place on screen at any pitch.
    const FTransform ArmsInCapsule = FirstPersonMesh->GetRelativeTransform() * GetMesh()->GetRelativeTransform();
    const FTransform CameraInCapsule(FirstPersonCameraComponent->GetRelativeLocation());
    FirstPersonMesh->AttachToComponent(FirstPersonCameraComponent, FAttachmentTransformRules::KeepRelativeTransform);
    ArmsNeutral = ArmsInCapsule.GetRelativeTransform(CameraInCapsule);
    FTransform ArmsInCamera = ArmsNeutral;
    ArmsInCamera.AddToTranslation(FirstPersonArmsOffset);
    FirstPersonMesh->SetRelativeTransform(ArmsInCamera);
    DefaultFieldOfView = FirstPersonCameraComponent->FieldOfView;
    DefaultFirstPersonFieldOfView = FirstPersonCameraComponent->FirstPersonFieldOfView;
    OnInventoryChanged(false);
}

void AMarau6Character::HideFirstPersonKit()
{
    // The owner's view keeps only sleeves, gloves and straps. The soldier's kit
    // shares the arms' spine bones, so whole material sections are hidden instead.
    static const TCHAR* Kit[] = { TEXT("backpack"), TEXT("boots"), TEXT("buttons"), TEXT("gasmask"), TEXT("glass"),
        TEXT("head"), TEXT("helmet"), TEXT("pants"), TEXT("respirator"), TEXT("shoulder"), TEXT("shovel"), TEXT("string") };
    const USkeletalMesh* Body = FirstPersonMesh->GetSkeletalMeshAsset();
    const FSkeletalMeshRenderData* Render = Body ? Body->GetResourceForRendering() : nullptr;
    if (!Render) return;
    const TArray<FSkeletalMaterial>& Slots = Body->GetMaterials();
    for (int32 Lod = 0; Lod < Render->LODRenderData.Num(); ++Lod)
    {
        const TArray<FSkelMeshRenderSection>& Sections = Render->LODRenderData[Lod].RenderSections;
        for (int32 Section = 0; Section < Sections.Num(); ++Section)
        {
            const int32 Slot = Sections[Section].MaterialIndex;
            if (!Slots.IsValidIndex(Slot)) continue;
            const FString Name = (Slots[Slot].MaterialSlotName.ToString() + GetNameSafe(Slots[Slot].MaterialInterface)).ToLower();
            for (const TCHAR* Part : Kit)
                if (Name.Contains(Part)) { FirstPersonMesh->ShowMaterialSection(Slot, Section, false, Lod); break; }
        }
    }
}

FRotator AMarau6Character::GetBaseAimRotation() const
{
    FRotator Aim = Super::GetBaseAimRotation();
    // The owner's arms copy the body pose and already pitch with the camera; a
    // pitched body would tilt the rifle twice. Others still see the body aim.
    if (IsLocallyControlled()) Aim.Pitch = 0.0;
    return Aim;
}

void AMarau6Character::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority()) UpdateExtraction();
    if (IsLocallyControlled() && GetNetMode() != NM_DedicatedServer) UpdateViewModel(DeltaSeconds);
}

void AMarau6Character::UpdateViewModel(float DeltaSeconds)
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC || DeltaSeconds <= 0.f) return;

    const int32 ForcedPose = CVarViewModelPose.GetValueOnGameThread();
    const bool bCanAim = IsAiming() && HasEquippedRifle() && !IsReloading() && !bInventoryOpen && CanParticipate();
    const float AimTarget = ForcedPose == 1 ? 1.f : ForcedPose == 2 ? 0.f : bCanAim ? 1.f : 0.f;
    const float SprintTarget = ForcedPose == 2 ? 1.f : ForcedPose == 1 ? 0.f : IsSprinting() ? 1.f : 0.f;
    AimAlpha = FMath::FInterpTo(AimAlpha, AimTarget, DeltaSeconds, 14.f);
    SprintAlpha = FMath::FInterpTo(SprintAlpha, SprintTarget, DeltaSeconds, 10.f);
    const float Steady = 1.f - .8f * AimAlpha;

    // Look sway: the rifle lags behind fast turns and settles back.
    const FRotator Turn = (PC->GetControlRotation() - LastViewRotation).GetNormalized();
    const FRotator SwayTarget(FMath::Clamp(-Turn.Pitch / DeltaSeconds * .012f, -4.f, 4.f) * Steady,
        FMath::Clamp(-Turn.Yaw / DeltaSeconds * .012f, -5.f, 5.f) * Steady, 0.f);
    SwayRotation = FMath::RInterpTo(SwayRotation, SwayTarget, DeltaSeconds, 9.f);

    // Camera recoil: spread each kick over a few frames, within the view pitch limits.
    if (!PendingRecoil.IsNearlyZero(1.e-3f))
    {
        const FRotator Step = PendingRecoil * (1.f - FMath::Exp(-DeltaSeconds * 30.f));
        PendingRecoil -= Step;
        FRotator Control = PC->GetControlRotation() + Step;
        Control.Pitch = FRotator::NormalizeAxis(Control.Pitch);
        if (PC->PlayerCameraManager)
            Control.Pitch = FMath::Clamp(Control.Pitch, PC->PlayerCameraManager->ViewPitchMin, PC->PlayerCameraManager->ViewPitchMax);
        PC->SetControlRotation(Control);
    }
    // Snap, then settle: part of each kick comes back down once the shot has landed.
    if (RecoilRecovery > 0.f && GetWorld()->GetTimeSeconds() - LastShotTime > .06)
    {
        const float Back = FMath::Min(RecoilRecovery, RecoilRecovery * (1.f - FMath::Exp(-DeltaSeconds * 12.f)) + .002f);
        RecoilRecovery -= Back;
        FRotator Control = PC->GetControlRotation();
        Control.Pitch = FRotator::NormalizeAxis(Control.Pitch - Back);
        if (PC->PlayerCameraManager)
            Control.Pitch = FMath::Clamp(Control.Pitch, PC->PlayerCameraManager->ViewPitchMin, PC->PlayerCameraManager->ViewPitchMax);
        PC->SetControlRotation(Control);
    }
    LastViewRotation = PC->GetControlRotation();

    // Run bob: one side-to-side cycle per stride pair and a dip on every step, in time
    // with the footsteps (1.6 m stride) at the 5 m/s move speed, plus a slow breath.
    // The rifle also rolls and nods with the steps so moving reads as running.
    const float Speed = GetCharacterMovement()->IsMovingOnGround() ? GetVelocity().Size2D() : 0.f;
    const float Cadence = FMath::Lerp(.0196f, .0125f, SprintAlpha);
    BobPhase = FMath::Fmod(BobPhase + DeltaSeconds * Speed * Cadence, 2.f * UE_PI);
    // Walking moves the rifle 30% less than before; sprinting keeps its full swing.
    const float BobAmount = FMath::Clamp(Speed / 500.f, 0.f, 1.4f) * FMath::Lerp(.7f, .9f, SprintAlpha) * Steady;
    const float Sway = FMath::Cos(BobPhase), Step = FMath::Abs(FMath::Sin(BobPhase));
    const FVector Bob(0.f, Sway * BobAmount * 1.3f,
        -Step * BobAmount * 1.5f + FMath::Sin(GetWorld()->GetTimeSeconds() * 1.6f) * .12f * Steady);
    const FRotator BobTurn = FRotator(-Step * BobAmount * 1.1f, Sway * BobAmount * .6f, Sway * BobAmount * 1.8f)
        * (1.f - .5f * SprintAlpha);

    // Reload: the rig keeps both hands on the rifle, so the owner's reload is staged on
    // the view model: cant the rifle toward the centre with the magazine side up, drop the
    // magazine, seat the new one, charge, and bring it back.
    FVector ReloadShift = FVector::ZeroVector;
    FRotator ReloadTurn = FRotator::ZeroRotator;
    float RigAlpha = 1.f;
    float ReloadArmsPitch = 0.f;
    if (ReloadPoseStart >= 0.0)
    {
        const float T = static_cast<float>((GetWorld()->GetTimeSeconds() - ReloadPoseStart) / PrototypeReloadDuration);
        const float Body = FMath::SmoothStep(0.f, .15f, T) * (1.f - FMath::SmoothStep(.8f, 1.f, T));
        auto Pulse = [T](float At, float Width) { return FMath::Max(0.f, 1.f - FMath::Abs(T - At) / Width); };
        const float MagOut = Pulse(.27f, .07f), MagIn = Pulse(.56f, .05f), Charge = Pulse(.73f, .05f);
        ReloadTurn = FRotator(8.f * Body - 6.f * MagOut + 4.f * MagIn, -22.f * Body, 28.f * Body);
        // Mid-reload the hand rig lets go, so the copied reload clip moves the left hand
        // off the handguard to the magazine and back; it takes the rifle again to finish.
        RigAlpha = 1.f - FMath::SmoothStep(.06f, .2f, T) * (1.f - FMath::SmoothStep(.78f, .93f, T));
        ReloadShift = FVector(-2.f * Body - 3.f * Charge, -5.f * Body, 1.f * Body - 2.f * MagOut + 1.5f * MagIn);
        // Pitch the arms up around the camera instead of lifting them: the hands and the
        // magazine swap come into view while the hidden collar near the pivot stays out.
        ReloadArmsPitch = 16.f * FMath::SmoothStep(.06f, .2f, T) * (1.f - FMath::SmoothStep(.78f, .93f, T));
    }

    ShotKick = FMath::FInterpTo(ShotKick, 0.f, DeltaSeconds, 18.f);
    const float Kick = ShotKick * (1.f - .5f * AimAlpha);

    // The arms only translate in camera space. Rotation goes through the inactive
    // aim camera instead: the first person rig points the rifle along it and keeps
    // both hands on the weapon, so turning it angles the rifle naturally.
    const FVector Offset = FirstPersonArmsOffset + FirstPersonAimOffset * AimAlpha + SprintArmsOffset * SprintAlpha
        + Bob + ReloadShift + FVector(-2.8f * Kick, SwayRotation.Yaw * .15f, SwayRotation.Pitch * .15f);
    FirstPersonMesh->SetRelativeTransform(ArmsNeutral * FTransform(FRotator(ReloadArmsPitch, 0.f, 0.f), Offset));
    ViewModelAim->SetRelativeRotation(SprintAimRotation * SprintAlpha + SwayRotation + BobTurn + ReloadTurn
        + FRotator(1.9f * Kick, 0.f, 0.f));

    FirstPersonCameraComponent->SetFieldOfView(FMath::Lerp(DefaultFieldOfView, DefaultFieldOfView * AimFieldOfViewScale, AimAlpha));

    // The rig reads its aim origin from this pose variable; point it at the aim camera.
    if (UAnimInstance* Anim = FirstPersonMesh->GetAnimInstance())
    {
        if (FObjectProperty* RigCamera = FindFProperty<FObjectProperty>(Anim->GetClass(), TEXT("First Person Camera")))
            RigCamera->SetObjectPropertyValue_InContainer(Anim, ViewModelAim);
        if (FNumericProperty* Rig = FindFProperty<FNumericProperty>(Anim->GetClass(), RigAlphaVariable))
            Rig->SetFloatingPointPropertyValue(Rig->ContainerPtrToValuePtr<void>(Anim), RigAlpha);
    }
}

void AMarau6Character::SetRagdoll(bool bEnable)
{
    if (bRagdoll == bEnable) return;
    bRagdoll = bEnable;
    if (bEnable)
    {
        Marau6Ragdoll::Start(this, FVector(LastHitDirection) * 350.f);
        FirstPersonMesh->SetVisibility(false, true);
    }
    else
    {
        Marau6Ragdoll::Stop(this, BodyNeutral, BodyCollisionProfile);
        FirstPersonMesh->SetVisibility(HasEquippedRifle(), true);
    }
}

float AMarau6Character::GetExtractionSecondsRemaining() const
{
    if (RaidStatus.State != EMarau6RaidState::Extracting || !GetWorld()) return 0.f;
    const AGameStateBase* State = GetWorld()->GetGameState();
    const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    return FMath::Max(0.f, static_cast<float>(RaidStatus.ExtractionEndServerTime - Now));
}

void AMarau6Character::UpdateExtraction()
{
    if (!HasAuthority() || !GetWorld() || !CanParticipate()) return;
    AMarau6ExtractionZone* Zone = ExtractionZone.Get();
    if (!GetController()) Zone = nullptr;
    else if (!IsValid(Zone) || !Zone->ContainsCharacter(this))
    {
        Zone = nullptr;
        for (TActorIterator<AMarau6ExtractionZone> It(GetWorld()); It; ++It)
        {
            if (It->ContainsCharacter(this)) { Zone = *It; break; }
        }
    }
    if (!Zone)
    {
        if (RaidStatus.State == EMarau6RaidState::Extracting)
        {
            RaidStatus.State = EMarau6RaidState::Active;
            RaidStatus.ExtractionEndServerTime = 0.0;
            ExtractionZone.Reset();
            ForceNetUpdate();
        }
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (RaidStatus.State != EMarau6RaidState::Extracting || Zone != ExtractionZone.Get())
    {
        Healing->Cancel(TEXT("Extracao interrompeu a cura: suprimento preservado.")); ExtractionZone = Zone;
        RaidStatus.State = EMarau6RaidState::Extracting;
        RaidStatus.ExtractionEndServerTime = Now + FMath::Clamp(Zone->GetHoldSeconds(),1.f,60.f);
        ForceNetUpdate();
    }
    if (Now >= RaidStatus.ExtractionEndServerTime) CompleteExtraction();
}

void AMarau6Character::CompleteExtraction()
{
    if (!HasAuthority() || !CanParticipate() || RaidStatus.State != EMarau6RaidState::Extracting
        || !ExtractionZone.IsValid() || !ExtractionZone->ContainsCharacter(this)
        || !GetController() || GetWorld()->GetTimeSeconds() < RaidStatus.ExtractionEndServerTime) return;
    RaidStatus.State = EMarau6RaidState::Extracted;
    if(!Inventory->FinalizeRaid(true)){RaidStatus.State=EMarau6RaidState::Extracting;return;}
    RaidStatus.ExtractionEndServerTime = 0.0;
    ExtractionZone.Reset();
    CancelReload();
    OnRep_RaidStatus();
    ForceNetUpdate();
}

void AMarau6Character::OnRep_RaidStatus()
{
    if (!CanParticipate())
    {
        StopJumping();
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
    }
    if (RaidStatus.State == EMarau6RaidState::Extracted)
    {
        SetActorHiddenInGame(true);
        SetActorEnableCollision(false);
    }
}

void AMarau6Character::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if(HasAuthority() && GetWorld() && GetWorld()->IsGameWorld()) Inventory->InitializeForRaid();
}
bool AMarau6Character::HasEquippedRifle() const { return Inventory && Inventory->HasRifle(); }
int32 AMarau6Character::GetRifleDurability() const
{
    const auto* Item=Inventory?Inventory->ItemAt(Marau6Slots::Weapon):nullptr;
    return Item && Item->IsRifle()?Item->Durability:0;
}
void AMarau6Character::OnInventoryChanged(bool bEquipmentChanged)
{
    Healing->NotifyInventoryChanged(bEquipmentChanged);
    const bool bEquipped=HasEquippedRifle();
    FirstPersonRifle->SetVisibility(bEquipped,true);
    ThirdPersonRifle->SetVisibility(bEquipped,true);
    FirstPersonMesh->SetVisibility(bEquipped,true);
    if(HasAuthority() && bEquipmentChanged && CanParticipate())
    {
        CancelReload();
        const FMarau6Item* Item=Inventory->ItemAt(Marau6Slots::Weapon);
        AmmoState.Magazine=Item?Item->Magazine:0;
        AmmoState.Reserve=Item?Item->Reserve:0;
        ForceNetUpdate();
    }
}
void AMarau6Character::InventoryShortcut(FKey Key)
{
    if(Key==EKeys::F)EquipSelectedItem();
    else if(Key==EKeys::G)DropSelectedItem();
    else if(Key==EKeys::P)ProtectSelectedItem();
    else if(Key==EKeys::H)Healing->UseSelected();
}
void AMarau6Character::ToggleInventory()
{
    if(IsLocallyControlled()){bInventoryOpen=!bInventoryOpen;if(bInventoryOpen)bFireNeedsRelease=true;InventoryMoveSource.Invalidate();}
}
void AMarau6Character::ConfirmInventoryMove()
{
    if(!IsLocallyControlled() || !CanParticipate() || !bInventoryOpen)
    {InventoryMoveSource.Invalidate();return;}
    if(InventoryMoveSource.IsValid())
    {
        const FGuid Source=InventoryMoveSource;InventoryMoveSource.Invalidate();
        const auto* Item=Inventory->GetSnapshot().Items.FindByPredicate([Source](const FMarau6Item& Value){return Value.Id==Source;});
        if(Item && Item->Slot!=SelectedInventorySlot) Inventory->ServerMove(Source,SelectedInventorySlot);
    }
    else if(const auto* Item=Inventory->ItemAt(SelectedInventorySlot)) InventoryMoveSource=Item->Id;
}
void AMarau6Character::NextInventorySlot() { if(bInventoryOpen) SelectedInventorySlot=(SelectedInventorySlot+1)%Marau6Slots::Total; }
void AMarau6Character::PreviousInventorySlot() { if(bInventoryOpen) SelectedInventorySlot=(SelectedInventorySlot+Marau6Slots::Total-1)%Marau6Slots::Total; }
void AMarau6Character::PickupLoot()
{
    if(!IsLocallyControlled() || !CanParticipate()) return;
    if(auto* Loot=Inventory->LookedAtLoot()) Inventory->ServerPickup(Loot,Loot->GetItem().Id);
}
void AMarau6Character::DropSelectedItem()
{
    if(IsLocallyControlled() && CanParticipate() && bInventoryOpen)
        if(const auto* Item=Inventory->ItemAt(SelectedInventorySlot)) Inventory->ServerDrop(Item->Id);
}
void AMarau6Character::RequestInventoryMove(FGuid Id,int32 Destination)
{
    const auto* Item=Inventory->GetSnapshot().Items.FindByPredicate([Id](const auto& I){return I.Id==Id;});
    if(!Item)return;
    if(auto* PC=Cast<AMarau6PlayerController>(GetController());PC && PC->IsInventoryVisible() && PC->GetInventoryContext().IsValid())
    {FMarau6InventoryUIRequest R;if(PC->CaptureInventoryDrag(Item->Slot,R))PC->SubmitInventoryDrop(R,Destination);}
    else Inventory->ServerMove(Id,Destination);
}
void AMarau6Character::EquipSelectedItem()
{
    if(IsLocallyControlled() && CanParticipate() && bInventoryOpen)
        if(const auto* Item=Inventory->ItemAt(SelectedInventorySlot)) RequestInventoryMove(Item->Id,Marau6Slots::Weapon);
}
void AMarau6Character::ProtectSelectedItem()
{
    if(!IsLocallyControlled() || !CanParticipate() || !bInventoryOpen) return;
    const auto* Item=Inventory->ItemAt(SelectedInventorySlot);if(!Item)return;
    if(Item->IsRifle()){if(auto* PC=Cast<AMarau6PlayerController>(GetController()))PC->SetInventoryNotice(TEXT("Rifles nao cabem no safe pocket."));return;}
    const bool bSafe=Marau6Slots::IsSafe(SelectedInventorySlot);
    for(int32 Slot=bSafe?0:Marau6Slots::SafeStart;Slot<(bSafe?Marau6Slots::BackpackCount:Marau6Slots::Weapon);++Slot)
        if(!Inventory->ItemAt(Slot)){RequestInventoryMove(Item->Id,Slot);return;}
    if(auto* PC=Cast<AMarau6PlayerController>(GetController()))PC->SetInventoryNotice(bSafe?TEXT("Mochila cheia."):TEXT("Safe pocket cheio."));
}

bool AMarau6Character::CanParticipate() const
{
    if(bAtBase || !IsAlive() || (RaidStatus.State!=EMarau6RaidState::Active && RaidStatus.State!=EMarau6RaidState::Extracting)) return false;
    if(HasAuthority() && UMarau6Persistence::UsesPersistentBase(GetWorld()))
        return GetWorld()->GetSubsystem<UMarau6Persistence>()->CanMutate();
    return true;
}
void AMarau6Character::OnRep_AtBase()
{
    SetActorHiddenInGame(bAtBase);SetActorEnableCollision(!bAtBase);
    if(bAtBase){GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->DisableMovement();}
    else if(IsAlive()){GetCharacterMovement()->SetMovementMode(MOVE_Walking);}
}
bool AMarau6Character::FindRaidEntry(FVector& Location,FRotator& Rotation) const
{
    if(!GetWorld())return false;
    TArray<AActor*> Starts;
    if(auto* Mode=GetWorld()->GetAuthGameMode())
        if(auto* Preferred=Mode->FindPlayerStart(GetController()))Starts.Add(Preferred);
    for(TActorIterator<APlayerStart> It(GetWorld());It;++It)Starts.AddUnique(*It);
    const auto* Capsule=GetCapsuleComponent();
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()+2.f,Capsule->GetScaledCapsuleHalfHeight()+2.f);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(Marau6RaidEntry),false,this);
    for(auto* Start:Starts)
    {
        Rotation=FRotator(0,Start->GetActorRotation().Yaw,0);
        // Keep map-authored height; search nearby spaces instead of embedding pawns
        // in terrain or placing both players at the same point.
        for(int32 Attempt=0;Attempt<17;++Attempt)
        {
            FVector Offset(0,0,10);
            if(Attempt>0)
            {
                const float Angle=(Attempt-1)%8*PI/4.f;
                const float Radius=Attempt<=8?150.f:300.f;
                Offset+=FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0);
            }
            const FVector Candidate=Start->GetActorLocation()+Offset;
            if(!GetWorld()->OverlapBlockingTestByProfile(Candidate,FQuat::Identity,Capsule->GetCollisionProfileName(),Shape,Query))
            {Location=Candidate;return true;}
        }
    }
    return false;
}
bool AMarau6Character::SetAtBase(bool bValue)
{
    if(!HasAuthority())return false;
    Healing->ResetForBase();
    if(!bValue)
    {
        FVector Location;FRotator Rotation;
        if(!FindRaidEntry(Location,Rotation))return false;
        SetActorEnableCollision(true);
        if(!TeleportTo(Location,Rotation,false,false)){SetActorEnableCollision(false);return false;}
        GetCharacterMovement()->StopMovementImmediately();
        Health=100;RaidStatus=FMarau6RaidStatus();ExtractionZone.Reset();LastServerFireTime=-1;bInventoryOpen=false;InventoryMoveSource.Invalidate();
        SetRagdoll(false);
        if(auto* PC=Cast<APlayerController>(GetController())){PC->SetControlRotation(Rotation);PC->ClientSetRotation(Rotation,true);}
    }
    bAtBase=bValue;CancelReload();
    OnRep_AtBase();if(!bAtBase)OnInventoryChanged(true);ForceNetUpdate();return true;
}
void AMarau6Character::RefreshEquippedAmmo()
{
    if(!HasAuthority())return;
    if(const auto* I=Inventory->ItemAt(Marau6Slots::Weapon)){AmmoState.Magazine=I->Magazine;AmmoState.Reserve=I->Reserve;ForceNetUpdate();}
}
