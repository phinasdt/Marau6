#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Marau6Persistence.generated.h"
class AMarau6PlayerController;
class UMarau6ItemLedger;

/** Local server journal and serialized HTTP bridge. Never instantiated as client storage. */
UCLASS()
class UMarau6Persistence : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static bool UsesPersistentBase(const UWorld* World);
    static FString Encode(const TSharedPtr<FJsonObject>& Object);
    static bool AtomicWrite(const FString& Path,const FString& Content);
    const FString& GetRoundId() const { return RaidId; }
    bool IsEntryPending() const { return bEntering; }
    void DeferDeath(FGuid Owner,FVector Location) { DeferredDeaths.Add(Owner,Location); }
    bool IsSettled() const { return Commands.IsEmpty() && !bSending; }
    bool CanMutate() const { return bReady && !bEntering && !bFatal; }
    bool CommitLedger();
    void SelectProfile(AMarau6PlayerController* PC,int32 Number);
    void SelectKit(AMarau6PlayerController* PC,bool bBasic);
    void MoveBase(AMarau6PlayerController* PC,int32 Source,int32 Destination,TFunction<void(bool,const FString&)> Done={});
    void EnterRaid(AMarau6PlayerController* PC);
    void NextRound(AMarau6PlayerController* PC);
    void Disconnect(AMarau6PlayerController* PC);
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMarau6Persistence,STATGROUP_Tickables); }
    const FString& GetStatus() const { return Status; }
private:
    friend class FMarau6BotDurabilityTest;
    friend class FMarau6HealingDurabilityTest;
    friend class FMarau6WearJournalTest;
    friend class FMarau6AmmoRollbackTest;
    struct FCommand
    {
        FString Path,Body;
        TFunction<void(const FString&)> Done;
    };
    bool Queue(const FString& Path,TSharedPtr<FJsonObject> Body,TFunction<void(const FString&)> Done={});
    bool SaveJournal();
    void Pump();
    void OpenRound();
    void Refresh(AMarau6PlayerController* PC,const FString& PreserveMessage=FString());
    void FlushDeferredDeaths();
    void AckSnapshot(const TArray<int32>& Ended);
    void Fail(const FString& Message);
    bool bRoundAcknowledged=false;
    bool bEnabled=false,bReady=false,bFatal=false,bSending=false,bEntering=false,bRecovery=false,bOpening=false;
    void* ServerLock=nullptr;
    float Retry=0;
    int64 Sequence=0;
    FString RaidId,Key,Url,JournalPath,Status,LastCommandError;
    TArray<FCommand> Commands;
    TMap<int32,TWeakObjectPtr<AMarau6PlayerController>> Claims;
    TSet<int32> Entered,Submitted,Completed;
    TMap<FGuid,FVector> DeferredDeaths;
    TArray<TSharedPtr<FJsonValue>> SeedTransforms;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game || Type==EWorldType::PIE; }
};
