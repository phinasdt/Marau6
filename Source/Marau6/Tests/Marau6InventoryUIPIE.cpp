#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Marau6PlayerController.h"
#include "Marau6InventoryWidget.h"
#include "Marau6HUDWidget.h"
#include "Marau6InventorySlot.h"
#include "Marau6Character.h"
#include "Marau6Persistence.h"
#include "Marau6InventoryComponent.h"
#include "Marau6ItemLedger.h"
#include "Marau6LootActor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "UObject/UnrealType.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
struct FInventoryPIEData
{
    FAutomationTestBase* Test;
    TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
    explicit FInventoryPIEData(FAutomationTestBase* T):Test(T),Settings(DuplicateObject(GetMutableDefault<ULevelEditorPlaySettings>(),GetTransientPackage())){}
};
TSharedPtr<SMarau6InventorySlot> FindSlot(TSharedRef<SWidget> Root,int32 Slot)
{
    if(Root->GetType()==FName(TEXT("SMarau6InventorySlot")))
    {auto S=StaticCastSharedRef<SMarau6InventorySlot>(Root);if(S->GetSlotIndex()==Slot)return S;}
    if(auto* Children=Root->GetChildren())for(int32 N=0;N<Children->Num();N++)if(auto S=FindSlot(Children->GetChildAt(N),Slot))return S;
    return nullptr;
}
class FInventoryPIEFlow : public IAutomationLatentCommand
{
    TSharedPtr<FInventoryPIEData> D;
    int32 Phase=0;double Started=FPlatformTime::Seconds(),At=Started,NextRequest=0;
    FGuid Rifle,OwnRifle2,AmmoBox;int32 AmmoPhase=0;double AmmoAt=0;FString Round;FMarau6InventoryUIRequest OldBase;
    bool Render=false,bHUDShot=false;
    void Next(){UE_LOG(LogTemp,Display,TEXT("MARAU6_UI_PIE phase %d passed"),Phase);++Phase;At=FPlatformTime::Seconds();NextRequest=0;}
    bool Move(AMarau6PlayerController* PC,int32 Source,int32 Destination)
    {
        if(!Render){FMarau6InventoryUIRequest R;return PC->CaptureInventoryDrag(Source,R) && PC->SubmitInventoryDrop(R,Destination);}
        auto* Widget=PC->GetInventoryWidget();if(!Widget)return false;
        auto From=FindSlot(Widget->TakeWidget(),Source),To=FindSlot(Widget->TakeWidget(),Destination);
        if(!From || !To)return false;auto& App=FSlateApplication::Get();
        auto Window=App.FindWidgetWindow(From.ToSharedRef());if(!Window)return false;Window->BringToFront(true);
        const auto A=From->GetCachedGeometry().LocalToAbsolute(From->GetCachedGeometry().GetLocalSize()*.5);
        const auto B=To->GetCachedGeometry().LocalToAbsolute(To->GetCachedGeometry().GetLocalSize()*.5);
        App.ClearKeyboardFocus(EFocusCause::SetDirectly);App.SetCursorPos(A);TSet<FKey> Empty,Pressed;Pressed.Add(EKeys::LeftMouseButton);
        App.ProcessMouseMoveEvent(FPointerEvent(0,A,A,Empty,EKeys::Invalid,0,FModifierKeysState()),true);
        App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,A,A,Pressed,EKeys::LeftMouseButton,0,FModifierKeysState()));
        D->Test->TestTrue(TEXT("Click focuses the inventory that received the mouse, not the other client"),App.GetKeyboardFocusedWidget()==Widget->TakeWidget());
        const FVector2D Mid=A+FVector2D(20,0);App.SetCursorPos(Mid);App.ProcessMouseMoveEvent(FPointerEvent(0,Mid,A,Pressed,EKeys::Invalid,0,FModifierKeysState()),false);
        const bool Begun=PC->HasOwnInventoryDrag();D->Test->TestTrue(TEXT("Slate mouse movement actually starts drag operation"),Begun);
        if(Begun)Screenshot(PC,TEXT("inventory-ui-drag.png"));
        App.SetCursorPos(B);App.ProcessMouseMoveEvent(FPointerEvent(0,B,Mid,Pressed,EKeys::Invalid,0,FModifierKeysState()),false);
        App.ProcessMouseButtonUpEvent(FPointerEvent(0,B,B,Empty,EKeys::LeftMouseButton,0,FModifierKeysState()));
        return Begun;
    }
    void Screenshot(AMarau6PlayerController* PC,const TCHAR* Name)
    {
        if(!Render)return;TArray<FColor> Pixels;FIntVector Size;
        const bool Captured=FSlateApplication::Get().TakeScreenshot(PC->GetInventoryWidget()->TakeWidget(),Pixels,Size);
        D->Test->TestTrue(TEXT("Rendered inventory screenshot captured"),Captured && Size.X>0 && Size.Y>0);
        if(Captured){TArray<uint8> PNG;FImageUtils::CompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectDir()/TEXT("LocalSettings")/Name));}
    }
