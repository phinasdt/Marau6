#include "Marau6InventorySlot.h"
#include "Marau6ItemIcon.h"
#include "Widgets/SBoxPanel.h"
#include "Marau6PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Marau6UIStyle.h"
#include "Widgets/SOverlay.h"
#include "Rendering/DrawElements.h"

TSharedRef<FMarau6InventoryDragOperation> FMarau6InventoryDragOperation::New(AMarau6PlayerController* PC,const FMarau6InventoryUIRequest& R,UTexture2D* Texture)
{
    auto Op=MakeShared<FMarau6InventoryDragOperation>();Op->Controller=PC;Op->Request=R;
    const auto* Item=PC->InventoryUIItemAt(R.Source);Op->Label=Item?Item->Name():TEXT("Item");Op->IconKind=Item?static_cast<int32>(Item->Kind):-1;Op->IconTexture=Texture;Op->Construct();return Op;
}
TSharedPtr<SWidget> FMarau6InventoryDragOperation::GetDefaultDecorator() const
{return SNew(SBorder).BorderImage(Marau6UI::SlotBoxSelected()).Padding(8)
    [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(90).HeightOverride(45)[SNew(SMarau6ItemIcon).Kind(IconKind).Texture(IconTexture)]]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label.ToUpper())).Font(Marau6UI::Mono(8)).ColorAndOpacity(Marau6UI::Selected())]];}

