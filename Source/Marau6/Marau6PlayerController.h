// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Marau6InventoryUIModel.h"
#include "Marau6PlayerController.generated.h"

class UMarau6Persistence;
class UMarau6InventoryWidget;
class UMarau6HUDWidget;
class UInputMappingContext;
class UUserWidget;
class UReverbEffect;
class USoundBase;
class USoundAttenuation;

/**
 *  Simple first person Player Controller
 *  Manages the input mapping context.
 *  Overrides the Player Camera Manager class.
 */
UCLASS(abstract, config="Game")
class MARAU6_API AMarau6PlayerController : public APlayerController
{
	GENERATED_BODY()
    friend class FMarau6InventoryReorderTest;
    friend class FMarau6UIAckTest;
	
public:

	/** Constructor */
	AMarau6PlayerController();

    bool IsUsingBase() const { return bUseBase; }
    /** Base choice for the next raid: BASIC KIT (a fresh rifle, own gear stays home) or CUSTOM KIT. */
    bool IsBasicKit() const { return bBasicKit; }
    void SelectKit(bool bBasic);
    /** Equipment slots show the fixed basic kit and cannot be edited; the custom loadout stays saved. */
    bool IsKitLocked(int32 Slot) const { return IsInventoryAtBase() && bBasicKit && Slot>=0 && Slot<100; }
    bool IsInRaid() const { return bInRaid; }
    const FString& GetBaseJson() const { return BaseJson; }
    const FString& GetBaseMessage() const { return BaseMessage; }
    const FString& GetBaseBackendStatus() const { return BaseBackendStatus; }
    int32 GetBaseProfile() const { return BaseProfile; }
    int32 GetBaseSelection() const { return BaseSelection; }
    int32 GetBaseSource() const { return BaseSource; }
    static int32 BaseSlot(int32 Index) { return Index<11?Index:100+Index-11; }
    bool IsInventoryAtBase() const;
    bool IsInventoryVisible() const;
    bool IsInventoryReadOnly() const;
    bool InventoryRequestPending() const;
    bool HasOwnInventoryDrag() const;
    bool HasInventoryWidget() const { return InventoryWidget!=nullptr; }
    bool HasHUDWidget() const { return HUDWidget!=nullptr; }
    UMarau6HUDWidget* GetHUDWidget() const {return HUDWidget.Get();}
    UMarau6InventoryWidget* GetInventoryWidget() const {return InventoryWidget.Get();}
    const TArray<FMarau6Item>& GetInventoryUIItems() const;
    const FMarau6Item* InventoryUIItemAt(int32 Slot) const;
    int32 InventoryUIRevision() const;
    int32 GetInventoryUISelection() const;
    int32 GetInventoryKeyboardSourceSlot() const { return KeyboardInventorySource.Item.IsValid()?KeyboardInventorySource.Source:INDEX_NONE; }
    FGuid GetInventoryContext() const {return InventoryContext;}
    FString GetInventoryNotice() const;
    void SetInventoryNotice(const FString& Message) {if(!PendingInventoryRequest.IsValid())InventoryMessage=Message;}
    void SelectInventorySlot(int32 Slot);
    bool CaptureInventoryDrag(int32 Slot,FMarau6InventoryUIRequest& Out);
    bool SubmitInventoryDrop(FMarau6InventoryUIRequest Request,int32 Destination);
    void CancelInventoryDrag();
    void RefreshInventoryContext();
    void UpdateInventoryUI();
    bool HandleInventoryKey(FKey Key);
    void InventoryKeyboardAccept();
    UFUNCTION(Server,Reliable) void ServerInventoryMoveUI(FMarau6InventoryUIRequest Request);
    UFUNCTION(Client,Reliable) void ClientInventoryResult(FGuid Request,FGuid Context,bool bSuccess,const FString& Message,int32 Revision);
    UFUNCTION(Client,Reliable) void ClientInventoryNotice(const FString& Message);
    virtual void PlayerTick(float Delta) override;
    virtual void TickActor(float Delta,ELevelTick Type,FActorTickFunction& Function) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void OnPossess(APawn* NewPawn) override;
    UFUNCTION(Server,Reliable) void ServerSelectProfile(int32 Number);
    UFUNCTION(Server,Reliable) void ServerBaseMove(int32 Source,int32 Destination);
    UFUNCTION(Server,Reliable) void ServerEnterRaid();
    UFUNCTION(Server,Reliable) void ServerSelectKit(bool bBasic);
    UFUNCTION(Server,Reliable) void ServerNextRound();
private:
    friend class UMarau6Persistence;
    UPROPERTY(Replicated) bool bUseBase=false;
    UPROPERTY(Replicated) bool bInRaid=false;
    UPROPERTY(Replicated) bool bBaseBusy=false;
    UPROPERTY(Replicated) bool bBasicKit=false;
    UPROPERTY(Replicated) FGuid InventoryContext;
    UPROPERTY(Transient) TObjectPtr<UMarau6InventoryWidget> InventoryWidget;
    UPROPERTY(Transient) TObjectPtr<UMarau6HUDWidget> HUDWidget;
    /** Local listener only: the map's soft room tail, see Marau6Audio. */
    UPROPERTY() TObjectPtr<UReverbEffect> AmbientReverb;
    /** Local listener only: decompression gas on raid entry. */
    UPROPERTY() TObjectPtr<USoundBase> GasSound;
    UPROPERTY() TObjectPtr<USoundAttenuation> GasAttenuation;
    bool bHeardRaid=false;
    void UpdateRaidGas();
    mutable FString CachedInventoryJson;
    mutable TArray<FMarau6Item> CachedBaseItems;
    /** Base view with the basic kit selected: the stash plus the kit preview in the equipment slots. */
    mutable TArray<FMarau6Item> CachedKitItems;
    mutable int32 CachedBaseRevision=0;
    FString InventoryContextKey,InventoryMessage,InventoryCompletionMessage,ObservedBaseMessage;
    FGuid LastInventoryContext,PendingInventoryRequest,PendingInventoryContext,ServerInventoryRequest;
    double PendingInventorySince=0;
    int32 InventoryConfirmedRevision=0;
    bool bInventoryInputActive=false,bInventoryAwaitingState=false,bLastUIAtBase=false;
    FMarau6InventoryUIRequest KeyboardInventorySource;
    TMap<FGuid,FMarau6InventoryUIReceipt> InventoryReceipts;
    TArray<FGuid> InventoryReceiptOrder;
    void CompleteInventoryRequest(const FMarau6InventoryUIRequest& Request,bool bSuccess,const FString& Message);
    UPROPERTY(Replicated) int32 BaseProfile=0;
    UPROPERTY(Replicated) FString BaseJson;
    UPROPERTY(Replicated) FString BaseMessage;
    UPROPERTY(Replicated) FString BaseBackendStatus;
    int32 BaseSelection=0,BaseSource=INDEX_NONE;
    void ToggleKit();
    void SelectOne();void SelectTwo();void BaseUp();void BaseDown();void BaseLeft();void BaseRight();void BaseAccept();void BaseEnter();void BaseNext();
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;
};
