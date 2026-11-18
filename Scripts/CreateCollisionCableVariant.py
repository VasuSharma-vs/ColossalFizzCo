"""Run in Unreal's Python editor to create a separate collision-cable BP variant.

The original BP and placed actors are never modified. An existing destination is
validated, not overwritten. Requires the compiled ColossalFizzCoProductEditor module.
"""
import hashlib
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
source = "/Game/Assest/Blueprints/BP_Cable"
destination = "/Game/Assest/Blueprints/BP_Cable_CollisionSafe"
source_file = root / "Content/Assest/Blueprints/BP_Cable.uasset"
original_hash = hashlib.sha256(source_file.read_bytes()).hexdigest()
if not unreal.EditorAssetLibrary.does_asset_exist(destination):
    if not unreal.CollisionCableEditorTools.create_collision_cable_variant(source, destination):
        raise RuntimeError("Collision cable variant creation/compilation failed; inspect the editor log.")

blueprint = unreal.load_asset(destination)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
actor_class = unreal.load_class(None, destination + ".BP_Cable_CollisionSafe_C")
if not actor_class:
    raise RuntimeError("Variant has no generated class.")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(0, 0, 500))
try:
    cables = actor.get_components_by_class(unreal.CollisionCableComponent)
    if len(cables) != 1:
        raise RuntimeError("Variant must contain exactly one custom cable.")
    cable = cables[0]
    if actor.get_components_by_class(unreal.CableComponent):
        raise RuntimeError("Variant still contains a stock cable.")
    if not cable.get_editor_property("enable_collision"):
        raise RuntimeError("Variant collision is disabled.")
    result = {
        "asset": destination,
        "component_class": cable.get_class().get_name(),
        "segments": cable.get_editor_property("num_segments"),
        "length_cm": cable.get_editor_property("cable_length"),
        "substep_seconds": cable.get_editor_property("substep_time"),
        "keep_collision_awake": cable.get_editor_property("keep_collision_awake"),
        "original_sha256": original_hash,
        "original_unchanged": original_hash == hashlib.sha256(source_file.read_bytes()).hexdigest(),
    }
    if not result["original_unchanged"]:
        raise RuntimeError("Original asset unexpectedly changed.")
finally:
    actors.destroy_actor(actor)

out = root / "Saved/CableValidation/Variant.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(result, indent=2))
unreal.log("COLLISION_CABLE_VARIANT " + json.dumps(result))
