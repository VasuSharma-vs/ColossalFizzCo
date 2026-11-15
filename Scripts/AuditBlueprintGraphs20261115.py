"""Export active gameplay Blueprint graphs without changing packages."""
import hashlib
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/Audit20261115/BlueprintGraphs'
out.mkdir(parents=True, exist_ok=True)
paths = ['/Game/Props/Colas/BP_Cola', '/Game/Props/Colas/BP_PlastickBottel',
         '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter',
         '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode',
         '/Game/DirtyActors/BP_ProductSceaner', '/Game/DirtyActors/BP_CheckoutCounter',
         '/Game/DirtyActors/BP_ATM', '/Game/DirtyActors/BP_Bucket',
         '/Game/AI/BasicNPC/BP_Basic_NPC', '/Game/AI/BP_NPC',
         '/Game/Assest/Blueprints/BP_ColaTank', '/Game/Assest/Blueprints/BP_ChargoTruck']
report = []
for path in paths:
    entry = {'path': path, 'graphs': []}
    try:
        bp = unreal.load_asset(path)
        if not bp:
            raise RuntimeError('not found')
        for graph in BlueprintTools.list_graphs(bp):
            name = graph.get_name()
            dsl = BlueprintTools.read_graph_dsl(graph)
            filename = path.rsplit('/', 1)[-1] + '__' + hashlib.sha256(name.encode()).hexdigest()[:12] + '.dsl'
            (out / filename).write_text(dsl, encoding='utf-8')
            entry['graphs'].append({'name': name, 'file': filename, 'lines': len(dsl.splitlines())})
    except Exception as exc:
        entry['error'] = str(exc)
    report.append(entry)
(out / 'Index.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('AUDIT_GRAPH_EXPORT_COMPLETE')
