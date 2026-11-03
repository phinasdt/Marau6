#if WITH_EDITOR
#include "Marau6Character.h"
#include "Marau6PlayerController.h"
#include "Marau6ShotFX.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
namespace
{
// Run on a map outside the persistent base, where the character starts armed.
// Saves the owner's view at several pitches, then aiming and sprinting, to
// LocalSettings/viewmodel_*.png. Development inspection only.
void CaptureViewModel(UWorld* World)
{
    if(!World || !World->IsGameWorld()) return;
    TWeakObjectPtr<AMarau6PlayerController> WeakPC(World->GetFirstPlayerController<AMarau6PlayerController>());
    if(!WeakPC.IsValid()) return;
    // LocalSettings/viewmodel_shots.txt may replace the list: one "name;pitch;command|command;delay" per line.
    struct FShot{FString Name;float Pitch=0;FString Command;float Delay=.8f;};
    TArray<FShot> Shots;
    TArray<FString> Lines;
    if(FFileHelper::LoadFileToStringArray(Lines,*(FPaths::ProjectDir()/TEXT("LocalSettings/viewmodel_shots.txt"))))
        for(const FString& Line:Lines)
        {
            TArray<FString> P;Line.ParseIntoArray(P,TEXT(";"),false);
            if(P.Num()>=2)Shots.Add({P[0].TrimStartAndEnd(),(float)FCString::Atod(*P[1]),P.Num()>2?P[2].TrimStartAndEnd():FString(),
                P.Num()>3?(float)FCString::Atod(*P[3]):.8f});
        }
    if(Shots.IsEmpty())
        Shots={{TEXT("+00"),0},{TEXT("+35"),35},{TEXT("-35"),-35},{TEXT("+75"),75},{TEXT("-65"),-65},
            {TEXT("aim"),0,TEXT("Marau6.ViewModelPose 1")},{TEXT("aim+20"),20,TEXT("Marau6.ViewModelPose 1")},
            {TEXT("sprint"),0,TEXT("Marau6.ViewModelPose 2")}};
    struct FState{int32 Phase=0;int32 Shot=0;double At=0;};
    TSharedRef<FState> S=MakeShared<FState>();
    S->At=FPlatformTime::Seconds();
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakPC,S,Shots](float)
    {
        AMarau6PlayerController* PC=WeakPC.Get();
        const double Now=FPlatformTime::Seconds();
        if(!PC || Now-S->At>60){UE_LOG(LogTemp,Error,TEXT("MARAU6_VIEWMODEL aborted phase=%d"),S->Phase);return false;}
        auto* C=Cast<AMarau6Character>(PC->GetPawn());
        if(S->Phase==0)
        {
            if(!C || !C->HasEquippedRifle())return true;
            PC->SetIgnoreLookInput(true);PC->SetIgnoreMoveInput(true);S->Phase=3;S->At=Now;return true;
        }
        if(S->Shot>=Shots.Num())
        {
            GEngine->Exec(PC->GetWorld(),TEXT("Marau6.ViewModelPose 0"));
            UE_LOG(LogTemp,Display,TEXT("MARAU6_VIEWMODEL done"));return false;
        }
        const FShot& Shot=Shots[S->Shot];
        if(S->Phase==3)
        {
            GEngine->Exec(PC->GetWorld(),TEXT("Marau6.ViewModelPose 0"));
            TArray<FString> Commands;Shot.Command.ParseIntoArray(Commands,TEXT("|"));
            for(const FString& Command:Commands)GEngine->Exec(PC->GetWorld(),*Command.TrimStartAndEnd());
            PC->SetControlRotation(FRotator(Shot.Pitch,PC->GetControlRotation().Yaw,0));S->Phase=4;S->At=Now;return true;
        }
        if(Now-S->At<Shot.Delay)return true;
        const FString File=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("LocalSettings")
            /FString::Printf(TEXT("viewmodel_%s.png"),*Shot.Name));
        // With UI: the raid HUD is a Slate widget and belongs in the inspection.
        FScreenshotRequest::RequestScreenshot(File,true,false);
        UE_LOG(LogTemp,Display,TEXT("MARAU6_VIEWMODEL requested %s"),*File);
        ++S->Shot;S->Phase=3;S->At=Now;return true;
    }));
}
// Shows the owner's shot presentation along the view without firing a server shot.
void PreviewShotFX(UWorld* World)
{
    APlayerController* PC=World?World->GetFirstPlayerController():nullptr;
    auto* C=PC?Cast<AMarau6Character>(PC->GetPawn()):nullptr;
    auto* Rifle=C?FindObject<USkeletalMeshComponent>(C,TEXT("FirstPersonRifle")):nullptr;
    if(!Rifle)return;
    const UCameraComponent* Camera=C->GetFirstPersonCameraComponent();
    const FVector From=Camera->GetComponentLocation();
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(Marau6PreviewShot),false,C);
    const FVector To=From+PC->GetControlRotation().Vector()*10000.f;
    const bool bHit=World->LineTraceSingleByChannel(Hit,From,To,ECC_Pawn,Query);
    const bool bCharacter=bHit && Cast<APawn>(Hit.GetActor());
    const FVector End=bHit?Hit.ImpactPoint:To;
    if(auto* FX=World->SpawnActor<AMarau6ShotFX>(AMarau6ShotFX::StaticClass(),FTransform(End)))
        FX->Present(Rifle,true,End,bHit?Hit.ImpactNormal:FVector::ZeroVector,bHit,bCharacter,From,Camera->FirstPersonScale);
}
FAutoConsoleCommandWithWorld PreviewShotFXCommand(TEXT("Marau6.PreviewShotFX"),
    TEXT("Spawn the owner's muzzle flash, tracer and smoke along the view; no server shot."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&PreviewShotFX));
FAutoConsoleCommandWithWorld CaptureViewModelCommand(TEXT("Marau6.CaptureViewModel"),
    TEXT("Save first person view captures at several pitches, aiming and sprinting."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&CaptureViewModel));
}
#endif
