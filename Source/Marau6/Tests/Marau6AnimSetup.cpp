#if WITH_EDITOR
#include "Marau6Character.h"
#include "Marau6ItemLedger.h"
#include "Components/DecalComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "Marau6LootActor.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/BlendSpace.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Templates/Function.h"
#include "K2Node_VariableGet.h"
#include "EdGraphSchema_K2.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

// Editor-only migration for project-owned copies. Template assets stay unchanged.
namespace Marau6AnimSetup
{
const TCHAR* TemplateLocomotion = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run");
const TCHAR* Folder = TEXT("/Game/Marau6/Anims");
const TCHAR* LocomotionName = TEXT("BS_Marau6_Locomotion");
const TCHAR* BodyPose = TEXT("/Game/Marau6/Anims/ABP_Marau6_TP_Rifle.ABP_Marau6_TP_Rifle");
const TCHAR* Arms = TEXT("/Game/Marau6/Anims/ABP_Marau6_FP_Rifle.ABP_Marau6_FP_Rifle");
// Players move at 5 m/s: the template only reaches its jog at 6 m/s, so normal
// movement read as a fast walk. Jog now lands on the move speed; aiming (3 m/s) still walks.
constexpr double JogSpeed = 500.0;

static bool Save(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Package, Asset, *File, Args);
}

static UBlendSpace* Locomotion()
{
    const FString Path = FString(Folder) / LocomotionName;
    UBlendSpace* Space = FPackageName::DoesPackageExist(Path)
        ? LoadObject<UBlendSpace>(nullptr, *(Path + TEXT(".") + LocomotionName)) : nullptr;
    if (!Space)
    {
        UObject* Template = LoadObject<UObject>(nullptr, TemplateLocomotion);
        if (!Template) return nullptr;
        Space = Cast<UBlendSpace>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
            .Get().DuplicateAsset(LocomotionName, Folder, Template));
    }
    if (!Space) return nullptr;
    Space->Modify();
    // Samples snap to the speed grid: the template's 150 cm/s steps would pull 500 to 450.
    const FStructProperty* Axes = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
    if (!Axes) return nullptr;
    FBlendParameter* SpeedAxis = Axes->ContainerPtrToValuePtr<FBlendParameter>(Space, 1);
    SpeedAxis->GridNum = FMath::RoundToInt((SpeedAxis->Max - SpeedAxis->Min) / 100.f);
    int32 Moved = 0;
    for (int32 Index = 0; Index < Space->GetNumberOfBlendSamples(); ++Index)
    {
        // Jog row: the template's 600, or any earlier copy that snapped elsewhere above the walk.
        const FVector Value = Space->GetBlendSample(Index).SampleValue;
        if (Value.Y > 300.0 && !FMath::IsNearlyEqual(Value.Y, JogSpeed)
            && Space->EditSampleValue(Index, FVector(Value.X, JogSpeed, Value.Z))) ++Moved;
    }
    Space->ValidateSampleData();
    Space->ResampleData();
    Space->PostEditChange();
    UE_LOG(LogTemp, Display, TEXT("MARAU6_ANIM locomotion jog samples moved=%d"), Moved);
    return Save(Space) ? Space : nullptr;
}

template<typename T> static T* NodeValue(UEdGraphNode* Node, const TCHAR* Struct, const TCHAR* Field, FProperty*& Out)
{
    const FStructProperty* NodeStruct = FindFProperty<FStructProperty>(Node->GetClass(), Struct);
    if (!NodeStruct) return nullptr;
    Out = NodeStruct->Struct->FindPropertyByName(Field);
    return Out ? Out->ContainerPtrToValuePtr<T>(NodeStruct->ContainerPtrToValuePtr<void>(Node)) : nullptr;
}

/** Whether Pin's upstream chain has a node matching Test; tells the upper body branch from the legs. */
static bool Reaches(const UEdGraphPin* Pin, TFunctionRef<bool(UEdGraphNode*)> Test, TSet<const UEdGraphNode*>& Seen)
{
    for (const UEdGraphPin* Link : Pin->LinkedTo)
    {
        UEdGraphNode* Node = Link->GetOwningNode();
        if (Test(Node)) return true;
        if (Seen.Contains(Node)) continue;
        Seen.Add(Node);
        for (const UEdGraphPin* Input : Node->Pins)
            if (Input->Direction == EGPD_Input && Reaches(Input, Test, Seen)) return true;
    }
    return false;
}

