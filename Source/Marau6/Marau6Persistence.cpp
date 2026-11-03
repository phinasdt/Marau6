#include "Marau6Persistence.h"
#include "Marau6Bots.h"
#include "Marau6Destruction.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Marau6ItemLedger.h"
#include "Marau6InventoryComponent.h"
#include "Marau6Character.h"
#include "Marau6PlayerController.h"
#include "Marau6LootActor.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"

namespace
{
TSharedPtr<FJsonObject> Parse(const FString& Text)
{TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
TSharedPtr<FJsonObject> Operation()
{auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("operationId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));return O;}
}
bool UMarau6Persistence::UsesPersistentBase(const UWorld* World)
{
    // Untitled automation fixture worlds intentionally retain the isolated combat mode.
    return World && World->IsGameWorld() && World->GetPackage()->GetName().Contains(TEXT("Lvl_FirstPerson"));
}
FString UMarau6Persistence::Encode(const TSharedPtr<FJsonObject>& O)
{FString Out;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&Out));return Out;}
bool UMarau6Persistence::AtomicWrite(const FString& Path,const FString& Content)
{
    auto& Files=FPlatformFileManager::Get().GetPlatformFile();
    if(!Files.CreateDirectoryTree(*FPaths::GetPath(Path))) return false;
    FTCHARToUTF8 Utf8(*Content);
    TUniquePtr<IFileHandle> File(Files.OpenWrite(*(Path+TEXT(".tmp"))));
    if(!File || !File->Write(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length()) || !File->Flush(true)) return false;
    File.Reset();
#if PLATFORM_WINDOWS
    return ::MoveFileExW(*(Path+TEXT(".tmp")),*Path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true,false,false,true);
#endif
}
void UMarau6Persistence::Fail(const FString& Message)
{bFatal=true;bReady=false;Status=Message;UE_LOG(LogTemp,Error,TEXT("Marau6 persistence: %s"),*Message);}
void UMarau6Persistence::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    bEnabled=UsesPersistentBase(&World) && World.GetNetMode()!=NM_Client;
    if(!bEnabled) return;
    JournalPath=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Persistence/current.json"));
#if WITH_EDITOR
    const bool bIntegration=FParse::Param(FCommandLine::Get(),TEXT("Marau6Integration"));
    if(bIntegration){FString Run;FGuid TestRun;if(!FParse::Value(FCommandLine::Get(),TEXT("Marau6IntegrationRun="),Run) || !FGuid::Parse(Run,TestRun)){Fail(TEXT("Integration run ID required"));return;}JournalPath=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/Persistence"),TestRun.ToString()+TEXT(".json"));}
#else
    const bool bIntegration=false;
#endif
#if PLATFORM_WINDOWS
    const FString LockName=FString::Printf(TEXT("Local\\Marau6Persistence-%u"),GetTypeHash(FPaths::ConvertRelativePathToFull(JournalPath)));
    ServerLock=::CreateMutexW(nullptr,true,*LockName);
    if(!ServerLock || ::GetLastError()==ERROR_ALREADY_EXISTS){if(ServerLock)::CloseHandle(ServerLock);ServerLock=nullptr;Fail(TEXT("Ja existe outro servidor usando este diario"));return;}
#endif
    FString Text;
    const FString ConfigPath=FPaths::Combine(FPaths::ProjectDir(),bIntegration?TEXT("LocalSettings/integration-server.json"):TEXT("LocalSettings/server.json"));
    if(!FFileHelper::LoadFileToString(Text,*ConfigPath))
    {Fail(TEXT("Backend nao preparado: execute scripts/Start-Marau6.ps1"));return;}
    auto Config=Parse(Text);
    if(!Config || !Config->TryGetStringField(TEXT("apiKey"),Key) || !Config->TryGetStringField(TEXT("url"),Url) || Url!=(bIntegration?TEXT("http://127.0.0.1:5187"):TEXT("http://127.0.0.1:5186")) || Key.Len()<32)
    {Fail(TEXT("Configuracao local do servidor invalida"));return;}
    // Keep the immutable seed locations separately from discarded loot.
    for(TActorIterator<AMarau6LootActor> It(&World);It;++It)
    {
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("kind"),static_cast<int32>(It->GetSeedKind()));
        const FVector P=It->GetActorLocation();O->SetNumberField(TEXT("x"),P.X);O->SetNumberField(TEXT("y"),P.Y);O->SetNumberField(TEXT("z"),P.Z);
        SeedTransforms.Add(MakeShared<FJsonValueObject>(O));
    }
    // Supplement the authored arena with ammo near each original rifle seed.
    // Cache these positions just like map loot so every round receives fresh GUIDs.
    const int32 AuthoredSeeds=SeedTransforms.Num();
    for(int32 N=0;N<AuthoredSeeds;N++)
    {
        auto Source=SeedTransforms[N]->AsObject();if(Source->GetIntegerField(TEXT("kind"))!=0)continue;
        const FVector Position(Source->GetNumberField(TEXT("x")),Source->GetNumberField(TEXT("y"))+100,Source->GetNumberField(TEXT("z")));
        auto Seed=MakeShared<FJsonObject>();Seed->SetNumberField(TEXT("kind"),3);Seed->SetNumberField(TEXT("x"),Position.X);Seed->SetNumberField(TEXT("y"),Position.Y);Seed->SetNumberField(TEXT("z"),Position.Z);SeedTransforms.Add(MakeShared<FJsonValueObject>(Seed));
        const FTransform Transform(FRotator::ZeroRotator,Position);auto* Box=World.SpawnActorDeferred<AMarau6LootActor>(AMarau6LootActor::StaticClass(),Transform);
        if(Box){World.GetSubsystem<UMarau6ItemLedger>()->RegisterSeed(Box,EMarau6ItemKind::Ammo);Box->FinishSpawning(Transform);}
    }
    if(FPaths::FileExists(JournalPath))
    {
        if(!FFileHelper::LoadFileToString(Text,*JournalPath)){Fail(TEXT("Nao foi possivel ler o diario; entrada bloqueada"));return;}
        auto O=Parse(Text);const TArray<TSharedPtr<FJsonValue>>* Pending=nullptr;
        if(!O || !O->TryGetStringField(TEXT("raidId"),RaidId) || !O->TryGetArrayField(TEXT("queue"),Pending))
        {Fail(TEXT("Diario invalido: preserve o arquivo para recuperacao"));return;}
        for(const auto& V:*Pending)
        {auto C=V->AsObject();FString Path,Body;if(!C || !C->TryGetStringField(TEXT("path"),Path) || !C->TryGetStringField(TEXT("body"),Body)){Fail(TEXT("Comando invalido no diario"));return;}Commands.Add({Path,Body,{}});}
        if(!RaidId.IsEmpty())
        {auto Close=Operation();Close->SetStringField(TEXT("raidId"),RaidId);Commands.Add({TEXT("/raids/close"),Encode(Close),{}});}
        bRecovery=true;Status=TEXT("Recuperando partida anterior...");
        if(!SaveJournal()) Fail(TEXT("Sem acesso de escrita ao diario"));
    }
    else {bRecovery=true;Status=TEXT("Preparando backend...");}
}
void UMarau6Persistence::Deinitialize()
{
    // Unacknowledged commands and the raid identity remain on disk for next startup.
    bEnabled=false;
#if PLATFORM_WINDOWS
    if(ServerLock){::ReleaseMutex(ServerLock);::CloseHandle(ServerLock);ServerLock=nullptr;}
#endif
    Super::Deinitialize();
}
bool UMarau6Persistence::SaveJournal()
{
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("raidId"),RaidId);
    TArray<TSharedPtr<FJsonValue>> Pending;
    for(const auto& C:Commands)
    {auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("path"),C.Path);O->SetStringField(TEXT("body"),C.Body);Pending.Add(MakeShared<FJsonValueObject>(O));}
    Root->SetArrayField(TEXT("queue"),Pending);return AtomicWrite(JournalPath,Encode(Root));
}
bool UMarau6Persistence::Queue(const FString& Path,TSharedPtr<FJsonObject> Body,TFunction<void(const FString&)> Done)
{
    Commands.Add({Path,Body?Encode(Body):TEXT(""),MoveTemp(Done)});
    if(SaveJournal()) return true;
    Commands.Pop();Fail(TEXT("Falha ao gravar diario; operacao nao confirmada"));return false;
}
void UMarau6Persistence::Tick(float Delta)
{
    if(!bEnabled || GetWorld()->bIsTearingDown) return;
    for(TActorIterator<AMarau6PlayerController> It(GetWorld());It;++It) It->BaseBackendStatus=Status;
    if(bFatal)return;
    if(bOpening && bRoundAcknowledged)
    {
        auto* Scenery=GetWorld()->GetSubsystem<UMarau6Destruction>();
        if(Scenery && Scenery->IsReadyForRound()){bOpening=false;bRoundAcknowledged=false;bReady=true;CommitLedger();}
    }
    // Players arrive with a profile already chosen: the first free one (Perfil 1, then 2).
    // Automated tests pick profiles themselves.
    if(bReady && !GIsAutomationTesting)
        for(TActorIterator<AMarau6PlayerController> It(GetWorld());It;++It)
            if(It->bUseBase && !It->bInRaid && It->BaseProfile==0)
                for(int32 Number=1;Number<=2;++Number)
                    if(!Claims.FindRef(Number).IsValid()){SelectProfile(*It,Number);break;}
    Retry-=Delta;
    if(Commands.IsEmpty() && !bSending && !bOpening && bRecovery)
    {bRecovery=false;RaidId.Empty();OpenRound();}
    if(Retry<=0) Pump();
}
void UMarau6Persistence::Pump()
{
    if(bSending || Commands.IsEmpty() || bFatal) return;
    bSending=true;
    const auto& C=Commands[0];auto R=FHttpModule::Get().CreateRequest();
    R->SetURL(Url+C.Path);R->SetVerb(C.Body.IsEmpty()?TEXT("GET"):TEXT("POST"));R->SetHeader(TEXT("X-Marau6-Key"),Key);
    R->SetHeader(TEXT("Content-Type"),TEXT("application/json"));R->SetTimeout(5.f);if(!C.Body.IsEmpty()) R->SetContentAsString(C.Body);
    R->OnProcessRequestComplete().BindWeakLambda(this,[this](FHttpRequestPtr Request,FHttpResponsePtr Response,bool bSuccess)
    {
        bSending=false;if(!bEnabled) return;
        const int32 Code=Response.IsValid()?Response->GetResponseCode():0;
        if(!bSuccess || Code>=500 || Code==0)
        {Retry=2;Status=TEXT("Backend indisponivel; operacoes registradas aguardam confirmacao");return;}
        LastCommandError.Empty();
        if(Code!=200)
        {
            auto Error=Parse(Response->GetContentAsString());FString Reason;
            if(Error && Error->TryGetStringField(TEXT("error"),Reason))
            {
                if(Reason.Contains(TEXT("Vers")))LastCommandError=TEXT("A base foi atualizada; tente novamente.");
                else if(Reason.Contains(TEXT("Destino")) || Reason.Contains(TEXT("Troca")))LastCommandError=TEXT("Troca incompativel; escolha outro destino.");
                else if(Reason.Contains(TEXT("pertence")))LastCommandError=TEXT("O item mudou de posicao; tente novamente.");
                else if(Reason.Contains(TEXT("reservado")))LastCommandError=TEXT("Equipamento reservado na partida.");
            }
            if(LastCommandError.IsEmpty())LastCommandError=TEXT("Transferencia recusada; atualizando o inventario.");
            // Base conflicts are recoverable; authoritative raid conflicts need investigation.
            if(Commands[0].Path.StartsWith(TEXT("/state/")) || Commands[0].Path==TEXT("/move"))
            {
                auto Done=MoveTemp(Commands[0].Done);Commands.RemoveAt(0);
                if(!SaveJournal()){Fail(TEXT("Falha ao atualizar diario"));return;}
                if(Done) Done(TEXT(""));return;
            }
            Fail(FString::Printf(TEXT("Backend rejeitou operacao (%d); diario preservado para analise"),Code));return;
        }
        auto Done=MoveTemp(Commands[0].Done);Commands.RemoveAt(0);
        if(!SaveJournal()){Fail(TEXT("Falha ao confirmar diario; reinicie para recuperar"));return;}
        Status=TEXT("Backend conectado");if(Done) Done(Response->GetContentAsString());
    });
    if(!R->ProcessRequest()){bSending=false;Retry=2;}
}
void UMarau6Persistence::OpenRound()
{
    bOpening=true;bReady=false;Sequence=0;Entered.Empty();Submitted.Empty();Completed.Empty();
    RaidId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    auto O=Operation();O->SetStringField(TEXT("raidId"),RaidId);
    Queue(TEXT("/raids/open"),O,[this](const FString&){if(auto* Scenery=GetWorld()->GetSubsystem<UMarau6Destruction>())Scenery->ResetForRound(RaidId);if(auto* Bots=GetWorld()->GetSubsystem<UMarau6Bots>())Bots->ResetForRound(RaidId);bRoundAcknowledged=true;});
}
void UMarau6Persistence::Refresh(AMarau6PlayerController* PC,const FString& PreserveMessage)
{
    if(!IsValid(PC) || PC->BaseProfile<1)return;
    PC->bBaseBusy=true;const int32 Profile=PC->BaseProfile;TWeakObjectPtr<AMarau6PlayerController> Weak=PC;
    if(!Queue(FString::Printf(TEXT("/state/%d"),Profile),nullptr,[Weak,Profile,PreserveMessage](const FString& Text)
    {if(auto* C=Weak.Get();C && C->BaseProfile==Profile){C->bBaseBusy=false;C->BaseJson=Text;C->BaseMessage=!PreserveMessage.IsEmpty()?PreserveMessage:Text.IsEmpty()?TEXT("Nao foi possivel atualizar; tente 1/2"):TEXT("Base pronta");C->ForceNetUpdate();}}))PC->bBaseBusy=false;
}
void UMarau6Persistence::SelectProfile(AMarau6PlayerController* PC,int32 Number)
{
    if(!IsValid(PC) || PC->bInRaid || (Number!=1 && Number!=2))return;
    if(!bReady || bFatal){PC->BaseMessage=TEXT("Backend nao esta pronto. Aguarde; se nao conectar, use Iniciar-Marau6.cmd.");PC->ForceNetUpdate();return;}
    if(PC->bBaseBusy || bEntering){PC->BaseMessage=TEXT("Aguarde a operacao pendente antes de escolher o perfil.");PC->ForceNetUpdate();return;}
    if(PC->BaseProfile!=0 && Entered.Contains(PC->BaseProfile)) {PC->BaseMessage=TEXT("Aguarde a proxima rodada para trocar de perfil");return;}
    auto Other=Claims.FindRef(Number);
    if(Other.IsValid() && Other.Get()!=PC){PC->BaseMessage=TEXT("Perfil ocupado pelo outro jogador");return;}
    if(PC->BaseProfile) Claims.Remove(PC->BaseProfile);
    PC->BaseProfile=Number;Claims.Add(Number,PC);PC->BaseJson.Empty();PC->BaseMessage=TEXT("Carregando perfil...");Refresh(PC);
}
void UMarau6Persistence::SelectKit(AMarau6PlayerController* PC,bool bBasic)
{
    if(!IsValid(PC) || PC->bInRaid)return;
    if(!bReady || bFatal){PC->BaseMessage=TEXT("Backend nao esta pronto. Aguarde; se nao conectar, use Iniciar-Marau6.cmd.");PC->ForceNetUpdate();return;}
    // Only this player's own state matters: the kit is read when their entry request is built.
    if(PC->bBaseBusy || (PC->BaseProfile!=0 && Entered.Contains(PC->BaseProfile)))
    {PC->BaseMessage=TEXT("Aguarde a operacao pendente antes de trocar o kit.");PC->ForceNetUpdate();return;}
    PC->bBasicKit=bBasic;
    PC->BaseMessage=bBasic?TEXT("KIT BASICO: rifle novo 20/40; seu equipamento fica na base."):TEXT("KIT PERSONALIZADO: entra com o equipamento preparado.");
    PC->ForceNetUpdate();
}
void UMarau6Persistence::MoveBase(AMarau6PlayerController* PC,int32 Source,int32 Destination,TFunction<void(bool,const FString&)> Done)
{
    auto Reject=[&](const FString& Why){if(IsValid(PC)){PC->BaseMessage=Why;PC->ForceNetUpdate();}if(Done)Done(false,Why);};
    if(!bReady || bFatal || !IsValid(PC) || PC->bInRaid || PC->BaseJson.IsEmpty()){Reject(TEXT("Inventario indisponivel neste estado."));return;}
    if(PC->bBaseBusy || bEntering){Reject(TEXT("Aguarde a transferencia pendente."));return;}
    if(PC->bBasicKit && (Source<100 || Destination<100)){Reject(TEXT("Kit basico bloqueado. Troque para o kit personalizado para editar."));return;}
    auto State=Parse(PC->BaseJson);if(!State){Reject(TEXT("Aguarde atualizar a base."));return;}
    const TArray<TSharedPtr<FJsonValue>>* Items;if(!State->TryGetArrayField(TEXT("items"),Items)){Reject(TEXT("Aguarde atualizar a base."));return;}
    FString Id;for(const auto& V:*Items)if(V->AsObject()->GetIntegerField(TEXT("slot"))==Source)Id=V->AsObject()->GetStringField(TEXT("id"));
    if(Id.IsEmpty()){Reject(TEXT("O item mudou de posicao; tente novamente."));return;}
    auto O=Operation();O->SetNumberField(TEXT("profile"),PC->BaseProfile);O->SetNumberField(TEXT("revision"),State->GetIntegerField(TEXT("revision")));
    O->SetStringField(TEXT("item"),Id);O->SetNumberField(TEXT("destination"),Destination);
    PC->bBaseBusy=true;PC->BaseMessage=TEXT("Salvando transferencia...");TWeakObjectPtr<AMarau6PlayerController> Weak=PC;const int32 Profile=PC->BaseProfile;
    auto Completion=MakeShared<TFunction<void(bool,const FString&)>>(MoveTemp(Done));
    if(!Queue(TEXT("/move"),O,[this,Weak,Profile,Completion](const FString& Text)
    {
        auto* C=Weak.Get();if(!C || C->BaseProfile!=Profile)return;
        C->bBaseBusy=false;const FString Why=Text.IsEmpty()?LastCommandError:TEXT("Transferencia salva.");
        C->BaseMessage=Why;if(!Text.IsEmpty())C->BaseJson=Text;else Refresh(C,Why);C->ForceNetUpdate();
        if(*Completion)(*Completion)(!Text.IsEmpty(),Why);
    }))
    {PC->bBaseBusy=false;const FString Why=TEXT("Nao foi possivel confirmar; os itens foram preservados.");PC->BaseMessage=Why;if(*Completion)(*Completion)(false,Why);}
}
void UMarau6Persistence::EnterRaid(AMarau6PlayerController* PC)
{
    if(!IsValid(PC) || PC->bInRaid)return;
    if(!bReady || bFatal){PC->BaseMessage=TEXT("Backend nao esta pronto. Aguarde; se nao conectar, use Iniciar-Marau6.cmd.");PC->ForceNetUpdate();return;}
    if(bEntering || PC->bBaseBusy){PC->BaseMessage=TEXT("Aguarde a confirmacao da operacao pendente.");PC->ForceNetUpdate();return;}
    if(PC->BaseProfile<1 || PC->BaseJson.IsEmpty()){PC->BaseMessage=TEXT("Escolha o perfil com 1 ou 2 antes de pressionar B.");PC->ForceNetUpdate();return;}
    auto* EntryPawn=Cast<AMarau6Character>(PC->GetPawn());
    if(Entered.Contains(PC->BaseProfile))
    {
        auto* Ledger=GetWorld()->GetSubsystem<UMarau6ItemLedger>();
        const FGuid ReservedOwner=EntryPawn?EntryPawn->GetInventory()->GetSessionInventoryId():FGuid();
        FGuid ProfileOwner;
        if(auto Profile=Parse(PC->BaseJson)){FString Id;if(Profile->TryGetStringField(TEXT("id"),Id))FGuid::Parse(Id,ProfileOwner);}
        if(EntryPawn && EntryPawn->IsAtBase() && ReservedOwner.IsValid() && ReservedOwner==ProfileOwner && !Ledger->IsFinalized(ReservedOwner))
        {
            PC->bInRaid=EntryPawn->SetAtBase(false);
            PC->BaseMessage=PC->bInRaid?TEXT("Entrada confirmada"):TEXT("Ponto ocupado; B tenta novamente sem reservar outro equipamento.");
            if(PC->bInRaid && !CommitLedger()){EntryPawn->SetAtBase(true);PC->bInRaid=false;}
            PC->ForceNetUpdate();return;
        }
        PC->BaseMessage=TEXT("Entrada utilizada. Aguarde o outro perfil e use N para nova rodada.");return;
    }
    FVector EntryLocation;FRotator EntryRotation;
    if(!EntryPawn || !EntryPawn->FindRaidEntry(EntryLocation,EntryRotation))
    {PC->BaseMessage=TEXT("Nao ha ponto de inicio livre no mapa; equipamento nao foi reservado.");PC->ForceNetUpdate();return;}
    auto State=Parse(PC->BaseJson);if(!State)return;
    auto O=Operation();O->SetStringField(TEXT("raidId"),RaidId);O->SetNumberField(TEXT("profile"),PC->BaseProfile);O->SetNumberField(TEXT("revision"),State->GetIntegerField(TEXT("revision")));
    // Only a basic entry names its kit, so custom entries keep their original body and receipt.
    if(PC->bBasicKit)O->SetStringField(TEXT("kit"),TEXT("basic"));
    bEntering=true;PC->BaseMessage=TEXT("Reservando equipamento...");TWeakObjectPtr<AMarau6PlayerController> Weak=PC;const int32 Number=PC->BaseProfile;
    Queue(TEXT("/raids/enter"),O,[this,Weak,Number](const FString& Text)
    {
        bEntering=false;auto Reply=Parse(Text);if(!Reply){Fail(TEXT("Resposta de entrada invalida"));return;}
        Entered.Add(Number);auto* C=Weak.Get();

        const auto Profile=Reply->GetObjectField(TEXT("profile"));FGuid Owner;FGuid::Parse(Profile->GetStringField(TEXT("id")),Owner);
        TArray<FMarau6Item> Equipment;
        for(const auto& V:Reply->GetArrayField(TEXT("items")))
        {auto I=V->AsObject();FMarau6Item Item;FGuid::Parse(I->GetStringField(TEXT("id")),Item.Id);Item.Kind=static_cast<EMarau6ItemKind>(I->GetIntegerField(TEXT("kind")));Item.Slot=I->GetIntegerField(TEXT("slot"));Item.Magazine=I->GetIntegerField(TEXT("magazine"));Item.Reserve=I->GetIntegerField(TEXT("reserve"));if(Item.IsRifle())I->TryGetNumberField(TEXT("durability"),Item.Durability);Equipment.Add(Item);}
        if(!C)
        {
            if(!GetWorld()->GetSubsystem<UMarau6ItemLedger>()->AbandonReserved(Owner,Equipment))Fail(TEXT("Nao foi possivel registrar a desistencia; diario preservado"));
            FlushDeferredDeaths();
            return;
        }
        auto* Pawn=Cast<AMarau6Character>(C->GetPawn());
        if(!Pawn || !GetWorld()->GetSubsystem<UMarau6ItemLedger>()->Import(Pawn->GetInventory(),Owner,Equipment)) {Fail(TEXT("Falha ao importar equipamento reservado"));return;}
        C->BaseJson=Encode(Profile);C->bInRaid=Pawn->SetAtBase(false);
        if(!C->bInRaid)C->BaseMessage=TEXT("Equipamento reservado; ponto ocupado. B tenta a entrada novamente.");
        C->ForceNetUpdate();
        if(!CommitLedger()){Pawn->SetAtBase(true);C->bInRaid=false;}
        FlushDeferredDeaths();
    });
}
bool UMarau6Persistence::CommitLedger()
{
    if(!bEnabled) return true;
    if(!bReady || bFatal) return false;
    auto* Ledger=GetWorld()->GetSubsystem<UMarau6ItemLedger>();
    auto O=Operation();O->SetStringField(TEXT("raidId"),RaidId);O->SetNumberField(TEXT("sequence"),static_cast<double>(Sequence+1));
    O->SetArrayField(TEXT("items"),Ledger->Export());
    TArray<int32> Ended;TArray<TSharedPtr<FJsonValue>> Results;
    for(int32 Number:Entered)
    {
        FGuid Owner;FGuid::Parse(Number==1?TEXT("11111111-1111-1111-1111-111111111111"):TEXT("22222222-2222-2222-2222-222222222222"),Owner);
        if(Ledger->IsFinalized(Owner) && !Submitted.Contains(Number))
        {auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("profile"),Number);R->SetStringField(TEXT("outcome"),Ledger->IsExtracted(Owner)?TEXT("extracted"):TEXT("eliminated"));Results.Add(MakeShared<FJsonValueObject>(R));Ended.Add(Number);}
    }
    O->SetArrayField(TEXT("results"),Results);
    if(!Queue(TEXT("/raids/snapshot"),O,[this,Ended](const FString&){AckSnapshot(Ended);})) return false;
    ++Sequence;for(int32 N:Ended) Submitted.Add(N);return true;
}
void UMarau6Persistence::AckSnapshot(const TArray<int32>& Ended)
{
    for(int32 N:Ended)
    {
        Completed.Add(N);
        if(auto* C=Claims.FindRef(N).Get())
        {C->bInRaid=false;C->BaseMessage=TEXT("Resultado salvo. Organize a base enquanto aguarda a proxima rodada.");if(auto* P=Cast<AMarau6Character>(C->GetPawn()))P->SetAtBase(true);C->ForceNetUpdate();Refresh(C);}
    }
}
void UMarau6Persistence::NextRound(AMarau6PlayerController* PC)
{
    if(!IsValid(PC) || PC->bInRaid)return;
    if(!bReady || bFatal){PC->BaseMessage=TEXT("Backend nao esta pronto. Aguarde; se nao conectar, use Iniciar-Marau6.cmd.");PC->ForceNetUpdate();return;}
    if(Completed.Num()!=2){PC->BaseMessage=TEXT("N abre outra rodada somente depois que os dois perfis terminarem a atual.");PC->ForceNetUpdate();return;}
    if(bEntering || !IsSettled()){PC->BaseMessage=TEXT("Aguarde a confirmacao dos resultados antes da nova rodada.");PC->ForceNetUpdate();return;}
    bReady=false;auto O=Operation();O->SetStringField(TEXT("raidId"),RaidId);
    Queue(TEXT("/raids/close"),O,[this](const FString&)
    {
        GetWorld()->GetSubsystem<UMarau6ItemLedger>()->ResetRound();
        for(const auto& V:SeedTransforms)
        {auto O=V->AsObject();const FTransform T(FRotator::ZeroRotator,FVector(O->GetNumberField(TEXT("x")),O->GetNumberField(TEXT("y")),O->GetNumberField(TEXT("z"))));auto* A=GetWorld()->SpawnActorDeferred<AMarau6LootActor>(AMarau6LootActor::StaticClass(),T);if(A){GetWorld()->GetSubsystem<UMarau6ItemLedger>()->RegisterSeed(A,static_cast<EMarau6ItemKind>(O->GetIntegerField(TEXT("kind"))));A->FinishSpawning(T);}}
        OpenRound();
    });
}
void UMarau6Persistence::Disconnect(AMarau6PlayerController* PC)
{
    if(!IsValid(PC) || !bEnabled || !GetWorld() || GetWorld()->bIsTearingDown) return;
    if(Entered.Contains(PC->BaseProfile)) if(auto* Pawn=Cast<AMarau6Character>(PC->GetPawn())) Pawn->GetInventory()->FinalizeRaid(false);
    if(Claims.FindRef(PC->BaseProfile).Get()==PC) Claims.Remove(PC->BaseProfile);
}
void UMarau6Persistence::FlushDeferredDeaths()
{
    if(bEntering || bFatal || !bReady)return;
    const auto Pending=DeferredDeaths;DeferredDeaths.Empty();
    for(const auto& Pair:Pending)
        if(!GetWorld()->GetSubsystem<UMarau6ItemLedger>()->FinalizeDisconnected(Pair.Key,Pair.Value))
        {Fail(TEXT("Nao foi possivel registrar a desconexao adiada; diario preservado"));return;}
}