"""Import the generated inventory icons as non-streaming Unreal UI textures.

Run with the Unreal Editor closed:
UnrealEditor-Cmd Marau6.uproject -run=pythonscript -script=<absolute path> -unattended -nullrhi
"""

from pathlib import Path
import unreal


PROJECT = Path(unreal.Paths.project_dir())
SOURCE_DIR = PROJECT / 'Content' / 'Marau6' / 'UI' / 'Icons'
DESTINATION = '/Game/Marau6/UI/Icons'
ICONS = (
    'T_UI_Icon_Rifle',
    'T_UI_Icon_Medkit',
    'T_UI_Icon_Ammo',
    'T_UI_Icon_Scrap',
)

for name in ICONS:
    source = SOURCE_DIR / (name + '.png')
    if not source.is_file():
        raise RuntimeError('Missing inventory icon: {}'.format(source))

    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(source))
    task.set_editor_property('destination_path', DESTINATION)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    asset_path = DESTINATION + '/' + name
    texture = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not texture:
        raise RuntimeError('Inventory icon import failed: {}'.format(asset_path))

    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('never_stream', True)
    texture.set_editor_property('srgb', True)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    unreal.log('Imported {} {}x{}'.format(name, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))
