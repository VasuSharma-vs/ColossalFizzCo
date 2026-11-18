"""Read-only asset check: spawn, move and reconstruct a transient cable instance.

Run through UnrealEditor-Cmd -ExecutePythonScript. Does not save assets or maps.
"""
import hashlib
import json
import math
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
asset_file = root / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset"
before_hash = hashlib.sha256(asset_file.read_bytes()).hexdigest()
path = "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C"
actor_class = unreal.load_class(None, path)
if not actor_class:
    raise RuntimeError("Could not load collision cable Blueprint class")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(12000, -8000, -3000))
checks = []

def distance(a, b):
    return math.sqrt((a.x-b.x)**2 + (a.y-b.y)**2 + (a.z-b.z)**2)

def inspect(label):
    components = actor.get_components_by_class(unreal.CollisionCableComponent)
    if len(components) != 1:
        raise RuntimeError("Expected exactly one custom cable")
    cable = components[0]
    points = cable.get_cable_particle_locations()
    start = cable.get_world_location()
    end = cable.get_world_transform().transform_location(cable.get_editor_property("end_location"))
    length = cable.get_editor_property("cable_length")
    if not points or distance(points[0], start) > 0.01 or distance(points[-1], end) > 0.01:
        raise RuntimeError(label + ": stale endpoint positions")
    # This BP has no external attachment. Its saved zero end offset makes a
    # hanging loop, and every simulated point should remain close to its mount.
    maximum_distance = max(distance(p, start) for p in points)
    if maximum_distance > length + 1:
        raise RuntimeError(label + ": rope retained points from another world location")
    checks.append({"stage": label, "max_distance_from_mount_cm": maximum_distance,
                   "point_count": len(points), "length_cm": length,
                   "collision_method": str(cable.get_editor_property("collision_method")),
                   "collision_points_per_frame": cable.get_editor_property("collision_points_per_frame")})

try:
    inspect("spawn_away_from_origin")
    actor.set_actor_transform(unreal.Transform(location=unreal.Vector(-11000, 7000, -4500),
        rotation=unreal.Rotator(0, 90, 0), scale=unreal.Vector(1.5, 1, 1)), False, True)
    inspect("translate_rotate_scale")
    cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
    cable.set_editor_property("end_location", unreal.Vector(0, 0, -100))
    inspect("edit_endpoint")
    # Property editing reconstructs this Blueprint component; always reacquire it.
    cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
    cable.set_editor_property("collision_points_per_frame", 999)
    cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
    if cable.get_editor_property("collision_points_per_frame") != len(cable.get_cable_particle_locations()):
        raise RuntimeError("Editor budget maximum did not clamp to the cable's point count")
    inspect("budget_maximum")
    cable.set_editor_property("collision_points_per_frame", 0)
    cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
    if cable.get_editor_property("collision_points_per_frame") != 1:
        raise RuntimeError("Editor budget minimum did not clamp to one point")
    inspect("budget_minimum")
finally:
    actors.destroy_actor(actor)

result = {"asset": path, "checks": checks,
          "asset_unchanged": before_hash == hashlib.sha256(asset_file.read_bytes()).hexdigest()}
if not result["asset_unchanged"]:
    raise RuntimeError("Validation unexpectedly changed the saved asset")
output = root / "Saved/CableBudgetValidation/BlueprintPlacement.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, indent=2))
unreal.log("CABLE_PLACEMENT_VALIDATION " + json.dumps(result))
