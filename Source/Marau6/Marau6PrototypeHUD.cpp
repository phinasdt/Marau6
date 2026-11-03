#include "Marau6PrototypeHUD.h"
#include "Marau6PlayerController.h"
#include "Serialization/JsonSerializer.h"
#include "Marau6Character.h"
#include "Marau6HealingComponent.h"
#include "Marau6InventoryComponent.h"
#include "Marau6LootActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"

void AMarau6PrototypeHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    const auto* BasePC=Cast<AMarau6PlayerController>(PlayerOwner);
    if(BasePC && BasePC->IsUsingBase() && !BasePC->IsInRaid() && BasePC->HasInventoryWidget())return;
    if(BasePC && BasePC->IsUsingBase() && !BasePC->IsInRaid())
    {
        DrawRect(FLinearColor(.025f,.035f,.045f,.98f),0,0,Canvas->ClipX,Canvas->ClipY);
        const float Scale=FMath::Min(Canvas->ClipX/1200.f,Canvas->ClipY/720.f);
        auto Text=[&](const FString& T,float X,float Y,FLinearColor Color=FLinearColor::White)
        {DrawText(T,Color,X*Scale,Y*Scale,GEngine->GetMediumFont(),Scale);};
        Text(FString::Printf(TEXT("MARAU6 | BASE | PERFIL: %d"),BasePC->GetBaseProfile()),25,20);
        Text(TEXT("1: Teste 1 | 2: Teste 2 | B: entrar na partida | N: nova rodada apos ambos terminarem"),25,55);
        Text(TEXT("Cima/baixo: slot | Esquerda/direita: coluna | Enter: marcar origem, depois selecionar destino e Enter novamente"),25,83);
        Text(BasePC->GetBaseMessage(),25,112,FLinearColor::Yellow);
        Text(BasePC->GetBaseBackendStatus(),25,140,FLinearColor(.65f,.75f,.85f));
        TSharedPtr<FJsonObject> State;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BasePC->GetBaseJson()),State);
        TMap<int32,TSharedPtr<FJsonObject>> Items;
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(State && State->TryGetArrayField(TEXT("items"),Values))for(const auto& V:*Values)Items.Add(V->AsObject()->GetIntegerField(TEXT("slot")),V->AsObject());
        Text(TEXT("EQUIPAMENTO PARA A PROXIMA PARTIDA"),25,185);
        Text(TEXT("STASH | 32 SLOTS"),440,185);
        for(int32 Index=0;Index<43;++Index)
        {
            const int32 Slot=AMarau6PlayerController::BaseSlot(Index);
            const float X=Index<11?25:440+((Index-11)/16)*365;
            const float Y=220+(Index<11?Index:(Index-11)%16)*26;
            FString Label=Slot==10?TEXT("ARMA"):Slot==8?TEXT("SAFE 1"):Slot==9?TEXT("SAFE 2"):Slot>=100?FString::Printf(TEXT("STASH %02d"),Slot-99):FString::Printf(TEXT("MOCHILA %d"),Slot+1);
            if(const auto* I=Items.Find(Slot))
            {
                const int32 Kind=(*I)->GetIntegerField(TEXT("kind"));
                Label+=Kind==0?TEXT(": Rifle"):Kind==1?TEXT(": Suprimentos"):TEXT(": Pecas");
                if(Kind==0)Label+=FString::Printf(TEXT(" [%d/%d | %.1f%%]"),(*I)->GetIntegerField(TEXT("magazine")),(*I)->GetIntegerField(TEXT("reserve")),((*I)->HasField(TEXT("durability"))?(*I)->GetIntegerField(TEXT("durability")):10000)/100.f);
            }
            else Label+=TEXT(": --");
            if(Index==BasePC->GetBaseSelection())DrawRect(FLinearColor(.18f,.2f,.05f,1.f),X*Scale-4,Y*Scale-2,350*Scale,25*Scale);
            if(Index==BasePC->GetBaseSource())Label=TEXT("* ")+Label;
            Text(Label,X,Y,Index==BasePC->GetBaseSelection()?FLinearColor::Yellow:Slot==8 || Slot==9?FLinearColor::Green:FLinearColor::White);
        }
        Text(TEXT("Perfis locais de desenvolvimento. Os dados sao salvos pelo servidor."),25,675,FLinearColor(.6f,.7f,.8f));
        return;
    }

    const AMarau6Character* Character = Cast<AMarau6Character>(PlayerOwner->GetPawn());
    if (!Character) return;
    if (BasePC && BasePC->HasHUDWidget())
    {
        if (Character->CanParticipate() && !Character->IsInventoryOpen())
        {
            const float X = Canvas->ClipX * 0.5f, Y = Canvas->ClipY * 0.5f;
            DrawLine(X - 7.f, Y, X + 7.f, Y, FLinearColor(.91f, .89f, .84f, .9f), 1.5f);
            DrawLine(X, Y - 7.f, X, Y + 7.f, FLinearColor(.91f, .89f, .84f, .9f), 1.5f);
        }
        return;
    }

    // Fallback without the Slate HUD, as terse as the widget: ammo over a red health bar
    // bottom right, extraction countdown, loot prompt, crosshair and the result.
    const bool bAlive = Character->IsAlive();
    const EMarau6RaidState Raid = Character->GetRaidState();
    const UMarau6InventoryComponent* Inventory=Character->GetInventory();
    const auto& Snapshot=Inventory->GetSnapshot();
    const FLinearColor Fault(1.f, .25f, .2f);
    UFont* Small=GEngine->GetSmallFont();UFont* Medium=GEngine->GetMediumFont();
    auto RightText=[&](const FString& T,float Right,float Y,FLinearColor Color,UFont* Font,float Scale=1.f)
    {float W=0.f,H=0.f;GetTextSize(T,W,H,Font,Scale);DrawText(T,Color,Right-W,Y,Font,Scale);return H;};
    if (bAlive)
    {
        const float Right = Canvas->ClipX - 36.f, BarW = 280.f, BarH = 28.f;
        const float BarTop = Canvas->ClipY - 32.f - BarH;
        float Top = BarTop;
        if (Character->HasEquippedRifle())
        {
            // "magazine / capacity (carried)", red when empty; carried is the rifle's
            // reserve plus unopened ammo boxes in the backpack.
            int32 Boxes = 0;
            for (int32 Slot = 0; Slot < Marau6Slots::BackpackCount; ++Slot)
                if (const FMarau6Item* Item = Inventory->ItemAt(Slot)) Boxes += Item->IsAmmo() ? 1 : 0;
            const int32 Carried = Character->GetReserveAmmo() + Boxes * Marau6Ammo::BoxRounds;
            const FString Ammo = FString::Printf(TEXT("%d / %d (%d)"), Character->GetMagazineAmmo(), Character->GetMagazineCapacity(), Carried);
            float W = 0.f, H = 0.f;
            GetTextSize(Ammo, W, H, GEngine->GetLargeFont(), 1.4f);
            Top = BarTop - 8.f - H;
            RightText(Ammo, Right, Top, Character->GetMagazineAmmo() == 0 ? Fault : FLinearColor::White, GEngine->GetLargeFont(), 1.4f);
            if (Character->IsReloading())
            {
                RightText(TEXT("RECARREGANDO"), Right, Top - 18.f, FLinearColor::Yellow, Small);
                Top -= 20.f;
            }
        }
        if (Character->GetHealing()->IsHealing())
            RightText(FString::Printf(TEXT("CURANDO %.1fs"), Character->GetHealing()->Remaining()), Right, Top - 18.f, FLinearColor::Green, Small);
        const float Health = FMath::Clamp(Character->GetHealth(), 0.f, 100.f);
        DrawRect(FLinearColor(.08f, .07f, .06f, .75f), Right - BarW, BarTop, BarW, BarH);
        DrawRect(FLinearColor(.7f, .12f, .1f, .92f), Right - BarW, BarTop, BarW * Health / 100.f, BarH);
        const FString Life = FString::Printf(TEXT("%.0f / 100"), FMath::CeilToFloat(Health));
        float LW = 0.f, LH = 0.f;GetTextSize(Life, LW, LH, Medium, 1.f);
        DrawText(Life, FLinearColor::White, Right - BarW * .5f - LW * .5f, BarTop + (BarH - LH) * .5f, Medium, 1.f);
        DrawText(TEXT("TAB"), FLinearColor(.75f, .78f, .8f), 28.f, Canvas->ClipY - 50.f, Small, 1.f);
    }
    if (Raid == EMarau6RaidState::Extracting)
    {
        const FString T = FString::Printf(TEXT("EXTRAINDO %.1fs"), Character->GetExtractionSecondsRemaining());
        float W = 0.f, H = 0.f;GetTextSize(T, W, H, Medium, 1.f);
        DrawText(T, FLinearColor::Green, Canvas->ClipX * .5f - W * .5f, 28.f, Medium, 1.f);
    }
    if (!Character->CanParticipate())
    {
        const FString Head = Raid == EMarau6RaidState::Extracted ? TEXT("EXTRAIDO") : TEXT("ELIMINADO");
        const FString Sub = Snapshot.bFinalized ? FString::Printf(TEXT("MANTIDOS %d | PERDIDOS %d"), Snapshot.Items.Num(), Snapshot.LostCount)
            : FString(BasePC && BasePC->IsUsingBase() ? TEXT("SALVANDO") : TEXT("REINICIE O PLAY"));
        const float X = Canvas->ClipX * .5f, Y = Canvas->ClipY * .5f;
        float W = 0.f, H = 0.f, SW = 0.f, SH = 0.f;GetTextSize(Head, W, H, Medium, 1.f);GetTextSize(Sub, SW, SH, Small, 1.f);
        DrawRect(FLinearColor(0.f, 0.f, 0.f, .7f), X - 150.f, Y - 35.f, 300.f, 70.f);
        DrawText(Head, Raid == EMarau6RaidState::Extracted ? FLinearColor::Green : FLinearColor::Red, X - W * .5f, Y - 25.f, Medium, 1.f);
        DrawText(Sub, FLinearColor::White, X - SW * .5f, Y + 8.f, Small, 1.f);
    }
    else
    {
        const float X = Canvas->ClipX * 0.5f, Y = Canvas->ClipY * 0.5f;
        DrawLine(X - 7.f, Y, X + 7.f, Y, FLinearColor::White, 1.5f);
        DrawLine(X, Y - 7.f, X, Y + 7.f, FLinearColor::White, 1.5f);
        if(const auto* Loot=Inventory->LookedAtLoot())
        {
            bool bHasSpace=Loot->GetItem().IsRifle() && !Character->HasEquippedRifle();
            for(int32 Slot=0;Slot<Marau6Slots::BackpackCount;++Slot) if(!Inventory->ItemAt(Slot)) bHasSpace=true;
            const FString Prompt=bHasSpace?TEXT("E  ")+Loot->GetItem().Name():TEXT("MOCHILA CHEIA");
            float W = 0.f, H = 0.f;GetTextSize(Prompt, W, H, Medium, 1.f);
            DrawText(Prompt,FLinearColor::Yellow,X-W*.5f,Canvas->ClipY*.65f,Medium,1);
        }
    }
    if(Character->IsInventoryOpen() && !(BasePC && BasePC->HasInventoryWidget()))
    {
        const float Left=FMath::Max(12.f,Canvas->ClipX-380.f), Top=205.f;
        DrawRect(FLinearColor(0,0,0,.85f),Left,Top,365,420);

        DrawText(Snapshot.bFinalized?TEXT("ITENS MANTIDOS NESTA PARTIDA"):TEXT("INVENTARIO"),FLinearColor::White,Left+12,Top+10,GEngine->GetMediumFont(),1);
        for(int32 Slot=0;Slot<Marau6Slots::Total;++Slot)
        {
            FString Label=Slot==Marau6Slots::Weapon?TEXT("ARMA") : Marau6Slots::IsSafe(Slot)
                ? FString::Printf(TEXT("SAFE %d"),Slot-Marau6Slots::SafeStart+1):FString::Printf(TEXT("MOCHILA %d"),Slot+1);
            const auto* Item=Inventory->ItemAt(Slot);
            FString Text=Label+TEXT(": ")+(Item?Item->Name():TEXT("--"));
            if(Item && Item->IsRifle()) Text+=FString::Printf(TEXT(" [%d/%d | %.1f%%]"),Item->Magazine,Item->Reserve,Item->Durability/100.f);
            if(Item && Item->Id==Character->GetInventoryMoveSource()) Text=TEXT("* ")+Text;
            const bool bSelected=Slot==Character->GetSelectedInventorySlot();
            if(bSelected) DrawRect(FLinearColor(.2f,.2f,0,.9f),Left+6,Top+45+Slot*24,353,23);
            DrawText(Text,bSelected?FLinearColor::Yellow:Marau6Slots::IsSafe(Slot)?FLinearColor::Green:FLinearColor::White,
                Left+12,Top+48+Slot*24,GEngine->GetSmallFont(),1);
        }
        DrawText(TEXT("Setas: slot | Enter: origem / destino"),FLinearColor::White,Left+12,Top+325,GEngine->GetSmallFont(),1);
        DrawText(TEXT("F: equipar | G: descartar | P: safe / mochila"),FLinearColor::White,Left+12,Top+349,GEngine->GetSmallFont(),1);
        DrawText(TEXT("H: usar Suprimentos | 40 de vida / 4s"),FLinearColor::White,Left+12,Top+373,GEngine->GetSmallFont(),1);
        DrawText(Snapshot.bFinalized?TEXT("Resultado guardado ate encerrar o Play"):TEXT("* Origem | Mesmo slot: cancelar | TAB: fechar"),FLinearColor::White,Left+12,Top+397,GEngine->GetSmallFont(),1);
    }

}