void FMarau6InventoryDragOperation::OnDrop(bool Handled,const FPointerEvent& Event)
{if(!Handled)if(auto* P=Controller.Get())P->SetInventoryNotice(TEXT("Movimentacao cancelada."));FGameDragDropOperation::OnDrop(Handled,Event);}
void SMarau6InventorySlot::Construct(const FArguments& Args)
{
    PC=Args._Controller;Slot=Args._Slot;
    RifleIcon=Args._RifleIcon;MedkitIcon=Args._MedkitIcon;ScrapIcon=Args._ScrapIcon;AmmoIcon=Args._AmmoIcon;
    SetToolTipText(TAttribute<FText>::CreateLambda([this]{const auto* I=Item();return I?FText::FromString(I->Name()):FText::GetEmpty();}));
    auto Label=[](TFunction<FString()> Get,FSlateFontInfo Font,TFunction<FLinearColor()> Color)
    {return SNew(STextBlock).Text_Lambda([Get]{return FText::FromString(Get());}).Font(Font).ColorAndOpacity_Lambda([Color]{return FSlateColor(Color());});};
    auto Ink=[this]{return IsSelected()?Marau6UI::Selected():Marau6UI::Text();};
    const bool bWeapon=Slot==Marau6Slots::Weapon;
    // Weapon: the rifle sits right of the title, above the ammo line. Others: between the number and the category.
    auto Overlay=SNew(SOverlay)
        +SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill).Padding(bWeapon?FMargin(104,8,12,30):FMargin(10,14,10,14))
            [SNew(SMarau6ItemIcon)
                .Kind_Lambda([this]{const auto* I=Item();return I?static_cast<int32>(I->Kind):-1;})
                .Texture_Lambda([this]{return IconForItem();})]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0,5,7,0)
            [SNew(STextBlock).Text(FText::FromString(TEXT("ORIGEM"))).Font(Marau6UI::Mono(7)).ColorAndOpacity(Marau6UI::Brass()).Visibility_Lambda([this]{return IsKeyboardSource()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})];
    if(Slot==Marau6Slots::Weapon)
    {
        Overlay->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(14,10)[Label([]{return FString(TEXT("ARMA"));},Marau6UI::Font(TEXT("Regular"),9.5f,40),[]{return Marau6UI::Muted();})];
        Overlay->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(14,0,14,10)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Label([this]{return CenterLabel();},Marau6UI::Font(TEXT("BoldCondensed"),20,40),Ink)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[Label([this]{return WeaponDetail();},Marau6UI::Mono(7.5f),[]{return Marau6UI::BrassText();})]];
    }
    else
    {
        Overlay->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(7,5,0,0)[Label([this]{return NumberLabel();},Marau6UI::Mono(6.5f),[]{return Marau6UI::Muted();})];
        // An empty safe pocket names itself; every other empty slot shows a dash.
        Overlay->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Center).Padding(11,2)
            [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(CenterLabel());}).Font(Marau6UI::Font(TEXT("Regular"),11,40))
                .ColorAndOpacity_Lambda([this,Ink]{return FSlateColor(Item()?Ink():Marau6UI::Muted());})
                .Visibility_Lambda([this]{return Item()?EVisibility::Collapsed:EVisibility::Visible;})
                .Justification_Lambda([this]{return !Item() && Marau6Slots::IsSafe(Slot)?ETextJustify::Left:ETextJustify::Center;})];
        Overlay->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,4)[Label([this]{return CategoryLabel();},Marau6UI::Mono(6),[]{return Marau6UI::BrassText();})];
    }
    // Basic kit: the preview items fade and a padlock marks the slot as fixed.
    ChildSlot[SNew(SBox).HeightOverride(Args._Height)[SNew(SBorder).BorderImage_Lambda([this]{return SlotBrush();}).Padding(FMargin(1))
        .ColorAndOpacity_Lambda([this]{auto* P=PC.Get();return FLinearColor(1,1,1,P && P->IsKitLocked(Slot)?.45f:1.f);})[Overlay]]];
}
const FMarau6Item* SMarau6InventorySlot::Item() const
{auto* P=PC.Get();return P?P->InventoryUIItemAt(Slot):nullptr;}
UTexture2D* SMarau6InventorySlot::IconForItem() const
{
    const auto* I=Item();if(!I)return nullptr;
    switch(I->Kind)
    {
        case EMarau6ItemKind::Rifle:return RifleIcon;
        case EMarau6ItemKind::Supply:return MedkitIcon;
        case EMarau6ItemKind::Scrap:return ScrapIcon;
        case EMarau6ItemKind::Ammo:return AmmoIcon;
        default:return nullptr;
    }
}
bool SMarau6InventorySlot::IsSelected() const
{auto* P=PC.Get();return P && P->GetInventoryUISelection()==Slot;}
bool SMarau6InventorySlot::IsKeyboardSource() const
{auto* P=PC.Get();return P && P->GetInventoryKeyboardSourceSlot()==Slot;}
FString SMarau6InventorySlot::NumberLabel() const
{return FString::Printf(TEXT("%02d"),Slot>=100?Slot-99:Marau6Slots::IsSafe(Slot)?Slot-Marau6Slots::SafeStart+1:Slot+1);}
FString SMarau6InventorySlot::CenterLabel() const
{
    if(const auto* I=Item())return FMarau6InventoryUIModel::ShortLabel(*I);
    return Marau6Slots::IsSafe(Slot)?FString::Printf(TEXT("SAFE %02d"),Slot-Marau6Slots::SafeStart+1):TEXT("\u2014");
}
FString SMarau6InventorySlot::CategoryLabel() const
{const auto* I=Item();return I?FMarau6InventoryUIModel::Category(*I):FString();}
FString SMarau6InventorySlot::WeaponDetail() const
{
    const auto* I=Item();if(!I)return FString();
    return I->IsRifle()?FString::Printf(TEXT("CARREGADOR %d / 20  \u00B7  RESERVA %d  \u00B7  %.1f%%"),I->Magazine,I->Reserve,I->Durability/100.f):FMarau6InventoryUIModel::Category(*I);
}
const FSlateBrush* SMarau6InventorySlot::SlotBrush() const
{
    auto* P=PC.Get();if(!P)return Marau6UI::SlotBox();
    if(FSlateApplication::IsInitialized())if(auto Drag=FSlateApplication::Get().GetDragDroppingContent();Drag.IsValid() && Drag->IsOfType<FMarau6InventoryDragOperation>())
    {auto Op=StaticCastSharedPtr<FMarau6InventoryDragOperation>(Drag);if(Op->Controller==PC)return Op->Request.Source==Slot?Marau6UI::SlotBoxSource():Marau6UI::SlotBox();}
    return IsSelected() || IsKeyboardSource()?Marau6UI::SlotBoxSelected():Marau6UI::SlotBox();
}
FReply SMarau6InventorySlot::OnMouseButtonDown(const FGeometry&,const FPointerEvent& E)
{
    if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
    if(auto* P=PC.Get()){P->SelectInventorySlot(Slot);if(P->InventoryUIItemAt(Slot) && !P->IsInventoryReadOnly() && !P->InventoryRequestPending())return FReply::Handled().DetectDrag(SharedThis(this),EKeys::LeftMouseButton);}
    return FReply::Handled();
}
FReply SMarau6InventorySlot::OnDragDetected(const FGeometry&,const FPointerEvent&)
{
    if(auto* P=PC.Get())
    {P->CancelInventoryDrag();FMarau6InventoryUIRequest R;if(P->CaptureInventoryDrag(Slot,R))return FReply::Handled().BeginDragDrop(FMarau6InventoryDragOperation::New(P,R,IconForItem()));}
    return FReply::Handled();
}
FReply SMarau6InventorySlot::OnDragOver(const FGeometry&,const FDragDropEvent& E)
{
    auto Op=E.GetOperationAs<FMarau6InventoryDragOperation>();auto* P=PC.Get();if(!Op || Op->Controller!=PC || !P)return FReply::Unhandled();
    const auto Reason=FMarau6InventoryUIModel::MoveReason(P->GetInventoryUIItems(),Op->Request.Source,Slot,P->IsInventoryAtBase());
    P->SetInventoryNotice(Reason.IsEmpty()?TEXT("Solte para mover, trocar ou abastecer o rifle com 20 balas."):Reason);return FReply::Handled();
}
FReply SMarau6InventorySlot::OnDrop(const FGeometry&,const FDragDropEvent& E)
{
    auto Op=E.GetOperationAs<FMarau6InventoryDragOperation>();auto* P=PC.Get();if(!Op || Op->Controller!=PC || !P)return FReply::Unhandled();
    P->SubmitInventoryDrop(Op->Request,Slot);return FReply::Handled();
}

