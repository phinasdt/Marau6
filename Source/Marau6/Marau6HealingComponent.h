#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Marau6HealingComponent.generated.h"
class AMarau6Character;
USTRUCT()
struct FMarau6HealingFeedback
{
    GENERATED_BODY()
    UPROPERTY() double EndsAt=0;
    UPROPERTY() FString Message;
};
/** Requests identify an owned item; only the server measures time and commits use. */
UCLASS()
class UMarau6HealingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMarau6HealingComponent();
    void UseSelected();
    /** Use of a specific supply; cancels an ongoing treatment instead. */
    void UseItem(FGuid Item);
    UFUNCTION(Server,Reliable) void ServerUse(FGuid Item);
    UFUNCTION(Server,Reliable) void ServerCancel();
    bool IsHealing() const { return bHealing; }
    float Remaining() const;
    const FString& Status()const { return Feedback.Message; }
    void UpdateHealing();
    void Cancel(const FString& Reason);
    void ResetForBase();
    void NotifyInventoryChanged(bool bEquipmentChanged);
    void CancelFromMovement();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const override;
private:
    AMarau6Character* Character()const;
    UPROPERTY(ReplicatedUsing=OnRep_Healing) bool bHealing=false;
    UPROPERTY(Replicated) FMarau6HealingFeedback Feedback;
    FGuid Source;
    int32 SourceSlot=INDEX_NONE;
    FVector Origin=FVector::ZeroVector;
    bool bCompleting=false;
    void Message(const FString& Text);
    void Finish();
    UFUNCTION() void OnRep_Healing();
    virtual void TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function)override;
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
};
