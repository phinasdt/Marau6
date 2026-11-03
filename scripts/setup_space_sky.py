"""Build the procedural space sky and install it in the FirstPerson test map.

Run with UnrealEditor-Cmd -run=pythonscript -script=<absolute path to this file>.
"""

from pathlib import Path
import re
import unreal


PROJECT = Path(unreal.Paths.project_dir())
SOURCE_DIR = PROJECT / 'Content' / 'Marau6' / 'Sky'
ASSET_DIR = '/Game/Marau6/Sky'
MAP = '/Game/FirstPerson/Lvl_FirstPerson'

SKY_SHADER = PROJECT / 'scripts' / 'space_sky.hlsl'

SOURCES = {
    'T_Marau6_OchrePlanet': SOURCE_DIR / 'Marau6_OchrePlanet.png',
    'T_Marau6_BluePlanet': SOURCE_DIR / 'Marau6_BluePlanet.png',
}
for source in SOURCES.values():
    if not source.is_file():
        raise RuntimeError('Missing sky image: {}'.format(source))

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
tasks = []
for name, source in SOURCES.items():
    # Delete the texture asset to reimport a changed image.
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_DIR + '/' + name):
        continue
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(source))
    task.set_editor_property('destination_path', ASSET_DIR)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    tasks.append(task)
asset_tools.import_asset_tasks(tasks)
textures = {name: unreal.EditorAssetLibrary.load_asset(ASSET_DIR + '/' + name)
            for name in SOURCES}
if not all(textures.values()):
    raise RuntimeError('A sky texture import failed')