static bool HasPose(UEdGraphNode* Node, const TCHAR* Sequence)
{
    if (Node->GetClass()->GetFName() != TEXT("AnimGraphNode_SequencePlayer")) return false;
    FProperty* Property = nullptr;
    const TObjectPtr<UObject>* Value = NodeValue<TObjectPtr<UObject>>(Node, TEXT("Node"), TEXT("Sequence"), Property);
    return Value && *Value && (*Value)->GetName() == Sequence;
}

static bool AdaptBody(UBlendSpace* Space)
{
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, BodyPose);
    UClass* SlotClass = FindObject<UClass>(nullptr, TEXT("/Script/AnimGraph.AnimGraphNode_Slot"));
    if (!Blueprint || !SlotClass) return false;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
            UE_LOG(LogTemp, Display, TEXT("MARAU6_ANIM node %s / %s"), *Graph->GetName(), *Node->GetClass()->GetName());
    UEdGraphNode* Legs = nullptr;
    int32 Swapped = 0, Slots = 0;
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node->GetClass() == SlotClass) ++Slots;
            if (Node->GetClass()->GetFName() != TEXT("AnimGraphNode_BlendSpacePlayer")) continue;
            FProperty* Property = nullptr;
            TObjectPtr<UBlendSpace>* Value = NodeValue<TObjectPtr<UBlendSpace>>(Node, TEXT("Node"), TEXT("BlendSpace"), Property);
            if (!Value) continue;
            const FString Current = *Value ? (*Value)->GetPathName() : FString();
            if (Current == TemplateLocomotion || *Value == Space)
            {
                Node->Modify();
                *Value = Space;
                // An exposed pin's default wins over the node property at compile time.
                if (UEdGraphPin* Pin = Node->FindPin(TEXT("BlendSpace"))) Pin->DefaultObject = Space;
                Legs = Node;
                ++Swapped;
            }
        }
    if (!Legs) { UE_LOG(LogTemp, Error, TEXT("MARAU6_ANIM locomotion node not found")); return false; }

    // Reload plays on the upper body only: insert a slot in the layered blend's
    // branch that does not carry locomotion, so the legs keep running.
    if (Slots == 0)
    {
        for (UEdGraph* Graph : Graphs)
            for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
            {
                if (Node->GetClass()->GetFName() != TEXT("AnimGraphNode_LayeredBoneBlend")) continue;
                UEdGraphPin* Base = Node->FindPin(TEXT("BasePose"));
                UEdGraphPin* Upper = Node->FindPin(TEXT("BlendPoses_0"));
                if (!Base || !Upper || Upper->LinkedTo.Num() != 1) continue;
                auto IsLegs = [Legs](UEdGraphNode* Candidate)
                    { return Candidate == Legs || Candidate->GetClass()->GetName().Contains(TEXT("StateMachine")); };
                auto IsRifle = [](UEdGraphNode* Candidate) { return HasPose(Candidate, TEXT("MF_Rifle_Idle_ADS")); };
                TSet<const UEdGraphNode*> Seen;
                const bool bUpperLegs = Reaches(Upper, IsLegs, Seen);
                Seen.Reset();
                const bool bBaseLegs = Reaches(Base, IsLegs, Seen);
                Seen.Reset();
                const bool bUpperRifle = Reaches(Upper, IsRifle, Seen);
                UE_LOG(LogTemp, Display, TEXT("MARAU6_ANIM layered blend base legs=%d upper legs=%d upper rifle=%d"),
                    bBaseLegs, bUpperLegs, bUpperRifle);
                if (bUpperLegs || !(bBaseLegs || bUpperRifle)) continue;
                UEdGraphPin* Source = Upper->LinkedTo[0];
                UEdGraphNode* Slot = NewObject<UEdGraphNode>(Graph, SlotClass, NAME_None, RF_Transactional);
                FProperty* Property = nullptr;
                if (FName* SlotName = NodeValue<FName>(Slot, TEXT("Node"), TEXT("SlotName"), Property)) *SlotName = AMarau6Character::ReloadSlot;
                Slot->CreateNewGuid();
                Slot->NodePosX = Node->NodePosX - 260;
                Slot->NodePosY = Node->NodePosY + 160;
                Graph->AddNode(Slot, false, false);
                Slot->AllocateDefaultPins();
                UEdGraphPin* SlotIn = Slot->FindPin(TEXT("Source"));
                UEdGraphPin* SlotOut = nullptr;
                for (UEdGraphPin* Pin : Slot->Pins) if (Pin->Direction == EGPD_Output) SlotOut = Pin;
                const UEdGraphSchema* Schema = Graph->GetSchema();
                if (!SlotIn || !SlotOut || !Schema) return false;
                Upper->BreakLinkTo(Source);
                if (!Schema->TryCreateConnection(Source, SlotIn) || !Schema->TryCreateConnection(SlotOut, Upper))
                {
                    UE_LOG(LogTemp, Error, TEXT("MARAU6_ANIM reload slot wiring failed"));
                    return false;
                }
                ++Slots;
            }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool bSaved = Blueprint->Status != BS_Error && Save(Blueprint);
    UE_LOG(LogTemp, Display, TEXT("MARAU6_ANIM body saved=%d locomotion=%d slots=%d"), bSaved, Swapped, Slots);
    return bSaved && Slots == 1;
}

