#pragma once
#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

/** Base/intendance look: brass labels on blackened steel over the corridor backdrop. Colors are sRGB. */
namespace Marau6UI
{
    inline FLinearColor SRGB(uint8 R,uint8 G,uint8 B,float A=1.f){FLinearColor C(FColor(R,G,B));C.A=A;return C;}
    inline FLinearColor Text(){return SRGB(232,226,214);}
    inline FLinearColor Muted(){return SRGB(164,156,142);}
    inline FLinearColor Dim(){return SRGB(112,104,92);}
    inline FLinearColor Brass(){return SRGB(201,168,106);}
    inline FLinearColor BrassText(){return SRGB(160,138,92);}
    inline FLinearColor Selected(){return SRGB(226,200,142);}
    inline FLinearColor Rule(){return SRGB(120,104,74,.55f);}
    inline FLinearColor Ready(){return SRGB(111,191,115);}
    inline FLinearColor Waiting(){return SRGB(214,170,74);}
    inline FLinearColor Fault(){return SRGB(204,92,72);}

    inline FSlateFontInfo Font(const TCHAR* Face,float Size,int32 Spacing=0)
    {FSlateFontInfo F=FCoreStyle::GetDefaultFontStyle(Face,Size);F.LetterSpacing=Spacing;return F;}
    inline FSlateFontInfo Mono(float Size){return Font(TEXT("Mono"),Size,80);}

    inline const FSlateBrush* White(){return FCoreStyle::Get().GetBrush("WhiteBrush");}
    inline const FSlateBrush* Panel(){static const FSlateRoundedBoxBrush B(SRGB(13,12,10,.80f),1.f,SRGB(70,62,48,.45f),1.f);return &B;}
    inline const FSlateBrush* Bar(){static const FSlateRoundedBoxBrush B(SRGB(9,8,7,.74f),0.f);return &B;}
    inline const FSlateBrush* SlotBox(){static const FSlateRoundedBoxBrush B(SRGB(18,16,13,.88f),1.f,SRGB(118,106,82,.80f),1.f);return &B;}
    inline const FSlateBrush* SlotBoxSelected(){static const FSlateRoundedBoxBrush B(SRGB(92,76,44,.92f),1.f,Brass(),1.5f);return &B;}
    inline const FSlateBrush* SlotBoxSource(){static const FSlateRoundedBoxBrush B(SRGB(42,42,42,.92f),1.f,SRGB(86,86,86),1.f);return &B;}
    inline const FSlateBrush* KeyCap(){static const FSlateRoundedBoxBrush B(SRGB(0,0,0,0),1.f,SRGB(150,145,135,.85f),1.f);return &B;}
    inline const FSlateBrush* Dot(){static const FSlateRoundedBoxBrush B(FLinearColor::White,5.f);return &B;}
    inline const FButtonStyle& FlatButton()
    {
        static const FSlateRoundedBoxBrush Hover(SRGB(201,168,106,.12f),1.f),Press(SRGB(201,168,106,.24f),1.f);
        static const FButtonStyle S=FButtonStyle().SetNormal(FSlateNoResource()).SetHovered(Hover).SetPressed(Press).SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        return S;
    }
}
