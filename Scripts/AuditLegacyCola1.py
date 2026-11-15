"""Read-only audit before replacing the legacy Cola1 container asset."""
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.actor import ActorTools

out = Path(unreal.Paths.project_saved_dir()) / 'Cola1Migration'
out.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
options = unreal.AssetRegistryDependencyOptions(True, True, True, True, True)
paths = ['/Game/Assest/meshs/Cola1', '/Game/Assest/meshs/SKM/SKM_ColaBottel_N', '/Game/Assest/meshs/SKM/SKM_ColaBottel']
report = {'references': {}, 'products': [], 'blueprints': [], 'maps': [], 'api': {}, 'legacy_asset_class': str(registry.get_asset_by_object_path(paths[0]+'.Cola1').asset_class_path.asset_name)}
for path in paths:
    report['references'][path] = [str(x) for x in registry.get_referencers(path, options)]
for name in ['consolidate_assets', 'find_package_referencers_for_asset']:
    report['api'][name] = getattr(unreal.EditorAssetLibrary, name).__doc__
report['api']['load_package'] = str(getattr(unreal, 'load_package', None))

def get(obj, name):
    try:
        value = obj.get_editor_property(name)
        if isinstance(value, (int, float, bool, str)) or value is None:
            return value
        if isinstance(value, unreal.Object):
            return value.get_path_name()
        return str(value)
    except Exception as error:
        return 'ERROR: ' + str(error)

assets = registry.get_assets_by_path('/Game', recursive=True)
for data in assets:
    if str(data.asset_class_path.asset_name) != 'InteractablePropDataAsset':
        continue
    asset = data.get_asset()
    mesh = get(asset, 'StaticMesh')
    if mesh not in [paths[0] + '.Cola1', '/Game/Assest/meshs/PlasticBottle/SM_PlasticBottle.SM_PlasticBottle']:
        continue
    report['products'].append({'path': asset.get_path_name(), **{key: get(asset, key) for key in [
        'StaticMesh', 'SkeletalMesh', 'MaterialOverrides', 'BottleMaterialType', 'FragmentAssembly',
        'Price', 'NumOfPortions', 'DefaultContentID', 'DefaultContentLevel', 'BottleMass',
        'CustomActiveActorClass', 'bEnableInstanceUniqueData', 'LiquidMaterial']}})

refs = set(sum(report['references'].values(), []))
refs.update(['/Game/Props/Colas/BP_Cola', '/Game/Props/Colas/BP_PlastickBottel',
             '/Game/Props/Colas/BP_SpicyCola', '/Game/Props/Colas/BP_SugerCrushCola',
             '/Game/TestingArea/TestingActor/TBP_InstanceActors'])
for package in sorted(refs):
    if '/__ExternalActors__/' in package:
        continue
    obj = unreal.load_asset(package)
    if not isinstance(obj, unreal.Blueprint):
        continue
    cdo = unreal.get_default_object(obj.generated_class())
    parts = ActorTools.get_components(cdo)
    report['blueprints'].append({'path': package, 'components': [
        {'name': c.get_name(), 'class': c.get_class().get_name(), 'static': get(c, 'StaticMesh') if isinstance(c, unreal.StaticMeshComponent) else None,
         'skeletal': get(c, 'SkeletalMeshAsset') if isinstance(c, unreal.SkeletalMeshComponent) else None}
        for c in parts if isinstance(c, (unreal.StaticMeshComponent, unreal.SkeletalMeshComponent))]})
for map_path in ['/Game/FirstPerson/Lvl_FirstPerson', '/Game/NormalColaPrototype/Lvl_NormalColaPrototype', '/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation']:
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if not world:
        raise RuntimeError('Cannot inspect ' + map_path)
    matches = []
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        parts = actor.get_components_by_class(unreal.StaticMeshComponent) + actor.get_components_by_class(unreal.SkeletalMeshComponent)
        old = [c.get_name() for c in parts if any(x in str(get(c, 'StaticMesh') if isinstance(c, unreal.StaticMeshComponent) else get(c, 'SkeletalMeshAsset')) for x in paths)]
        if old:
            matches.append({'path': actor.get_path_name(), 'label': actor.get_actor_label(), 'class': actor.get_class().get_name(), 'components': old})
    report['maps'].append({'path': map_path, 'legacy_actors': matches})
(out / 'Audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('COLA1_READ_ONLY_AUDIT_COMPLETE products=' + str(len(report['products'])))
