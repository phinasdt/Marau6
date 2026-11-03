#include "Marau6InventoryWidget.h"
#include "Marau6PlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Marau6InventorySlot.h"
#include "Marau6InventoryUIModel.h"
#include "Marau6UIStyle.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Rendering/DrawElements.h"

namespace
{
/** Brass tick marks along the inspection panel's edge. */
class SMarau6Ruler : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMarau6Ruler){} SLATE_END_ARGS()
    void Construct(const FArguments&){SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(7,7);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool) const override
    {
        for(float Y=10.f;Y<G.GetLocalSize().Y-10.f;Y+=36.f)
        {TArray<FVector2D> Tick;Tick.Add(FVector2D(0,Y));Tick.Add(FVector2D(7,Y));FSlateDrawElement::MakeLines(Out,L+1,G.ToPaintGeometry(),Tick,ESlateDrawEffect::None,Marau6UI::Rule(),true,1.5f);}
        return L+1;
    }
};
}

UMarau6InventoryWidget::UMarau6InventoryWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> Corridor(TEXT("/Game/Marau6/UI/T_Marau6_BaseCorridor.T_Marau6_BaseCorridor"));
    Background=Corridor.Object;
    static ConstructorHelpers::FObjectFinder<UTexture2D> Rifle(TEXT("/Game/Marau6/UI/Icons/T_UI_Icon_Rifle.T_UI_Icon_Rifle"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> Medkit(TEXT("/Game/Marau6/UI/Icons/T_UI_Icon_Medkit.T_UI_Icon_Medkit"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> Scrap(TEXT("/Game/Marau6/UI/Icons/T_UI_Icon_Scrap.T_UI_Icon_Scrap"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> Ammo(TEXT("/Game/Marau6/UI/Icons/T_UI_Icon_Ammo.T_UI_Icon_Ammo"));
    RifleIcon=Rifle.Object;
    MedkitIcon=Medkit.Object;
    ScrapIcon=Scrap.Object;
    AmmoIcon=Ammo.Object;
}
float UMarau6InventoryWidget::ViewWidth() const
{
    const auto* PC=GetOwningPlayer<AMarau6PlayerController>();
    float W=GetCachedGeometry().GetLocalSize().X*UWidgetLayoutLibrary::GetViewportScale(this);if(W<100){int32 X=1024,Y=720;if(PC)PC->GetViewportSize(X,Y);W=FMath::Max(640,X);}
    return W;
}
float UMarau6InventoryWidget::GroupWidth(int32 Group) const
{
    const auto* PC=GetOwningPlayer<AMarau6PlayerController>();
    const float W=FMath::Max(440.f,ViewWidth()-76.f);
    if(PC && !PC->IsInventoryAtBase())return W*(Group==0?.50f:.46f);
    if(W<1180)return Group==2?W*.98f:W*.485f;
    return W*(Group==0?.245f:Group==1?.42f:.30f);
}
FString UMarau6InventoryWidget::Occupancy(int32 Start,int32 Count) const
{
    int32 Used=0;
    if(const auto* PC=GetOwningPlayer<AMarau6PlayerController>())for(const auto& I:PC->GetInventoryUIItems())if(I.Slot>=Start && I.Slot<Start+Count)++Used;
    return FString::Printf(TEXT("%02d / %02d"),Used,Count);
}
TSharedRef<SWidget> UMarau6InventoryWidget::RebuildWidget()
{
    SetIsFocusable(true);TWeakObjectPtr<AMarau6PlayerController> PC=GetOwningPlayer<AMarau6PlayerController>();
    if(Background){BackgroundBrush.SetResourceObject(Background);BackgroundBrush.ImageSize=FVector2D(Background->GetSizeX(),Background->GetSizeY());}
    auto AtBase=[PC]{return PC.IsValid() && PC->IsInventoryAtBase();};
    auto BaseOnly=[AtBase]{return AtBase()?EVisibility::Visible:EVisibility::Collapsed;};
    auto RaidOnly=[AtBase]{return AtBase()?EVisibility::Collapsed:EVisibility::Visible;};
    auto Label=[](const FString& Text,const FSlateFontInfo& Font,const FLinearColor& Color){return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(Color);};
    auto Live=[](TFunction<FString()> Get,const FSlateFontInfo& Font,const FLinearColor& Color){return SNew(STextBlock).Text_Lambda([Get]{return FText::FromString(Get());}).Font(Font).ColorAndOpacity(Color).AutoWrapText(true);};
    auto Rule=[](const FLinearColor& Color){return SNew(SBox).HeightOverride(1)[SNew(SImage).Image(Marau6UI::White()).ColorAndOpacity(Color)];};
    auto Hint=[Label](const FString& Key,const FString& Action)
    {return SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[Label(Key,Marau6UI::Mono(7.5f),Marau6UI::Text())]+SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)[Label(Action,Marau6UI::Mono(7.5f),Marau6UI::Muted())];};
    auto Heading=[Rule](const FString& Title,TFunction<FString()> Count)
    {
        return SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Bottom)[SNew(STextBlock).Text(FText::FromString(Title)).Font(Marau6UI::Font(TEXT("Medium"),15,60)).ColorAndOpacity(Marau6UI::Text())]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(12,0,0,2)[SNew(STextBlock).Text_Lambda([Count]{return FText::FromString(Count());}).Font(Marau6UI::Mono(7.5f)).ColorAndOpacity(Marau6UI::Muted())]]
            +SVerticalBox::Slot().AutoHeight()[Rule(Marau6UI::Rule())];
    };
    auto Grid=[PC,this](int32 Start,int32 Count,int32 Columns,float Height)
    {
        auto G=SNew(SUniformGridPanel).SlotPadding(FMargin(3));
        for(int32 N=0;N<Count;++N)G->AddSlot(N%Columns,N/Columns)
            [SNew(SMarau6InventorySlot).Controller(PC).Slot(Start+N).Height(Height)
                .RifleIcon(RifleIcon).MedkitIcon(MedkitIcon).ScrapIcon(ScrapIcon).AmmoIcon(AmmoIcon)];
        return G;
    };
    auto Panel=[](TSharedRef<SWidget> Content,FMargin Inset){return SNew(SBorder).BorderImage(Marau6UI::Panel()).Padding(Inset)[Content];};
    auto KeyButton=[PC,Label](const TCHAR* Cap,const TCHAR* Text,FKey Key)
    {
        return SNew(SButton).IsFocusable(false).ButtonStyle(&Marau6UI::FlatButton()).ContentPadding(FMargin(0,0,10,0))
            .OnClicked_Lambda([PC,Key]{if(auto* P=PC.Get())P->HandleInventoryKey(Key);return FReply::Handled();})
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(30).HeightOverride(28)[SNew(SBorder).BorderImage(Marau6UI::KeyCap()).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0)[Label(Cap,Marau6UI::Font(TEXT("Medium"),10),Marau6UI::Text())]]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0,0,0)[Label(Text,Marau6UI::Font(TEXT("Regular"),10.5f,40),Marau6UI::Text())]];
    };
    auto Tab=[Label](const FString& Name,bool bActive)
    {
        return SNew(SVerticalBox)
            +SVerticalBox::Slot().FillHeight(1).Padding(0,18,bActive?60:0,0)[SNew(SOverlay)
                +SOverlay::Slot().VAlign(VAlign_Center)[Label(Name,Marau6UI::Font(TEXT("Regular"),11.5f,80),bActive?Marau6UI::Text():Marau6UI::Muted())]
                // Sections other than EQUIPAMENTO are not built yet.
                +SOverlay::Slot().VAlign(VAlign_Bottom).Padding(0,0,0,3)[SNew(SBox).Visibility(bActive?EVisibility::Collapsed:EVisibility::HitTestInvisible)[Label(TEXT("EM BREVE"),Marau6UI::Mono(6),Marau6UI::BrassText())]]]
            +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(2)[SNew(SImage).Image(Marau6UI::White()).ColorAndOpacity(bActive?Marau6UI::Brass():FLinearColor::Transparent)]];
    };

    // Header: logo, section tabs (only EQUIPAMENTO exists in this prototype) and the operator profile.
    auto Tabs=SNew(SHorizontalBox).Visibility_Lambda([this,AtBase]{return AtBase() && ViewWidth()>=1100?EVisibility::HitTestInvisible:EVisibility::Collapsed;});
    const TCHAR* TabNames[]={TEXT("OPERA\u00C7\u00C3O"),TEXT("EQUIPAMENTO"),TEXT("OFICINA"),TEXT("CONTRATOS"),TEXT("HANGAR")};
    for(int32 N=0;N<5;N++)Tabs->AddSlot().AutoWidth().Padding(0,0,44,0)[Tab(TabNames[N],N==1)];
    auto Header=SNew(SBox).HeightOverride(80)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Label(TEXT("MARAU6"),Marau6UI::Font(TEXT("BoldCondensed"),24,20),Marau6UI::Text())]
            +SVerticalBox::Slot().AutoHeight().Padding(1,-2,0,0)[SNew(STextBlock).Text_Lambda([AtBase]{return FText::FromString(AtBase()?TEXT("BASE / INTEND\u00CANCIA"):TEXT("INCURS\u00C3O / INVENT\u00C1RIO"));}).Font(Marau6UI::Font(TEXT("Regular"),7.5f,80)).ColorAndOpacity(Marau6UI::Brass())]]
        +SHorizontalBox::Slot().AutoWidth().Padding(200,0,0,14)[Tabs]
        +SHorizontalBox::Slot().FillWidth(1)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[Label(TEXT("OPERADOR"),Marau6UI::Mono(7.5f),Marau6UI::Muted())]
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,5,0,0)[SNew(STextBlock).Text_Lambda([PC]{const int32 P=PC.IsValid()?PC->GetBaseProfile():0;return FText::FromString(P>0?FString::Printf(TEXT("PERFIL %02d"),P):TEXT("PERFIL --"));}).Font(Marau6UI::Font(TEXT("Medium"),14,60)).ColorAndOpacity(Marau6UI::Text())]]];

    // Command row: clickable key caps at the base, close/cancel and read-only state on the right.
    auto Keys=SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,28,0)[SNew(STextBlock).Text(FText::FromString(TEXT("SOMENTE LEITURA"))).Font(Marau6UI::Mono(7.5f)).ColorAndOpacity(Marau6UI::Brass())
            .Visibility_Lambda([PC]{return PC.IsValid() && PC->IsInventoryReadOnly()?EVisibility::Visible:EVisibility::Collapsed;})]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).Visibility_Lambda(BaseOnly)[Hint(TEXT("ESC"),TEXT("CANCELAR"))]]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).Visibility_Lambda(RaidOnly)[Hint(TEXT("TAB"),TEXT("FECHAR"))]];

    // Kit for the next raid, above the character's gear. BASIC enters with a fresh rifle
    // and leaves everything below at the base; CUSTOM enters with what is equipped here.
    auto KitTab=[PC,Label](const TCHAR* Name,const TCHAR* Detail,bool bBasic)
    {
        auto Active=[PC,bBasic]{return PC.IsValid() && PC->IsBasicKit()==bBasic;};
        return SNew(SButton).IsFocusable(false).ButtonStyle(&Marau6UI::FlatButton()).ContentPadding(FMargin(0,0,48,0))
            .OnClicked_Lambda([PC,bBasic]{if(auto* P=PC.Get())P->SelectKit(bBasic);return FReply::Handled();})
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Name)).Font(Marau6UI::Font(TEXT("Medium"),14,80))
                    .ColorAndOpacity_Lambda([Active]{return FSlateColor(Active()?Marau6UI::Text():Marau6UI::Muted());})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,8)[Label(Detail,Marau6UI::Mono(7),Marau6UI::Muted())]
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(2)[SNew(SImage).Image(Marau6UI::White())
                    .ColorAndOpacity_Lambda([Active]{return FSlateColor(Active()?Marau6UI::Brass():FLinearColor::Transparent);})]]];
    };
    auto Kits=SNew(SHorizontalBox).Visibility_Lambda(BaseOnly)
        +SHorizontalBox::Slot().AutoWidth()[KitTab(TEXT("KIT BÁSICO"),TEXT("RIFLE 20/40 · 2 MUNIÇÕES · 1 KIT MÉDICO"),true)]
        +SHorizontalBox::Slot().AutoWidth()[KitTab(TEXT("KIT PERSONALIZADO"),TEXT("ENTRA COM O EQUIPAMENTO ABAIXO"),false)]
        +SHorizontalBox::Slot().FillWidth(1)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[KeyButton(TEXT("K"),TEXT("TROCAR KIT"),EKeys::K)];

    auto Groups=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(20,20));
    Groups->AddSlot().VAlign(VAlign_Top)[SNew(SBox).WidthOverride_Lambda([this]{return GroupWidth(0);})[Panel(SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("ARMA EQUIPADA"),[this]{return Occupancy(Marau6Slots::Weapon,1);})]
        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[Grid(Marau6Slots::Weapon,1,1,134)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,30,0,0)[Heading(TEXT("MOCHILA"),[this]{return Occupancy(0,Marau6Slots::BackpackCount);})]
        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[Grid(0,Marau6Slots::BackpackCount,4,72)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,30,0,0)[Heading(TEXT("SAFE POCKET"),[this]{return Occupancy(Marau6Slots::SafeStart,Marau6Slots::SafeCount);})]
        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[Grid(Marau6Slots::SafeStart,Marau6Slots::SafeCount,2,66)]
        +SVerticalBox::Slot().AutoHeight().Padding(2,22,0,0)[Label(TEXT("Protegidos na morte \u00B7 Rifles n\u00E3o cabem."),Marau6UI::Font(TEXT("Regular"),9),Marau6UI::Muted())],
        FMargin(16,14,16,22))]];
    Groups->AddSlot().VAlign(VAlign_Top)[SNew(SBox).WidthOverride_Lambda([this]{return GroupWidth(1);}).Visibility_Lambda(BaseOnly)[Panel(SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("STASH"),[this]{return Occupancy(100,32)+TEXT(" OCUPADOS");})]
        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[Grid(100,32,4,80)],
        FMargin(18,14,18,18))]];

    // Inspection: structured copy of the selected slot.
    auto Inspect=[PC]{const auto* P=PC.Get();if(!P)return FMarau6ItemInspection();const int32 S=P->GetInventoryUISelection();return FMarau6InventoryUIModel::Inspect(P->InventoryUIItemAt(S),S);};
    auto Hints=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Hint(TEXT("ARRASTAR"),TEXT("MOVER / TROCAR"))]
        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[Hint(TEXT("SETAS + ENTER"),TEXT("MOVER"))];
    const TCHAR* RaidHints[][2]={{TEXT("F"),TEXT("EQUIPAR")},{TEXT("G"),TEXT("DESCARTAR")},{TEXT("P"),TEXT("SAFE / MOCHILA")},{TEXT("H"),TEXT("USAR SUPRIMENTO")}};
    for(const auto& H:RaidHints)Hints->AddSlot().AutoHeight().Padding(0,12,0,0)[SNew(SBox).Visibility_Lambda(RaidOnly)[Hint(H[0],H[1])]];
    Groups->AddSlot().VAlign(VAlign_Top)[SNew(SBox).WidthOverride_Lambda([this]{return GroupWidth(2);})[Panel(SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[SNew(SMarau6Ruler)]
        +SHorizontalBox::Slot().FillWidth(1).Padding(16,0,0,0)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("INSPE\u00C7\u00C3O"),[PC]{return PC.IsValid()?FMarau6InventoryUIModel::SlotName(PC->GetInventoryUISelection()):FString();})]
            +SVerticalBox::Slot().AutoHeight().Padding(0,34,0,0)[Live([Inspect]{return Inspect().Category;},Marau6UI::Mono(7.5f),Marau6UI::BrassText())]
            +SVerticalBox::Slot().AutoHeight().Padding(0,26,0,0)[Live([Inspect]{return Inspect().Title;},Marau6UI::Font(TEXT("BoldCondensed"),28,10),Marau6UI::Text())]
            +SVerticalBox::Slot().AutoHeight()[Live([Inspect]{return Inspect().Subtitle;},Marau6UI::Mono(7),Marau6UI::Muted())]
            +SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)[Rule(Marau6UI::Rule())]
            +SVerticalBox::Slot().AutoHeight().Padding(0,24,0,0)[Live([Inspect]{return Inspect().Lead;},Marau6UI::Font(TEXT("Regular"),12),Marau6UI::Text())]
            +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Live([Inspect]{return Inspect().Body;},Marau6UI::Font(TEXT("Regular"),10.5f),Marau6UI::Muted())]
            +SVerticalBox::Slot().AutoHeight().Padding(0,24,0,0)[Rule(Marau6UI::Rule())]
            +SVerticalBox::Slot().AutoHeight().Padding(0,22,0,0)[Hints]],
        FMargin(6,14,18,26))]];

    // Footer: current notice with a status lamp, plus the mouse/keyboard legend.
    auto Lamp=[PC,AtBase]
    {
        if(!PC.IsValid())return Marau6UI::Dim();
        if(PC->InventoryRequestPending())return Marau6UI::Waiting();
        if(!AtBase())return PC->IsInventoryReadOnly()?Marau6UI::Dim():Marau6UI::Ready();
        const FString& Status=PC->GetBaseBackendStatus();
        return Status==TEXT("Backend conectado")?Marau6UI::Ready():Status.StartsWith(TEXT("Preparando")) || Status.StartsWith(TEXT("Recuperando"))?Marau6UI::Waiting():Marau6UI::Fault();
    };
    // Base actions sit in the bottom bar; ENTRAR is the primary action, rightmost.
    auto HintButton=[PC,Hint](const TCHAR* Key,const TCHAR* Action,FKey Bound)
    {
        return SNew(SButton).IsFocusable(false).ButtonStyle(&Marau6UI::FlatButton()).ContentPadding(FMargin(0))
            .OnClicked_Lambda([PC,Bound]{if(auto* P=PC.Get())P->HandleInventoryKey(Bound);return FReply::Handled();})[Hint(Key,Action)];
    };
    static const FSlateRoundedBoxBrush PrimaryFill(Marau6UI::Brass(),2.f);
    auto Enter=SNew(SButton).IsFocusable(false).ButtonStyle(&Marau6UI::FlatButton()).ContentPadding(FMargin(0))
        .OnClicked_Lambda([PC]{if(auto* P=PC.Get())P->HandleInventoryKey(EKeys::B);return FReply::Handled();})
        [SNew(SBorder).BorderImage(&PrimaryFill).Padding(FMargin(64,18))[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Label(TEXT("B"),Marau6UI::Mono(11),Marau6UI::SRGB(40,32,18))]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20,0,0,0)[Label(TEXT("ENTRAR"),Marau6UI::Font(TEXT("BoldCondensed"),30,120),Marau6UI::SRGB(18,15,10))]]];
    auto BaseLegend=SNew(SHorizontalBox).Visibility_Lambda(BaseOnly)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,36,0)[HintButton(TEXT("1"),TEXT("PERFIL 1"),EKeys::One)]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,36,0)[HintButton(TEXT("2"),TEXT("PERFIL 2"),EKeys::Two)]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,28,0)[HintButton(TEXT("N"),TEXT("NOVA RODADA"),EKeys::N)]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Enter];
    auto RaidLegend=SNew(SHorizontalBox).Visibility_Lambda(RaidOnly)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,30,0)[Hint(TEXT("TAB"),TEXT("FECHAR"))]
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,30,0)[Hint(TEXT("ARRASTAR"),TEXT("MOVER / TROCAR"))]
        +SHorizontalBox::Slot().AutoWidth()[Hint(TEXT("SETAS + ENTER"),TEXT("MOVER"))];
    auto Footer=SNew(SBox).MinDesiredHeight(84)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,14,2)[SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SImage).Image(Marau6UI::Dot()).ColorAndOpacity_Lambda([Lamp]{return FSlateColor(Lamp());})]]
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,10,28,10)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Live([PC]{return PC.IsValid()?PC->GetInventoryNotice().ToUpper():FString();},Marau6UI::Font(TEXT("Medium"),12,40),Marau6UI::Text())]
            +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[Live([PC,AtBase]{return AtBase()?PC->GetBaseBackendStatus().ToUpper():FString(TEXT("O JOGO CONTINUA: DANO, BOTS E EXTRA\u00C7\u00C3O PERMANECEM ATIVOS"));},Marau6UI::Mono(7),Marau6UI::Muted())]]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[BaseLegend]+SVerticalBox::Slot().AutoHeight()[RaidLegend]]];

    auto Content=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(Marau6UI::Bar()).Padding(0)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(20,0)[Header]
            +SVerticalBox::Slot().AutoHeight()[Rule(Marau6UI::Rule())]
            +SVerticalBox::Slot().AutoHeight().Padding(20,14)[Keys]]]
        +SVerticalBox::Slot().AutoHeight().Padding(20,16,20,0)[Kits]
        +SVerticalBox::Slot().FillHeight(1).Padding(20,14,8,10)[SNew(SScrollBox)+SScrollBox::Slot().Padding(0,0,12,0)[Groups]]
        +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(Marau6UI::Bar()).Padding(0)[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Rule(Marau6UI::Rule())]
            +SVerticalBox::Slot().AutoHeight().Padding(20,0)[Footer]]];
    // The corridor fills the screen at the base; the raid keeps a dark, opaque backdrop over the live world.
    auto Screen=SNew(SOverlay)
        +SOverlay::Slot()[SNew(SImage).Image(Marau6UI::White()).ColorAndOpacity(Marau6UI::SRGB(10,10,9))]
        +SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFill).Visibility_Lambda([this,AtBase]{return AtBase() && Background?EVisibility::HitTestInvisible:EVisibility::Collapsed;})[SNew(SImage).Image(&BackgroundBrush)]]
        +SOverlay::Slot()[SNew(SImage).Image(Marau6UI::White()).ColorAndOpacity(Marau6UI::SRGB(0,0,0,.25f)).Visibility(EVisibility::HitTestInvisible)]
        +SOverlay::Slot()[Content];
    // Preserve readable physical text sizes; reflow and scroll instead of shrinking
    // every slot when the project's viewport DPI curve scales down a small window.
    return SNew(SDPIScaler).DPIScale_Lambda([this]{return 1.f/FMath::Max(.25f,UWidgetLayoutLibrary::GetViewportScale(this));})[Screen];
}
FReply UMarau6InventoryWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{if(E.IsRepeat() && E.GetKey()!=EKeys::Up && E.GetKey()!=EKeys::Down && E.GetKey()!=EKeys::Left && E.GetKey()!=EKeys::Right)return FReply::Handled();
if(auto* PC=GetOwningPlayer<AMarau6PlayerController>();PC && PC->HandleInventoryKey(E.GetKey()))return FReply::Handled();return Super::NativeOnPreviewKeyDown(G,E);}
