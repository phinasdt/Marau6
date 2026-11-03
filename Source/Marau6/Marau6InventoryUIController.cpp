#include "Marau6PlayerController.h"
#include "Marau6InventoryUIModel.h"
#include "Marau6InventoryWidget.h"
#include "Marau6HUDWidget.h"
#include "Marau6InventorySlot.h"
#include "Marau6Character.h"
#include "Marau6HealingComponent.h"
#include "Marau6InventoryComponent.h"
#include "Marau6ItemLedger.h"
#include "Marau6Persistence.h"
#include "Marau6Audio.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"

bool AMarau6PlayerController::IsInventoryAtBase() const {return bUseBase && !bInRaid;}
bool AMarau6PlayerController::IsInventoryVisible() const
{const auto* C=Cast<AMarau6Character>(GetPawn());return IsInventoryAtBase() || (C && C->IsInventoryOpen());}
bool AMarau6PlayerController::IsInventoryReadOnly() const
{
    if(IsInventoryAtBase())return BaseProfile==0 || BaseJson.IsEmpty();
    const auto* C=Cast<AMarau6Character>(GetPawn());return !C || !C->CanParticipate() || C->GetInventory()->GetSnapshot().bFinalized;
}
const TArray<FMarau6Item>& AMarau6PlayerController::GetInventoryUIItems() const
{
    if(!IsInventoryAtBase())
    {if(const auto* C=Cast<AMarau6Character>(GetPawn()))return C->GetInventory()->GetSnapshot().Items;static const TArray<FMarau6Item> Empty;return Empty;}
    if(CachedInventoryJson!=BaseJson)
    {
        CachedInventoryJson=BaseJson;CachedBaseItems.Empty();CachedBaseRevision=0;
        TSharedPtr<FJsonObject> O;const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BaseJson),O) && O)
        {
            O->TryGetNumberField(TEXT("revision"),CachedBaseRevision);
            if(O->TryGetArrayField(TEXT("items"),Values))for(const auto& V:*Values)
            {
                auto J=V->AsObject();if(!J)continue;FMarau6Item I;FString Id;int32 Kind=0;
                if(!J->TryGetStringField(TEXT("id"),Id) || !FGuid::Parse(Id,I.Id))continue;
                J->TryGetNumberField(TEXT("kind"),Kind);I.Kind=static_cast<EMarau6ItemKind>(Kind);
                J->TryGetNumberField(TEXT("slot"),I.Slot);J->TryGetNumberField(TEXT("magazine"),I.Magazine);J->TryGetNumberField(TEXT("reserve"),I.Reserve);
                if(I.IsRifle())J->TryGetNumberField(TEXT("durability"),I.Durability);
                CachedBaseItems.Add(I);
            }
        }
    }
    if(!bBasicKit)return CachedBaseItems;
    // Basic kit preview: rifle 20/40, two ammo boxes and a medkit, as the backend mints
    // them on entry. Fixed preview ids; the profile's own equipment stays untouched.
    CachedKitItems=CachedBaseItems.FilterByPredicate([](const FMarau6Item& I){return I.Slot>=100;});
    const TPair<EMarau6ItemKind,int32> Kit[]={{EMarau6ItemKind::Rifle,Marau6Slots::Weapon},{EMarau6ItemKind::Ammo,0},{EMarau6ItemKind::Ammo,1},{EMarau6ItemKind::Supply,2}};
    for(int32 N=0;N<4;N++)
    {
        FMarau6Item I;I.Id=FGuid(0x4B495400,0,0,N+1);I.Kind=Kit[N].Key;I.Slot=Kit[N].Value;
        if(I.IsRifle()){I.Magazine=20;I.Reserve=40;}
        CachedKitItems.Add(I);
    }
    return CachedKitItems;
}
const FMarau6Item* AMarau6PlayerController::InventoryUIItemAt(int32 Slot) const
{return GetInventoryUIItems().FindByPredicate([Slot](const auto& I){return I.Slot==Slot;});}
int32 AMarau6PlayerController::InventoryUIRevision() const
{if(IsInventoryAtBase()){GetInventoryUIItems();return CachedBaseRevision;}const auto* C=Cast<AMarau6Character>(GetPawn());return C?C->GetInventory()->GetSnapshot().Revision:0;}
int32 AMarau6PlayerController::GetInventoryUISelection() const
{if(IsInventoryAtBase())return BaseSlot(BaseSelection);const auto* C=Cast<AMarau6Character>(GetPawn());return C?C->GetSelectedInventorySlot():0;}
void AMarau6PlayerController::SelectInventorySlot(int32 Slot)
{
    if(!FMarau6InventoryUIModel::ValidSlot(Slot,IsInventoryAtBase()))return;
    if(IsInventoryAtBase())BaseSelection=Slot>=100?11+Slot-100:Slot;
    else if(auto* C=Cast<AMarau6Character>(GetPawn()))C->SelectInventorySlot(Slot);
}
FString AMarau6PlayerController::GetInventoryNotice() const
{
    if(!InventoryMessage.IsEmpty())return InventoryMessage;
    if(IsInventoryAtBase())return BaseMessage;
    if(const auto* C=Cast<AMarau6Character>(GetPawn());C && !C->GetHealing()->Status().IsEmpty())return C->GetHealing()->Status();
    return IsInventoryReadOnly()?TEXT("Resultado confirmado: itens somente para consulta."):TEXT("Arraste para mover ou trocar. Clique para ver detalhes.");
}
bool AMarau6PlayerController::InventoryRequestPending() const
{return PendingInventoryRequest.IsValid() || (IsInventoryAtBase() && bBaseBusy);}
void AMarau6PlayerController::RefreshInventoryContext()
{
    if(!HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown)return;
    const auto* C=Cast<AMarau6Character>(GetPawn());const auto* Bridge=GetWorld()->GetSubsystem<UMarau6Persistence>();
    const FString Key=FString::Printf(TEXT("%d/%d/%d/%u/%d/%s"),bUseBase,bInRaid,BaseProfile,C?C->GetUniqueID():0,C?static_cast<int32>(C->GetRaidState()):-1,Bridge?*Bridge->GetRoundId():TEXT(""));
    if(Key!=InventoryContextKey)
    {InventoryContextKey=Key;InventoryContext=FGuid::NewGuid();ServerInventoryRequest.Invalidate();ForceNetUpdate();}
}
bool AMarau6PlayerController::CaptureInventoryDrag(int32 Slot,FMarau6InventoryUIRequest& Out)
{
    if(HasAuthority())RefreshInventoryContext();
    if(!IsInventoryVisible() || IsInventoryReadOnly() || !InventoryContext.IsValid()){InventoryMessage=TEXT("Inventario indisponivel neste estado.");return false;}
    if(InventoryRequestPending()){InventoryMessage=TEXT("Aguarde a transferencia pendente.");return false;}
    if(IsKitLocked(Slot)){InventoryMessage=TEXT("Kit basico bloqueado. Troque para o kit personalizado para editar.");return false;}
    const auto* I=InventoryUIItemAt(Slot);if(!I)return false;
    Out=FMarau6InventoryUIRequest();Out.RequestId=FGuid::NewGuid();Out.Context=InventoryContext;Out.Source=Slot;Out.Item=I->Id;Out.Profile=BaseProfile;Out.Revision=InventoryUIRevision();
    SelectInventorySlot(Slot);return true;
}
bool AMarau6PlayerController::SubmitInventoryDrop(FMarau6InventoryUIRequest R,int32 Destination)
{
    if(IsKitLocked(Destination)){InventoryMessage=TEXT("Kit basico bloqueado. Troque para o kit personalizado para editar.");return false;}
    if(InventoryRequestPending()){InventoryMessage=TEXT("Aguarde a transferencia pendente.");return false;}
    if(IsInventoryReadOnly() || !IsInventoryVisible() || R.Context!=InventoryContext){InventoryMessage=TEXT("O inventario mudou; tente novamente.");return false;}
    const auto* I=InventoryUIItemAt(R.Source);
    if(!I || I->Id!=R.Item){InventoryMessage=TEXT("O item mudou de posicao; tente novamente.");return false;}
    const FString Reason=FMarau6InventoryUIModel::MoveReason(GetInventoryUIItems(),R.Source,Destination,IsInventoryAtBase());
    if(!Reason.IsEmpty()){InventoryMessage=Reason;return false;}
    R.Destination=Destination;const auto* Other=InventoryUIItemAt(Destination);R.DestinationItem=Other?Other->Id:FGuid();
    PendingInventoryRequest=R.RequestId;PendingInventoryContext=R.Context;PendingInventorySince=GetWorld()->GetTimeSeconds();bInventoryAwaitingState=false;
    InventoryMessage=TEXT("Aguardando confirmacao da transferencia...");ServerInventoryMoveUI(R);return true;
}
void AMarau6PlayerController::CompleteInventoryRequest(const FMarau6InventoryUIRequest& R,bool Success,const FString& Message)
{
    const int32 Revision=InventoryUIRevision();
    if(ServerInventoryRequest==R.RequestId)ServerInventoryRequest.Invalidate();
    FMarau6InventoryUIReceipt Receipt;Receipt.Request=R;Receipt.bSuccess=Success;Receipt.Message=Message;Receipt.Revision=Revision;
    InventoryReceipts.Add(R.RequestId,Receipt);InventoryReceiptOrder.Add(R.RequestId);
    while(InventoryReceiptOrder.Num()>64){InventoryReceipts.Remove(InventoryReceiptOrder[0]);InventoryReceiptOrder.RemoveAt(0);}
    ClientInventoryResult(R.RequestId,R.Context,Success,Message,Revision);
}
void AMarau6PlayerController::ServerInventoryMoveUI_Implementation(FMarau6InventoryUIRequest R)
{
    RefreshInventoryContext();if(!R.RequestId.IsValid())return;
    if(const auto* Old=InventoryReceipts.Find(R.RequestId))
    {if(Old->Request.Same(R))ClientInventoryResult(R.RequestId,R.Context,Old->bSuccess,Old->Message,Old->Revision);else ClientInventoryResult(R.RequestId,R.Context,false,TEXT("Solicitacao alterada; tente novamente."),InventoryUIRevision());return;}
    if(ServerInventoryRequest.IsValid())
    {if(ServerInventoryRequest!=R.RequestId)ClientInventoryResult(R.RequestId,R.Context,false,TEXT("Aguarde a transferencia pendente."),InventoryUIRevision());return;}
    auto Reject=[&](const FString& Why){CompleteInventoryRequest(R,false,Why);};
    if(R.Context!=InventoryContext || R.Profile!=BaseProfile || IsInventoryReadOnly()){Reject(TEXT("Inventario indisponivel ou alterado; tente novamente."));return;}
    if((IsInventoryAtBase() && bBaseBusy) || R.Revision!=InventoryUIRevision()){Reject(bBaseBusy?TEXT("Aguarde a transferencia pendente."):TEXT("O inventario foi atualizado; tente novamente."));return;}
    const auto* I=InventoryUIItemAt(R.Source);const auto* Other=InventoryUIItemAt(R.Destination);
    if(!I || I->Id!=R.Item || (Other?Other->Id:FGuid())!=R.DestinationItem){Reject(TEXT("O item mudou de posicao; tente novamente."));return;}
    const auto Reason=FMarau6InventoryUIModel::MoveReason(GetInventoryUIItems(),R.Source,R.Destination,IsInventoryAtBase());
    if(!Reason.IsEmpty()){Reject(Reason);return;}
    ServerInventoryRequest=R.RequestId;
    if(IsInventoryAtBase())
    {
        TWeakObjectPtr<AMarau6PlayerController> Weak(this);
        GetWorld()->GetSubsystem<UMarau6Persistence>()->MoveBase(this,R.Source,R.Destination,[Weak,R](bool Success,const FString& Why)
        {if(auto* PC=Weak.Get())PC->CompleteInventoryRequest(R,Success,Why);});
    }
    else
    {
        auto* C=Cast<AMarau6Character>(GetPawn());
        const bool Success=C && GetWorld()->GetSubsystem<UMarau6ItemLedger>()->Move(C->GetInventory(),R.Item,R.Destination);
        CompleteInventoryRequest(R,Success,Success?TEXT("Transferencia confirmada."):TEXT("Nao foi possivel confirmar; os itens foram preservados."));
    }
}
void AMarau6PlayerController::ClientInventoryResult_Implementation(FGuid Request,FGuid Context,bool Success,const FString& Message,int32 Revision)
{
    if(Request!=PendingInventoryRequest)return;
    if(Context!=InventoryContext || Context!=PendingInventoryContext){PendingInventoryRequest.Invalidate();return;}
    InventoryCompletionMessage=Message;InventoryConfirmedRevision=Revision;bInventoryAwaitingState=Success;
    if(!Success || InventoryUIRevision()>=Revision)
    {PendingInventoryRequest.Invalidate();bInventoryAwaitingState=false;InventoryMessage=Message;}
    else InventoryMessage=TEXT("Confirmacao recebida; atualizando os itens...");
}
void AMarau6PlayerController::ClientInventoryNotice_Implementation(const FString& Message)
{if(!PendingInventoryRequest.IsValid())InventoryMessage=Message;}
void AMarau6PlayerController::CancelInventoryDrag()
{
    KeyboardInventorySource=FMarau6InventoryUIRequest();BaseSource=INDEX_NONE;
    if(auto* C=Cast<AMarau6Character>(GetPawn()))C->CancelInventorySelection();
    if(FSlateApplication::IsInitialized())
    {
        auto Drag=FSlateApplication::Get().GetDragDroppingContent();
        if(Drag.IsValid() && Drag->IsOfType<FMarau6InventoryDragOperation>() && StaticCastSharedPtr<FMarau6InventoryDragOperation>(Drag)->Controller.Get()==this)FSlateApplication::Get().CancelDragDrop();
    }
}
void AMarau6PlayerController::UpdateInventoryUI()
{
    if(!IsLocalPlayerController() || !GetWorld() || GetWorld()->bIsTearingDown)return;
    const bool AtBase=IsInventoryAtBase();
    if(bLastUIAtBase && !AtBase)if(auto* C=Cast<AMarau6Character>(GetPawn()))C->CloseInventoryFromUI();
    bLastUIAtBase=AtBase;
    if(AtBase && ObservedBaseMessage!=BaseMessage && !PendingInventoryRequest.IsValid())
    {ObservedBaseMessage=BaseMessage;InventoryMessage.Empty();}
    if(LastInventoryContext!=InventoryContext)
    {CancelInventoryDrag();LastInventoryContext=InventoryContext;PendingInventoryRequest.Invalidate();bInventoryAwaitingState=false;InventoryMessage.Empty();}
    if(PendingInventoryRequest.IsValid() && bInventoryAwaitingState && InventoryUIRevision()>=InventoryConfirmedRevision)
    {PendingInventoryRequest.Invalidate();bInventoryAwaitingState=false;InventoryMessage=InventoryCompletionMessage;}
    if(PendingInventoryRequest.IsValid() && !bInventoryAwaitingState && GetWorld()->GetTimeSeconds()-PendingInventorySince>8.f)InventoryMessage=TEXT("Ainda aguardando confirmacao; nao repita a transferencia.");
    if(FSlateApplication::IsInitialized())
    {
        auto Drag=FSlateApplication::Get().GetDragDroppingContent();
        if(Drag.IsValid() && Drag->IsOfType<FMarau6InventoryDragOperation>())
        {
            auto Own=StaticCastSharedPtr<FMarau6InventoryDragOperation>(Drag);
            if(Own->Controller.Get()==this)
            {const auto* I=InventoryUIItemAt(Own->Request.Source);if(!IsInventoryVisible() || IsInventoryReadOnly() || Own->Request.Context!=InventoryContext || !I || I->Id!=Own->Request.Item)CancelInventoryDrag();}
        }
    }
    if(!GetLocalPlayer() || !GetLocalPlayer()->ViewportClient)return;
    const bool Visible=IsInventoryVisible();
    if(Visible && !InventoryWidget)
    {InventoryWidget=CreateWidget<UMarau6InventoryWidget>(this,UMarau6InventoryWidget::StaticClass());if(InventoryWidget)InventoryWidget->AddToPlayerScreen(20);}
    if(InventoryWidget)InventoryWidget->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    // The raid HUD steps aside while the inventory covers the screen.
    const bool ShowHUD=!AtBase && !Visible && Cast<AMarau6Character>(GetPawn());
    if(ShowHUD && !HUDWidget)
    {HUDWidget=CreateWidget<UMarau6HUDWidget>(this,UMarau6HUDWidget::StaticClass());if(HUDWidget)HUDWidget->AddToPlayerScreen(10);}
    if(HUDWidget)HUDWidget->SetVisibility(ShowHUD?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(Visible!=bInventoryInputActive)
    {
        bInventoryInputActive=Visible;bShowMouseCursor=Visible;CancelInventoryDrag();
        if(Visible && InventoryWidget)
        {FInputModeGameAndUI Mode;Mode.SetWidgetToFocus(InventoryWidget->TakeWidget());Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);}
        else {SetInputMode(FInputModeGameOnly());if(PlayerInput)PlayerInput->FlushPressedKeys();}
    }
}
void AMarau6PlayerController::PlayerTick(float Delta)
{Super::PlayerTick(Delta);UpdateInventoryUI();UpdateRaidGas();}
void AMarau6PlayerController::TickActor(float Delta,ELevelTick Type,FActorTickFunction& Function)
{
    // Dedicated-server remote controllers do not execute PlayerTick.
    if(HasAuthority())RefreshInventoryContext();Super::TickActor(Delta,Type,Function);
}
void AMarau6PlayerController::EndPlay(EEndPlayReason::Type Reason)
{
    CancelInventoryDrag();if(InventoryWidget){InventoryWidget->RemoveFromParent();InventoryWidget=nullptr;}
    if(HUDWidget){HUDWidget->RemoveFromParent();HUDWidget=nullptr;}
    // An activated reverb outlives PIE on the editor's audio device unless removed.
    if(IsLocalPlayerController())UGameplayStatics::DeactivateReverbEffect(this,Marau6Audio::AmbientReverbTag);
    if(IsLocalPlayerController()){bShowMouseCursor=false;bInventoryInputActive=false;if(GetLocalPlayer() && GetLocalPlayer()->ViewportClient)SetInputMode(FInputModeGameOnly());}
    Super::EndPlay(Reason);
}
void AMarau6PlayerController::InventoryKeyboardAccept()
{
    if(InventoryRequestPending() || IsInventoryReadOnly())return;
    if(!KeyboardInventorySource.Item.IsValid())
    {if(CaptureInventoryDrag(GetInventoryUISelection(),KeyboardInventorySource))InventoryMessage=TEXT("Origem selecionada. Escolha o destino e pressione Enter.");else if(!InventoryUIItemAt(GetInventoryUISelection()))InventoryMessage=TEXT("Selecione um item para mover.");}
    else {const auto R=KeyboardInventorySource;KeyboardInventorySource=FMarau6InventoryUIRequest();SubmitInventoryDrop(R,GetInventoryUISelection());}
}
bool AMarau6PlayerController::HandleInventoryKey(FKey Key)
{
    auto* C=Cast<AMarau6Character>(GetPawn());
    if(Key==EKeys::Escape){if(HasOwnInventoryDrag()){CancelInventoryDrag();InventoryMessage=TEXT("Movimentacao cancelada.");return true;}return false;}
    if(Key==EKeys::Tab){if(!IsInventoryAtBase() && C){CancelInventoryDrag();C->ToggleInventoryFromUI();UpdateInventoryUI();}return true;}
    if(Key==EKeys::One && IsInventoryAtBase()){CancelInventoryDrag();SelectOne();return true;}
    if(Key==EKeys::Two && IsInventoryAtBase()){CancelInventoryDrag();SelectTwo();return true;}
    if(Key==EKeys::B && IsInventoryAtBase()){CancelInventoryDrag();BaseEnter();return true;}
    if(Key==EKeys::N && IsInventoryAtBase()){CancelInventoryDrag();BaseNext();return true;}
    if(Key==EKeys::K && IsInventoryAtBase()){CancelInventoryDrag();ToggleKit();return true;}
    if(Key==EKeys::Up){if(IsInventoryAtBase())BaseUp();else if(C)C->SelectInventorySlot((C->GetSelectedInventorySlot()+10)%11);return true;}
    if(Key==EKeys::Down){if(IsInventoryAtBase())BaseDown();else if(C)C->SelectInventorySlot((C->GetSelectedInventorySlot()+1)%11);return true;}
    if(Key==EKeys::Left && IsInventoryAtBase()){BaseLeft();return true;}
    if(Key==EKeys::Right && IsInventoryAtBase()){BaseRight();return true;}
    if(Key==EKeys::Enter){InventoryKeyboardAccept();return true;}
    if(Key==EKeys::F || Key==EKeys::G || Key==EKeys::P || Key==EKeys::H)
    {if(!InventoryRequestPending() && !IsInventoryAtBase() && C){InventoryMessage.Empty();C->InventoryShortcut(Key);}return true;}
    return false;
}
bool AMarau6PlayerController::HasOwnInventoryDrag() const
{
    if(!FSlateApplication::IsInitialized())return false;
    auto Drag=FSlateApplication::Get().GetDragDroppingContent();
    return Drag.IsValid() && Drag->IsOfType<FMarau6InventoryDragOperation>() && StaticCastSharedPtr<FMarau6InventoryDragOperation>(Drag)->Controller.Get()==this;
}