bool SMarau6InventorySlot::IsBlockedDestination() const
{
    auto* P=PC.Get();if(!P || !FSlateApplication::IsInitialized())return false;
    auto Drag=FSlateApplication::Get().GetDragDroppingContent();
    if(!Drag.IsValid() || !Drag->IsOfType<FMarau6InventoryDragOperation>())return false;
    const auto Op=StaticCastSharedPtr<FMarau6InventoryDragOperation>(Drag);
    return Op->Controller==PC && Op->Request.Source!=Slot &&
        !FMarau6InventoryUIModel::MoveReason(P->GetInventoryUIItems(),Op->Request.Source,Slot,P->IsInventoryAtBase()).IsEmpty();
}
int32 SMarau6InventorySlot::OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Painted=SCompoundWidget::OnPaint(Args,Geometry,Culling,Out,Layer,Style,bEnabled);
    const FVector2D Size=Geometry.GetLocalSize();
    if(Slot==Marau6Slots::Weapon && !Item())for(int32 N=0;N<5;N++)
    {
        const float X=Size.X-122.f+N*17.f;TArray<FVector2D> Bar;Bar.Add(FVector2D(X,22.f));Bar.Add(FVector2D(X,N%2?50.f:42.f));
        FSlateDrawElement::MakeLines(Out,Painted+1,Geometry.ToPaintGeometry(),Bar,ESlateDrawEffect::None,Marau6UI::Muted(),true,2.f);
    }
    if(auto* P=PC.Get();P && P->IsKitLocked(Slot))
    {
        // Padlock, top right: a 12x9 body under a rounded shackle.
        const FVector2D Origin(Size.X-24.f,8.f);const FLinearColor Ink=Marau6UI::Brass();
        TArray<FVector2D> Body={Origin+FVector2D(0,8),Origin+FVector2D(14,8),Origin+FVector2D(14,18),Origin+FVector2D(0,18),Origin+FVector2D(0,8)};
        FSlateDrawElement::MakeLines(Out,Painted+1,Geometry.ToPaintGeometry(),Body,ESlateDrawEffect::None,Ink,true,1.5f);
        TArray<FVector2D> Shackle;
        for(int32 N=0;N<=8;N++){const float A=PI*N/8.f;Shackle.Add(Origin+FVector2D(7.f-4.5f*FMath::Cos(A),8.f-6.f*FMath::Sin(A)));}
        FSlateDrawElement::MakeLines(Out,Painted+1,Geometry.ToPaintGeometry(),Shackle,ESlateDrawEffect::None,Ink,true,1.5f);
    }
    if(!IsBlockedDestination())return Painted+1;
    const float Inset=5.f;
    TArray<FVector2D> Points;Points.Add(FVector2D(Inset,Inset));Points.Add(Size-FVector2D(Inset,Inset));
    const FLinearColor Cross(.55f,.55f,.55f,.9f);
    FSlateDrawElement::MakeLines(Out,Painted+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Cross,true,2.f);
    Points[0]=FVector2D(Inset,Size.Y-Inset);Points[1]=FVector2D(Size.X-Inset,Inset);
    FSlateDrawElement::MakeLines(Out,Painted+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Cross,true,2.f);
    return Painted+1;
}