/** First person: a RigAlpha variable drives the hand rig's alpha, so the reload can release the hands. */
static bool AdaptArms()
{
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, Arms);
    if (!Blueprint) return false;
    const FName Variable(AMarau6Character::RigAlphaVariable);
    if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Variable) == INDEX_NONE)
    {
        FEdGraphPinType Type(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Float, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Variable, Type, TEXT("1.0"))) return false;
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    int32 Wired = 0;
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
        {
            if (Node->GetClass()->GetFName() != TEXT("AnimGraphNode_ControlRig")) continue;
            UEdGraphPin* Alpha = Node->FindPin(TEXT("Alpha"));
            if (!Alpha) { UE_LOG(LogTemp, Error, TEXT("MARAU6_ANIM rig has no Alpha pin")); continue; }
            if (Alpha->LinkedTo.Num() == 1)
                if (auto* Get = Cast<UK2Node_VariableGet>(Alpha->LinkedTo[0]->GetOwningNode()); Get && Get->GetVarName() == Variable) { ++Wired; continue; }
            Alpha->BreakAllPinLinks();
            UK2Node_VariableGet* Get = NewObject<UK2Node_VariableGet>(Graph);
            Get->VariableReference.SetSelfMember(Variable);
            Get->CreateNewGuid();
            Get->NodePosX = Node->NodePosX - 240;
            Get->NodePosY = Node->NodePosY + 120;
            Graph->AddNode(Get, false, false);
            Get->AllocateDefaultPins();
            if (!Graph->GetSchema()->TryCreateConnection(Get->GetValuePin(), Alpha)) { UE_LOG(LogTemp, Error, TEXT("MARAU6_ANIM rig alpha wiring failed")); return false; }
            ++Wired;
        }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool bSaved = Blueprint->Status != BS_Error && Save(Blueprint);
    UE_LOG(LogTemp, Display, TEXT("MARAU6_ANIM arms saved=%d rig alpha wired=%d"), bSaved, Wired);
    return bSaved && Wired > 0;
}

static void Setup()
{
    UBlendSpace* Space = Locomotion();
    if (!Space || !AdaptBody(Space) || !AdaptArms())
        UE_LOG(LogTemp, Error, TEXT("MARAU6_ANIM setup failed; template assets remain unchanged."));
}
// Inspection aids for Marau6.CaptureViewModel shot lists (docs/recarga-locomocao.md).
static void PreviewReload(const TArray<FString>& Args, UWorld* World)
{
    if (!World || !World->IsGameWorld()) return;
    const bool bOthers = Args.Num() > 0 && Args[0] == TEXT("others");
    for (TActorIterator<AMarau6Character> It(World); It; ++It)
        if (!bOthers || !It->IsPlayerControlled()) It->DebugReload();
}

