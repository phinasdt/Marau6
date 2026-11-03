#include "Marau6HUDWidget.h"
#include "Marau6PlayerController.h"
#include "Marau6Character.h"
#include "Marau6HealingComponent.h"
#include "Marau6InventoryComponent.h"
#include "Marau6LootActor.h"
#include "Marau6UIStyle.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture2D.h"

namespace
{
/** Vector ammo icon: three brass rounds, each a casing under a rounded tip. */
class SMarau6RoundsIcon : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMarau6RoundsIcon){}
        SLATE_ATTRIBUTE(FLinearColor,Color)
    SLATE_END_ARGS()
    void Construct(const FArguments& A){Color=A._Color;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(20,18);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool) const override
    {
        static const FSlateRoundedBoxBrush Tip(FLinearColor::White,FVector4(2.5f,2.5f,0.f,0.f));
        const FVector2D Size=G.GetLocalSize();const float W=4.5f,Gap=2.5f,TipH=Size.Y*.34f,Rim=1.5f;
        const FLinearColor Ink=Color.Get(Marau6UI::Brass());
        for(int32 N=0;N<3;N++)
        {
            const float X=N*(W+Gap);
            FSlateDrawElement::MakeBox(Out,L+1,G.ToPaintGeometry(FVector2D(W,TipH),FSlateLayoutTransform(FVector2D(X,0))),&Tip,ESlateDrawEffect::None,Ink*FLinearColor(.85f,.78f,.7f));
            FSlateDrawElement::MakeBox(Out,L+1,G.ToPaintGeometry(FVector2D(W,Size.Y-TipH-Rim-.5f),FSlateLayoutTransform(FVector2D(X,TipH+.5f))),Marau6UI::White(),ESlateDrawEffect::None,Ink);
            FSlateDrawElement::MakeBox(Out,L+1,G.ToPaintGeometry(FVector2D(W+1.f,Rim),FSlateLayoutTransform(FVector2D(X-.5f,Size.Y-Rim))),Marau6UI::White(),ESlateDrawEffect::None,Ink*.8f);
        }
        return L+1;
    }
private:
    TAttribute<FLinearColor> Color;
};

/** Segmented gauge: dim empty segments, the filled share in the given color. */
class SMarau6Meter : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMarau6Meter){}
        SLATE_ATTRIBUTE(float,Fill)
        SLATE_ATTRIBUTE(FLinearColor,Color)
        SLATE_ARGUMENT_DEFAULT(int32,Segments){20};
    SLATE_END_ARGS()
    void Construct(const FArguments& A){Fill=A._Fill;Color=A._Color;Segments=FMath::Max(1,A._Segments);SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(260,8);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool) const override
    {
        const FVector2D Size=G.GetLocalSize();const float Gap=3.f,Seg=(Size.X-Gap*(Segments-1))/Segments;
        const float Filled=FMath::Clamp(Fill.Get(0.f),0.f,1.f)*Segments;const FLinearColor Ink=Color.Get(Marau6UI::Brass());
        for(int32 N=0;N<Segments;N++)
        {
            const FVector2D At(N*(Seg+Gap),0);
            FSlateDrawElement::MakeBox(Out,L+1,G.ToPaintGeometry(FVector2D(Seg,Size.Y),FSlateLayoutTransform(At)),Marau6UI::White(),ESlateDrawEffect::None,Marau6UI::SRGB(70,62,48,.55f));
            const float Part=FMath::Clamp(Filled-N,0.f,1.f);
            if(Part>0)FSlateDrawElement::MakeBox(Out,L+2,G.ToPaintGeometry(FVector2D(Seg*Part,Size.Y),FSlateLayoutTransform(At)),Marau6UI::White(),ESlateDrawEffect::None,Ink);
        }
        return L+2;
    }
private:
    TAttribute<float> Fill;
    TAttribute<FLinearColor> Color;
    int32 Segments=20;
};
}

