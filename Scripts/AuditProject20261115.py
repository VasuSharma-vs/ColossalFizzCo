"""Read-only asset/Blueprint/data-table audit; never saves packages."""
import collections
import hashlib
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/Audit20261115'
out.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
files = list((root / 'Content').rglob('*.uasset')) + list((root / 'Content').rglob('*.umap'))
hashes = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
report = {'mode': 'read-only; compile in memory; never save', 'assets': [], 'blueprints': [], 'tables': [], 'products': []}
seen = set()
for data in registry.get_assets_by_path('/Game', recursive=True):
    path = str(data.package_name)
    if path in seen:
        continue
    seen.add(path)
    cls = str(data.asset_class_path.asset_name)
    report['assets'].append({'path': path, 'class': cls})
    if cls in ('Blueprint', 'AnimBlueprint', 'WidgetBlueprint'):
        entry = {'path': path, 'parent': data.get_tag_value('ParentClass')}
        try:
            bp = data.get_asset()
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            entry['status'] = str(bp.get_editor_property('status'))
        except Exception as exc:
            entry['error'] = str(exc)
        report['blueprints'].append(entry)
    elif cls == 'DataTable':
        entry = {'path': path}
        try:
            entry['rows'] = [str(x) for x in unreal.DataTableFunctionLibrary.get_data_table_row_names(data.get_asset())]
            entry['json'] = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(data.get_asset())
        except Exception as exc:
            entry['error'] = str(exc)
        report['tables'].append(entry)
    elif cls == 'InteractablePropDataAsset':
        entry = {'path': path, 'properties': {}}
        try:
            obj = data.get_asset()
            for name in ('ItemDisplayName', 'Price', 'NumOfPortions', 'DefaultContentID', 'DefaultContentLevel', 'BottleMaterialType', 'StaticMesh', 'CustomActiveActorClass', 'LiquidDensityGPerMl'):
                try:
                    value = obj.get_editor_property(name)
                    entry['properties'][name] = value if value is None or isinstance(value, (str, int, float, bool)) else str(value)
                except Exception:
                    pass
        except Exception as exc:
            entry['error'] = str(exc)
        report['products'].append(entry)
report['class_counts'] = dict(collections.Counter(x['class'] for x in report['assets']))
report['blueprint_counts'] = dict(collections.Counter(x.get('status', 'exception') for x in report['blueprints']))
report['package_hashes'] = hashes
report['changed_packages'] = [str(p.relative_to(root)) for p in files if hashlib.sha256(p.read_bytes()).hexdigest() != hashes[str(p.relative_to(root))]]
(out / 'AssetAudit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('AUDIT20261115_COMPLETE ' + json.dumps({'classes': report['class_counts'], 'blueprints': report['blueprint_counts'], 'changed_packages': report['changed_packages']}))
