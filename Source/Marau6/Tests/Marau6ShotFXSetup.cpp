#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "IAssetTools.h"
#include "Sound/SoundWave.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// Editor-only generator for the assets used by AMarau6ShotFX. Materials are built
// from engine math nodes over the flash and smoke textures painted by
// scripts/Generate-ShotTextures.py and the impact textures (blood, bullet hole,
// dust) painted by scripts/Generate-ImpactTextures.py. The decompression gas is imported from the WAV
// written by scripts/Generate-Decompression.py; the rifle fire from S_RifleFire.wav,
// converted from the supplied clip (docs/som-disparo.md).
namespace Marau6ShotFXAssets
{
template<typename T> T* Add(UMaterial* M)
{
    T* E = NewObject<T>(M);
    M->GetExpressionCollection().AddExpression(E);
    return E;
}
UMaterialExpression* Mul(UMaterial* M, UMaterialExpression* A, UMaterialExpression* B)
{
    auto* E = Add<UMaterialExpressionMultiply>(M);
    E->A.Connect(0, A);
    E->B.Connect(0, B);
    return E;
}
UMaterialExpression* Scalar(UMaterial* M, const TCHAR* Name, float Value)
{
    auto* E = Add<UMaterialExpressionScalarParameter>(M);
    E->ParameterName = Name;
    E->DefaultValue = Value;
    return E;
}
UMaterialExpression* Color(UMaterial* M, FLinearColor Value)
{
    auto* E = Add<UMaterialExpressionVectorParameter>(M);
    E->ParameterName = TEXT("Color");
    E->DefaultValue = Value;
    return E;
}
// Soft round falloff from the centre of the UV square: (1 - 2 * distance)^2.
UMaterialExpression* RadialMask(UMaterial* M)
{
    auto* Centre = Add<UMaterialExpressionConstant2Vector>(M);
    Centre->R = .5f;
    Centre->G = .5f;
    auto* Distance = Add<UMaterialExpressionDistance>(M);
    Distance->A.Connect(0, Add<UMaterialExpressionTextureCoordinate>(M));
    Distance->B.Connect(0, Centre);
    auto* Two = Add<UMaterialExpressionConstant>(M);
    Two->R = 2.f;
    auto* Inverse = Add<UMaterialExpressionOneMinus>(M);
    Inverse->Input.Connect(0, Mul(M, Distance, Two));
    auto* Clamp = Add<UMaterialExpressionSaturate>(M);
    Clamp->Input.Connect(0, Inverse);
    auto* Curve = Add<UMaterialExpressionPower>(M);
    Curve->Base.Connect(0, Clamp);
    Curve->ConstExponent = 2.f;
    return Curve;
}
// Opaque where a tube faces the viewer, fading toward its silhouette. The end caps
// (normals along the tube's local Z) vanish, so a trail seen end-on shows no disc.
UMaterialExpression* TubeMask(UMaterial* M, float Exponent)
{
    auto* Edge = Add<UMaterialExpressionFresnel>(M);
    Edge->Exponent = Exponent;
    Edge->BaseReflectFraction = 0.f;
    auto* Inverse = Add<UMaterialExpressionOneMinus>(M);
    Inverse->Input.Connect(0, Edge);
    auto* Up = Add<UMaterialExpressionConstant3Vector>(M);
    Up->Constant = FLinearColor(0.f, 0.f, 1.f);
    auto* Axis = Add<UMaterialExpressionTransform>(M);
    Axis->TransformSourceType = TRANSFORMSOURCE_Local;
    Axis->TransformType = TRANSFORM_World;
    Axis->Input.Connect(0, Up);
    auto* Along = Add<UMaterialExpressionDotProduct>(M);
    Along->A.Connect(0, Axis);
    Along->B.Connect(0, Add<UMaterialExpressionVertexNormalWS>(M));
    auto* Facing = Add<UMaterialExpressionAbs>(M);
    Facing->Input.Connect(0, Along);
    auto* Side = Add<UMaterialExpressionOneMinus>(M);
    Side->Input.Connect(0, Facing);
    auto* Sharp = Add<UMaterialExpressionPower>(M);
    Sharp->Base.Connect(0, Side);
    Sharp->ConstExponent = 4.f;
    return Mul(M, Inverse, Sharp);
}

// UV in [0, 1] moved into tile Frame (its corner in UV, 0 or .5 per axis) of a 2x2 atlas.
UMaterialExpression* AtlasTile(UMaterial* M, UMaterialExpression* UV)
{
    auto* Frame = Add<UMaterialExpressionVectorParameter>(M);
    Frame->ParameterName = TEXT("Frame");
    Frame->DefaultValue = FLinearColor::Transparent;
    auto* Corner = Add<UMaterialExpressionComponentMask>(M);
    Corner->Input.Connect(0, Frame);
    Corner->R = Corner->G = true;
    auto* Half = Add<UMaterialExpressionConstant>(M);
    Half->R = .5f;
    auto* Tile = Add<UMaterialExpressionAdd>(M);
    Tile->A.Connect(0, Mul(M, UV, Half));
    Tile->B.Connect(0, Corner);
    return Tile;
}
UMaterialExpressionTextureSampleParameter2D* Sample(UMaterial* M, UTexture* Texture, UMaterialExpression* TileUV,
    const TCHAR* Name, EMaterialSamplerType Type = SAMPLERTYPE_Color)
{
    auto* Sample = Add<UMaterialExpressionTextureSampleParameter2D>(M);
    Sample->ParameterName = Name;
    Sample->Texture = Texture;
    Sample->SamplerType = Type;
    Sample->Coordinates.Connect(0, TileUV);
    return Sample;
}
// Samples tile Frame of a 2x2 atlas at UV in [0, 1].
UMaterialExpressionTextureSampleParameter2D* AtlasSample(UMaterial* M, UTexture* Texture, UMaterialExpression* UV,
    const TCHAR* Name = TEXT("Flash"))
{
    return Sample(M, Texture, AtlasTile(M, UV), Name);
}
// Sprite UV shrunk about the centre by the Grow parameter, so a quad drawn at full
// size shows a smaller puff and swelling only changes a parameter. Tile borders are
// clear, so the clamped UV leaves the outside empty.
UMaterialExpression* GrowUV(UMaterial* M)
{
    auto* Centre = Add<UMaterialExpressionConstant>(M);
    Centre->R = .5f;
    auto* FromCentre = Add<UMaterialExpressionSubtract>(M);
    FromCentre->A.Connect(0, Add<UMaterialExpressionTextureCoordinate>(M));
    FromCentre->B.Connect(0, Centre);
    auto* Shrunk = Add<UMaterialExpressionDivide>(M);
    Shrunk->A.Connect(0, FromCentre);
    Shrunk->B.Connect(0, Scalar(M, TEXT("Grow"), 1.f));
    auto* Back = Add<UMaterialExpressionAdd>(M);
    Back->A.Connect(0, Shrunk);
    Back->B.Connect(0, Centre);
    auto* Inside = Add<UMaterialExpressionSaturate>(M);
    Inside->Input.Connect(0, Back);
    return Inside;
}

bool Save(UMaterial* M)
{
    M->PreEditChange(nullptr);
    M->PostEditChange();
    FAssetRegistryModule::AssetCreated(M);
    UPackage* Package = M->GetOutermost();
    Package->MarkPackageDirty();
    const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bSaved = UPackage::SavePackage(Package, M, *File, Args);
    UE_LOG(LogTemp, Display, TEXT("MARAU6_SHOTFX %s saved=%d"), *M->GetName(), bSaved);
    return bSaved;
}
bool SavePackageOf(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bSaved = UPackage::SavePackage(Package, Asset, *File, Args);
    UE_LOG(LogTemp, Display, TEXT("MARAU6_SHOTFX %s saved=%d"), *Asset->GetName(), bSaved);
    return bSaved;
}
UMaterial* Create(const TCHAR* Name, EBlendMode Blend)
{
    // Re-runs replace the asset in place, so an existing package must be fully loaded to save.
    const FString Path = FString::Printf(TEXT("/Game/Marau6/Effects/%s"), Name);
    UPackage* Package = FPackageName::DoesPackageExist(Path) ? LoadPackage(nullptr, *Path, LOAD_None) : nullptr;
    if (!Package) Package = CreatePackage(*Path);
    Package->FullyLoad();
    if (UMaterial* Old = FindObject<UMaterial>(Package, Name))
        Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    UMaterial* M = NewObject<UMaterial>(Package, Name, RF_Public | RF_Standalone);
    M->BlendMode = Blend;
    M->SetShadingModel(MSM_Unlit);
    M->TwoSided = true;
    M->bUsedWithStaticLighting = false;
    return M;
}

// Bullet holes and blood: deferred decals showing tile Frame of a 2x2 atlas, tinted
// by Color, coverage in the texture's alpha. Roughness is Roughness plus RoughnessFromRed
// times the texture's red, so thick, dark blood stays glossier than its thin film.
UMaterial* Decal(const TCHAR* Name, UTexture* Texture, UTexture* NormalMap, FLinearColor Tint, float Roughness, float RoughnessFromRed)
{
    UMaterial* M = Create(Name, BLEND_Translucent);
    M->MaterialDomain = MD_DeferredDecal;
    M->SetShadingModel(MSM_DefaultLit);
    M->TwoSided = false;
    UMaterialExpression* TileUV = AtlasTile(M, Add<UMaterialExpressionTextureCoordinate>(M));
    auto* Paint = Sample(M, Texture, TileUV, TEXT("Decal"));
    auto* Rough = Add<UMaterialExpressionConstant>(M);
    Rough->R = Roughness;
    auto* FromRed = Add<UMaterialExpressionConstant>(M);
    FromRed->R = RoughnessFromRed;
    auto* Red = Add<UMaterialExpressionComponentMask>(M);
    Red->Input.Connect(0, Paint);
    Red->R = true;
    auto* Gloss = Add<UMaterialExpressionAdd>(M);
    Gloss->A.Connect(0, Rough);
    Gloss->B.Connect(0, Mul(M, Red, FromRed));
    auto* Coverage = Add<UMaterialExpressionMultiply>(M);
    Coverage->A.Connect(4, Paint);
    Coverage->B.Connect(0, Scalar(M, TEXT("Opacity"), 1.f));
    M->GetEditorOnlyData()->BaseColor.Connect(0, Mul(M, Paint, Color(M, Tint)));
    M->GetEditorOnlyData()->Roughness.Connect(0, Gloss);
    M->GetEditorOnlyData()->Opacity.Connect(0, Coverage);
    if (NormalMap)
        M->GetEditorOnlyData()->Normal.Connect(0, Sample(M, NormalMap, TileUV, TEXT("Normal"), SAMPLERTYPE_Normal));
    return M;
}

// Imports Saved/<Folder>/<Name> into Destination, replacing the asset of the same name.
UObject* ImportGenerated(const TCHAR* Folder, const TCHAR* Name, const TCHAR* Destination, const TCHAR* Script)
{
    const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / Folder / Name);
    if (!FPaths::FileExists(File))
    {
        UE_LOG(LogTemp, Error, TEXT("MARAU6_SHOTFX missing %s; run %s first."), *File, Script);
        return nullptr;
    }
    // An asset already loaded at startup (the player controller's gas) keeps its file
    // open for lazy bulk data; load it fully and let go of the file so it can be replaced.
    if (UPackage* Existing = FindPackage(nullptr, *(FString(Destination) / FPaths::GetBaseFilename(File))))
    {
        Existing->FullyLoad();
        ResetLoaders(Existing);
    }
    UAutomatedAssetImportData* Import = NewObject<UAutomatedAssetImportData>();
    Import->Filenames.Add(File);
    Import->DestinationPath = Destination;
    Import->bReplaceExisting = true;
    const TArray<UObject*> Imported = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetsAutomated(Import);
    return Imported.Num() == 1 ? Imported[0] : nullptr;
}
bool ImportSound(const TCHAR* Name, const TCHAR* Script)
{
    UObject* Sound = ImportGenerated(TEXT("SourceAudio"), Name, TEXT("/Game/Marau6/Audio"), Script);
    return Cast<USoundWave>(Sound) && SavePackageOf(Sound);
}
UTexture2D* ImportEffectTexture(const TCHAR* Name, const TCHAR* Script = TEXT("scripts/Generate-ShotTextures.py"),
    bool bNormalMap = false)
{
    auto* Texture = Cast<UTexture2D>(ImportGenerated(TEXT("SourceTextures"), Name, TEXT("/Game/Marau6/Effects"), Script));
    if (!Texture) return nullptr;
    Texture->PreEditChange(nullptr);
    Texture->LODGroup = bNormalMap ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_Effects;
    Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Default;
    Texture->SRGB = !bNormalMap;
    Texture->PostEditChange();
    return SavePackageOf(Texture) ? Texture : nullptr;
}

