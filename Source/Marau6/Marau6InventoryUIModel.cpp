#include "Marau6InventoryUIModel.h"

bool FMarau6InventoryUIModel::ValidSlot(int32 S,bool bBase)
{return (S>=0 && S<Marau6Slots::Total) || (bBase && S>=100 && S<132);}
FString FMarau6InventoryUIModel::SlotName(int32 S)
{
    if(S==10)return TEXT("ARMA EQUIPADA");
    if(S>=100 && S<132)return FString::Printf(TEXT("STASH %02d"),S-99);
    if(S==8 || S==9)return FString::Printf(TEXT("SAFE %d"),S-7);
    return FString::Printf(TEXT("MOCHILA %d"),S+1);
}
FString FMarau6InventoryUIModel::MoveReason(const TArray<FMarau6Item>& Items,int32 S,int32 D,bool bBase)
{
    if(!ValidSlot(S,bBase) || !ValidSlot(D,bBase))return TEXT("Destino indisponivel neste inventario.");
    const auto* A=Items.FindByPredicate([S](const auto& I){return I.Slot==S;});
    const auto* B=Items.FindByPredicate([D](const auto& I){return I.Slot==D;});
    if(!A)return TEXT("O item mudou de posicao; tente novamente.");
    if(S==D)return TEXT("Movimentacao cancelada.");
    if(A->IsAmmo() && B && B->IsRifle())return B->Reserve<=Marau6Ammo::MaximumReserve-Marau6Ammo::BoxRounds?FString():TEXT("Reserva cheia: a caixa inteira precisa caber (limite 120).");
    auto Accepts=[](int32 Slot,const FMarau6Item& I){return Slot>=100 || Marau6Slots::Accepts(Slot,I);};
    if(!Accepts(D,*A))return Marau6Slots::IsSafe(D)?TEXT("Rifles nao cabem no safe pocket."):TEXT("O espaco de arma aceita somente rifles.");
    if(B && !Accepts(S,*B))return TEXT("Troca incompativel: os dois itens precisam caber nos destinos.");
    return FString();
}
FString FMarau6InventoryUIModel::Details(const FMarau6Item& I)
{
    FString Info=I.Name()+TEXT("\n")+SlotName(I.Slot)+TEXT("\n\n");
    if(I.IsRifle())return Info+FString::Printf(TEXT("Carregador: %d / 20\nReserva: %d\nDurabilidade: %.1f%%\n\n"),I.Magazine,I.Reserve,I.Durability/100.f)
        +(I.Durability<=0?TEXT("Sem condicao de disparar.\n\n"):TEXT("Automatico: segure o botao de disparo. Perde 1 ponto por disparo aceito.\n\n"))+TEXT("Recarregar nao repara.\nRifles nao cabem no safe pocket.");
    if(I.IsAmmo())return Info+TEXT("Caixa de 20 balas\n\nArraste sobre um rifle para abastecer sua reserva.\nLimite da reserva: 120 balas.\nA caixa e consumida inteira. Use R para carregar a arma.");
    if(I.Kind==EMarau6ItemKind::Supply)return Info+TEXT("Recupera ate 40 de vida\nTempo: 4 segundos\nUso unico\n\nUse na partida, parado no chao.\nMovimento ou dano interrompem a cura.\nVida cheia nao consome o item.");
    return Info+TEXT("Material coletavel\n\nPode ser guardado e extraido.\nSem uso de crafting neste prototipo.");
}
FString FMarau6InventoryUIModel::ShortLabel(const FMarau6Item& I)
{return I.IsRifle()?TEXT("RIFLE"):I.IsAmmo()?TEXT("MUN"):I.Kind==EMarau6ItemKind::Supply?TEXT("MED"):TEXT("MAT");}
FString FMarau6InventoryUIModel::Category(const FMarau6Item& I)
{return I.IsRifle()?FString::Printf(TEXT("%.1f%%"),I.Durability/100.f):I.IsAmmo()?TEXT("20 BALAS"):I.Kind==EMarau6ItemKind::Supply?TEXT("SUPRIMENTOS"):TEXT("PE\u00C7AS");}
FMarau6ItemInspection FMarau6InventoryUIModel::Inspect(const FMarau6Item* I,int32 Slot)
{
    FMarau6ItemInspection R;
    if(!I){R.Category=TEXT("SLOT VAZIO");R.Title=TEXT("\u2014");R.Subtitle=SlotName(Slot);R.Lead=TEXT("Selecione um item para ver os detalhes.");return R;}
    if(I->IsRifle())
    {
        R.Category=TEXT("ARMA PRINCIPAL");R.Title=TEXT("RIFLE");R.Subtitle=FString::Printf(TEXT("CONDI\u00C7\u00C3O %.1f%%"),I->Durability/100.f);
        R.Lead=FString::Printf(TEXT("Carregador %d / 20  \u00B7  Reserva %d"),I->Magazine,I->Reserve);
        R.Body=(I->Durability<=0?TEXT("Sem condi\u00E7\u00E3o de disparar.\n"):TEXT("Autom\u00E1tico: segure o bot\u00E3o de disparo. Perde 1 ponto por disparo aceito.\n"))+FString(TEXT("Recarregar n\u00E3o repara. Rifles n\u00E3o cabem no safe pocket."));
    }
    else if(I->IsAmmo())
    {
        R.Category=TEXT("MUNI\u00C7\u00C3O");R.Title=TEXT("MUNI\u00C7\u00C3O");R.Subtitle=TEXT("CAIXA DE 20 BALAS");
        R.Lead=TEXT("Arraste sobre um rifle para abastecer sua reserva.");
        R.Body=TEXT("Limite da reserva: 120 balas.\nA caixa \u00E9 consumida inteira. Use R para carregar a arma.");
    }
    else if(I->Kind==EMarau6ItemKind::Supply)
    {
        R.Category=TEXT("SUPRIMENTO M\u00C9DICO");R.Title=TEXT("SUPRIMENTOS");R.Subtitle=TEXT("USO \u00DANICO");
        R.Lead=TEXT("Recupera at\u00E9 40 de vida em 4 segundos.");
        R.Body=TEXT("Use na partida, parado no ch\u00E3o.\nMovimento ou dano interrompem a cura.\nVida cheia n\u00E3o consome o item.");
    }
    else
    {
        R.Category=TEXT("MATERIAL COLET\u00C1VEL");R.Title=TEXT("PE\u00C7AS");R.Subtitle=TEXT("RECUPERADAS EM INCURS\u00C3O");
        R.Lead=TEXT("Pode ser guardado e extra\u00EDdo.");R.Body=TEXT("Sem uso de crafting neste prot\u00F3tipo.");
    }
    return R;
}
