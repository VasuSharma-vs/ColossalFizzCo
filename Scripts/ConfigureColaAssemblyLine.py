"""Seed the two isolated prototype maps with existing manufacturing raw materials.
Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>, editor closed.
This is an idempotent editor migration; it never grants stock during play/load.
"""
import json
import shutil
from pathlib import Path
import unreal

ROOT = '/Game/NormalColaPrototype'
TAG = 'ColaPrototypeRawStock'
project = Path(unreal.Paths.project_dir())
evidence = project / 'Saved/ManufacturingQA/Assets_20261113'
evidence.mkdir(parents=True, exist_ok=True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
catalog = unreal.load_asset(ROOT + '/DA_NormalColaSupplyCatalog')
if not catalog:
    raise RuntimeError('Existing supply catalog missing')
offers = {str(o.get_editor_property('OfferID')): o for o in catalog.get_editor_property('Offers')}
def product(offer_id):
    ref = offers[offer_id].get_editor_property('Product')
    result = unreal.load_asset(str(ref)) if isinstance(ref, str) else ref
    if not isinstance(result, unreal.InteractablePropDataAsset):
        raise RuntimeError('Supply product missing: ' + offer_id + ' / ' + str(ref))
    return result
empty = product('EmptyBottle')
cap = product('TwistCap')
bulk = product('CylinderLemonade')
if empty.get_editor_property('DefaultContentLevel') != 0 or bulk.get_editor_property('DefaultContentLevel') != 1:
    raise RuntimeError('Expected empty bottles and full existing Normal bulk cola')
if empty.get_editor_property('BottleMaterialType') != unreal.BottleMaterialType.PLASTIC or not cap.get_editor_property('bIsBottleCap'):
    raise RuntimeError('Unexpected raw material types')

def footprint(data):
    mesh = data.get_editor_property('StaticMesh')
    if not mesh: raise RuntimeError('Stock mesh missing: ' + data.get_name())
    bounds = mesh.get_bounds()
    return bounds, bounds.box_extent.x * 2, bounds.box_extent.y * 2

b_bounds, bw, bd = footprint(empty)
c_bounds, cw, cd = footprint(cap)
s_bounds, sw, sd = footprint(bulk)
b_pitch_x, b_pitch_y = bw + 16, bd + 16
c_pitch_x, c_pitch_y = cw + 16, cd + 16
b_width, c_width = bw + b_pitch_x * 4, cw + c_pitch_x * 4
right_width = max(c_width, sw)
width = b_width + right_width + 60
depth = max(bd + b_pitch_y, cd + c_pitch_y + sd + 32) + 40
center = unreal.Vector(-2600, -2780, 105)
top = 110
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
report = {'scope': 'Existing plastic bottle, Normal manual cap and Lemonade Normal Bulk Cola Container only',
          'stock_per_map': {'empty_bottles': 10, 'normal_caps': 10, 'full_normal_bulk_containers': 1}, 'maps': []}
for name in ['Lvl_NormalColaPrototype', 'Lvl_NormalColaPrototype_Validation']:
    path = ROOT + '/' + name
    original = project / 'Content/NormalColaPrototype' / (name + '.umap')
    backup = evidence / 'Before' / original.name
    if not backup.exists():
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, backup)
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    if not world: raise RuntimeError('Cannot load ' + path)
    existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    rack = existing.get('Assembly line raw material rack')
    if not rack: rack = actors.spawn_actor_from_class(unreal.StaticMeshActor, center)
    rack.set_actor_label('Assembly line raw material rack')
    rack.static_mesh_component.set_static_mesh(cube)
    rack.static_mesh_component.set_collision_profile_name('BlockAll')
    rack.set_actor_location(center, False, True)
    rack.set_actor_scale3d(unreal.Vector((width + 40) / 100, depth / 100, .1))
    left = center.x - width / 2
    start_y = center.y - depth / 2 + 20
    sign = existing.get('Assembly line instructions')
    if not sign: sign = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(center.x, center.y - depth / 2 - 20, 140))
    sign.set_actor_label('Assembly line instructions')
    sign.set_actor_rotation(unreal.Rotator(yaw=-90), True)
    text = sign.get_component_by_class(unreal.TextRenderComponent)
    text.set_text('START HERE: COLA ASSEMBLY LINE\nEmpty bottle > Fill at workbench > Attach Normal cap\nTwo players: G holds, R turns > Place sealed bottle on shelf')
    text.set_world_size(12)
    spawned = []
    def place(data, label, x, y):
        bounds, _, _ = footprint(data)
        x -= bounds.origin.x
        y -= bounds.origin.y
        actor = existing.get(label)
        if not actor:
            cls = data.get_editor_property('CustomActiveActorClass') or unreal.ActivePropActor
            actor = actors.spawn_actor_from_class(cls, unreal.Vector(x, y, top - bounds.origin.z + bounds.box_extent.z + 1))
        if not isinstance(actor, unreal.ActivePropActor): raise RuntimeError('Wrong stock class: ' + label)
        actor.set_actor_label(label)
        for key, value in {'PropDataAsset': data, 'ISID': 0, 'ProductID': 0,
                           'State': unreal.PropState.AT_REST, 'bAutoConvertToISMIfAtRest': True,
                           'bDebugRandomizeContentLevelOnISMRegistration': False, 'Tags': [TAG]}.items():
            actor.set_editor_property(key, value)
        actor.set_actor_location(unreal.Vector(x, y, top - bounds.origin.z + bounds.box_extent.z + 1), False, True)
        actor.set_actor_rotation(unreal.Rotator(), True)
        actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
        spawned.append({'label': label, 'product': data.get_path_name(), 'position': [x, y, actor.get_actor_location().z],
                        'default_level': data.get_editor_property('DefaultContentLevel')})
    for i in range(10):
        place(empty, 'Assembly stock empty bottle ' + str(i + 1), left + bw / 2 + (i % 5) * b_pitch_x, start_y + bd / 2 + (i // 5) * b_pitch_y)
        place(cap, 'Assembly stock Normal cap ' + str(i + 1), left + b_width + 60 + cw / 2 + (i % 5) * c_pitch_x, start_y + cd / 2 + (i // 5) * c_pitch_y)
    place(bulk, 'Assembly stock Normal Bulk Cola Container', left + b_width + 60 + right_width / 2, start_y + cd + c_pitch_y + 32 + sd / 2)
    stock = [a for a in actors.get_all_level_actors() if a.actor_has_tag(TAG)]
    if len(stock) != 21: raise RuntimeError('Expected exactly 21 initial raw material identities')
    # Verify all stock footprints are supported and separated at native size.
    rectangles = []
    for actor in stock:
        data = actor.get_editor_property('PropDataAsset')
        bounds, sx, sy = footprint(data)
        location = actor.get_actor_location()
        rect = (location.x + bounds.origin.x - sx / 2, location.x + bounds.origin.x + sx / 2,
                location.y + bounds.origin.y - sy / 2, location.y + bounds.origin.y + sy / 2)
        if abs(location.x + bounds.origin.x - center.x) + sx / 2 > (width + 40) / 2 + .1 or abs(location.y + bounds.origin.y - center.y) + sy / 2 > depth / 2 + .1:
            raise RuntimeError('Unsupported stock placement: ' + actor.get_actor_label())
        for previous in rectangles:
            if rect[0] < previous[1] and rect[1] > previous[0] and rect[2] < previous[3] and rect[3] > previous[2]:
                raise RuntimeError('Overlapping raw stock: ' + actor.get_actor_label())
        rectangles.append(rect)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, path): raise RuntimeError('Map save failed: ' + path)
    report['maps'].append({'path': path, 'stock': spawned, 'supported_nonoverlapping_stock': True,
                           'rack_dimensions': [width + 40, depth, 10]})
(evidence / 'AssemblyLineAssets.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('COLA_ASSEMBLY_RAW_STOCK_CONFIGURED')
