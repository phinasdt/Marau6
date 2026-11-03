#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Marau6Character.h"
#include "Marau6PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Marau6InventoryComponent.h"
#include "Marau6ItemLedger.h"
#include "Marau6LootActor.h"
#include "Marau6ExtractionZone.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "UObject/GarbageCollection.h"

namespace
{
struct FInventoryWorld
{
    UWorld* World;
    UMarau6ItemLedger* Ledger;
    UClass* PawnClass;
    FInventoryWorld()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        Ledger=World->GetSubsystem<UMarau6ItemLedger>();
        PawnClass=LoadClass<AMarau6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
        ++GFrameCounter;World->GetTimerManager().Tick(0.f);
    }
    ~FInventoryWorld()
    {
        GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
        if(World->IsRooted()) World->RemoveFromRoot();
    }
    AMarau6Character* Pawn(FVector Location)
    {
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* P=World->SpawnActor<AMarau6Character>(PawnClass,Location,FRotator::ZeroRotator,Spawn);
        auto* PC=World->SpawnActor<APlayerController>();PC->Possess(P);PC->SetControlRotation(FRotator(0,180,0));
        P->GetCharacterMovement()->SetMovementMode(MOVE_Flying);P->DispatchBeginPlay();return P;
    }
    AMarau6LootActor* Loot(EMarau6ItemKind Kind,FVector Location)
    {
        auto* Actor=World->SpawnActor<AMarau6LootActor>(AMarau6LootActor::StaticClass(),Location,FRotator::ZeroRotator);
        Ledger->RegisterSeed(Actor,Kind);Actor->DispatchBeginPlay();return Actor;
    }
    bool Pickup(UMarau6InventoryComponent* C,AMarau6LootActor* Actor,FGuid Expected)
    {
        if(IsValid(Actor)) C->Character()->GetController()->SetControlRotation((Actor->GetActorLocation()-C->Character()->GetPawnViewLocation()).Rotation());
        return Ledger->Pickup(C,Actor,Expected);
    }
    void Advance(float Seconds)
    {
        while(Seconds>KINDA_SMALL_NUMBER)
        {const float Step=FMath::Min(.1f,Seconds);++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,Step);World->GetTimerManager().Tick(Step);Seconds-=Step;}
    }
    AMarau6LootActor* Find(FGuid Id)
    {
        for(TActorIterator<AMarau6LootActor> It(World);It;++It) if(It->GetItem().Id==Id) return *It;return nullptr;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6InventoryTest,"Marau6.Inventory.Transfers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6InventoryTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;
    if(!TestNotNull(TEXT("Authoritative ledger"),F.Ledger)) return false;
    auto* A=F.Pawn(FVector(0,0,96));auto* B=F.Pawn(FVector(0,300,96));
    auto* IA=A->GetInventory();auto* IB=B->GetInventory();
    TestEqual(TEXT("One starting rifle per player"),F.Ledger->GetItemCount(),2);
    if(!TestNotNull(TEXT("Starting rifle"),IA->ItemAt(10))) return false;
    const FGuid RifleA=IA->ItemAt(10)->Id;
    TestTrue(TEXT("Starting rifles are individually identified"),RifleA!=IB->ItemAt(10)->Id);
    auto* Loot=F.Loot(EMarau6ItemKind::Supply,FVector(120,0,96));const FGuid Id=Loot->GetItem().Id;
    TestTrue(TEXT("Nearby supply picked up"),F.Pickup(IA,Loot,Id));
    TestFalse(TEXT("Simultaneous second player cannot duplicate"),F.Pickup(IB,Loot,Id));
    TestFalse(TEXT("Repeated pickup rejected"),F.Pickup(IA,Loot,Id));
    TestTrue(TEXT("Identity retained after pickup"),IA->ItemAt(0) && IA->ItemAt(0)->Id==Id);
    TestEqual(TEXT("Transfers do not create items"),F.Ledger->GetItemCount(),3);
    TestFalse(TEXT("Other player's item cannot be moved"),F.Ledger->Move(IB,Id,8));
    TestTrue(TEXT("Small item moves into safe pocket"),F.Ledger->Move(IA,Id,8));
    TestFalse(TEXT("Rifle cannot enter safe pocket"),F.Ledger->Move(IA,RifleA,9));
    TestFalse(TEXT("Invalid destination rejected"),F.Ledger->Move(IA,Id,500));
    for(int32 I=0;I<8;++I)
    {
        auto* Item=F.Loot(EMarau6ItemKind::Scrap,FVector(120,0,96));
        TestTrue(TEXT("Backpack slot fills"),F.Pickup(IA,Item,Item->GetItem().Id));
    }
    auto* Extra=F.Loot(EMarau6ItemKind::Supply,FVector(120,0,96));const FGuid ExtraId=Extra->GetItem().Id;
    const int32 Count=F.Ledger->GetItemCount();
    TestFalse(TEXT("Full backpack rejects pickup"),F.Pickup(IA,Extra,ExtraId));
    TestTrue(TEXT("Rejected loot remains available"),IsValid(Extra) && !Extra->IsActorBeingDestroyed());
    TestEqual(TEXT("Full pickup cannot create or lose items"),F.Ledger->GetItemCount(),Count);
    auto* Far=F.Loot(EMarau6ItemKind::Supply,FVector(1500,300,96));
    TestFalse(TEXT("Out-of-range pickup rejected"),F.Pickup(IB,Far,Far->GetItem().Id));
    auto* Behind=F.Loot(EMarau6ItemKind::Supply,FVector(120,300,96));
    auto* Wall=F.World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5,80,100));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Wall->SetActorLocation(FVector(60,300,120));
    TestFalse(TEXT("Wall blocks pickup"),F.Pickup(IB,Behind,Behind->GetItem().Id));
    Wall->Destroy();
    B->GetController()->SetControlRotation(FRotator(0,180,0));
    TestFalse(TEXT("Server rejects loot behind the aim direction"),F.Ledger->Pickup(IB,Behind,Behind->GetItem().Id));
    TestFalse(TEXT("Forged identity rejected"),F.Pickup(IB,Behind,FGuid::NewGuid()));
    TestTrue(TEXT("Removing wall permits pickup"),F.Pickup(IB,Behind,Behind->GetItem().Id));
    const FGuid Stale=IA->ItemAt(0)->Id;
    TestTrue(TEXT("Drop succeeds without minting a new identity"),F.Ledger->Drop(IA,Stale));
    TestTrue(TEXT("Dropped actor keeps identity"),F.Find(Stale)!=nullptr);
    TestFalse(TEXT("Stale ID drop cannot affect replacement slot"),F.Ledger->Drop(IA,Stale));
    A->SetActorLocation(FVector(230,0,96));
    TestTrue(TEXT("Empty slot accepts previously rejected loot"),F.Pickup(IA,Extra,ExtraId));
    TestTrue(TEXT("Slot now contains different identity"),IA->ItemAt(0) && IA->ItemAt(0)->Id==ExtraId);
    TestFalse(TEXT("Stale selection cannot drop replacement item"),F.Ledger->Drop(IA,Stale));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6InventoryWeaponsTest,"Marau6.Inventory.WeaponsAndResults",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6InventoryWeaponsTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;auto* A=F.Pawn(FVector(0,0,96));auto* B=F.Pawn(FVector(0,170,96));
    auto* IA=A->GetInventory();auto* IB=B->GetInventory();const FGuid Original=IA->ItemAt(10)->Id;
    auto Shoot=[&](){A->ProcessEvent(A->FindFunctionChecked(TEXT("ServerFire")),nullptr);};
    Shoot();F.Advance(.3f);Shoot();
    TestEqual(TEXT("Accepted shots update item magazine"),IA->ItemAt(10)->Magazine,18);
    auto* Rifle=F.Loot(EMarau6ItemKind::Rifle,FVector(100,0,96));const FGuid Second=Rifle->GetItem().Id;
    TestTrue(TEXT("Second rifle goes to backpack"),F.Pickup(IA,Rifle,Second));
    A->ProcessEvent(A->FindFunctionChecked(TEXT("ServerReload")),nullptr);
    TestTrue(TEXT("Reload active before weapon swap"),A->IsReloading());
    TestTrue(TEXT("Equip swaps weapon and backpack slot"),F.Ledger->Move(IA,Second,10));
    TestFalse(TEXT("Swap cancels pending reload"),A->IsReloading());F.Advance(3);
    TestTrue(TEXT("Previous rifle occupies selected backpack slot"),IA->ItemAt(0) && IA->ItemAt(0)->Id==Original);
    TestEqual(TEXT("Previous rifle does not reload after swap"),IA->ItemAt(0)->Magazine,18);
    TestEqual(TEXT("Previous reserve preserved"),IA->ItemAt(0)->Reserve,40);
    TestEqual(TEXT("New rifle has its own magazine"),A->GetMagazineAmmo(),20);
    TestTrue(TEXT("Drop previous rifle"),F.Ledger->Drop(IA,Original));
    auto* Dropped=F.Find(Original);
    if(!TestNotNull(TEXT("Dropped rifle exists"),Dropped)) return false;
    TestEqual(TEXT("Dropped rifle remembers spent ammunition"),Dropped->GetItem().Magazine,18);
    TestEqual(TEXT("Dropped rifle carries wear"),Dropped->GetItem().Durability,9800);
    B->SetActorLocation(Dropped->GetActorLocation()+FVector(0,90,0));
    TestTrue(TEXT("Other player can take rifle"),F.Pickup(IB,Dropped,Original));
    TestTrue(TEXT("Other player equips same rifle"),F.Ledger->Move(IB,Original,10));
    TestEqual(TEXT("Rifle keeps magazine through change of owner"),B->GetMagazineAmmo(),18);
    TestEqual(TEXT("Rifle keeps reserve through change of owner"),B->GetReserveAmmo(),40);
    TestEqual(TEXT("New owner retains rifle wear"),B->GetRifleDurability(),9800);
    auto* Safe=F.Loot(EMarau6ItemKind::Supply,A->GetActorLocation()+FVector(100,0,0));const FGuid SafeId=Safe->GetItem().Id;
    TestTrue(TEXT("Pick safe item"),F.Pickup(IA,Safe,SafeId));TestTrue(TEXT("Protect safe item"),F.Ledger->Move(IA,SafeId,8));
    auto* Cargo=F.Loot(EMarau6ItemKind::Scrap,A->GetActorLocation()+FVector(100,0,0));const FGuid CargoId=Cargo->GetItem().Id;
    TestTrue(TEXT("Pick ordinary cargo"),F.Pickup(IA,Cargo,CargoId));
    const int32 Before=F.Ledger->GetItemCount();const FGuid OwnerA=IA->GetSessionInventoryId();
    FDamageEvent Damage;A->TakeDamage(100,Damage,B->GetController(),B);
    TestTrue(TEXT("Death finalizes inventory"),IA->GetSnapshot().bFinalized);
    TestEqual(TEXT("Only safe item stays in result"),IA->GetSnapshot().Items.Num(),1);
    TestTrue(TEXT("Protected identity retained"),IA->ItemAt(8) && IA->ItemAt(8)->Id==SafeId);
    TestTrue(TEXT("Backpack and equipped rifle become loot"),F.Find(CargoId) && F.Find(Second));
    TestFalse(TEXT("No live equipped rifle after death"),IA->HasRifle());
    TestFalse(TEXT("Dead player cannot drop protected result"),F.Ledger->Drop(IA,SafeId));
    F.Ledger->Finalize(IA,false);TestEqual(TEXT("Repeated death creates no identities"),F.Ledger->GetItemCount(),Before);
    A->Destroy();CollectGarbage(RF_NoFlags);TestEqual(TEXT("Result custody survives pawn removal"),F.Ledger->GetHeldResult(OwnerA).Num(),1);
    B->SetActorLocation(FVector(1000,1000,96));
    auto* Zone=F.World->SpawnActor<AMarau6ExtractionZone>(AMarau6ExtractionZone::StaticClass(),FVector(1000,1000,0),FRotator::ZeroRotator);
    B->Tick(0);F.Advance(5.1f);B->Tick(0);
    TestEqual(TEXT("Extraction completed with inventory"),B->GetRaidState(),EMarau6RaidState::Extracted);
    const FGuid OwnerB=IB->GetSessionInventoryId();const int32 Kept=IB->GetSnapshot().Items.Num();
    TestEqual(TEXT("Extraction keeps both rifles"),Kept,2);
    TestFalse(TEXT("Extracted result cannot be transferred"),F.Ledger->Drop(IB,Original));
    B->Destroy();TestEqual(TEXT("Extraction result survives pawn removal"),F.Ledger->GetHeldResult(OwnerB).Num(),Kept);
    TestEqual(TEXT("Death and extraction conserve identity count"),F.Ledger->GetItemCount(),Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6InventoryRecoveryTest,"Marau6.Inventory.RecoveryAndUnarmed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6InventoryRecoveryTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;auto* A=F.Pawn(FVector(0,0,96));auto* I=A->GetInventory();const FGuid Rifle=I->ItemAt(10)->Id;
    TestTrue(TEXT("Equipped weapon can be dropped"),F.Ledger->Drop(I,Rifle));
    TestFalse(TEXT("Unarmed presentation replicated state"),I->HasRifle());
    A->ProcessEvent(A->FindFunctionChecked(TEXT("ServerFire")),nullptr);A->ProcessEvent(A->FindFunctionChecked(TEXT("ServerReload")),nullptr);
    TestFalse(TEXT("Unarmed cannot reload"),A->IsReloading());TestEqual(TEXT("Unarmed does not create ammo"),A->GetMagazineAmmo(),0);
    auto* Loot=F.Find(Rifle);A->SetActorLocation(Loot->GetActorLocation()+FVector(0,90,0));
    TestTrue(TEXT("Picking rifle while unarmed auto equips"),F.Pickup(I,Loot,Rifle));
    TestEqual(TEXT("Original ammunition restored from item"),A->GetMagazineAmmo(),20);
    TestEqual(TEXT("Ordinary pickup leaves no orphan custody"),F.Ledger->GetRecoveryCount(),0);
    auto* Orphan=F.Loot(EMarau6ItemKind::Scrap,FVector(500,0,96));const int32 Count=F.Ledger->GetItemCount();Orphan->Destroy();
    TestEqual(TEXT("Unexpected loot removal preserves recovery custody"),F.Ledger->GetRecoveryCount(),1);
    TestEqual(TEXT("Unexpected removal never erases identity"),F.Ledger->GetItemCount(),Count);
    F.World->bIsTearingDown=true;I->FinalizeRaid(false);F.World->bIsTearingDown=false;
    TestEqual(TEXT("Failed death loot spawn preserves rifle in recovery"),F.Ledger->GetRecoveryCount(),2);
    A->Destroy();TestEqual(TEXT("Recovery survives pawn destruction"),F.Ledger->GetRecoveryCount(),2);
    F.Ledger->Tick(1.1f);
    TestEqual(TEXT("Recovery retries restore available loot"),F.Ledger->GetRecoveryCount(),0);
    TestTrue(TEXT("Recovered rifle keeps its identity"),F.Find(Rifle)!=nullptr);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6BackpackReloadTest,"Marau6.Inventory.ReloadFromBackpack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6BackpackReloadTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;auto* A=F.Pawn(FVector(0,0,96));auto* I=A->GetInventory();
    FMarau6Item Rifle;Rifle.Id=FGuid::NewGuid();Rifle.Kind=EMarau6ItemKind::Rifle;Rifle.Slot=Marau6Slots::Weapon;Rifle.Magazine=10;Rifle.Reserve=0;
    FMarau6Item Box;Box.Id=FGuid::NewGuid();Box.Kind=EMarau6ItemKind::Ammo;Box.Slot=3;
    if(!TestTrue(TEXT("Rifle 10/0 and one box in the backpack"),F.Ledger->Import(I,FGuid::NewGuid(),{Rifle,Box})))return false;
    A->RefreshEquippedAmmo();
    A->ProcessEvent(A->FindFunctionChecked(TEXT("ServerReload")),nullptr);
    TestTrue(TEXT("R reloads with an empty reserve when the backpack has ammo"),A->IsReloading());
    TestNull(TEXT("The box is opened into the reserve"),I->ItemAt(3));
    F.Advance(2.2f);
    TestEqual(TEXT("Magazine filled from the box"),A->GetMagazineAmmo(),20);
    TestEqual(TEXT("Rest of the box stays in reserve"),A->GetReserveAmmo(),10);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6ReentrantInventoryTest,"Marau6.Inventory.ReentrantDrop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6ReentrantInventoryTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;auto* A=F.Pawn(FVector(0,0,96));auto* I=A->GetInventory();const FGuid Id=I->ItemAt(10)->Id;
    bool bInside=false,bReentered=false;
    const FDelegateHandle Handler=F.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
    {
        if(!bInside && Actor->IsA<AMarau6LootActor>())
        {
            bInside=true;bReentered=F.Ledger->Drop(I,Id);
            for(int32 N=0;N<16;++N) F.Loot(EMarau6ItemKind::Scrap,FVector(2000,N*100,96));
        }
    }));
    TestTrue(TEXT("Outer transfer succeeds"),F.Ledger->Drop(I,Id));
    F.World->RemoveOnActorSpawnedHandler(Handler);
    int32 Copies=0;for(TActorIterator<AMarau6LootActor> It(F.World);It;++It) if(It->GetItem().Id==Id) ++Copies;
    TestFalse(TEXT("Spawn callback cannot transfer reserved identity again"),bReentered);
    TestEqual(TEXT("Exactly one world representation per identity"),Copies,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6FinalizationCallbackTest,"Marau6.Inventory.FinalizationCallbacks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6FinalizationCallbackTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;auto* A=F.Pawn(FVector(0,0,96));auto* I=A->GetInventory();
    auto* Safe=F.Loot(EMarau6ItemKind::Supply,FVector(120,0,96));const FGuid SafeId=Safe->GetItem().Id;
    TestTrue(TEXT("Prepare safe item"),F.Pickup(I,Safe,SafeId));TestTrue(TEXT("Protect before death"),F.Ledger->Move(I,SafeId,8));
    auto* Bag=F.Loot(EMarau6ItemKind::Scrap,FVector(120,0,96));const FGuid BagId=Bag->GetItem().Id;
    TestTrue(TEXT("Prepare ordinary cargo"),F.Pickup(I,Bag,BagId));
    const int32 Before=F.Ledger->GetItemCount();bool bExpanded=false;
    const auto Handler=F.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
    {
        if(!bExpanded && Actor->IsA<AMarau6LootActor>())
        {
            bExpanded=true;
            TestTrue(TEXT("Transferred identity assigned before spawn callbacks"),CastChecked<AMarau6LootActor>(Actor)->GetItem().Id.IsValid());
            for(int32 N=0;N<32;++N) F.Loot(EMarau6ItemKind::Scrap,FVector(3000,N*100,96));
        }
    }));
    FDamageEvent Damage;A->TakeDamage(100,Damage,nullptr,nullptr);F.World->RemoveOnActorSpawnedHandler(Handler);
    TestTrue(TEXT("Callback exercised ledger growth"),bExpanded);
    TestEqual(TEXT("New seeds do not corrupt finalization"),F.Ledger->GetItemCount(),Before+32);
    TestEqual(TEXT("Safe result survives reentrant ledger allocation"),I->GetSnapshot().Items.Num(),1);
    TestTrue(TEXT("Ordinary cargo still becomes loot"),F.Find(BagId)!=nullptr);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6RollbackReservationTest,"Marau6.Inventory.RollbackReservation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6RollbackReservationTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;auto* A=F.Pawn(FVector(0,0,96));auto* I=A->GetInventory();
    for(int32 N=0;N<2;++N){auto* Loot=F.Loot(EMarau6ItemKind::Supply,FVector(120,0,96));TestTrue(TEXT("Prepare rollback cargo"),F.Pickup(I,Loot,Loot->GetItem().Id));}
    const FGuid First=I->ItemAt(0)->Id,Second=I->ItemAt(1)->Id;
    bool bInside=false,bMoved=false;
    const auto Handler=F.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
    {
        if(!bInside && Actor->IsA<AMarau6LootActor>())
        {bInside=true;bMoved=F.Ledger->Move(I,Second,0);Actor->Destroy();}
    }));
    const bool bDropped=F.Ledger->Drop(I,First);F.World->RemoveOnActorSpawnedHandler(Handler);
    TestTrue(TEXT("Failure callback exercised"),bInside);
    TestFalse(TEXT("Reserved source slot cannot receive another item"),bMoved);
    TSet<int32> Slots;bool bUnique=true;
    for(const auto& Item:I->GetSnapshot().Items){if(Slots.Contains(Item.Slot)) bUnique=false;Slots.Add(Item.Slot);}
    TestTrue(TEXT("Rollback cannot produce overlapping inventory slots"),bUnique);
    if(!bDropped) TestTrue(TEXT("Failed drop restores source item"),I->ItemAt(0) && I->ItemAt(0)->Id==First);
    TestEqual(TEXT("Rollback conserves all identities"),F.Ledger->GetItemCount(),3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6InventoryReorderTest,"Marau6.Inventory.RaidReorderInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6InventoryReorderTest::RunTest(const FString& Parameters)
{
    FInventoryWorld F;
    auto* A=F.Pawn(FVector(0,0,96));
    auto* PCClass=LoadClass<AMarau6PlayerController>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonPlayerController.BP_FirstPersonPlayerController_C"));
    auto* PC=F.World->SpawnActor<AMarau6PlayerController>(PCClass);PC->Possess(A);
    PC->Player=NewObject<ULocalPlayer>(GEngine);
    PC->bUseBase=true;PC->bInRaid=true;
    TestTrue(TEXT("Input fixture owns a local player"),A->IsLocallyControlled());
    auto* I=A->GetInventory();
    auto* Loot=F.Loot(EMarau6ItemKind::Supply,FVector(120,0,96));const FGuid Id=Loot->GetItem().Id;
    TestTrue(TEXT("Pickup fixture"),F.Pickup(I,Loot,Id));
    A->bInventoryOpen=true;A->SelectedInventorySlot=0;PC->BaseAccept();
    A->SelectedInventorySlot=3;PC->BaseAccept();
    TestTrue(TEXT("Enter moves selected item to chosen empty raid slot"),I->ItemAt(3) && I->ItemAt(3)->Id==Id);
    TestNull(TEXT("Source becomes empty"),I->ItemAt(0));
    TestEqual(TEXT("Reorder preserves item count"),F.Ledger->GetItemCount(),2);
    A->SelectedInventorySlot=10;PC->BaseAccept();A->SelectedInventorySlot=3;PC->BaseAccept();
    TestTrue(TEXT("Incompatible swap leaves supply in backpack"),I->ItemAt(3) && I->ItemAt(3)->Id==Id);
    A->SelectedInventorySlot=3;PC->BaseAccept();A->SelectedInventorySlot=8;PC->BaseAccept();
    TestTrue(TEXT("Enter moves supplies into safe pocket"),I->ItemAt(8) && I->ItemAt(8)->Id==Id);
    A->SelectedInventorySlot=10;PC->BaseAccept();A->SelectedInventorySlot=8;PC->BaseAccept();
    TestTrue(TEXT("Rifle cannot replace safe item"),I->ItemAt(8) && I->ItemAt(8)->Id==Id);
    auto* OtherLoot=F.Loot(EMarau6ItemKind::Scrap,FVector(120,0,96));const FGuid OtherId=OtherLoot->GetItem().Id;
    TestTrue(TEXT("Second pickup"),F.Pickup(I,OtherLoot,OtherId));
    A->SelectedInventorySlot=0;PC->BaseAccept();A->SelectedInventorySlot=8;PC->BaseAccept();
    TestTrue(TEXT("Occupied slots swap without losing identity"),I->ItemAt(0) && I->ItemAt(0)->Id==Id && I->ItemAt(8) && I->ItemAt(8)->Id==OtherId);
    A->SelectedInventorySlot=0;PC->BaseAccept();A->ToggleInventory();A->ToggleInventory();
    A->SelectedInventorySlot=2;PC->BaseAccept();A->SelectedInventorySlot=4;PC->BaseAccept();
    TestTrue(TEXT("Closing inventory cancels origin and empty slots do not select"),I->ItemAt(0) && I->ItemAt(0)->Id==Id);
    A->SelectedInventorySlot=0;PC->BaseAccept();PC->BaseAccept();
    TestFalse(TEXT("Same slot cancels selection"),A->GetInventoryMoveSource().IsValid());
    A->bInventoryOpen=false;PC->BaseAccept();
    TestFalse(TEXT("Closed inventory ignores Enter"),A->GetInventoryMoveSource().IsValid());
    A->bInventoryOpen=true;A->SelectedInventorySlot=0;PC->BaseAccept();
    TestTrue(TEXT("Selected source can leave inventory before confirmation"),F.Ledger->Drop(I,Id));
    auto* Replacement=F.Loot(EMarau6ItemKind::Supply,FVector(0,120,96));const FGuid ReplacementId=Replacement->GetItem().Id;
    TestTrue(TEXT("Replacement occupies old source slot"),F.Pickup(I,Replacement,ReplacementId));
    A->SelectedInventorySlot=1;PC->BaseAccept();
    TestTrue(TEXT("Stale origin cannot move replacement item"),I->ItemAt(0) && I->ItemAt(0)->Id==ReplacementId && !I->ItemAt(1));
    A->SelectedInventorySlot=0;A->RaidStatus.State=EMarau6RaidState::Extracted;PC->BaseAccept();
    TestFalse(TEXT("Extracted player cannot select a move"),A->GetInventoryMoveSource().IsValid());
    A->RaidStatus.State=EMarau6RaidState::Active;
    A->bInventoryOpen=true;A->Health=0;PC->BaseAccept();
    TestFalse(TEXT("Eliminated player cannot select a move"),A->GetInventoryMoveSource().IsValid());
    TestEqual(TEXT("All input operations conserve unique items"),F.Ledger->GetItemCount(),4);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarau6UIContractTest,"Marau6.Inventory.UIContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarau6UIContractTest::RunTest(const FString&)
{
    const auto* RPC=AMarau6PlayerController::StaticClass()->FindFunctionByName(TEXT("ServerInventoryMoveUI"));
    if(!TestNotNull(TEXT("Correlated authoritative inventory UI request exists"),RPC))return false;
    TestTrue(TEXT("UI move is reliable server RPC"),RPC->HasAllFunctionFlags(FUNC_Net|FUNC_NetServer|FUNC_NetReliable));
    TestNotNull(TEXT("Native inventory user widget registered"),FindObject<UClass>(nullptr,TEXT("/Script/Marau6.Marau6InventoryWidget")));
    return true;
}
#endif
