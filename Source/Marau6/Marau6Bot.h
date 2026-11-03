#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Marau6Bot.generated.h"
class AMarau6Character;
class USkeletalMeshComponent;
class UAnimSequence;
class UTextRenderComponent;
class UMarau6Footsteps;
UENUM()
enum class EMarau6BotState : uint8 { Patrol, Investigate, Combat, Dead };
/** Server-only decisions; clients receive movement, health and cosmetic shots. */
UCLASS()
class AMarau6Bot : public ACharacter
{
    GENERATED_BODY()
public:
    AMarau6Bot();
    bool CanSee(const AMarau6Character* Player) const;
    bool CanAct() const;
    void StepAI();
    float GetHealth() const { return Health; }
    bool IsAlive() const { return Health>0; }
    EMarau6BotState GetBotState() const { return State; }
    /** Dead bots stay as ragdolls this long (5 min); a new round clears them anyway. */
    static constexpr float CorpseSeconds = 300.f;
    FGuid GetLootId() const { return LootId; }
    int32 GetShotCount() const { return ShotCount; }
    FVector GetLastSeen() const { return LastSeen; }
    virtual float TakeDamage(float Damage,const FDamageEvent& Event,AController* Instigator,AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
private:
    friend class FMarau6BotBehaviorTest;
    UPROPERTY(ReplicatedUsing=OnRep_State) float Health=100;
    UPROPERTY(ReplicatedUsing=OnRep_State) EMarau6BotState State=EMarau6BotState::Patrol;
    UPROPERTY(Replicated) FGuid LootId;
    UPROPERTY(Replicated) int32 ShotCount=0;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Rifle;
    UPROPERTY() TObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Nameplate;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMarau6Footsteps> Footsteps;
    bool bWalkingAnimation=false,bResolvingDeath=false;
    TWeakObjectPtr<AMarau6Character> Target;
    FVector Home=FVector::ZeroVector,LastSeen=FVector::ZeroVector;
    double SeenAt=-100,ReactAt=0,LastShot=-100,NextPathAt=0;
    bool Navigate(const FVector& Destination);
    void FireAt(AMarau6Character* Player);
    UFUNCTION() void OnRep_State();
    UFUNCTION(NetMulticast,Unreliable) void PresentShot(FVector_NetQuantize Point,FVector_NetQuantizeNormal Normal,bool bHit,bool bCharacter);
};
