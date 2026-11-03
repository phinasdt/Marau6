#pragma once
#include "CoreMinimal.h"
#include "Marau6InventoryTypes.h"
#include "Marau6InventoryUIModel.generated.h"

USTRUCT()
struct FMarau6InventoryUIRequest
{
    GENERATED_BODY()
    UPROPERTY() FGuid RequestId;
    UPROPERTY() FGuid Context;
    UPROPERTY() FGuid Item;
    UPROPERTY() FGuid DestinationItem;
    UPROPERTY() int32 Source=INDEX_NONE;
    UPROPERTY() int32 Destination=INDEX_NONE;
    UPROPERTY() int32 Profile=0;
    UPROPERTY() int32 Revision=0;
    bool Same(const FMarau6InventoryUIRequest& B) const
    {return RequestId==B.RequestId && Context==B.Context && Item==B.Item && DestinationItem==B.DestinationItem && Source==B.Source && Destination==B.Destination && Profile==B.Profile && Revision==B.Revision;}
};

struct FMarau6InventoryUIReceipt
{
    FMarau6InventoryUIRequest Request;
    bool bSuccess=false;
    FString Message;
    int32 Revision=0;
};

/** Structured copy for the inspection panel. */
struct FMarau6ItemInspection
{
    FString Category,Title,Subtitle,Lead,Body;
};

/** Presentation rules; the ledger/backend remain the only item writers. */
struct FMarau6InventoryUIModel
{
    static bool ValidSlot(int32 Slot,bool bBase);
    static FString MoveReason(const TArray<FMarau6Item>& Items,int32 Source,int32 Destination,bool bBase);
    static FString SlotName(int32 Slot);
    static FString Details(const FMarau6Item& Item);
    /** Short code and category shown inside a slot, e.g. MED / SUPRIMENTOS. */
    static FString ShortLabel(const FMarau6Item& Item);
    static FString Category(const FMarau6Item& Item);
    static FMarau6ItemInspection Inspect(const FMarau6Item* Item,int32 Slot);
};