void Setup()
{
    UTexture2D* FlashSide = ImportEffectTexture(TEXT("T_MuzzleFlash_Side.png"));
    UTexture2D* FlashFront = ImportEffectTexture(TEXT("T_MuzzleFlash_Front.png"));
    UTexture2D* SmokePuff = ImportEffectTexture(TEXT("T_MuzzleSmoke.png"));
    const TCHAR* ImpactScript = TEXT("scripts/Generate-ImpactTextures.py");
    UTexture2D* BloodSplatter = ImportEffectTexture(TEXT("T_BloodSplatter.png"), ImpactScript);
    UTexture2D* BloodMist = ImportEffectTexture(TEXT("T_BloodMist.png"), ImpactScript);
    UTexture2D* HolePaint = ImportEffectTexture(TEXT("T_BulletHole.png"), ImpactScript);
    UTexture2D* HoleNormal = ImportEffectTexture(TEXT("T_BulletHole_N.png"), ImpactScript, true);
    UTexture2D* Dust = ImportEffectTexture(TEXT("T_ImpactDust.png"), ImpactScript);
    if (!FlashSide || !FlashFront || !SmokePuff || !BloodSplatter || !BloodMist || !HolePaint || !HoleNormal || !Dust)
    {
        UE_LOG(LogTemp, Error, TEXT("MARAU6_SHOTFX asset generation failed: textures."));
        return;
    }

    // Flame painted premultiplied: its colour is the emission. Each blade picks its
    // texture (side or front) and atlas tile at runtime.
    UMaterial* Flash = Create(TEXT("M_MuzzleFlash"), BLEND_Additive);
    Flash->GetEditorOnlyData()->EmissiveColor.Connect(0, Mul(Flash, Mul(Flash,
        AtlasSample(Flash, FlashSide, Add<UMaterialExpressionTextureCoordinate>(Flash)),
        Color(Flash, FLinearColor(1.f, .9f, .8f))), Scalar(Flash, TEXT("Intensity"), 14.f)));

    // Muzzle smoke sprites, swelled by Grow (see GrowUV).
    UMaterial* Muzzle = Create(TEXT("M_MuzzleSmoke"), BLEND_Translucent);
    Muzzle->SetShadingModel(MSM_DefaultLit);
    Muzzle->TranslucencyLightingMode = TLM_VolumetricNonDirectional;
    auto* Puffs = AtlasSample(Muzzle, SmokePuff, GrowUV(Muzzle));
    UMaterialExpression* Tone = Mul(Muzzle, Puffs, Color(Muzzle, FLinearColor(.62f, .6f, .58f)));
    auto* Glow = Add<UMaterialExpressionConstant>(Muzzle);
    Glow->R = .12f;
    Muzzle->GetEditorOnlyData()->BaseColor.Connect(0, Tone);
    Muzzle->GetEditorOnlyData()->EmissiveColor.Connect(0, Mul(Muzzle, Tone, Glow));
    auto* Density = Add<UMaterialExpressionMultiply>(Muzzle);
    Density->A.Connect(4, Puffs);
    Density->B.Connect(0, Scalar(Muzzle, TEXT("Opacity"), .4f));
    Muzzle->GetEditorOnlyData()->Opacity.Connect(0, Density);

    UMaterial* Tracer = Create(TEXT("M_Tracer"), BLEND_Additive);
    Tracer->GetEditorOnlyData()->EmissiveColor.Connect(0,
        Mul(Tracer, Mul(Tracer, Color(Tracer, FLinearColor(1.f, .7f, .35f)), Scalar(Tracer, TEXT("Intensity"), 25.f)), TubeMask(Tracer, 2.f)));

    // Smoke breaks up through world-space noise, so a drifting trail looks wispy.
    UMaterial* Smoke = Create(TEXT("M_ShotSmoke"), BLEND_Translucent);
    auto* Wisps = Add<UMaterialExpressionNoise>(Smoke);
    Wisps->Scale = .04f;
    Wisps->Levels = 3;
    Wisps->bTurbulence = true;
    Wisps->OutputMin = .15f;
    Wisps->OutputMax = 1.f;
    Smoke->GetEditorOnlyData()->EmissiveColor.Connect(0, Color(Smoke, FLinearColor(.55f, .55f, .55f)));
    Smoke->GetEditorOnlyData()->Opacity.Connect(0,
        Mul(Smoke, Mul(Smoke, Scalar(Smoke, TEXT("Opacity"), .3f), TubeMask(Smoke, 1.5f)), Wisps));

    // Impact dust and blood mist: a lit sprite of the dust atlas (the blood mist atlas
    // is swapped in through Puff at runtime), tinted by Color, swelled by Grow and
    // faded where it meets the surface it came off.
    UMaterial* Puff = Create(TEXT("M_ImpactPuff"), BLEND_Translucent);
    Puff->SetShadingModel(MSM_DefaultLit);
    Puff->TranslucencyLightingMode = TLM_VolumetricNonDirectional;
    auto* Cloud = AtlasSample(Puff, Dust, GrowUV(Puff), TEXT("Puff"));
    UMaterialExpression* Shade = Mul(Puff, Cloud, Color(Puff, FLinearColor(.45f, .42f, .38f)));
    auto* Ambient = Add<UMaterialExpressionConstant>(Puff);
    Ambient->R = .3f;
    Puff->GetEditorOnlyData()->BaseColor.Connect(0, Shade);
    Puff->GetEditorOnlyData()->EmissiveColor.Connect(0, Mul(Puff, Shade, Ambient));
    auto* Thickness = Add<UMaterialExpressionMultiply>(Puff);
    Thickness->A.Connect(4, Cloud);
    Thickness->B.Connect(0, Scalar(Puff, TEXT("Opacity"), .5f));
    auto* Soften = Add<UMaterialExpressionDepthFade>(Puff);
    Soften->InOpacity.Connect(0, Thickness);
    Soften->FadeDistanceDefault = 3.f;
    Puff->GetEditorOnlyData()->Opacity.Connect(0, Soften);

    // Blood drops in flight: tubes, opaque where they face the viewer, broken up by noise.
    UMaterial* Drop = Create(TEXT("M_BloodDrop"), BLEND_Translucent);
    auto* Breakup = Add<UMaterialExpressionNoise>(Drop);
    Breakup->Scale = .08f;
    Breakup->Levels = 3;
    Breakup->OutputMin = .3f;
    Breakup->OutputMax = 1.f;
    auto* Soft = Add<UMaterialExpressionFresnel>(Drop);
    Soft->Exponent = 1.2f;
    Soft->BaseReflectFraction = 0.f;
    auto* Core = Add<UMaterialExpressionOneMinus>(Drop);
    Core->Input.Connect(0, Soft);
    Drop->GetEditorOnlyData()->EmissiveColor.Connect(0, Color(Drop, FLinearColor(.22f, 0.f, 0.f)));
    Drop->GetEditorOnlyData()->Opacity.Connect(0, Mul(Drop, Mul(Drop, Scalar(Drop, TEXT("Opacity"), .95f), Core), Breakup));

    UMaterial* Hole = Decal(TEXT("M_BulletHole"), HolePaint, HoleNormal, FLinearColor::White, .85f, 0.f);
    UMaterial* Blood = Decal(TEXT("M_BloodDecal"), BloodSplatter, nullptr, FLinearColor::White, .08f, .6f);

    bool bSaved = true;
    for (UMaterial* M : { Flash, Muzzle, Tracer, Smoke, Puff, Drop, Hole, Blood }) bSaved &= Save(M);
    if (!bSaved || !ImportSound(TEXT("S_RifleFire.wav"), TEXT("the ffmpeg conversion in docs/som-disparo.md"))
        || !ImportSound(TEXT("S_Decompression.wav"), TEXT("scripts/Generate-Decompression.py")))
        UE_LOG(LogTemp, Error, TEXT("MARAU6_SHOTFX asset generation failed."));
}
static FAutoConsoleCommand SetupCommand(TEXT("Marau6.SetupShotFX"),
    TEXT("Import the shot and impact textures, create or replace the shot materials in /Game/Marau6/Effects and import the rifle fire and decompression gas."),
    FConsoleCommandDelegate::CreateStatic(&Setup));
}
#endif