public:
    explicit FInventoryPIEFlow(TSharedPtr<FInventoryPIEData> In):D(In){Render=FParse::Param(FCommandLine::Get(),TEXT("Marau6UIRender"));}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Now-Started>120){D->Test->AddError(FString::Printf(TEXT("UI PIE timeout phase %d"),Phase));return true;}
        UWorld* Server=nullptr;TArray<UWorld*> Clients;for(const auto& W:GEngine->GetWorldContexts())if(auto* World=W.World();World && World->WorldType==EWorldType::PIE){if(World->GetNetMode()==NM_DedicatedServer)Server=World;else if(World->GetNetMode()==NM_Client)Clients.Add(World);}
        if(!Server || Clients.Num()!=2)return false;
        auto* Bridge=Server->GetSubsystem<UMarau6Persistence>();AMarau6PlayerController* Local[2]={nullptr,nullptr};AMarau6PlayerController* Remote[2]={nullptr,nullptr};
        for(int32 N=0;N<2;N++)for(TActorIterator<AMarau6PlayerController> It(Clients[N]);It;++It)if(It->IsLocalController())Local[N]=*It;
        for(TActorIterator<AMarau6PlayerController> It(Server);It;++It)if(It->GetBaseProfile()>0 && It->GetBaseProfile()<=2)Remote[It->GetBaseProfile()-1]=*It;
        if(!Local[0] || !Local[1])return false;
        if(Phase==0)
        {
            if(!Bridge->CanMutate() || !Local[0]->HasInventoryWidget())return false;Local[0]->ServerSelectProfile(1);Local[1]->ServerSelectProfile(2);
            // The base capture uses the full-HD layout the base screen was designed at.
            if(Render){auto Window=FSlateApplication::Get().FindWidgetWindow(Local[0]->GetInventoryWidget()->TakeWidget());if(Window)Window->Resize(FVector2D(1920,1080));}
            Next();return false;
        }
        if(Phase==1)
        {
            if(Local[0]->GetBaseProfile()!=1 || Local[1]->GetBaseProfile()!=2 || !Local[0]->InventoryUIItemAt(10) || !Local[1]->InventoryUIItemAt(10) || !Local[0]->HasInventoryWidget() || Local[0]->InventoryRequestPending() || Now-At<1)return false;
            Rifle=Local[0]->InventoryUIItemAt(10)->Id;OwnRifle2=Local[1]->InventoryUIItemAt(10)->Id;Round=Bridge->GetRoundId();Screenshot(Local[0],TEXT("inventory-ui-base.png"));
            if(!Move(Local[0],10,104)){D->Test->AddError(TEXT("Base drag failed: ")+Local[0]->GetInventoryNotice()+TEXT(" context=")+Local[0]->GetInventoryContext().ToString());return true;}Next();return false;
        }
        if(!Remote[0] || !Remote[1])return false;
        auto* A=Cast<AMarau6Character>(Remote[0]->GetPawn());auto* B=Cast<AMarau6Character>(Remote[1]->GetPawn());auto* CA=Cast<AMarau6Character>(Local[0]->GetPawn());
        if(!A || !B || !CA)return false;
        if(Phase==2)
        {
            if(Local[0]->InventoryRequestPending() || !Local[0]->InventoryUIItemAt(104) || Local[0]->InventoryUIItemAt(104)->Id!=Rifle)return false;
            D->Test->TestNull(TEXT("Confirmed base source empty"),Local[0]->InventoryUIItemAt(10));if(!Move(Local[0],104,10)){D->Test->AddError(TEXT("Base return drag failed"));return true;}Next();return false;
        }
        if(Phase==3)
        {
            if(Local[0]->InventoryRequestPending() || !Local[0]->InventoryUIItemAt(10) || Local[0]->InventoryUIItemAt(10)->Id!=Rifle)return false;
            Local[0]->ServerBaseMove(100,10);Next();return false;
        }
        if(Phase==4)
        {
            if(Now-At<1 || Local[0]->InventoryRequestPending() || !Bridge->IsSettled() || !Local[0]->GetBaseMessage().Contains(TEXT("Troca")))return false;
            D->Test->TestTrue(TEXT("Backend refusal survives profile refresh"),Local[0]->GetInventoryNotice().Contains(TEXT("Troca")));
            if(Render){auto Window=FSlateApplication::Get().FindWidgetWindow(Local[0]->GetInventoryWidget()->TakeWidget());if(Window)Window->Resize(FVector2D(760,640));}
            Next();return false;
        }
        if(Phase==5)
        {
            if(Now-At<.7)return false;Screenshot(Local[0],TEXT("inventory-ui-small.png"));
            if(Render){auto Window=FSlateApplication::Get().FindWidgetWindow(Local[0]->GetInventoryWidget()->TakeWidget());if(Window)Window->Resize(FVector2D(1050,850));}
            if(!Local[0]->CaptureInventoryDrag(10,OldBase)){D->Test->AddError(TEXT("Capture old context"));return true;}OldBase.Destination=1;
            // Profile 2 takes the basic kit: a fresh rifle, its own gear stays at the base.
            Local[1]->ServerSelectKit(true);Local[0]->ServerEnterRaid();Local[1]->ServerEnterRaid();Next();return false;
        }
        if(Phase==6)
        {
            if(!Local[0]->IsInRaid() || !Local[1]->IsInRaid())
            {if(Now>=NextRequest){for(auto* PC:Local)if(!PC->IsInRaid())PC->ServerEnterRaid();NextRequest=Now+.5;}return false;}
            if(!CA->CanParticipate() || !CA->GetInventory()->ItemAt(10) || !B->GetInventory()->ItemAt(10))return false;
            if(Render && !bHUDShot)
            {
                if(!Local[0]->HasHUDWidget() || Now-At<1.5)return false;bHUDShot=true;
                Local[0]->GetHUDWidget()->TakeWidget()->SlatePrepass();
                TArray<FColor> Pixels;FIntVector Size;auto Window=FSlateApplication::Get().FindWidgetWindow(Local[0]->GetHUDWidget()->TakeWidget());
                const bool Captured=Window.IsValid() && FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size);
                D->Test->TestTrue(TEXT("Rendered raid HUD screenshot captured"),Captured && Size.X>0 && Size.Y>0);
                if(Captured){TArray<uint8> PNG;FImageUtils::CompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectDir()/TEXT("LocalSettings")/TEXT("hud-raid.png")));}
            }
            const FMarau6Item* Kit=B->GetInventory()->ItemAt(10);
            D->Test->TestTrue(TEXT("Basic kit enters with a new 20/40 rifle"),Kit->Id!=OwnRifle2 && Kit->Magazine==20 && Kit->Reserve==40 && Kit->Durability==10000);
            const FMarau6Item* Box1=B->GetInventory()->ItemAt(0);const FMarau6Item* Box2=B->GetInventory()->ItemAt(1);const FMarau6Item* Medkit=B->GetInventory()->ItemAt(2);
            D->Test->TestTrue(TEXT("Basic kit carries two ammo boxes and a medkit"),Box1 && Box1->IsAmmo() && Box2 && Box2->IsAmmo() && Medkit && Medkit->Kind==EMarau6ItemKind::Supply);
            D->Test->TestFalse(TEXT("Entry restores mouse capture"),Local[0]->bShowMouseCursor);D->Test->TestFalse(TEXT("Entry does not open stale result UI"),CA->IsInventoryOpen());
            Local[0]->ServerInventoryMoveUI(OldBase);CA->ToggleInventoryFromUI();Local[0]->UpdateInventoryUI();Next();return false;
        }
        if(Phase==7)
        {
            if(Now-At<.7)return false;D->Test->TestEqual(TEXT("Old base context cannot move raid rifle"),CA->GetInventory()->ItemAt(10)->Id,Rifle);Screenshot(Local[0],TEXT("inventory-ui-raid.png"));
            if(!Move(Local[0],10,0)){D->Test->AddError(TEXT("Raid drag failed"));return true;}Next();return false;
        }
        if(Phase==8)
        {
            if(Local[0]->InventoryRequestPending() || !CA->GetInventory()->ItemAt(0) || CA->GetInventory()->ItemAt(0)->Id!=Rifle)return false;
            FMarau6InventoryUIRequest R;if(!Local[0]->CaptureInventoryDrag(0,R))return false;
            D->Test->TestFalse(TEXT("Rifle safe drop rejected"),Local[0]->SubmitInventoryDrop(R,9));D->Test->TestTrue(TEXT("Visible safe rejection reason"),Local[0]->GetInventoryNotice().Contains(TEXT("Rifles")));
            R.Destination=1;auto Stale=R;Stale.RequestId=FGuid::NewGuid();Stale.Destination=2;Local[0]->ServerInventoryMoveUI(R);Local[0]->ServerInventoryMoveUI(Stale);Next();return false;
        }
        if(Phase==9)
        {
            if(Now-At<.7 || !CA->GetInventory()->ItemAt(1))return false;
            D->Test->TestEqual(TEXT("Only first concurrent request changes item location"),CA->GetInventory()->ItemAt(1)->Id,Rifle);D->Test->TestNull(TEXT("Stale request cannot move a different source"),CA->GetInventory()->ItemAt(2));
            if(!Move(Local[0],1,10)){D->Test->AddError(TEXT("Reequip drag failed"));return true;}Next();return false;
        }
        if(Phase==10)
        {
            if(Local[0]->InventoryRequestPending() || !CA->GetInventory()->ItemAt(10) || CA->GetInventory()->ItemAt(10)->Id!=Rifle)return false;
            if(AmmoPhase==0)
            {
                auto* Input=Cast<UEnhancedInputComponent>(CA->InputComponent);bool Bound=false;
                const auto* FireField=FindFProperty<FObjectProperty>(AMarau6Character::StaticClass(),TEXT("PrototypeFireAction"));
                auto* Action=FireField?FireField->GetObjectPropertyValue_InContainer(CA):nullptr;
                if(Input)for(const auto& Binding:Input->GetActionEventBindings())if(Binding->GetAction()==Action && Binding->GetTriggerEvent()==ETriggerEvent::Triggered)Bound=true;
                D->Test->TestTrue(TEXT("Real local player binds continuous fire"),Bound);
                CA->ToggleInventoryFromUI();Local[0]->UpdateInventoryUI();Local[0]->SetControlRotation(FRotator(75,90,0));Remote[0]->SetControlRotation(FRotator(75,90,0));
                AmmoPhase=-2;AmmoAt=Now;return false;
            }
            if(AmmoPhase==-2)
            {
                if(A->GetMagazineAmmo()<=16){AmmoPhase=-1;AmmoAt=Now;return false;}
                const auto* Field=FindFProperty<FObjectProperty>(AMarau6Character::StaticClass(),TEXT("PrototypeFireAction"));
                auto* Action=Cast<UInputAction>(Field->GetObjectPropertyValue_InContainer(CA));
                if(auto* Subsystem=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Local[0]->GetLocalPlayer()))Subsystem->InjectInputForAction(Action,FInputActionValue(true),{}, {});
                return false;
            }
            if(AmmoPhase==-1)
            {
                if(Now-AmmoAt<.7 || !Bridge->IsSettled())return false;
                D->Test->TestEqual(TEXT("Held input fires four rounds and release stops"),A->GetMagazineAmmo(),16);D->Test->TestEqual(TEXT("Automatic four shots cost4 percent"),A->GetRifleDurability(),9600);
                CA->ToggleInventoryFromUI();Local[0]->UpdateInventoryUI();AmmoPhase=3;
            }
            if(AmmoPhase==3)
            {
                AMarau6LootActor* Box=nullptr;for(TActorIterator<AMarau6LootActor> It(Server);It;++It)if(It->GetItem().IsAmmo()){Box=*It;break;}
                if(!Box){D->Test->AddError(TEXT("Arena must generate ammunition loot"));return true;}
                AmmoBox=Box->GetItem().Id;A->SetActorLocation(Box->GetActorLocation()+FVector(-120,0,71));A->GetCharacterMovement()->StopMovementImmediately();
                AmmoAt=Now;AmmoPhase=1;return false;
            }
            if(AmmoPhase==1)
            {
                const auto* Owned=CA->GetInventory()->GetSnapshot().Items.FindByPredicate([this](const auto& I){return I.Id==AmmoBox;});
                if(!Owned)
                {
                    if(Now-AmmoAt<.5 || Now<NextRequest)return false;
                    for(TActorIterator<AMarau6LootActor> It(Clients[0]);It;++It)if(It->GetItem().Id==AmmoBox)
                    {auto Aim=(It->GetActorLocation()-CA->GetPawnViewLocation()).Rotation();Local[0]->SetControlRotation(Aim);Remote[0]->SetControlRotation(Aim);CA->GetInventory()->ServerPickup(*It,AmmoBox);}
                    NextRequest=Now+.5;return false;
                }
                Screenshot(Local[0],TEXT("inventory-ui-ammo.png"));
                if(!Move(Local[0],Owned->Slot,10)){D->Test->AddError(TEXT("Ammo drag failed"));return true;}AmmoPhase=2;return false;
            }
            if(Local[0]->InventoryRequestPending() || CA->GetReserveAmmo()!=60 || !Bridge->IsSettled())return false;
            D->Test->TestFalse(TEXT("Consumed box disappears on owning client"),CA->GetInventory()->GetSnapshot().Items.ContainsByPredicate([this](const auto& I){return I.Id==AmmoBox;}));
            B->TakeDamage(100,FDamageEvent(),Remote[0],A);A->SetActorLocation(FVector(1400,0,96));A->GetCharacterMovement()->StopMovementImmediately();Next();return false;
        }
        if(Phase==11)
        {
            if(Remote[0]->IsInRaid() || Remote[1]->IsInRaid() || !Bridge->IsSettled() || !Local[0]->InventoryUIItemAt(10) || !Local[1]->InventoryUIItemAt(10))return false;
            // Still on the basic kit: the panel shows the locked kit preview, the profile keeps its own rifle.
            D->Test->TestTrue(TEXT("Basic kit death keeps the base rifle"),Local[1]->GetBaseJson().Contains(OwnRifle2.ToString(EGuidFormats::DigitsWithHyphensLower)));
            D->Test->TestTrue(TEXT("Basic kit equipment stays locked at the base"),Local[1]->IsKitLocked(10) && Local[1]->InventoryUIItemAt(10)->Id!=OwnRifle2);
            D->Test->TestEqual(TEXT("Extracted rifle reserve retains box rounds"),Local[0]->InventoryUIItemAt(10)->Reserve,60);D->Test->TestEqual(TEXT("Extracted rifle in base retains identity"),Local[0]->InventoryUIItemAt(10)->Id,Rifle);Local[0]->ServerNextRound();Next();return false;
        }
        if(Phase==12)
        {
            if(Bridge->GetRoundId()==Round || !Bridge->CanMutate())return false;
            Local[0]->ServerEnterRaid();Next();return false;
        }
        if(Phase==13)
        {
            if(!Local[0]->IsInRaid() || !CA->CanParticipate() || !CA->GetInventory()->ItemAt(10))return false;
            D->Test->TestFalse(TEXT("Second raid has no leftover open inventory"),CA->IsInventoryOpen());D->Test->TestFalse(TEXT("Second raid restores mouse capture"),Local[0]->bShowMouseCursor);
            A->SetActorLocation(FVector(1400,0,96));A->GetCharacterMovement()->StopMovementImmediately();Next();return false;
        }
        if(Phase==14)
        {
            if(Remote[0]->IsInRaid() || !Bridge->IsSettled() || !Local[0]->InventoryUIItemAt(10))return false;
            auto O=MakeShared<FJsonObject>();FString Run;FParse::Value(FCommandLine::Get(),TEXT("Marau6IntegrationRun="),Run);O->SetStringField(TEXT("run"),Run);O->SetStringField(TEXT("item"),Rifle.ToString(EGuidFormats::DigitsWithHyphensLower));
            FFileHelper::SaveStringToFile(UMarau6Persistence::Encode(O),*(FPaths::ProjectDir()/TEXT("LocalSettings/inventory-ui-result.json")));return true;
        }
        return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6InventoryUIPIE,"EditorLifecycle.InventoryUIOnlineAndTeardown",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6InventoryUIPIE::RunTest(const FString&)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("Marau6Integration")) || !FParse::Param(FCommandLine::Get(),TEXT("Marau6InventoryUITest"))){AddError(TEXT("Requires isolated launcher -PIE -InventoryUI"));return false;}
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/FirstPerson/Lvl_FirstPerson"));auto D=MakeShared<FInventoryPIEData>(this);
    D->Settings->SetPlayNetMode(PIE_Client);D->Settings->SetPlayNumberOfClients(2);D->Settings->SetRunUnderOneProcess(true);D->Settings->NewWindowWidth=1050;D->Settings->NewWindowHeight=850;
    FRequestPlaySessionParams P;P.EditorPlaySettings=D->Settings.Get();P.bAllowOnlineSubsystem=false;
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIEForAutomationCommand(P));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4));ADD_LATENT_AUTOMATION_COMMAND(FInventoryPIEFlow(D));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2));return true;
}
#endif
