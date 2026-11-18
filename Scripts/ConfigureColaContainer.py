"""Upgrade the designer's tank Blueprint, preserving its mesh/scale and existing graphs."""
from pathlib import Path
import json
import unreal

out=Path(unreal.Paths.project_dir())/'Saved/ColaContainer_20261118'
out.mkdir(parents=True,exist_ok=True)
folder='/Game/Assest/Blueprints/Cola_Container'
path=folder+'/BP_ColaCointainer'
tools=unreal.AssetToolsHelpers.get_asset_tools()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report={'assets':[]}

def save(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    status=str(bp.get_editor_property('status'))
    if 'ERROR' in status.upper(): raise RuntimeError('Blueprint compile failed: '+bp.get_path_name())
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp,False): raise RuntimeError('Save failed: '+bp.get_path_name())
    report['assets'].append({'asset':bp.get_path_name(),'status':status})

def material(name,color,metallic):
    asset=unreal.load_asset(folder+'/'+name) if unreal.EditorAssetLibrary.does_asset_exist(folder+'/'+name) else None
    if asset: return asset
    asset=tools.create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
    rgb=unreal.MaterialEditingLibrary.create_material_expression(asset,unreal.MaterialExpressionConstant3Vector,-300,0)
    rgb.set_editor_property('constant',unreal.LinearColor(*color))
    unreal.MaterialEditingLibrary.connect_material_property(rgb,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for prop,value,y in [(unreal.MaterialProperty.MP_METALLIC,metallic,100),(unreal.MaterialProperty.MP_ROUGHNESS,.3,200)]:
        scalar=unreal.MaterialEditingLibrary.create_material_expression(asset,unreal.MaterialExpressionConstant,-300,y)
        scalar.set_editor_property('r',value)
        unreal.MaterialEditingLibrary.connect_material_property(scalar,'',prop)
    unreal.MaterialEditingLibrary.recompile_material(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    return asset

bp=unreal.load_asset(path)
if not isinstance(bp,unreal.Blueprint): raise RuntimeError('Expected existing tank Blueprint '+path)
template=unreal.load_object(None,path+'.BP_ColaCointainer_C:StaticMesh_GEN_VARIABLE')
before={key:str(template.get_editor_property(key)) for key in ['static_mesh','relative_location','relative_rotation','relative_scale3d','override_materials']}
unreal.BlueprintEditorLibrary.reparent_blueprint(bp,unreal.PressurizedColaContainer)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=unreal.get_default_object(bp.generated_class())
for key,value in {'initial_units':200.,'outlet_pressure':8.,'outlet_temperature':85.,'max_outlet_units_per_second':8.,'valve_opening':0.}.items():
    cdo.set_editor_property(key,value)
port=cdo.get_editor_property('supply_port')
port.set_editor_property('capacity_units',200.)
port.set_editor_property('supply_enabled',False)
port.set_editor_property('allow_managed_content_bridge',True)
port.set_editor_property('max_connected_hoses',1)

# Use the product catalog's stable PID, not the legacy data asset's runtime ID.
catalog=unreal.load_asset('/Game/Props/Products/DA_ProductCatalog')
if not catalog: raise RuntimeError('Product catalog is missing')
content_id=0
for entry in catalog.get_editor_property('products'):
    definition=str(entry.get_editor_property('definition'))
    if 'DA_Content_Lemonade' in definition:
        content_id=entry.get_editor_property('pid'); break
if content_id<=0: raise RuntimeError('Lemonade catalog PID not found')
port.set_editor_property('resource_id',content_id)
port.set_editor_property('content_temperature',85.)

template=unreal.load_object(None,path+'.BP_ColaCointainer_C:StaticMesh_GEN_VARIABLE')
mesh=template.get_editor_property('static_mesh')
box=mesh.get_bounding_box(); scale=template.get_editor_property('relative_scale3d')
offset=template.get_editor_property('relative_location')
# Put the new controls just outside the authored tank's +X face. All remain viewport-editable.
face=offset.x+box.max.x*scale.x
center_y=offset.y+(box.min.y+box.max.y)*.5*scale.y
center_z=offset.z+(box.min.z+box.max.z)*.5*scale.z
port.set_editor_property('relative_location',unreal.Vector(face+35,center_y,center_z))
valve=cdo.get_editor_property('valve_pivot')
valve.set_editor_property('relative_location',unreal.Vector(12,0,55))
valve.set_editor_property('relative_rotation',unreal.Rotator(-90,0,0))
display=cdo.get_editor_property('supply_display')
display.set_editor_property('relative_location',unreal.Vector(face+36,center_y,center_z-30))
display.set_editor_property('world_size',12.)
wheel=cdo.get_editor_property('valve_wheel')
wheel.set_material(0,material('M_ColaValveRed',(.22,.012,.006,1),.7))
for component in cdo.get_components_by_class(unreal.StaticMeshComponent):
    if component.get_name()=='ValveSpoke': component.set_material(0,material('M_ColaValveBrass',(.65,.32,.045,1),.85))
save(bp)
after={key:str(template.get_editor_property(key)) for key in before}
if after!=before: raise RuntimeError('Authored tank art changed during migration')
report['container']={'content_id':content_id,'preserved_art':after,'capacity':200,'temperature':85,'pressure':8,'max_rate':8,'valve_opening':0,
    'port_location':str(port.get_editor_property('relative_location'))}

# A matching two-ended hose preset uses the existing reusable transport actor.
hose_path=folder+'/BP_ColaHose'
if unreal.EditorAssetLibrary.does_asset_exist(hose_path): hose=unreal.load_asset(hose_path)
else:
    factory=unreal.BlueprintFactory(); factory.set_editor_property('parent_class',unreal.HoseCableActor)
    hose=tools.create_asset('BP_ColaHose',folder,unreal.Blueprint,factory)
hcdo=unreal.get_default_object(hose.generated_class())
transfer=hcdo.get_editor_property('transfer')
transfer.set_editor_property('resource_id',content_id)
transfer.set_editor_property('max_units_per_second',5.)
transfer.set_editor_property('buffer_capacity_units',2.)
transfer.set_editor_property('burst_pressure_threshold',4.)
transfer.set_editor_property('burst_delay',.25)
cable=hcdo.get_editor_property('cable')
cable.set_editor_property('cable_length',1200.)
cable.set_editor_property('num_segments',32)
cable.set_editor_property('end_location',unreal.Vector(500,0,0))
save(hose)
for other in ['/Game/NormalColaPrototype/BP_ManualColaFillingMachine','/Game/NormalColaPrototype/BP_ColaSupplyContainer']:
    asset=unreal.load_asset(other)
    if asset: save(asset)

spawned=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(0,0,0))
try:
    report['spawned']={'class':spawned.get_class().get_name(),'port':str(spawned.get_editor_property('supply_port').get_editor_property('relative_location')),
        'valve_opening':spawned.get_editor_property('valve_opening'),
        'components':[c.get_name() for c in spawned.get_components_by_class(unreal.SceneComponent)]}
finally: actors.destroy_actor(spawned)
(out/'AssetConfiguration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('COLA_CONTAINER_ASSETS_VERIFIED')
