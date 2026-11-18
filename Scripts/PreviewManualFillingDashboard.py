"""Render the real UMG dashboard and inspect a transient Blueprint instance; saves no assets/maps."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir())
out = root / "Saved/ManualFillingPhysical"
out.mkdir(parents=True, exist_ok=True)
result = {}
actor = None
try:
    bp = unreal.load_asset("/Game/NormalColaPrototype/BP_ManualColaFillingMachine")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if "ERROR" in str(bp.get_editor_property("status")).upper():
        raise RuntimeError("Manual machine Blueprint compilation failed")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector(10000, -10000, 1500))
    actor.set_editor_property("supported_content_count", 7)
    spheres = actor.get_components_by_class(unreal.SphereComponent)
    arrows = actor.get_components_by_class(unreal.ArrowComponent)
    result["seven_trace_ports"] = len(spheres) == 7
    result["seven_hidden_arrows"] = len(arrows) == 7 and all(a.get_editor_property("hidden_in_game") for a in arrows)
    result["seven_editable_transforms"] = len(actor.get_editor_property("hose_port_transforms")) == 7
    for name in ("bottle_input_volume", "cap_input_volume", "seal_input_volume", "supply_input_volume"):
        component = actor.get_editor_property(name)
        socket_before = actor.get_components_by_class(unreal.SphereComponent)[0].get_world_location()
        previous = component.get_editor_property("relative_location")
        component.set_editor_property("relative_location", previous + unreal.Vector(5, 10, 15))
        result[name + "_editable"] = bool(component.get_editor_property("editable_when_inherited"))
        if name == "supply_input_volume":
            socket_after = actor.get_components_by_class(unreal.SphereComponent)[0].get_world_location()
            result["supply_volume_moves_sockets"] = (socket_after - socket_before - unreal.Vector(5, 10, 15)).length() < 0.1
    actor.set_editor_property("supported_content_count", 4)
    result["four_trace_ports"] = len(actor.get_components_by_class(unreal.SphereComponent)) == 4
    actors.destroy_actor(actor)
    actor = None
    result["dashboard_rendered"] = unreal.ManualFillingEditorTools.render_dashboard_preview(str(out / "Dashboard.png"))
    result["accepted"] = all(result.values())
    if not result["accepted"]:
        raise RuntimeError("Manual filling editor checks failed")
except Exception as exc:
    result["error"] = str(exc)
    result["accepted"] = False
    unreal.log_error(str(exc))
finally:
    if actor:
        unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
    (out / "EditorChecks.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.SystemLibrary.quit_editor()
