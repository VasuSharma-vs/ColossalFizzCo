"""Build the loose prop mesh from the designer's bottle-origin cap without changing the source."""
import unreal

source_path = "/Game/Assest/meshs/PlasticBottle/SM_NormalPlasticCap"
target_path = "/Game/NormalColaPrototype/SM_NormalPlasticCap_Loose"
source = unreal.load_asset(source_path)
target = unreal.load_asset(target_path) if unreal.EditorAssetLibrary.does_asset_exist(target_path) else unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("SM_NormalPlasticCap_Loose", "/Game/NormalColaPrototype", source)
if not target:
    raise RuntimeError("Cannot duplicate the authored cap")
mesh = unreal.DynamicMesh()
assets = unreal.GeometryScript_AssetUtils
collision = unreal.GeometryScript_Collision
before = source.get_bounding_box()
pivot = (before.min + before.max) * 0.5
_, result = assets.copy_mesh_from_static_mesh(source, mesh, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
if result != unreal.GeometryScriptOutcomePins.SUCCESS:
    raise RuntimeError("Cannot read the source geometry")
unreal.GeometryScript_MeshTransforms.translate_mesh(mesh, unreal.Vector(-pivot.x, -pivot.y, -pivot.z))
_, result = assets.copy_mesh_to_static_mesh(mesh, target, unreal.GeometryScriptCopyMeshToAssetOptions(), unreal.GeometryScriptMeshWriteLOD())
if result != unreal.GeometryScriptOutcomePins.SUCCESS:
    raise RuntimeError("Cannot write the recentered geometry")
# Cook one convex hull directly from the finished centered geometry.
options = unreal.GeometryScriptCollisionFromMeshOptions()
options.method = unreal.GeometryScriptCollisionGenerationMethod.CONVEX_HULLS
options.auto_detect_boxes = False
options.auto_detect_capsules = False
options.auto_detect_spheres = False
collision.set_static_mesh_collision_from_mesh(mesh, target, options)
after = target.get_bounding_box()
if (after.min + after.max).length() > 0.01 or ((after.max-after.min)-(before.max-before.min)).length() > 0.01:
    raise RuntimeError("Loose cap bounds or dimensions do not match the design")
if not unreal.EditorAssetLibrary.save_loaded_asset(target, False):
    raise RuntimeError("Cannot save the verified cap")
unreal.log("LOOSE_CAP_VALIDATED pivot=" + str(pivot))
