"""Import the base/intendance corridor backdrop used behind the inventory UI.

Run with the editor closed:
UnrealEditor-Cmd Marau6.uproject -run=pythonscript -script=<absolute path to this file> -unattended -nullrhi
"""

from pathlib import Path
import unreal


PROJECT = Path(unreal.Paths.project_dir())
SOURCE = PROJECT / 'Content' / 'Marau6' / 'UI' / 'Marau6_BaseCorridor.png'
ASSET_DIR = '/Game/Marau6/UI'
NAME = 'T_Marau6_BaseCorridor'

if not SOURCE.is_file():
    raise RuntimeError('Missing UI background: {}'.format(SOURCE))

task = unreal.AssetImportTask()
task.set_editor_property('filename', str(SOURCE))
task.set_editor_property('destination_path', ASSET_DIR)
task.set_editor_property('destination_name', NAME)
task.set_editor_property('automated', True)
task.set_editor_property('replace_existing', True)
task.set_editor_property('save', False)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

texture = unreal.EditorAssetLibrary.load_asset(ASSET_DIR + '/' + NAME)
if not texture:
    raise RuntimeError('UI background import failed')
# Shown 1:1 in Slate: no mips or streaming, UI compression keeps the dark gradients clean.
texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property('never_stream', True)
texture.set_editor_property('srgb', True)
unreal.EditorAssetLibrary.save_asset(ASSET_DIR + '/' + NAME)
unreal.log('Imported {} {}x{}'.format(NAME, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))
