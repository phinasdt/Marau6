#include "Marau6HealingComponent.h"
#include "Marau6Character.h"
#include "Marau6InventoryComponent.h"
#include "Marau6ItemLedger.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UMarau6HealingComponent::UMarau6HealingComponent()
{SetIsReplicatedByDefault(true);PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.05f;}
AMarau6Character* UMarau6HealingComponent::Character()const{return Cast<AMarau6Character>(GetOwner());}
void UMarau6HealingComponent::Message(const FString& Text)
{Feedback.Message=Text;if(auto* C=Character();C && C->HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown)C->ForceNetUpdate();}
float UMarau6HealingComponent::Remaining()const
{
    if(!bHealing || !GetWorld())return 0;
    const auto* GS=GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    return FMath::Max(0.f,static_cast<float>(Feedback.EndsAt-Now));
}
void UMarau6HealingComponent::UseSelected()
{
    auto* C=Character();if(!C || !C->IsLocallyControlled())return;
    if(bHealing){ServerCancel();return;}
    if(!C->IsInventoryOpen()){Message(TEXT("Aperte duas vezes a tecla do slot (1-8) ou abra o inventario (TAB) e pressione H."));return;}
    const auto* Item=C->GetInventory()->ItemAt(C->GetSelectedInventorySlot());ServerUse(Item?Item->Id:FGuid());
}
void UMarau6HealingComponent::UseItem(FGuid Id)
{
    auto* C=Character();if(!C || !C->IsLocallyControlled())return;
    if(bHealing)ServerCancel();else ServerUse(Id);
}
void UMarau6HealingComponent::ServerUse_Implementation(FGuid Id)
{
    auto* C=Character();if(!C || !C->HasAuthority() || bCompleting)return;
    if(bHealing){Message(TEXT("Tratamento em andamento. H cancela."));return;}
    if(!C->CanParticipate() || C->GetRaidState()!=EMarau6RaidState::Active || !C->GetController())
    {Message(TEXT("Cura disponivel somente vivo e fora da contagem de extracao."));return;}
    if(C->GetHealth()>=100){Message(TEXT("Vida cheia: nenhum suprimento consumido."));return;}
    if(C->IsReloading()){Message(TEXT("Aguarde terminar a recarga antes de se curar."));return;}
    if(!C->GetCharacterMovement()->IsMovingOnGround() || C->GetVelocity().SizeSquared()>25)
    {Message(TEXT("Pare no chao para iniciar a cura."));return;}
    const auto* I=C->GetInventory()->GetSnapshot().Items.FindByPredicate([Id](const FMarau6Item& Item){return Item.Id==Id;});
    if(!I || I->Kind!=EMarau6ItemKind::Supply || I->Slot<0 || I->Slot>=Marau6Slots::Weapon || C->GetInventory()->GetSnapshot().bFinalized)
    {Message(TEXT("Selecione um Suprimento da mochila ou do safe."));return;}
    Source=I->Id;SourceSlot=I->Slot;Origin=C->GetActorLocation();bHealing=true;Feedback.EndsAt=GetWorld()->GetTimeSeconds()+4;
    Message(TEXT("Tratando ferimentos... H cancela; mova-se para interromper."));OnRep_Healing();C->ForceNetUpdate();
}
void UMarau6HealingComponent::OnRep_Healing()
{
    if(auto* C=Character();bHealing && C && C->IsLocallyControlled())
    {C->bInventoryOpen=false;C->InventoryMoveSource.Invalidate();}
}
void UMarau6HealingComponent::ServerCancel_Implementation(){Cancel(TEXT("Cura cancelada: suprimento preservado."));}
void UMarau6HealingComponent::Cancel(const FString& Reason)
{
    auto* C=Character();if(!C || !C->HasAuthority() || !bHealing || bCompleting)return;
    bHealing=false;Feedback.EndsAt=0;Source.Invalidate();SourceSlot=INDEX_NONE;Message(Reason);
}
void UMarau6HealingComponent::CancelFromMovement()
{
    if(!bHealing)return;
    if(auto* C=Character();C && C->HasAuthority())Cancel(TEXT("Movimento interrompeu a cura: suprimento preservado."));
    else ServerCancel();
}
void UMarau6HealingComponent::NotifyInventoryChanged(bool bEquipmentChanged)
{
    auto* C=Character();if(!bHealing || !C || !C->HasAuthority() || bCompleting)return;
    const auto* I=C->GetInventory()->ItemAt(SourceSlot);
    if(bEquipmentChanged || C->GetInventory()->GetSnapshot().bFinalized || !I || I->Id!=Source || I->Kind!=EMarau6ItemKind::Supply)
        Cancel(TEXT("Mudanca de equipamento interrompeu a cura: item nao consumido."));
}
void UMarau6HealingComponent::UpdateHealing()
{
    auto* C=Character();if(!bHealing || !C || !C->HasAuthority() || bCompleting)return;
    if(!GetWorld() || GetWorld()->bIsTearingDown || !C->CanParticipate() || C->GetRaidState()!=EMarau6RaidState::Active || !C->GetController())
    {Cancel(TEXT("Cura interrompida pelo estado da partida."));return;}
    if(!C->GetCharacterMovement()->IsMovingOnGround() || C->GetVelocity().SizeSquared()>25 || FVector::DistSquared(C->GetActorLocation(),Origin)>25)
    {Cancel(TEXT("Movimento interrompeu a cura: suprimento preservado."));return;}
    NotifyInventoryChanged(false);if(!bHealing)return;
    if(C->GetHealth()>=100){Cancel(TEXT("Vida cheia: suprimento preservado."));return;}
    if(GetWorld()->GetTimeSeconds()>=Feedback.EndsAt)Finish();
}
void UMarau6HealingComponent::Finish()
{
    auto* C=Character();if(!C || !bHealing || bCompleting)return;
    TGuardValue<bool> Completing(bCompleting,true);
    const FGuid Id=Source;const float Before=C->GetHealth();
    // Mark inactive before publishing inventory callbacks. The ledger commits health
    // before Sync, so a callback cannot cancel a paid-for effect or reuse this use.
    bHealing=false;Feedback.EndsAt=0;Source.Invalidate();SourceSlot=INDEX_NONE;
    auto* Ledger=GetWorld()->GetSubsystem<UMarau6ItemLedger>();
    if(!Ledger || !Ledger->ConsumeForHealing(C->GetInventory(),Id))
    {Message(TEXT("Nao foi possivel confirmar a cura. Item e vida foram preservados."));return;}
    Message(FString::Printf(TEXT("Cura concluida: +%.0f de vida."),C->GetHealth()-Before));C->ForceNetUpdate();
}
void UMarau6HealingComponent::ResetForBase()
{
    if(auto* C=Character();C && C->HasAuthority()){Cancel(TEXT("Cura interrompida ao retornar a base."));Feedback.EndsAt=0;Feedback.Message.Empty();C->ForceNetUpdate();}
}
void UMarau6HealingComponent::TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function)
{Super::TickComponent(Delta,Tick,Function);UpdateHealing();}
void UMarau6HealingComponent::EndPlay(const EEndPlayReason::Type Reason)
{Cancel(TEXT("Cura interrompida ao encerrar a sessao."));Super::EndPlay(Reason);}
void UMarau6HealingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UMarau6HealingComponent,bHealing);DOREPLIFETIME_CONDITION(UMarau6HealingComponent,Feedback,COND_OwnerOnly);}
