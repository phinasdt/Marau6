"""Import the ground loot models as single static meshes under /Game/Marau6/Items.

Run with the editor closed:
UnrealEditor-Cmd Marau6.uproject -run=pythonscript -script=<absolute path to this file> -unattended -nullrhi
Sources default to SourceArt/Loot in the project; override with the MARAU6_LOOT_SOURCE environment variable.
"""

import os
from pathlib import Path
import unreal


SOURCE_DIR = Path(os.environ.get('MARAU6_LOOT_SOURCE', str(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'SourceArt' / 'Loot')))
ASSET_DIR = '/Game/Marau6/Items'

SOURCES = {
    'Medkit': SOURCE_DIR / 'medkit_low.glb',
    'AmmoBox': SOURCE_DIR / 'ammo_box.glb',
    'ScrapMetal': SOURCE_DIR / 'scrap_metal.glb',
}
for source in SOURCES.values():
    if not source.is_file():
        raise RuntimeError('Missing loot model: {}'.format(source))

manager = unreal.InterchangeManager.get_interchange_manager_scripted()
gltf_pipeline = '/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline'

for name, source in SOURCES.items():
    folder = '{}/{}'.format(ASSET_DIR, name)
    # The medkit carries a stray joint and the ammo box is eleven parts:
    # force one static mesh per file so the loot actor can swap a single asset.
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property('asset_name', 'SM_' + name)
    common = pipeline.get_editor_property('common_meshes_properties')
    common.set_editor_property('force_all_mesh_as_type', unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    common.set_editor_property('import_lods', False)
    meshes = pipeline.get_editor_property('mesh_pipeline')
    meshes.set_editor_property('import_static_meshes', True)
    meshes.set_editor_property('import_skeletal_meshes', False)
    meshes.set_editor_property('combine_static_meshes_behavior', unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    meshes.set_editor_property('collision', False)
    pipeline.get_editor_property('animation_pipeline').set_editor_property('import_animations', False)

    params = unreal.ImportAssetParameters()
    params.set_editor_property('is_automated', True)
    params.set_editor_property('replace_existing', True)
    overrides = [unreal.SoftObjectPath(pipeline.get_path_name())]
    if unreal.EditorAssetLibrary.does_asset_exist(gltf_pipeline.split('.')[0]):
        overrides.append(unreal.SoftObjectPath(gltf_pipeline))
    params.set_editor_property('override_pipelines', overrides)

    manager.import_asset(folder, unreal.InterchangeManager.create_source_data(str(source)), params)
    if hasattr(manager, 'wait_until_all_tasks_done'):
        manager.wait_until_all_tasks_done(False)

unreal.EditorAssetLibrary.save_directory(ASSET_DIR)

for name in SOURCES:
    folder = '{}/{}'.format(ASSET_DIR, name)
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True):
        asset = unreal.load_asset(path)
        line = '{} {}'.format(asset.get_class().get_name(), path)
        if isinstance(asset, unreal.StaticMesh):
            box = asset.get_bounding_box()
            line += ' min={} max={} materials={}'.format(box.min, box.max, len(asset.static_materials))
        unreal.log('Marau6 loot import: ' + line)
