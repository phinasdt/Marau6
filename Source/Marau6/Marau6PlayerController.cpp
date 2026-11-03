// Copyright Epic Games, Inc. All Rights Reserved.


#include "Marau6PlayerController.h"
#include "Marau6Persistence.h"
#include "Marau6Audio.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Marau6Character.h"
#include "Net/UnrealNetwork.h"
#include "Marau6PrototypeHUD.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Marau6CameraManager.h"
#include "Blueprint/UserWidget.h"
#include "Marau6.h"
#include "Widgets/Input/SVirtualJoystick.h"

AMarau6PlayerController::AMarau6PlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = AMarau6CameraManager::StaticClass();
    AmbientReverb = CreateDefaultSubobject<UReverbEffect>(TEXT("AmbientReverb"));
    Marau6Audio::ConfigureAmbientReverb(*AmbientReverb);
    static ConstructorHelpers::FObjectFinder<USoundBase> Gas(TEXT("/Game/Marau6/Audio/S_Decompression.S_Decompression"));
    GasSound = Gas.Object;
    GasAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("GasAttenuation"));
    Marau6Audio::ConfigureOwnerSend(GasAttenuation->Attenuation, .3f);
}

// bInRaid is set by the server and replicated; a listen server host has no OnRep, so watch it here.
void AMarau6PlayerController::UpdateRaidGas()
{
    if(!IsLocalPlayerController() || bInRaid==bHeardRaid)return;
    bHeardRaid=bInRaid;
    if(bInRaid && GasSound)UGameplayStatics::PlaySoundAtLocation(this, GasSound, GetFocalLocation(), FRotator::ZeroRotator,
        Marau6Audio::GasVolume, Marau6Audio::GasPitch, 0.f, GasAttenuation);
}

void AMarau6PlayerController::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority() && UMarau6Persistence::UsesPersistentBase(GetWorld()))
    {bUseBase=true;BaseMessage=TEXT("1: Teste 1 | 2: Teste 2");if(auto* P=Cast<AMarau6Character>(GetPawn()))P->SetAtBase(true);
    // The basic kit is the default loadout; automated tests choose their kit explicitly.
    if(!GIsAutomationTesting)bBasicKit=true;}


    if (IsLocalPlayerController())
    {
        ClientSetHUD(AMarau6PrototypeHUD::StaticClass());
        UGameplayStatics::ActivateReverbEffect(this, AmbientReverb, Marau6Audio::AmbientReverbTag,
            0.f, Marau6Audio::AmbientReverbVolume, .5f);
    }

	
	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogMarau6, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AMarau6PlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::One,IE_Pressed,this,&AMarau6PlayerController::SelectOne);
    InputComponent->BindKey(EKeys::Two,IE_Pressed,this,&AMarau6PlayerController::SelectTwo);
    InputComponent->BindKey(EKeys::Up,IE_Pressed,this,&AMarau6PlayerController::BaseUp).bConsumeInput=false;
    InputComponent->BindKey(EKeys::Down,IE_Pressed,this,&AMarau6PlayerController::BaseDown).bConsumeInput=false;
    InputComponent->BindKey(EKeys::Left,IE_Pressed,this,&AMarau6PlayerController::BaseLeft).bConsumeInput=false;
    InputComponent->BindKey(EKeys::Right,IE_Pressed,this,&AMarau6PlayerController::BaseRight).bConsumeInput=false;
    InputComponent->BindKey(EKeys::Enter,IE_Pressed,this,&AMarau6PlayerController::BaseAccept);
    InputComponent->BindKey(EKeys::B,IE_Pressed,this,&AMarau6PlayerController::BaseEnter);
    InputComponent->BindKey(EKeys::N,IE_Pressed,this,&AMarau6PlayerController::BaseNext);
    InputComponent->BindKey(EKeys::K,IE_Pressed,this,&AMarau6PlayerController::ToggleKit);


	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
	
}

