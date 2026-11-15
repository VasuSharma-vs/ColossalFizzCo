"""Replace Cola1 with the existing modular plastic assembly; run editor closed.

Backs up every candidate package before editing. A compatibility redirector is
retained for old save-file object paths; no old mesh geometry remains at Cola1.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
content = root / 'Content'
out = root / 'Saved/Cola1Migration'
out.mkdir(parents=True, exist_ok=True)
backup = out / 'Before'
old_path = '/Game/Assest/meshs/Cola1'
new_path = '/Game/Assest/meshs/PlasticBottle/SM_PlasticBottle'
old_sk = '/Game/Assest/meshs/SKM/SKM_ColaBottel_N'
new_sk = '/Game/Assest/meshs/PlasticBottle/SKM_PlasticBottle'
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
options = unreal.AssetRegistryDependencyOptions(True, True, False, False, False)
all_assets = registry.get_assets_by_path('/Game', recursive=True)
old = unreal.load_asset(old_path)
shell = unreal.load_asset(new_path)
skin = unreal.load_asset(new_sk)
old_skin = unreal.load_asset(old_sk)
catalog = unreal.load_asset('/Game/NormalColaPrototype/DA_NormalColaSupplyCatalog')
template = unreal.load_asset('/Game/NormalColaPrototype/DA_NormalEmptyBottle')
if not all([old, shell, skin, catalog, template]):
    raise RuntimeError('Required source, destination or catalog asset missing')

def disk(package):
    relative = package.removeprefix('/Game/')
    for suffix in ('.uasset', '.umap'):
        path = content / (relative + suffix)
        if path.is_file():
            return path
    return None

refs = set(str(x) for path in (old_path, old_sk)
           for x in registry.get_referencers(path, options) if str(x).startswith('/Game/'))
products = []
databases = []
for data in all_assets:
    kind = str(data.asset_class_path.asset_name)
    if kind == 'IISMDB':
        databases.append(data)
        refs.add(str(data.package_name))
    if kind != 'InteractablePropDataAsset':
        continue
    asset = data.get_asset()
    mesh = asset.get_editor_property('StaticMesh')
    if mesh in (old, shell) and not asset.get_editor_property('bIsBottleCap'):
        products.append(asset)
        refs.add(str(data.package_name))
refs.add(old_path)
refs.add('/Game/NormalColaPrototype/DA_NormalColaSupplyCatalog')
before = {}
for package in sorted(refs):
    f = disk(package)
    if f:
        rel = f.relative_to(content)
        before[rel.as_posix()] = hashlib.sha256(f.read_bytes()).hexdigest()
        destination = backup / rel
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, destination)
if not (out / 'BeforeManifest.json').exists():
    (out / 'BeforeManifest.json').write_text(json.dumps(before, indent=2), encoding='utf-8')
protected = {str(f.relative_to(content)): hashlib.sha256(f.read_bytes()).hexdigest()
             for f in (content / 'Assest/meshs/PlasticBottle').rglob('*.uasset')}

def resolve(ref):
    if isinstance(ref, unreal.Object):
        return ref
    return unreal.load_asset(str(ref)) if ref and str(ref) != 'None' else None

recipes = catalog.get_editor_property('Contents')
# Resolve catalog soft materials before reading them through Python bindings.
for data in all_assets:
    if str(data.asset_class_path.asset_name) in ('Material', 'MaterialInstanceConstant'):
        data.get_asset()
inner = unreal.load_asset('/Game/Assest/meshs/PlasticBottle/SM_PlasticBottelInnerContent')
default_liquid = inner.get_editor_property('StaticMaterials')[0].get_editor_property('MaterialInterface')
flavors = {'DA_BasicCola': 901, 'DA_Cola': 902, 'DA_GoodCola': 902,
           'DA_NormalCola': 905, 'DA_SweetCola': 903, 'DA_SpicyCola': 904}
economic_fields = ['Price', 'NumOfPortions', 'DefaultContentID', 'DefaultContentLevel',
                   'ConsumableTemperature', 'ExpiryLifetimeMinutes', 'TasteEntries', 'StimuliPerPortion']
economy = {a.get_path_name(): {k: str(a.get_editor_property(k)) for k in economic_fields} for a in products}
physical = ['BottleMaterialType', 'BottleMass', 'GameplayMassKg', 'PressureResistanceMultiplier',
            'PressureDamageThreshold', 'PressureFailureThreshold', 'StrengthDamageRate',
            'BreakageFailureThreshold', 'bCanInflate', 'bCanBend', 'bCanDent', 'bCanCrack',
            'bCanRecover', 'bSupportsRepair', 'DefaultCapType', 'SupportedCapMask']
template_values = {k: template.get_editor_property(k) for k in physical}
for asset in products:
    cid = asset.get_editor_property('DefaultContentID') or flavors.get(asset.get_name(), 901)
    row = recipes.get(cid) or recipes[901]
    visual = row.get_editor_property('VisualByBottle')[unreal.BottleMaterialType.PLASTIC]
    liquid = asset.get_editor_property('LiquidMaterial') or resolve(visual.get_editor_property('Material'))
    overrides = list(asset.get_editor_property('MaterialOverrides'))
    if asset.get_name() not in [x.get_name() for x in products if x.get_path_name().startswith('/Game/NormalColaPrototype/')]:
        if len(overrides) > 1:
            liquid = overrides[1]
    liquid = liquid or default_liquid
    if not liquid:
        raise RuntimeError('No separate liquid material for ' + asset.get_path_name())
    asset.modify()
    asset.set_editor_property('LiquidMaterial', liquid)
    asset.set_editor_property('StaticMesh', shell)
    asset.set_editor_property('SkeletalMesh', skin)
    asset.set_editor_property('MaterialOverrides', [])
    for k, v in template_values.items():
        asset.set_editor_property(k, v)
    asset.set_editor_property('DefaultCapMesh', unreal.load_asset('/Game/Assest/meshs/PlasticBottle/SM_NormalPlasticCap'))
    asset.set_editor_property('CapAttachmentTransform', unreal.Transform())
    asset.set_editor_property('FragmentAssembly', None)
    asset.set_editor_property('FractureFragments', [])
    asset.set_editor_property('bPersistBrokenHullAfterFracture', False)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Cannot save ' + asset.get_path_name())

packages = []
for package in sorted(refs):
    try:
        loaded = unreal.load_package(package)
        if loaded:
            packages.append(loaded)
    except Exception as error:
        raise RuntimeError('Cannot load referencer ' + package + ': ' + str(error))

changed_components = []
for kind in (unreal.StaticMeshComponent, unreal.SkeletalMeshComponent):
    for component in list(unreal.ObjectIterator(kind)):
        if not component.get_path_name().startswith('/Game/'):
            continue
        is_static = isinstance(component, unreal.StaticMeshComponent)
        mesh = component.get_editor_property('StaticMesh' if is_static else 'SkeletalMeshAsset')
        if mesh != (old if is_static else old_skin):
            continue
        component.modify()
        if is_static:
            component.set_static_mesh(shell)
        else:
            component.set_skeletal_mesh_asset(skin)
        component.set_editor_property('OverrideMaterials', [])
        changed_components.append(component.get_path_name())

for data in databases:
    asset = data.get_asset()
    definitions = asset.get_editor_property('ProductDefinitions')
    dirty = False
    for pid, definition in definitions.items():
        if definition.get_editor_property('StaticMesh') not in (old, shell):
            continue
        if definition.get_editor_property('bIsBottleCap') or definition.get_editor_property('bLiquidVisualOnly'):
            continue
        definition.set_editor_property('StaticMesh', shell)
        definition.set_editor_property('SkeletalMesh', skin)
        definition.set_editor_property('MaterialOverrides', [])
        definition.set_editor_property('BottleMaterialType', unreal.BottleMaterialType.PLASTIC)
        definition.set_editor_property('FragmentAssembly', None)
        definition.set_editor_property('FractureFragments', [])
        definitions[pid] = definition
        dirty = True
    if dirty:
        asset.modify()
        asset.set_editor_property('ProductDefinitions', definitions)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
            raise RuntimeError('Cannot save database ' + asset.get_path_name())

if old != shell and not unreal.EditorAssetLibrary.consolidate_assets(shell, [old]):
    raise RuntimeError('Unreal asset consolidation failed; restore from Before backup before retrying')
# An external actor can keep its dirty flag on the owning world instead of its
# external package. Force the known referencers to disk so unloaded WP actors
# do not continue to serialize the old mesh path.
if not unreal.EditorLoadingAndSavingUtils.save_packages(packages, False):
    raise RuntimeError('One or more referencer packages could not be saved')
for asset in products:
    for k in economic_fields:
        if str(asset.get_editor_property(k)) != economy[asset.get_path_name()][k]:
            raise RuntimeError('Unexpected economic/content change: ' + asset.get_path_name() + ' ' + k)
for relative, digest in protected.items():
    if hashlib.sha256((content / relative).read_bytes()).hexdigest() != digest:
        raise RuntimeError('Supplied plastic source art changed: ' + relative)
report = {'products': [a.get_path_name() for a in products], 'candidate_packages': len(refs),
          'changed_components': changed_components, 'economy_preserved': True,
          'source_art_preserved': True, 'compatibility_redirector': old_path,
          'destination': new_path, 'backup': str(backup)}
(out / 'Migration.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('COLA1_MIGRATION_COMPLETE ' + str(len(products)) + ' products')
