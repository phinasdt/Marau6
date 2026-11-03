#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"

/** Draws the generated inventory art without stretching its square texture. */
class SMarau6ItemIcon : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMarau6ItemIcon){}
        SLATE_ATTRIBUTE(int32, Kind)
        SLATE_ATTRIBUTE(UTexture2D*, Texture)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Kind = Args._Kind;
        Texture = Args._Texture;
        SetVisibility(EVisibility::HitTestInvisible);
        // The rifle texture is square, but its long silhouette is meant to fill
        // the horizontal weapon slot. Clip the transparent top and bottom.
        SetClipping(EWidgetClipping::ClipToBoundsAlways);
    }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64.f, 64.f); }

private:
    TAttribute<int32> Kind;
    TAttribute<UTexture2D*> Texture;

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const int32 ItemKind = Kind.Get(-1);
        UTexture2D* IconTexture = Texture.Get(nullptr);
        if (ItemKind < 0 || !IconTexture)
        {
            return Layer;
        }

        const FVector2D LocalSize = Geometry.GetLocalSize();
        const float Side = ItemKind == 0
            ? LocalSize.X
            : FMath::Min(LocalSize.X, LocalSize.Y) * 0.98f;
        const FVector2D DrawSize(Side, Side);
        const FVector2D DrawPosition((LocalSize.X - Side) * 0.5f, (LocalSize.Y - Side) * 0.5f);
        const FGeometry IconGeometry = Geometry.MakeChild(DrawSize, FSlateLayoutTransform(DrawPosition));

        FSlateBrush Brush;
        Brush.SetResourceObject(IconTexture);
        Brush.ImageSize = FVector2D(1024.f, 1024.f);
        Brush.DrawAs = ESlateBrushDrawType::Image;
        FSlateDrawElement::MakeBox(Out, Layer + 1, IconGeometry.ToPaintGeometry(), &Brush,
            ESlateDrawEffect::None, FLinearColor::White);
        return Layer + 1;
    }
};
