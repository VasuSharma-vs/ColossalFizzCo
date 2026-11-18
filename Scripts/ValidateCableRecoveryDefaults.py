"""Read-only saved-Blueprint check for recovery defaults and Pawn filtering."""
import hashlib
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
asset_file = root / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset"
before = hashlib.sha256(asset_file.read_bytes()).hexdigest()
cls = unreal.load_class(None, "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = actors.spawn_actor_from_class(cls, unreal.Vector(12000,-8000,5000), transient=True)
try:
    cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
    defaults = {name: cable.get_editor_property(name) for name in [
        "smooth_collision_recovery", "use_bounds_center_recovery", "ignore_player_collision",
        "collision_recovery_rate", "min_collision_recovery_speed", "max_collision_recovery_speed",
        "collision_points_per_frame", "num_segments"]}
    for flag in ["smooth_collision_recovery", "use_bounds_center_recovery", "ignore_player_collision"]:
        if not defaults[flag]:
            raise RuntimeError("Expected enabled default: " + flag)
    meshes = []
    for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
        channels = {name: str(mesh.get_collision_response_to_channel(channel)) for name,channel in [
            ("Pawn", unreal.CollisionChannel.ECC_PAWN),
            ("WorldStatic", unreal.CollisionChannel.ECC_WORLD_STATIC),
            ("WorldDynamic", unreal.CollisionChannel.ECC_WORLD_DYNAMIC),
            ("PhysicsBody", unreal.CollisionChannel.ECC_PHYSICS_BODY)]}
        if "IGNORE" not in channels["Pawn"]:
            raise RuntimeError(mesh.get_name() + " still blocks Pawn: " + str(channels))
        if any("BLOCK" not in channels[name] for name in ["WorldStatic","WorldDynamic","PhysicsBody"]):
            raise RuntimeError(mesh.get_name() + " lost world/physics collision: " + str(channels))
        meshes.append({"name": mesh.get_name(), "responses": channels})
    if len(meshes) != 3:
        raise RuntimeError("Expected the three original mesh components")
    points = cable.get_cable_particle_locations()
    result = {"defaults": defaults, "mesh_collision": meshes, "point_count": len(points),
              "asset_unchanged_by_validation": before == hashlib.sha256(asset_file.read_bytes()).hexdigest()}
    if not result["asset_unchanged_by_validation"]:
        raise RuntimeError("Validation unexpectedly rewrote the asset")
    out = root / "Saved/CableSmoothRecoveryValidation/BlueprintDefaults.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2))
    unreal.log("CABLE_RECOVERY_DEFAULTS_VALIDATED " + json.dumps(result))
finally:
    actors.destroy_actor(actor)