AMarau6Character* UMarau6HUDWidget::Pawn() const
{const auto* PC=GetOwningPlayer();return PC?Cast<AMarau6Character>(PC->GetPawn()):nullptr;}
TSharedRef<SWidget> UMarau6HUDWidget::RebuildWidget()
{
    TWeakObjectPtr<UMarau6HUDWidget> Self(this);
    auto Char=[Self]{return Self.IsValid()?Self->Pawn():nullptr;};
    auto Label=[](const FString& Text,const FSlateFontInfo& Font,const FLinearColor& Color){return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(Color);};
    auto Live=[](TFunction<FString()> Get,const FSlateFontInfo& Font,TFunction<FLinearColor()> Color)
    {return SNew(STextBlock).Text_Lambda([Get]{return FText::FromString(Get());}).Font(Font).ColorAndOpacity_Lambda([Color]{return FSlateColor(Color());});};
    auto Fixed=[](FLinearColor C){return [C]{return C;};};
    auto Shown=[](TFunction<bool()> If){return [If]{return If()?EVisibility::HitTestInvisible:EVisibility::Collapsed;};};
    auto Cap=[Label](const TCHAR* Key,float Width)
    {return SNew(SBox).WidthOverride(Width).HeightOverride(30)[SNew(SBorder).BorderImage(Marau6UI::KeyCap()).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0)[Label(Key,Marau6UI::Font(TEXT("Medium"),10),Marau6UI::Text())]];};

    // Bottom right: magazine / capacity big, everything carried (reserve plus unopened boxes) small, then health.
    auto Alive=[Char]{const auto* C=Char();return C && C->IsAlive();};
    auto Armed=[Char]{const auto* C=Char();return C && C->IsAlive() && C->HasEquippedRifle();};
    auto Carried=[Char]
    {
        const auto* C=Char();if(!C)return 0;int32 Boxes=0;
        for(int32 Index=0;Index<Marau6Slots::BackpackCount;++Index)if(const auto* I=C->GetInventory()->ItemAt(Index))Boxes+=I->IsAmmo()?1:0;
        return C->GetReserveAmmo()+Boxes*Marau6Ammo::BoxRounds;
    };
    auto Reloading=[Char,Armed]{return Armed() && Char()->IsReloading();};
    auto Healing=[Char]{const auto* C=Char();return C && C->IsAlive() && C->GetHealing()->IsHealing();};
    auto Health=[Char]{const auto* C=Char();return C?FMath::Max(0.f,C->GetHealth()):0.f;};
    // Ammo row, as wide as the health bar under it: icon left, "20 / 20 (60)" right,
    // where the bracket is everything carried (reserve plus boxes in the backpack).
    auto Ammo=SNew(SVerticalBox).Visibility_Lambda(Shown(Armed))
        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SBox).Visibility_Lambda(Shown(Reloading))[Label(TEXT("RECARREGANDO"),Marau6UI::Mono(7),Marau6UI::Waiting())]]
        +SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(220)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(20).HeightOverride(18)[SNew(SMarau6RoundsIcon).Color(Marau6UI::Brass())]]
            +SHorizontalBox::Slot().FillWidth(1)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[Live([Char,Armed]{return Armed()?FString::Printf(TEXT("%d"),Char()->GetMagazineAmmo()):FString();},Marau6UI::Font(TEXT("BoldCondensed"),20,20),
                [Char,Armed]{return Armed() && Char()->GetMagazineAmmo()==0?Marau6UI::Fault():Marau6UI::Text();})]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(5,0,0,2)[Live([Char,Armed]{return Armed()?FString::Printf(TEXT("/ %d"),Char()->GetMagazineCapacity()):FString();},Marau6UI::Font(TEXT("BoldCondensed"),13,20),Fixed(Marau6UI::Muted()))]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(7,0,0,2)[Live([Carried]{return FString::Printf(TEXT("(%d)"),Carried());},Marau6UI::Font(TEXT("BoldCondensed"),12,20),[Carried]{return Carried()==0?Marau6UI::Fault():Marau6UI::Muted();})]]];
    auto HealthBar=SNew(SBox).WidthOverride(220).HeightOverride(16)[SNew(SOverlay)
        +SOverlay::Slot()[SNew(SMarau6Meter).Segments(1).Fill_Lambda([Health]{return Health()/100.f;}).Color(Marau6UI::SRGB(178,40,32,.92f))]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Live([Health]{return FString::Printf(TEXT("%.0f / 100"),FMath::CeilToFloat(Health()));},Marau6UI::Font(TEXT("BoldCondensed"),10,20),Fixed(Marau6UI::Text()))]];
    auto HealLine=SNew(SBox).Visibility_Lambda(Shown(Healing))[Live([Char,Healing]{return Healing()?FString::Printf(TEXT("CURANDO %.1fs"),Char()->GetHealing()->Remaining()):FString();},Marau6UI::Mono(7),Fixed(Marau6UI::Ready()))];
    auto Vitals=SNew(SBox).Visibility_Lambda(Shown(Alive))[SNew(SBorder).BorderImage(Marau6UI::Panel()).Padding(FMargin(10,6,10,8))[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Ammo]
        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,6,0,0)[HealLine]
        +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[HealthBar]]];

    // Extraction countdown, only while inside the zone.
    auto Extracting=[Char]{const auto* C=Char();return C && C->GetRaidState()==EMarau6RaidState::Extracting;};
    auto Extraction=SNew(SBox).Visibility_Lambda(Shown(Extracting))[SNew(SBorder).BorderImage(Marau6UI::Panel()).Padding(FMargin(14,7))
        [Live([Char,Extracting]{return Extracting()?FString::Printf(TEXT("EXTRAINDO %.1fs"),Char()->GetExtractionSecondsRemaining()):FString();},Marau6UI::Font(TEXT("BoldCondensed"),15,40),Fixed(Marau6UI::Ready()))]];

    // Loot under the crosshair.
    auto LootPrompt=[Char]()->FString
    {
        const auto* C=Char();if(!C || !C->CanParticipate())return FString();
        const auto* Loot=C->GetInventory()->LookedAtLoot();if(!Loot)return FString();
        bool bSpace=Loot->GetItem().IsRifle() && !C->HasEquippedRifle();
        for(int32 Index=0;Index<Marau6Slots::BackpackCount;++Index)if(!C->GetInventory()->ItemAt(Index))bSpace=true;
        return bSpace?Loot->GetItem().Name():FString(TEXT("MOCHILA CHEIA"));
    };
    auto Prompt=SNew(SBox).Visibility_Lambda([LootPrompt]{return LootPrompt().IsEmpty()?EVisibility::Collapsed:EVisibility::HitTestInvisible;})
        [SNew(SBorder).BorderImage(Marau6UI::Panel()).Padding(FMargin(8,6,12,6))[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cap(TEXT("E"),30)]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0,0,0)[Live(LootPrompt,Marau6UI::Font(TEXT("Regular"),10.5f,40),Fixed(Marau6UI::Text()))]]];

    // Result once the player can no longer act in this raid.
    auto Over=[Char]{const auto* C=Char();return C && !C->CanParticipate();};
    auto Extracted=[Char]{const auto* C=Char();return C && C->GetRaidState()==EMarau6RaidState::Extracted;};
    auto Outcome=[Self,Char]
    {
        const auto* C=Char();if(!C)return FString();const auto& Snapshot=C->GetInventory()->GetSnapshot();
        if(Snapshot.bFinalized)return FString::Printf(TEXT("MANTIDOS %d · PERDIDOS %d"),Snapshot.Items.Num(),Snapshot.LostCount);
        const auto* PC=Self.IsValid()?Cast<AMarau6PlayerController>(Self->GetOwningPlayer()):nullptr;
        return FString(PC && PC->IsUsingBase()?TEXT("SALVANDO"):TEXT("REINICIE O PLAY"));
    };
    auto Result=SNew(SBox).Visibility_Lambda(Shown(Over))
        [SNew(SBorder).BorderImage(Marau6UI::Panel()).Padding(FMargin(28,18)).HAlign(HAlign_Center)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Live([Extracted]{return Extracted()?FString(TEXT("EXTRAÍDO")):FString(TEXT("ELIMINADO"));},Marau6UI::Font(TEXT("BoldCondensed"),30,40),
                [Extracted]{return Extracted()?Marau6UI::Ready():Marau6UI::Fault();})]
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,6,0,0)[Live(Outcome,Marau6UI::Mono(7.5f),Fixed(Marau6UI::Muted()))]]];

    auto Screen=SNew(SOverlay)
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0,28,0,0)[Extraction]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0,180,0,0)[Prompt]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0,0,0,220)[Result]
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(28,0,0,28)[SNew(SBox).Visibility_Lambda(Shown(Alive))[Cap(TEXT("TAB"),48)]]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0,0,28,28)[Vitals];
    // Laid out at 1920x1080 and scaled to the viewport.
    return SNew(SDPIScaler).Visibility(EVisibility::HitTestInvisible).DPIScale_Lambda([Self]
    {
        if(!Self.IsValid())return 1.f;int32 X=1920,Y=1080;if(const auto* PC=Self->GetOwningPlayer())PC->GetViewportSize(X,Y);
        const float Fit=FMath::Max(.4f,FMath::Min(X/1920.f,Y/1080.f));
        return Fit/FMath::Max(.25f,UWidgetLayoutLibrary::GetViewportScale(Self.Get()));
    })[Screen];
}
