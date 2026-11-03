// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPtr.h"
#include "Marau6Character.generated.h"

class UMarau6InventoryComponent;
class UMarau6HealingComponent;
class UMarau6Footsteps;
class UMarau6MovementComponent;
class UMarau6ItemLedger;
class AMarau6ExtractionZone;
class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UAnimInstance;
class UInputMappingContext;
class UAnimSequence;
class USoundBase;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UENUM(BlueprintType)
enum class EMarau6RaidState : uint8 { Active, Extracting, Extracted, Eliminated };

USTRUCT()
struct FMarau6RaidStatus
{
    GENERATED_BODY()
    UPROPERTY() EMarau6RaidState State = EMarau6RaidState::Active;
    UPROPERTY() double ExtractionEndServerTime = 0.0;
};

/** One replicated snapshot keeps the owner's counters and reload state together. */
USTRUCT(BlueprintType)
struct FMarau6AmmoState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    int32 Magazine = 20;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    int32 Reserve = 40;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bReloading = false;
};

/**
 *  A basic first person character
 */
UCLASS(abstract)
class AMarau6Character : public ACharacter
{
	GENERATED_BODY()
    friend class FMarau6InventoryReorderTest;
    friend class FMarau6UIInputTest;
    friend class FMarau6AutomaticTest;
    friend class FMarau6RagdollTest;

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

    /** Presentation only: neither mesh participates in damage or collision. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USkeletalMeshComponent> FirstPersonRifle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USkeletalMeshComponent> ThirdPersonRifle;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    TSoftClassPtr<UAnimInstance> PrototypeFirstPersonPose;

    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    TSoftClassPtr<UAnimInstance> PrototypeThirdPersonPose;

    /** Camera-space shift of the owner's arms and rifle (X forward, Y right, Z up). */
    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    FVector FirstPersonArmsOffset = FVector(18.0, 0.0, -4.0);

    /** Camera-space shift added at full aim so the rifle sights sit on the screen centre. */
    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    FVector FirstPersonAimOffset = FVector(12.0, -9.5, 0.5);

    /** Camera-space shift of the arms at full sprint. */
    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    FVector SprintArmsOffset = FVector(0.0, 1.0, -3.0);

    /** Where the rifle points at full sprint, relative to the view. */
    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    FRotator SprintAimRotation = FRotator(-10.0, -30.0, 0.0);

    /** World field of view multiplier while aiming down sights. */
    UPROPERTY(EditDefaultsOnly, Category = "Presentation")
    float AimFieldOfViewScale = 0.8f;

    /** Upward and maximum sideways camera kick per accepted shot, in degrees. */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    FVector2D RecoilKick = FVector2D(1.3, 0.45);

    /** Share of each upward kick that settles back on its own right after the shot. */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float RecoilSettle = 0.35f;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

    /** Inactive camera the first person rig aims from; turning it angles the rifle. */
    UPROPERTY(VisibleAnywhere, Category = "Presentation")
    TObjectPtr<UCameraComponent> ViewModelAim;

protected:

	/** Vida atual, controlada pelo servidor. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	float Health = 100.0f;

	/** Atualiza o cliente quando a vida muda. */
	UFUNCTION()
	void OnRep_Health();

    /** Direction of the last damaging shot; pushes the ragdoll on death. */
    UPROPERTY(Replicated)
    FVector_NetQuantizeNormal LastHitDirection = FVector::ZeroVector;


    /** Runtime mapping for the prototype; existing movement assets stay unchanged. */
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> PrototypeFireAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> PrototypeReloadAction;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    FMarau6AmmoState AmmoState;

    FTimerHandle ReloadTimer;


    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> PrototypeFireMapping;

    void DoFire();
    void StartAutomaticFire();
    void StopAutomaticFire();
    bool bFireNeedsRelease=false;
    double NextLocalFireTime=-1;
    void DoReload();
    void StartSprint();
    void StopSprint();
    void StartAim();
    void StopAim();
    void AddRecoil();
    void UpdateViewModel(float DeltaSeconds);

    // Owner-only presentation state; nothing here is replicated or trusted by the server.
    FTransform ArmsNeutral;
    float AimAlpha = 0.f;
    float SprintAlpha = 0.f;
    float BobPhase = 0.f;
    /** Owner's clock for the staged first person reload; negative when not reloading. */
    double ReloadPoseStart = -1.0;
    float ShotKick = 0.f;
    float RecoilRecovery = 0.f;
    double LastShotTime = -1.0;
    FRotator SwayRotation = FRotator::ZeroRotator;
    FRotator LastViewRotation = FRotator::ZeroRotator;
    FRotator PendingRecoil = FRotator::ZeroRotator;
    float DefaultFieldOfView = 90.f;
    float DefaultFirstPersonFieldOfView = 70.f;

    // Dead bodies ragdoll; a new round revives this same pawn, so keep how to undo it.
    void SetRagdoll(bool bEnable);
    void HideFirstPersonKit();
    bool bRagdoll = false;
    FTransform BodyNeutral;
    FName BodyCollisionProfile;

    UFUNCTION(Server, Reliable)
    void ServerReload();

    void FinishReload();
    void CancelReload();

