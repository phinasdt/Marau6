#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Input/DragAndDrop.h"
#include "Marau6InventoryUIModel.h"
class AMarau6PlayerController;
class UTexture2D;

class FMarau6InventoryDragOperation : public FGameDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FMarau6InventoryDragOperation,FGameDragDropOperation)
    TWeakObjectPtr<AMarau6PlayerController> Controller;
    FMarau6InventoryUIRequest Request;
    FString Label;
    int32 IconKind=-1;
    UTexture2D* IconTexture=nullptr;
    static TSharedRef<FMarau6InventoryDragOperation> New(AMarau6PlayerController* PC,const FMarau6InventoryUIRequest& R,UTexture2D* Texture);
    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
    virtual void OnDrop(bool Handled,const FPointerEvent& Event) override;
};

class SMarau6InventorySlot : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMarau6InventorySlot){}
        SLATE_ARGUMENT(TWeakObjectPtr<AMarau6PlayerController>,Controller)
        SLATE_ARGUMENT(int32,Slot)
        SLATE_ARGUMENT_DEFAULT(float,Height){66.f};
        SLATE_ARGUMENT(UTexture2D*,RifleIcon)
        SLATE_ARGUMENT(UTexture2D*,MedkitIcon)
        SLATE_ARGUMENT(UTexture2D*,ScrapIcon)
        SLATE_ARGUMENT(UTexture2D*,AmmoIcon)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    int32 GetSlotIndex() const {return Slot;}
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnDragDetected(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnDragOver(const FGeometry&,const FDragDropEvent&) override;
    virtual FReply OnDrop(const FGeometry&,const FDragDropEvent&) override;
private:
    TWeakObjectPtr<AMarau6PlayerController> PC;
    int32 Slot=INDEX_NONE;
    UTexture2D* RifleIcon=nullptr;
    UTexture2D* MedkitIcon=nullptr;
    UTexture2D* ScrapIcon=nullptr;
    UTexture2D* AmmoIcon=nullptr;
    const FMarau6Item* Item() const;
    UTexture2D* IconForItem() const;
    bool IsSelected() const;
    bool IsKeyboardSource() const;
    FString NumberLabel() const;
    FString CenterLabel() const;
    FString CategoryLabel() const;
    FString WeaponDetail() const;
    const FSlateBrush* SlotBrush() const;
    bool IsBlockedDestination() const;
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
};
