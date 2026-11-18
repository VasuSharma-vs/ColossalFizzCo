"""Upgrade only the saved custom cable BP in an isolated headless editor.

Save the loaded BP from the main editor first so its in-memory edits are retained.
This script never saves a level or any other Blueprint.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal

if "-CableFeatureUpgrade" not in unreal.SystemLibrary.get_command_line():
    raise RuntimeError("Run this migration in its own -CableFeatureUpgrade helper")
root = Path(unreal.Paths.project_dir()).resolve()
if root.name == "IsolatedProject":
    root = root.parents[2]
out = root / "Saved/CableFeatureValidation"
out.mkdir(parents=True, exist_ok=True)
asset_path = "/Game/Assest/Blueprints/BP_Cable_CollisionSafe"
asset_file = root / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset"
backup = out / "BP_Cable_CollisionSafe.BeforeFeatures.uasset"
if not backup.exists():
    shutil.copy2(asset_file, backup)
original = root / "Content/Assest/Blueprints/BP_Cable.uasset"
original_hash = hashlib.sha256(original.read_bytes()).hexdigest()
if not unreal.CollisionCableEditorTools.upgrade_cable_feature_integration(asset_path):
    raise RuntimeError("Native cable Blueprint upgrade failed; inspect the compiler log")

cls = unreal.load_class(None, asset_path + ".BP_Cable_CollisionSafe_C")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = actors.spawn_actor_from_class(cls, unreal.Vector(12000, -8000, 5000), transient=True)
try:
    cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
    names = ["enable_collision", "enable_self_collision", "enable_sleeping", "enable_point_weight",
             "enable_grabbing", "enable_movement_restrictions", "enable_visual_subdivision"]
    flags = {name: cable.get_editor_property(name) for name in names}
    feature_enum = [unreal.CableFeature.WORLD_COLLISION, unreal.CableFeature.SELF_COLLISION,
                    unreal.CableFeature.SLEEPING, unreal.CableFeature.POINT_WEIGHT,
                    unreal.CableFeature.GRABBING_AND_PULLING, unreal.CableFeature.MOVEMENT_RESTRICTIONS,
                    unreal.CableFeature.VISUAL_SUBDIVISION]
    switches = []
    for name, feature in zip(names, feature_enum):
        cable.set_cable_feature_enabled(feature, False)
        if cable.get_editor_property(name):
            raise RuntimeError("Switch failed to disable: " + name)
        cable.set_cable_feature_enabled(feature, True)
        if not cable.get_editor_property(name):
            raise RuntimeError("Switch failed to enable: " + name)
        switches.append(name)
    responses = {}
    for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
        response = str(mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN))
        if "IGNORE" not in response:
            raise RuntimeError(mesh.get_name() + " unexpectedly blocks players")
        responses[mesh.get_name()] = response
    report = {"asset": asset_path, "backup": str(backup), "saved_defaults": flags,
              "switches_verified": switches, "point_count": len(cable.get_cable_particle_locations()),
              "mass_kg": cable.get_editor_property("point_mass_kg"),
              "visual_subdivisions": cable.get_editor_property("visual_subdivisions"),
              "mesh_pawn_collision": responses,
              "original_bp_unchanged": original_hash == hashlib.sha256(original.read_bytes()).hexdigest()}
    if not report["original_bp_unchanged"]:
        raise RuntimeError("Original cable BP was unexpectedly modified")
    (out / "BlueprintUpgrade.json").write_text(json.dumps(report, indent=2))
    unreal.log("CABLE_FEATURE_BLUEPRINT_VERIFIED " + json.dumps(report))
finally:
    actors.destroy_actor(actor)
