#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Marau6HUDWidget.generated.h"
class AMarau6Character;

/** In-raid HUD in the base screen's style: ammo counter over a health bar, extraction countdown, loot prompt and the TAB cue. */
UCLASS()
class UMarau6HUDWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    AMarau6Character* Pawn() const;
};