def follow_camera(material):
    """Shift every vertex by the camera position. The sky sphere and the planets sit around
    the world origin, so this keeps them centred on the viewer: they read as infinitely far
    away and do not shift against the sky while the player walks around the arena."""
    camera = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionCameraPositionWS, -350, 300)
    unreal.MaterialEditingLibrary.connect_material_property(
        camera, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

def make_material(name, texture, transparent=False):
    path = ASSET_DIR + '/' + name
    material = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not material:
        material = asset_tools.create_asset(name, ASSET_DIR,
                                            unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError('Material creation failed: ' + name)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    if transparent:
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    else:
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
        material.set_editor_property('is_sky', True)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    sample = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSample, -350, 0)
    sample.set_editor_property('texture', texture)
    unreal.MaterialEditingLibrary.connect_material_property(
        sample, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if transparent:
        unreal.MaterialEditingLibrary.connect_material_property(
            sample, 'A', unreal.MaterialProperty.MP_OPACITY)
    follow_camera(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    return material

def make_sky_material():
    """Unlit sky whose colour comes from scripts/space_sky.hlsl in a Custom node."""
    name = 'M_Marau6_SpaceSky'
    path = ASSET_DIR + '/' + name
    material = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not material:
        material = asset_tools.create_asset(name, ASSET_DIR,
                                            unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError('Material creation failed: ' + name)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    # With r.SkyAtmosphere=0 a sky-flagged mesh is not drawn at all: the sky stays black.
    material.set_editor_property('is_sky', False)
    lib = unreal.MaterialEditingLibrary
    lib.delete_all_material_expressions(material)

    # The functions live in a struct because a Custom node's code is a function body.
    source = re.sub(r'//[^\n]*', '', SKY_SHADER.read_text(encoding='utf-8'))
    code = ('struct FMarau6SpaceSky\n{\n' + source + '\n};\n'
            'FMarau6SpaceSky Sky;\n'
            'float3 d = -normalize(CamVec);\n'
            'float pix = length(fwidth(d));\n'
            'return Sky.SpaceSky(d, SkyTime * Speed, pix, AuroraGain, NebulaGain, StarGain);\n')
    custom = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property('code', code)
    custom.set_editor_property('description', 'Marau6 Space Sky')
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = ['CamVec', 'SkyTime', 'Speed', 'AuroraGain', 'NebulaGain', 'StarGain']
    inputs = []
    for pin in pins:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property('input_name', pin)
        inputs.append(custom_input)
    custom.set_editor_property('inputs', inputs)

    camera = lib.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -700, -200)
    time = lib.create_material_expression(material, unreal.MaterialExpressionTime, -700, -100)
    lib.connect_material_expressions(camera, '', custom, 'CamVec')
    lib.connect_material_expressions(time, '', custom, 'SkyTime')
    # Tweak these on a material instance to tune the sky without editing the shader.
    for row, (pin, default) in enumerate((('Speed', 1.0), ('AuroraGain', 1.0),
                                          ('NebulaGain', 1.0), ('StarGain', 1.0))):
        param = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter,
                                               -700, row * 100)
        param.set_editor_property('parameter_name', pin)
        param.set_editor_property('default_value', default)
        lib.connect_material_expressions(param, '', custom, pin)
    lib.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    follow_camera(material)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    return material

sky_material = make_sky_material()
# The old 8K panorama rendered blurry; the procedural sky replaces it.
OLD_PANORAMA = ASSET_DIR + '/T_Marau6_SpaceSky'
if unreal.EditorAssetLibrary.does_asset_exist(OLD_PANORAMA):
    unreal.EditorAssetLibrary.delete_asset(OLD_PANORAMA)
(SOURCE_DIR / 'Marau6_SpaceSky_Panorama.png').unlink(missing_ok=True)
ochre_material = make_material('M_Marau6_OchrePlanet', textures['T_Marau6_OchrePlanet'], True)
blue_material = make_material('M_Marau6_BluePlanet', textures['T_Marau6_BluePlanet'], True)

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
actors = unreal.EditorLevelLibrary.get_all_level_actors()
# The template's volumetric clouds draw over the sky sphere; the sky has its own nebulae.
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for cloud in [a for a in actors if isinstance(a, unreal.VolumetricCloud)]:
    actor_subsystem.destroy_actor(cloud)
actors = unreal.EditorLevelLibrary.get_all_level_actors()
sky = next((a for a in actors if a.get_actor_label() == 'Marau6 Space Sky'), None)
if not sky:
    sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    sky.set_actor_label('Marau6 Space Sky')

component = sky.static_mesh_component
# Each actor lives in its own World Partition package; mark it dirty so it is saved.
sky.modify()
component.modify()
component.set_static_mesh(unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Sphere'))
component.set_material(0, sky_material)
component.set_editor_property('cast_shadow', False)
# A colliding sphere this size fills the arena: the navmesh ends up empty and the
# destruction sections never spawn. The profile is what survives a reload.
component.set_collision_profile_name('NoCollision')
component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
component.set_editor_property('can_ever_affect_navigation', False)
sky.set_actor_location(unreal.Vector(0, 0, 0), False, False)
# A much larger sphere stops being drawn, so distance comes from follow_camera instead.
sky.set_actor_scale3d(unreal.Vector(1500, 1500, 1500))

plane = unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Plane')
def place_planet(label, material, position, scale):
    actor = next((a for a in actors if a.get_actor_label() == label), None)
    if not actor:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.StaticMeshActor, position)
        actor.set_actor_label(label)
    actor.modify()
    mesh = actor.static_mesh_component
    mesh.modify()
    mesh.set_static_mesh(plane)
    mesh.set_material(0, material)
    mesh.set_editor_property('cast_shadow', False)
    mesh.set_collision_profile_name('NoCollision')
    mesh.set_editor_property('can_ever_affect_navigation', False)
    # Culling uses the unshifted position; the margin covers the camera's offset in the arena.
    mesh.set_editor_property('bounds_scale', 2.0)
    actor.set_actor_location(position, False, False)
    actor.set_actor_rotation(unreal.MathLibrary.make_rot_from_z(
        unreal.Vector(-position.x, -position.y, -position.z)), False)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return actor

ochre = place_planet('Marau6 Ochre Planet', ochre_material,
                     unreal.Vector(35000, 12000, 20000), 170)
blue = place_planet('Marau6 Blue Planet', blue_material,
                    unreal.Vector(35000, -18000, 10500), 65)

if not unreal.EditorLevelLibrary.save_current_level():
    raise RuntimeError('Could not save FirstPerson test map')
if not unreal.EditorLoadingAndSavingUtils.save_packages(
        [ochre.get_outermost(), blue.get_outermost()], False):
    raise RuntimeError('Could not save planet actor packages')
# save_current_level skips the external actor packages that hold these actors.
if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False):
    raise RuntimeError('Could not save the sky actor packages')
unreal.log_warning('SPACE_SKY_OK: procedural sky material and two planet planes saved')