bool AMarau6PlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AMarau6PlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarau6PlayerController,bUseBase);DOREPLIFETIME(AMarau6PlayerController,bInRaid);
    DOREPLIFETIME(AMarau6PlayerController,InventoryContext);DOREPLIFETIME(AMarau6PlayerController,bBaseBusy);DOREPLIFETIME(AMarau6PlayerController,bBasicKit);
    DOREPLIFETIME(AMarau6PlayerController,BaseProfile);DOREPLIFETIME(AMarau6PlayerController,BaseJson);DOREPLIFETIME(AMarau6PlayerController,BaseMessage);DOREPLIFETIME(AMarau6PlayerController,BaseBackendStatus);
}
void AMarau6PlayerController::OnPossess(APawn* NewPawn)
{Super::OnPossess(NewPawn);if(UMarau6Persistence::UsesPersistentBase(GetWorld()))if(auto* P=Cast<AMarau6Character>(NewPawn))P->SetAtBase(true);}
void AMarau6PlayerController::SelectOne(){if(bUseBase && !bInRaid){InventoryMessage.Empty();BaseSource=INDEX_NONE;ServerSelectProfile(1);}}
void AMarau6PlayerController::SelectTwo(){if(bUseBase && !bInRaid){InventoryMessage.Empty();BaseSource=INDEX_NONE;ServerSelectProfile(2);}}
void AMarau6PlayerController::BaseUp(){if(bUseBase && !bInRaid)BaseSelection=(BaseSelection+42)%43;}
void AMarau6PlayerController::BaseDown(){if(bUseBase && !bInRaid)BaseSelection=(BaseSelection+1)%43;}
void AMarau6PlayerController::BaseAccept()
{
    if(IsLocalPlayerController() && InventoryContext.IsValid() && IsInventoryVisible()){InventoryKeyboardAccept();return;}
    if(auto* RaidCharacter=Cast<AMarau6Character>(GetPawn()))
        if(!bUseBase || bInRaid){RaidCharacter->ConfirmInventoryMove();return;}
    if(!bUseBase || bInRaid || BaseProfile==0)return;
    if(BaseSource==INDEX_NONE)BaseSource=BaseSelection;
    else {ServerBaseMove(BaseSlot(BaseSource),BaseSlot(BaseSelection));BaseSource=INDEX_NONE;}
}
void AMarau6PlayerController::BaseEnter(){if(bUseBase && !bInRaid){InventoryMessage.Empty();ServerEnterRaid();}}
void AMarau6PlayerController::BaseNext(){if(bUseBase && !bInRaid){InventoryMessage.Empty();ServerNextRound();}}
void AMarau6PlayerController::ServerSelectProfile_Implementation(int32 N){GetWorld()->GetSubsystem<UMarau6Persistence>()->SelectProfile(this,N);}
void AMarau6PlayerController::ServerBaseMove_Implementation(int32 S,int32 D)
{TWeakObjectPtr<AMarau6PlayerController> Weak(this);GetWorld()->GetSubsystem<UMarau6Persistence>()->MoveBase(this,S,D,[Weak](bool,const FString& Why){if(auto* PC=Weak.Get())PC->ClientInventoryNotice(Why);});}
void AMarau6PlayerController::SelectKit(bool bBasic){if(bUseBase && !bInRaid && bBasic!=bBasicKit){InventoryMessage.Empty();ServerSelectKit(bBasic);}}
void AMarau6PlayerController::ToggleKit(){SelectKit(!bBasicKit);}
void AMarau6PlayerController::ServerSelectKit_Implementation(bool bBasic){GetWorld()->GetSubsystem<UMarau6Persistence>()->SelectKit(this,bBasic);}
void AMarau6PlayerController::ServerEnterRaid_Implementation(){GetWorld()->GetSubsystem<UMarau6Persistence>()->EnterRaid(this);}
void AMarau6PlayerController::ServerNextRound_Implementation(){GetWorld()->GetSubsystem<UMarau6Persistence>()->NextRound(this);}
void AMarau6PlayerController::BaseRight()
{if(bUseBase && !bInRaid)BaseSelection=BaseSelection<11?11+BaseSelection:BaseSelection<27?BaseSelection+16:FMath::Min(BaseSelection-27,10);}
void AMarau6PlayerController::BaseLeft()
{if(bUseBase && !bInRaid)BaseSelection=BaseSelection<11?27+BaseSelection:BaseSelection<27?FMath::Min(BaseSelection-11,10):BaseSelection-16;}