/** Runs non-player pawns in a 1.5 m circle beside where they stand; "1" also runs the viewer forward. */
static void PreviewRun(const TArray<FString>& Args, UWorld* World)
{
    APlayerController* PC = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
    APawn* Viewer = PC ? PC->GetPawn() : nullptr;
    if (!Viewer) return;
    const bool bViewer = Args.Num() > 0 && Args[0] == TEXT("1");
    TArray<TPair<TWeakObjectPtr<ACharacter>, FVector>> Runners;
    for (TActorIterator<AMarau6Character> It(World); It; ++It)
    {
        if (*It == Viewer ? !bViewer : It->IsPlayerControlled()) continue;
        It->GetCharacterMovement()->bRunPhysicsWithNoController = true;
        It->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Runners.Add({ *It, It->GetActorLocation() + It->GetActorRightVector() * 150.f });
    }
    if (bViewer) PC->SetIgnoreMoveInput(false);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Runners, Viewer = TWeakObjectPtr<APawn>(Viewer), Start = FPlatformTime::Seconds()](float)
    {
        if (FPlatformTime::Seconds() - Start > 8.0) return false;
        for (const auto& [Runner, Center] : Runners)
        {
            ACharacter* C = Runner.Get();
            if (!C) continue;
            // The viewer keeps a centre at its side and so runs straight; the others circle theirs.
            const FVector Around = C == Viewer.Get() ? C->GetActorLocation() + C->GetActorRightVector() * 150.f : Center;
            const FVector Radial = (C->GetActorLocation() - Around).GetSafeNormal2D();
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial);
            const float Distance = FVector::Dist2D(C->GetActorLocation(), Around);
            C->AddMovementInput((Tangent + Radial * (150.f - Distance) / 150.f).GetSafeNormal2D());
        }
        return true;
    }));
}
/** A level, side-on soldier 4 m ahead of the viewer, for reading third person poses. */
static void PreviewSoldier(UWorld* World)
{
    APlayerController* PC = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
    APawn* Viewer = PC ? PC->GetPawn() : nullptr;
    if (!Viewer) return;
    // Face the most open of eight directions so cover does not hide the soldier.
    double BestYaw = PC->GetControlRotation().Yaw, BestRoom = 0.0;
    for (int32 Step = 0; Step < 8; ++Step)
    {
        const double Yaw = Step * 45.0;
        const FVector From = Viewer->GetActorLocation(), To = From + FRotator(0.0, Yaw, 0.0).Vector() * 1500.0;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(Marau6PreviewSoldier), false, Viewer);
        const double Room = World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Query) ? Hit.Distance : 1500.0;
        if (Room > BestRoom) { BestRoom = Room; BestYaw = Yaw; }
    }
    PC->SetControlRotation(FRotator(0.0, BestYaw, 0.0));
    const FRotator Facing(0.0, BestYaw, 0.0);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    World->SpawnActor<AMarau6Character>(Viewer->GetClass(), Viewer->GetActorLocation() + Facing.Vector() * FMath::Min(400.0, BestRoom - 120.0),
        FRotator(0.0, Facing.Yaw + 90.0, 0.0), Spawn);
    PC->SetIgnoreMoveInput(true);
    // The owner's arms would cover the soldier; this view is only for reading the body.
    if (auto* Own = Cast<AMarau6Character>(Viewer)) Own->GetFirstPersonMesh()->SetVisibility(false, true);
}

