#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Marau6InventoryWidget.generated.h"
class UTexture2D;

UCLASS()
class UMarau6InventoryWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UMarau6InventoryWidget(const FObjectInitializer& Initializer);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry&,const FKeyEvent&) override;
    float GroupWidth(int32 Group) const;
    FString Occupancy(int32 Start,int32 Count) const;
    float ViewWidth() const;
private:
    /** Base/intendance corridor shown behind the base screen; the raid keeps a plain dark backdrop. */
    UPROPERTY() TObjectPtr<UTexture2D> Background;
    UPROPERTY() TObjectPtr<UTexture2D> RifleIcon;
    UPROPERTY() TObjectPtr<UTexture2D> MedkitIcon;
    UPROPERTY() TObjectPtr<UTexture2D> ScrapIcon;
    UPROPERTY() TObjectPtr<UTexture2D> AmmoIcon;
    FSlateBrush BackgroundBrush;
};
