"""Ajusta as texturas da lasgun para a arma em primeira pessoa ficar nitida.

Executar (editor fechado):
  UnrealEditor-Cmd.exe Marau6.uproject -run=pythonscript -script=<abs>/scripts/fix_lasgun_textures.py -unattended -nullrhi

O que faz em cada textura de /Game/Weapons/Lasgun/Textures:
  - filter = TF_Default: o importador glTF (Interchange) gravou TF_Trilinear vindo do
    sampler do .glb, o que desliga o filtro anisotropico. Default usa o filtro do grupo (aniso).
  - LODGroup Weapon / WeaponNormalMap / WeaponSpecular (aniso, sem limite de tamanho, LODBias 0).
  - never_stream = True: a arma em primeira pessoa fica colada na camera; mantem sempre o mip 0.
  - lod_bias = 0, max_texture_size = 0, mip_gen_settings = do grupo.
  - Compressao: _BC  -> TC_Default sRGB; _N -> TC_Normalmap (BC5) linear;
                _ORM -> TC_Default linear (o T_Lasgun_LasgunStock2_ORM tinha sido importado
                como normal map, o que estragava oclusao/rugosidade/metalico da coronha 2).
"""
import unreal

TEX_PATH = "/Game/Weapons/Lasgun/Textures"

KIND = {
    "_BC": (unreal.TextureGroup.TEXTUREGROUP_WEAPON, unreal.TextureCompressionSettings.TC_DEFAULT, True),
    "_N": (unreal.TextureGroup.TEXTUREGROUP_WEAPON_NORMAL_MAP, unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    "_ORM": (unreal.TextureGroup.TEXTUREGROUP_WEAPON_SPECULAR, unreal.TextureCompressionSettings.TC_DEFAULT, False),
}

COMMON = [
    ("filter", unreal.TextureFilter.TF_DEFAULT),
    ("never_stream", True),
    ("lod_bias", 0),
    ("max_texture_size", 0),
    ("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP),
    ("virtual_texture_streaming", False),
]


def kind_of(name):
    for suffix in ("_ORM", "_BC", "_N"):
        if name.endswith(suffix):
            return KIND[suffix]
    return None


changed = 0
for path in unreal.EditorAssetLibrary.list_assets(TEX_PATH, recursive=True, include_folder=False):
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(tex, unreal.Texture2D):
        continue
    kind = kind_of(tex.get_name())
    if kind is None:
        unreal.log_warning("fix_lasgun_textures: sufixo desconhecido, ignorado: %s" % path)
        continue
    group, compression, srgb = kind
    wanted = [("lod_group", group), ("compression_settings", compression), ("srgb", srgb)] + COMMON
    diffs = [(k, v) for k, v in wanted if tex.get_editor_property(k) != v]
    if not diffs:
        continue
    for k, v in diffs:
        unreal.log("fix_lasgun_textures: %s %s: %s -> %s" % (tex.get_name(), k, tex.get_editor_property(k), v))
    # set_editor_properties dispara Pre/PostEditChange uma vez e reconstroi a textura.
    tex.set_editor_properties(dict(diffs))
    if unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
        changed += 1
    else:
        unreal.log_error("fix_lasgun_textures: falha ao salvar %s" % path)

unreal.log("fix_lasgun_textures: %d texturas alteradas" % changed)
