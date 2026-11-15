"""Scoped manual filling update. Save/close the editor first; existing products and saves are retained."""
import json
import shutil
from pathlib import Path
import unreal

ROOT='/Game/NormalColaPrototype'
project=Path(unreal.Paths.project_dir())
out=project/'Saved/ManualFillingUpdate'
out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def backup(path):
    source=project/'Content'/(path.removeprefix('/Game/')+'.uasset')
    if not source.exists(): source=source.with_suffix('.umap')
    dest=out/'BeforeAssets'/source.relative_to(project/'Content')
    if source.exists() and not dest.exists(): dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,dest)
def fields(obj,values):
    for k,v in values.items(): obj.set_editor_property(k,v)

adapter_path=ROOT+'/BP_ManualColaFillingMachine'
backup(adapter_path)
adapter=unreal.load_asset(adapter_path)
if not adapter: adapter=unreal.EditorAssetLibrary.duplicate_asset('/Game/Props/BP_ASM_Actor',adapter_path)
if not adapter: raise RuntimeError('Cannot reuse the existing carry-interface adapter')
unreal.BlueprintEditorLibrary.reparent_blueprint(adapter,unreal.ManualColaFillingMachine)
fields(unreal.get_default_object(adapter.generated_class()),{'bReplicates':True,'bReplicateMovement':True,'bAlwaysRelevant':True})
unreal.BlueprintEditorLibrary.compile_blueprint(adapter)
if 'ERROR' in str(adapter.get_editor_property('Status')).upper(): raise RuntimeError('Machine carry adapter failed compilation')
if not unreal.EditorAssetLibrary.save_loaded_asset(adapter,False): raise RuntimeError('Cannot save machine carry adapter')
path=ROOT+'/DA_ManualFillingMachine' 
backup(path)
product=unreal.load_asset(path)
if not product:
    factory=unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class',unreal.InteractablePropDataAsset)
    product=unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_ManualFillingMachine',ROOT,unreal.InteractablePropDataAsset,factory)
fields(product,{'ItemDisplayName':unreal.Text('Manual Normal Cola Filling Machine'),'StaticMesh':unreal.load_asset('/Engine/BasicShapes/Cube'),
    'CustomActiveActorClass':adapter.generated_class(),'bEnableInstanceUniqueData':True,'bCanGrab':True,'bCanConsume':False,
    'bIsBottleCap':False,'DefaultContentID':0,'DefaultContentLevel':0.,'DefaultCapSealed':0.,'NumOfPortions':0,
    'GameplayMassKg':20.,'bHasExpiry':False,'MaterialOverrides':[]})
if not unreal.EditorAssetLibrary.save_loaded_asset(product,False): raise RuntimeError('Cannot save machine product')
catalog_path=ROOT+'/DA_NormalColaSupplyCatalog'
backup(catalog_path)
catalog=unreal.load_asset(catalog_path)
offers=list(catalog.get_editor_property('Offers'))
existing=next((o for o in offers if str(o.get_editor_property('OfferID'))=='ManualFillingMachine'),None)
if existing: offers.remove(existing)
offer=unreal.ShopSupplyOffer()
fields(offer,{'OfferID':'ManualFillingMachine','DisplayName':unreal.Text('Manual Filling Machine'),'Kind':unreal.ShopSupplyKind.MANUAL_FILLING_MACHINE,'Product':product,'UnitCostCents':5000})
offers.append(offer); catalog.set_editor_property('Offers',offers)
if not unreal.EditorAssetLibrary.save_loaded_asset(catalog,False): raise RuntimeError('Cannot save machine offer')
report={'machine_product':path,'price_cents':5000,'maps':[],'tuning':{'temporary_seal_seconds':3.,'safe_residual_pressure':.1,'full_bottle_residual_pressure':.6,'nominal_portions_per_second':1.,'lever_lease_seconds':.4}}
for name in ['Lvl_NormalColaPrototype','Lvl_NormalColaPrototype_Validation']:
    map_path=ROOT+'/'+name; backup(map_path)
    world=unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if not world: raise RuntimeError('Cannot open '+map_path)
    all_actors=actors.get_all_level_actors()
    benches=[a for a in all_actors if a.get_actor_label()=='Supplies and Normal Cola workbench']
    if len(benches)!=1: raise RuntimeError('Expected the existing single starter workbench')
    old=benches[0]
    if isinstance(old,unreal.ManualColaFillingMachine): machine=old
    else:
        machine=actors.spawn_actor_from_class(adapter.generated_class(),unreal.Vector(-2600,-2200,50))
        machine.set_actor_label('Supplies and Normal Cola workbench')
        actors.destroy_actor(old)
    fields(machine,{'PropDataAsset':product,'ISID':0,'ProductID':0,'State':unreal.PropState.UNSETTLED,'bAutoConvertToISMIfAtRest':False})
    machine.set_replicates(True)
    machine.set_replicate_movement(True)
    machine.set_actor_location(unreal.Vector(-2600,-2200,50),False,True)
    machine.set_actor_scale3d(unreal.Vector(1,1,1))
    station=machine.get_component_by_class(unreal.ShopProductionStationComponent)
    fields(station,{'bManualLeverMode':True,'bAllowSoloCap':True,'bRequiresCooperativeCap':False})
    for actor in all_actors:
        label=actor.get_actor_label()
        if label=='Assembly line instructions':
            actor.get_component_by_class(unreal.TextRenderComponent).set_text('MANUAL NORMAL COLA\nE: open machine | Align bottle | Connect one supply pipe\nHOLD F / lever to fill; release to stop\nTemporary seal + R, or wait for pressure then R\nSolo works; team up or buy more machines to work faster')
        elif label=='Supply delivery platform': actor.set_actor_scale3d(unreal.Vector(5.2,19,.1))
        elif label=='Supply delivery access ramp': actor.set_actor_location(unreal.Vector(-3100,-3325,55),False,True)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world,map_path): raise RuntimeError('Map save failed')
    report['maps'].append({'path':map_path,'manual_machine_count':len([a for a in actors.get_all_level_actors() if isinstance(a,unreal.ManualColaFillingMachine)]),'solo':station.get_editor_property('bAllowSoloCap'),'held_lever':station.get_editor_property('bManualLeverMode'),'active':machine.get_editor_property('bIsActive'),'replicates':machine.get_editor_property('bReplicates'),'replicate_movement':machine.get_editor_property('bReplicateMovement')})
(out/'AssetConfiguration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('MANUAL_COLA_FILLING_CONFIGURED')
