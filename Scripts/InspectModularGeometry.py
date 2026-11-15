"""Read-only validation of installed plastic mesh pivots, collision and material slots."""
import unreal
import json
import re
from pathlib import Path
out = {}
for path in ["/Game/Assest/meshs/PlasticBottle/" + n for n in ("SM_PlasticBottle","SM_NormalPlasticCap","SM_PlasticBottelInnerContent")] + ["/Game/NormalColaPrototype/SM_NormalPlasticCap_Loose"]:
    mesh = unreal.load_asset(path)
    name = mesh.get_name()
    box = mesh.get_bounding_box()
    geometry = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    out[name] = {"min": [box.min.x, box.min.y, box.min.z], "max": [box.max.x, box.max.y, box.max.z],
                 "collision": sum(len(geometry.get_editor_property(field)) for field in ("box_elems","sphere_elems","sphyl_elems","convex_elems")),
                 "materials": [slot.material_interface.get_path_name() if slot.material_interface else None for slot in mesh.get_editor_property("static_materials")],
                 "collision_trace": str(mesh.get_editor_property("body_setup").get_editor_property("collision_trace_flag")), "convex_bounds": [re.search(r"ElemBox=\((.*?),IsValid=", x.export_text()).group(1) for x in geometry.get_editor_property("convex_elems")]}
for name in ("SKM_PlasticBottle", "SKM_PlasticBottelInnerContent"):
    mesh = unreal.load_asset("/Game/Assest/meshs/PlasticBottle/" + name)
    out[name] = {"materials": [slot.material_interface.get_path_name() if slot.material_interface else None
                               for slot in mesh.get_editor_property("materials")]}
Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ModularGeometry.json").write_text(json.dumps(out, indent=2))
