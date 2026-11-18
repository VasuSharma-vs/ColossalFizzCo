"""Create a small, isolated map for the opt-in real host/client machine visual check."""
from pathlib import Path
import json
import unreal

path = '/Game/__CodexMachineVisualCheck/Lvl_ManualMachineVisual'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError('The isolated visual-check map already exists; inspect it instead of overwriting it.')

machine_bp = unreal.load_asset('/Game/NormalColaPrototype/BP_ManualColaFillingMachine')
product = unreal.load_asset('/Game/NormalColaPrototype/DA_ManualFillingMachine')
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
if not machine_bp or not product or not cube:
    raise RuntimeError('The saved machine Blueprint, product definition, or engine cube is missing.')

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
if not world:
    raise RuntimeError('Could not create the isolated visual-check world.')
world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase.static_class())
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def spawn(cls, location, label):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location))
    if not actor:
        raise RuntimeError('Could not spawn ' + label)
    actor.set_actor_label(label)
    return actor

floor = spawn(unreal.StaticMeshActor, (0, 0, -5), 'Machine visual check ground')
floor.static_mesh_component.set_static_mesh(cube)
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(100, 30, .1))
manager = spawn(unreal.PropManager, (0, 0, 0), 'Isolated machine prop manager')
for name, value in {'InitialSaveSlot': '', 'AutosaveSlot': 'MachineVisualCheck_NoAutosave',
                    'bEnableAutosave': False, 'ProductCatalog': None,
                    'DefaultProductDataAssets': [product]}.items():
    manager.set_editor_property(name, value)

machine = spawn(machine_bp.generated_class(), (0, 0, 100), 'Actual saved manual filling machine')
for name, value in {'PropDataAsset': product, 'ISID': 0, 'ProductID': 0,
                    'State': unreal.PropState.UNSETTLED, 'bAutoConvertToISMIfAtRest': False}.items():
    machine.set_editor_property(name, value)
machine.set_actor_scale3d(unreal.Vector(1, 1, 1))
machine.set_replicates(True)
machine.set_replicate_movement(True)
fixture = spawn(unreal.NormalColaPrototypeTestActor, (0, 0, 0), 'Manual machine visual network fixture')
spawn(unreal.PlayerStart, (600, 0, 150), 'Isolated visual check player start')

if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
    raise RuntimeError('Could not save the isolated machine visual-check map.')
result = {'map': path, 'saved': True, 'machine_class': machine.get_class().get_path_name(),
          'product': product.get_path_name(), 'actor_count': len(actors.get_all_level_actors()),
          'fixture': fixture.get_name(), 'autoload_save': '', 'autosave': False,
          'catalog': 'Only the real manual machine product; unrelated game art is not referenced.'}
out = Path(unreal.Paths.project_saved_dir()) / 'ManualMachineVisual_20261118'
out.mkdir(parents=True, exist_ok=True)
(out / 'IsolatedMap.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
unreal.log('MANUAL_MACHINE_VISUAL_MAP_CREATED ' + path)
