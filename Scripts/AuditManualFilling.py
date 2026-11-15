"""Read-only saved-map/control audit for manual filling integration."""
import json
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_dir())/'Saved/ManualFillingUpdate'
report=[]
def vec(v): return [v.x,v.y,v.z]
for name in ['Lvl_NormalColaPrototype','Lvl_NormalColaPrototype_Validation']:
    world=unreal.EditorLoadingAndSavingUtils.load_map('/Game/NormalColaPrototype/'+name)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    entry={'map':name,'actors':[]}
    for a in actors:
        label=a.get_actor_label()
        if isinstance(a,(unreal.ShopCustomerFlow,unreal.ManualColaFillingMachine,unreal.PlayerStart)) or label in ['Checkout queue','Shared checkout - interact to bill','Assembly line instructions']:
            row={'label':label,'class':a.get_class().get_name(),'location':vec(a.get_actor_location()),'rotation':str(a.get_actor_rotation())}
            if isinstance(a,unreal.ShopCustomerFlow):
                row['route']={k:(a.get_editor_property(k).get_path_name() if a.get_editor_property(k) else None) for k in ['CheckoutStation','QueuePoint','SpawnPoint','EntrancePoint']}
            if isinstance(a,unreal.ManualColaFillingMachine):
                product=a.get_editor_property('PropDataAsset'); row['product']=product.get_path_name() if product else None
            row['controls']=[{'name':c.get_name(),'tags':[str(t) for t in c.get_editor_property('ComponentTags')],'location':vec(c.get_world_location())} for c in a.get_components_by_class(unreal.SceneComponent) if c.component_has_tag('ShopCustomerInteractionPoint') or c.component_has_tag('ShopInteractionPoint')]
            entry['actors'].append(row)
    report.append(entry)
(out/'SavedMapAudit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('MANUAL_FILLING_MAP_AUDIT_COMPLETE')