/** Turns the viewer to an absolute yaw, to frame previews away from cover. */
static void PreviewYaw(const TArray<FString>& Args, UWorld* World)
{
    APlayerController* PC = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
    if (PC && Args.Num() > 0) PC->SetControlRotation(FRotator(PC->GetControlRotation().Pitch, FCString::Atof(*Args[0]), 0.0));
}
/** One loot item of each kind in a row 2.5 m ahead of the viewer, on the floor. */
static void PreviewLoot(UWorld* World)
{
    APlayerController* PC = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
    APawn* Viewer = PC ? PC->GetPawn() : nullptr;
    auto* Ledger = World ? World->GetSubsystem<UMarau6ItemLedger>() : nullptr;
    if (!Viewer || !Ledger) return;
    const FRotator Facing(0.0, PC->GetControlRotation().Yaw, 0.0);
    PC->SetControlRotation(FRotator(-25.0, Facing.Yaw, 0.0));
    const EMarau6ItemKind Kinds[] = { EMarau6ItemKind::Rifle, EMarau6ItemKind::Supply, EMarau6ItemKind::Ammo, EMarau6ItemKind::Scrap };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector At = Viewer->GetActorLocation() + Facing.Vector() * 250.0
            + FRotationMatrix(Facing).GetUnitAxis(EAxis::Y) * (Index - 1.5) * 70.0 - FVector(0, 0, 70.0);
        // Register the kind before BeginPlay, which would otherwise seed the default kind.
        auto* Loot = World->SpawnActorDeferred<AMarau6LootActor>(AMarau6LootActor::StaticClass(), FTransform(At));
        if (!Loot) continue;
        Ledger->RegisterSeed(Loot, Kinds[Index]);
        Loot->FinishSpawning(FTransform(At));
    }
}
/** Debug: big bullet hole, blood and engine default decals on the surface under the crosshair. */
static void PreviewDecal(UWorld* World)
{
    APlayerController* PC = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
    if (!PC || !PC->PlayerCameraManager) return;
    const FVector From = PC->PlayerCameraManager->GetCameraLocation();
    const FVector Dir = PC->PlayerCameraManager->GetCameraRotation().Vector();
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(Marau6PreviewDecal), false, PC->GetPawn());
    if (!World->LineTraceSingleByChannel(Hit, From, From + Dir * 5000.0, ECC_Visibility, Query)) return;
    const FVector Side = FVector::VectorPlaneProject(FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetUnitAxis(EAxis::Y), Hit.ImpactNormal).GetSafeNormal();
    const TCHAR* Paths[] = { TEXT("/Game/Marau6/Effects/M_BulletHole.M_BulletHole"), TEXT("/Game/Marau6/Effects/M_BloodDecal.M_BloodDecal"),
        TEXT("/Engine/EngineMaterials/DefaultDeferredDecalMaterial.DefaultDeferredDecalMaterial") };
    for (int32 Index = 0; Index < 3; ++Index)
        if (UMaterialInterface* Look = LoadObject<UMaterialInterface>(nullptr, Paths[Index]))
        {
            UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Look, FVector(10.0, 35.0, 35.0),
                Hit.ImpactPoint + Side * (Index - 1) * 80.0, (-Hit.ImpactNormal).Rotation(), 60.f);
            if (Decal) Decal->SetFadeScreenSize(0.f);
            UE_LOG(LogTemp, Display, TEXT("MARAU6_DECAL %s spawned=%d at %s normal %s"), Paths[Index], Decal != nullptr,
                *Hit.ImpactPoint.ToString(), *Hit.ImpactNormal.ToString());
        }
}
static FAutoConsoleCommandWithWorld PreviewDecalCommand(TEXT("Marau6.PreviewDecal"),
    TEXT("Debug: large bullet hole, blood and engine default decals under the crosshair."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&PreviewDecal));
static FAutoConsoleCommandWithWorld PreviewLootCommand(TEXT("Marau6.PreviewLoot"),
    TEXT("Lay out one loot item of each kind ahead of the viewer."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&PreviewLoot));
static FAutoConsoleCommandWithWorld PreviewSoldierCommand(TEXT("Marau6.PreviewSoldier"),
    TEXT("Spawn a level, side-on soldier 4 m ahead of the viewer."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&PreviewSoldier));
static FAutoConsoleCommandWithWorldAndArgs PreviewYawCommand(TEXT("Marau6.PreviewYaw"),
    TEXT("Set the viewer's yaw in degrees."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PreviewYaw));
static FAutoConsoleCommandWithWorldAndArgs PreviewReloadCommand(TEXT("Marau6.PreviewReload"),
    TEXT("Reload every character (\"others\": all but players); preview pawns only show the pose."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PreviewReload));
static FAutoConsoleCommandWithWorldAndArgs PreviewRunCommand(TEXT("Marau6.PreviewRun"),
    TEXT("Run preview pawns in a circle ahead of the viewer for 8 s; 1 also runs the viewer."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PreviewRun));
static FAutoConsoleCommand SetupCommand(TEXT("Marau6.SetupLocomotion"),
    TEXT("Running locomotion copy and the upper body reload slot for the third person pose."),
    FConsoleCommandDelegate::CreateStatic(&Setup));
}
#endif
