#include "Marau6Bot.h"
#include "Marau6DestructibleSection.h"
#include "Marau6Character.h"
#include "Marau6Ragdoll.h"
#include "Marau6ShotFX.h"
#include "Marau6Persistence.h"
#include "Marau6ItemLedger.h"
#include "Marau6ShotEffect.h"
#include "Marau6Footsteps.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMarau6Bot::AMarau6Bot()
{
    bReplicates=true;SetReplicateMovement(true);PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.2f;
    AIControllerClass=AAIController::StaticClass();AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;
    GetCapsuleComponent()->InitCapsuleSize(34,96);GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);GetMesh()->SetCanEverAffectNavigation(false);GetMesh()->SetRelativeLocation(FVector(0,0,-96));GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(TEXT("/Game/Marau6/Characters/Krieg/SKM_Krieg.SKM_Krieg"));GetMesh()->SetSkeletalMesh(Body.Object);
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"));IdleAnimation=Idle.Object;
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Walk(TEXT("/Game/Characters/Mannequins/Anims/Rifle/Walk/MF_Rifle_Walk_Fwd.MF_Rifle_Walk_Fwd"));WalkAnimation=Walk.Object;
    Rifle=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BotRifle"));Rifle->SetupAttachment(GetMesh(),TEXT("HandGrip_R"));Rifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);Rifle->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Gun(TEXT("/Game/Weapons/Lasgun/Meshes/SKM_Lasgun.SKM_Lasgun"));Rifle->SetSkeletalMesh(Gun.Object);
    Nameplate=CreateDefaultSubobject<UTextRenderComponent>(TEXT("BotLabel"));Nameplate->SetupAttachment(GetRootComponent());Nameplate->SetRelativeLocation(FVector(0,0,125));Nameplate->SetText(FText::FromString(TEXT("BOT")));Nameplate->SetHorizontalAlignment(EHTA_Center);Nameplate->SetWorldSize(20);Nameplate->SetTextRenderColor(FColor::Red);Nameplate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Footsteps=CreateDefaultSubobject<UMarau6Footsteps>(TEXT("Footsteps"));
    bUseControllerRotationYaw=false;GetCharacterMovement()->bOrientRotationToMovement=true;GetCharacterMovement()->MaxWalkSpeed=250;GetCharacterMovement()->RotationRate=FRotator(0,360,0);
}
void AMarau6Bot::BeginPlay()
{
    Super::BeginPlay();Home=GetActorLocation();if(HasAuthority()){LootId=FGuid::NewGuid();if(!GetController())SpawnDefaultController();}
    if(GetNetMode()!=NM_DedicatedServer && IdleAnimation)GetMesh()->PlayAnimation(IdleAnimation,true);
}
bool AMarau6Bot::CanAct()const
{
    if(!HasAuthority() || !IsAlive() || !GetWorld() || GetWorld()->bIsTearingDown)return false;
    if(UMarau6Persistence::UsesPersistentBase(GetWorld())){auto* B=GetWorld()->GetSubsystem<UMarau6Persistence>();return B && B->CanMutate();}return true;
}
bool AMarau6Bot::CanSee(const AMarau6Character* P)const
{
    if(!CanAct() || !IsValid(P) || !P->CanParticipate())return false;
    const FVector Offset=P->GetActorLocation()-GetActorLocation();
    if(Offset.SizeSquared()>FMath::Square(2000.f) || FVector::DotProduct(GetActorForwardVector(),Offset.GetSafeNormal2D())<.5f)return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(Marau6BotSight),false,this);FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation()+FVector(0,0,64),P->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Q) || Hit.GetActor()==P;
}
void AMarau6Bot::Tick(float Delta)
{
    Super::Tick(Delta);if(HasAuthority())StepAI();
    if(GetNetMode()!=NM_DedicatedServer)if(auto* PC=GetWorld()->GetFirstPlayerController())if(PC->PlayerCameraManager)Nameplate->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Nameplate->GetComponentLocation()).Rotation());
    if(IsAlive() && GetNetMode()!=NM_DedicatedServer){const bool Walking=GetVelocity().SizeSquared2D()>100;if(Walking!=bWalkingAnimation){bWalkingAnimation=Walking;GetMesh()->PlayAnimation(Walking?WalkAnimation:IdleAnimation,true);}}
}
bool AMarau6Bot::Navigate(const FVector& Destination)
{
    auto* AI=Cast<AAIController>(GetController());auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!AI || !Nav)return false;
    FNavLocation Point;if(!Nav->ProjectPointToNavigation(Destination,Point,FVector(150,150,350)))return false;
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Point.Location,this);
    if(!Path || !Path->IsValid() || Path->IsPartial()){AI->StopMovement();return false;}
    return AI->MoveToLocation(Point.Location,65,true,true,false,false,nullptr,false)!=EPathFollowingRequestResult::Failed;
}
void AMarau6Bot::StepAI()
{
    auto* AI=Cast<AAIController>(GetController());if(!CanAct()){if(AI){AI->ClearFocus(EAIFocusPriority::Gameplay);AI->StopMovement();}return;}
    const double Now=GetWorld()->GetTimeSeconds();auto* Current=Target.Get();
    if(!IsValid(Current) || !Current->CanParticipate()){Target.Reset();Current=nullptr;}
    if(!Current || !CanSee(Current))
    {
        AMarau6Character* Closest=nullptr;double Distance=DBL_MAX;
        for(TActorIterator<AMarau6Character> It(GetWorld());It;++It)if(CanSee(*It)){const double D=FVector::DistSquared(It->GetActorLocation(),GetActorLocation());if(D<Distance){Distance=D;Closest=*It;}}
        if(Closest && Closest!=Current){Target=Closest;Current=Closest;ReactAt=Now+1;}
    }
    if(Current && CanSee(Current))
    {
        State=EMarau6BotState::Combat;LastSeen=Current->GetActorLocation();SeenAt=Now;if(AI)AI->SetFocus(Current);
        const FVector Direction=(LastSeen-GetActorLocation()).GetSafeNormal2D();if(!Direction.IsNearlyZero())SetActorRotation(Direction.Rotation());
        if(FVector::DistSquared2D(LastSeen,GetActorLocation())>FMath::Square(800.f)){if(Now>=NextPathAt){Navigate(LastSeen);NextPathAt=Now+1;}}else if(AI)AI->StopMovement();
        if(Now>=ReactAt && Now-LastShot>=.8)FireAt(Current);
    }
    else
    {
        if(AI)AI->ClearFocus(EAIFocusPriority::Gameplay);
        if(Current && Now-SeenAt<5){State=EMarau6BotState::Investigate;if(Now>=NextPathAt){Navigate(LastSeen);NextPathAt=Now+1;}}
        else{Target.Reset();State=EMarau6BotState::Patrol;if(Now>=NextPathAt){auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation Point;if(Nav && AI && AI->GetMoveStatus()!=EPathFollowingStatus::Moving && Nav->GetRandomReachablePointInRadius(Home,600,Point))Navigate(Point.Location);NextPathAt=Now+2;}}
    }
}
void AMarau6Bot::FireAt(AMarau6Character* Player)
{
    if(!CanAct() || !CanSee(Player))return;const double Now=GetWorld()->GetTimeSeconds();if(Now<ReactAt || Now-LastShot<.8)return;
    const FVector Start=GetActorLocation()+FVector(0,0,64),Aim=(Player->GetActorLocation()+FVector(0,0,40)-Start).GetSafeNormal();
    const FVector End=Start+FMath::VRandCone(Aim,FMath::DegreesToRadians(3.f))*2000;FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(Marau6BotShot),false,this);
    const bool bHit=GetWorld()->LineTraceSingleByChannel(Hit,Start,End,ECC_Pawn,Q);LastShot=Now;++ShotCount;
    auto* Victim=bHit?Cast<AMarau6Character>(Hit.GetActor()):nullptr;const bool bCharacter=Victim && Victim->CanParticipate();
    if(bCharacter || (bHit && Cast<AMarau6DestructibleSection>(Hit.GetActor()))){FDamageEvent E;Hit.GetActor()->TakeDamage(10,E,GetController(),this);}
    PresentShot(bHit?Hit.ImpactPoint:End,bHit?Hit.ImpactNormal:FVector::ZeroVector,bHit,bCharacter);ForceNetUpdate();
}
float AMarau6Bot::TakeDamage(float Damage,const FDamageEvent& Event,AController* DamageInstigator,AActor* Causer)
{
    if(bResolvingDeath || !CanAct() || !CanBeDamaged() || !FMath::IsFinite(Damage) || Damage<=0)return 0;const float Applied=FMath::Min(Damage,Health);
    if(Applied>=Health)
    {
        TGuardValue<bool> Resolving(bResolvingDeath,true);
        FHitResult Floor;FCollisionQueryParams Q(SCENE_QUERY_STAT(Marau6BotLoot),false,this);FVector Point=GetActorLocation()-FVector(0,0,70);
        if(GetWorld()->LineTraceSingleByChannel(Floor,GetActorLocation(),GetActorLocation()-FVector(0,0,500),ECC_Visibility,Q))Point=Floor.ImpactPoint+FVector(0,0,25);
        auto* Ledger=GetWorld()->GetSubsystem<UMarau6ItemLedger>();if(!Ledger || !Ledger->GrantWorld(LootId,EMarau6ItemKind::Scrap,Point))return 0;
        Health=0;State=EMarau6BotState::Dead;Target.Reset();if(auto* AI=Cast<AAIController>(GetController())){AI->ClearFocus(EAIFocusPriority::Gameplay);AI->StopMovement();}
        GetCharacterMovement()->DisableMovement();OnRep_State();SetLifeSpan(CorpseSeconds);
    }
    else Health-=Applied;
    ForceNetUpdate();Super::TakeDamage(Applied,Event,DamageInstigator,Causer);return Applied;
}
void AMarau6Bot::OnRep_State()
{if(!IsAlive()){Nameplate->SetText(FText::FromString(TEXT("BOT ELIMINADO")));GetCharacterMovement()->DisableMovement();Marau6Ragdoll::Start(this,FVector::ZeroVector);}}
void AMarau6Bot::PresentShot_Implementation(FVector_NetQuantize Point,FVector_NetQuantizeNormal Normal,bool bHit,bool bCharacter)
{
    if(GetNetMode()==NM_DedicatedServer)return;
    if(auto* Effect=GetWorld()->SpawnActor<AMarau6ShotEffect>())Effect->Present(Rifle,false,Point,Normal,bHit,bCharacter);
    if(auto* FX=GetWorld()->SpawnActor<AMarau6ShotFX>(AMarau6ShotFX::StaticClass(),FTransform(FVector(Point))))FX->Present(Rifle,false,Point,Normal,bHit,bCharacter,FVector::ZeroVector,1.f);
}
void AMarau6Bot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMarau6Bot,Health);DOREPLIFETIME(AMarau6Bot,State);DOREPLIFETIME(AMarau6Bot,LootId);DOREPLIFETIME(AMarau6Bot,ShotCount);}
void AMarau6Bot::EndPlay(const EEndPlayReason::Type Reason)
{
    if(HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown)
        if(auto* AI=Cast<AAIController>(GetController())){AI->StopMovement();AI->Destroy();}
    Super::EndPlay(Reason);
}