    /** Everyone sees the reload: the owner-only ammo state cannot drive the pose. */
    UPROPERTY(ReplicatedUsing=OnRep_ReloadPose) bool bReloadPose=false;
    UFUNCTION() void OnRep_ReloadPose();
    void SetReloadPose(bool bValue);
    UPROPERTY() TObjectPtr<UAnimSequence> ReloadAnimation;
    /** Recorded reload, cut in three and timed to the staged reload: magazine out, magazine in, charge. */
    UPROPERTY() TObjectPtr<USoundBase> ReloadMagOut;
    UPROPERTY() TObjectPtr<USoundBase> ReloadMagIn;
    UPROPERTY() TObjectPtr<USoundBase> ReloadCharge;
    TArray<FTimerHandle> ReloadSoundTimers;

    UFUNCTION(Server, Unreliable)
    void ServerFire();

    UFUNCTION(NetMulticast, Unreliable)
    void MulticastShot(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal, bool bBlockingHit, bool bHitCharacter);

    UPROPERTY(ReplicatedUsing=OnRep_AtBase) bool bAtBase=false;
    UFUNCTION() void OnRep_AtBase();
    friend class UMarau6ItemLedger;
    friend class UMarau6HealingComponent;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMarau6HealingComponent> Healing;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMarau6InventoryComponent> Inventory;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMarau6Footsteps> Footsteps;
    bool bInventoryOpen=false;
    int32 SelectedInventorySlot=0;
    FGuid InventoryMoveSource;
    void ToggleInventory();
    void NextInventorySlot();
    void PreviousInventorySlot();
    void PickupLoot();
    void DropSelectedItem();
    void EquipSelectedItem();
    void ProtectSelectedItem();
    virtual void PostInitializeComponents() override;
    UPROPERTY(ReplicatedUsing=OnRep_RaidStatus)
    FMarau6RaidStatus RaidStatus;
    TWeakObjectPtr<AMarau6ExtractionZone> ExtractionZone;
    UFUNCTION() void OnRep_RaidStatus();
    void UpdateExtraction();
    void CompleteExtraction();
    virtual void BeginPlay() override;

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Server-owned cooldown, never accepted from a client. */
    double LastServerFireTime = -1.0;

    static constexpr int32 PrototypeMagazineCapacity = 20;
    static constexpr float PrototypeReloadDuration = 2.0f;

    static constexpr float PrototypeDamage = 25.0f;
    static constexpr float PrototypeRange = 10000.0f;
    static constexpr double PrototypeFireInterval = 60.0 / 450.0;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* MouseLookAction;
	
public:
	AMarau6Character(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    virtual void Tick(float DeltaSeconds) override;
    UMarau6InventoryComponent* GetInventory() const { return Inventory; }
    UMarau6HealingComponent* GetHealing() const { return Healing; }
    bool HasEquippedRifle() const;
    UMarau6MovementComponent* GetMarau6Movement() const;
    bool IsSprinting() const;
    bool IsAiming() const;
    int32 GetRifleDurability() const;
    bool IsInventoryOpen() const { return bInventoryOpen; }
    void ConfirmInventoryMove();
    void SelectInventorySlot(int32 Slot) { if(Slot>=0 && Slot<11)SelectedInventorySlot=Slot; }
    void CancelInventorySelection() { InventoryMoveSource.Invalidate(); }
    void ToggleInventoryFromUI() { ToggleInventory(); }
    void InventoryShortcut(FKey Key);
    void CloseInventoryFromUI() { bInventoryOpen=false;InventoryMoveSource.Invalidate(); }
    void RequestInventoryMove(FGuid Id,int32 Destination);
    FGuid GetInventoryMoveSource() const { return InventoryMoveSource; }
    int32 GetSelectedInventorySlot() const { return SelectedInventorySlot; }
    void OpenInventoryResult() { bInventoryOpen=true;bFireNeedsRelease=true; }
    void OnInventoryChanged(bool bEquipmentChanged);
    void RefreshEquippedAmmo();
    EMarau6RaidState GetRaidState() const { return RaidStatus.State; }
    bool CanParticipate() const;
    bool SetAtBase(bool bValue);
    bool FindRaidEntry(FVector& Location,FRotator& Rotation) const;
    bool IsAtBase() const { return bAtBase; }
    float GetExtractionSecondsRemaining() const;
    
    /** Current health is read-only outside the server damage handler. */
    UFUNCTION(BlueprintPure, Category = "Health")
    float GetHealth() const { return Health; }

    UFUNCTION(BlueprintPure, Category = "Health")
    bool IsAlive() const { return Health > 0.0f; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    int32 GetMagazineAmmo() const { return AmmoState.Magazine; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    int32 GetReserveAmmo() const { return AmmoState.Reserve; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsReloading() const { return AmmoState.bReloading; }

    int32 GetMagazineCapacity() const { return PrototypeMagazineCapacity; }

#if WITH_EDITOR
    /** Inspection aid: reload now, or only show the reload pose on a pawn without a controller. */
    void DebugReload();
#endif

    /** Montage slot added to the body pose's upper branch by Marau6.SetupLocomotion. */
    static inline const FName ReloadSlot = TEXT("DefaultSlot");
    /** First person pose variable (added by Marau6.SetupLocomotion) blending the hand rig in and out. */
    static constexpr const TCHAR* RigAlphaVariable = TEXT("RigAlpha");

    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;


	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Pose aim. Flat for the owner, whose arms already pitch with the camera. */
    virtual FRotator GetBaseAimRotation() const override;

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

};

