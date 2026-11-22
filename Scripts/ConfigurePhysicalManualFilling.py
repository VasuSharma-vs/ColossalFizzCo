"""Install physical seal supplies, hose containers, and separate depots. Preserve existing layouts."""
from pathlib import Path
import json
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
folder = "/Game/NormalColaPrototype"
out = root / "Saved/ManualFillingPhysical"
out.mkdir(parents=True, exist_ok=True)


def backup(path):
    relative = path.removeprefix("/Game/")
    source = root / "Content" / (relative + ".uasset")
    if not source.exists():
        source = source.with_suffix(".umap")
    target = out / "BeforeAssets" / source.relative_to(root / "Content")
    if source.exists() and not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError("Cannot save " + asset.get_path_name())


catalog_path = folder + "/DA_NormalColaSupplyCatalog"
backup(catalog_path)
catalog = unreal.load_asset(catalog_path)
if not catalog:
    raise RuntimeError("Existing supply catalog missing")

hose_bp_path = folder + "/BP_ColaSupplyContainer"
hose_bp = unreal.load_asset(hose_bp_path)
if not hose_bp:
    hose_bp = unreal.EditorAssetLibrary.duplicate_asset("/Game/Props/BP_ASM_Actor", hose_bp_path)
if not hose_bp:
    raise RuntimeError("Cannot create the supply-container carry adapter")
unreal.BlueprintEditorLibrary.reparent_blueprint(hose_bp, unreal.ColaSupplyContainer)
unreal.BlueprintEditorLibrary.compile_blueprint(hose_bp)
if "ERROR" in str(hose_bp.get_editor_property("status")).upper():
    raise RuntimeError("Hose container Blueprint failed compilation")
save(hose_bp)

seal_material_path = folder + "/M_TemporaryPressureSeal"
seal_material = unreal.load_asset(seal_material_path)
if not seal_material:
    seal_material = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_TemporaryPressureSeal", folder, unreal.Material, unreal.MaterialFactoryNew())
    color = unreal.MaterialEditingLibrary.create_material_expression(seal_material, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property("constant", unreal.LinearColor(0.9, 0.35, 0.035, 1))
    unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(seal_material)
    save(seal_material)

seal_path = folder + "/DA_TemporaryPressureSeal"
seal = unreal.load_asset(seal_path)
if not seal:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.InteractablePropDataAsset)
    seal = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_TemporaryPressureSeal", folder, unreal.InteractablePropDataAsset, factory)
cap_offer = next(o for o in catalog.get_editor_property("offers") if str(o.get_editor_property("offer_id")) == "TwistCap")
cap_product = cap_offer.get_editor_property("product")
if not isinstance(cap_product, unreal.InteractablePropDataAsset):
    cap_product = unreal.load_asset(str(cap_product))
for name, value in {
    "item_display_name": unreal.Text("Temporary pressure seal"),
    "static_mesh": seal.get_editor_property("static_mesh") or cap_product.get_editor_property("static_mesh"),
    "custom_active_actor_class": seal.get_editor_property("custom_active_actor_class") or unreal.load_class(None, "/Game/Props/BP_ASM_Actor.BP_ASM_Actor_C"),
    "material_overrides": [seal_material], "bEnableInstanceUniqueData": True,
    "bCanGrab": True, "bCanConsume": False, "bIsBottleCap": False,
    "bIsTemporaryPressureSeal": True, "num_of_portions": seal.get_editor_property("num_of_portions") or 4,
    "default_content_id": 0, "default_content_level": 0.0,
    "default_cap_sealed": 0.0, "bHasExpiry": False, "gameplay_mass_kg": 0.01,
}.items():
    seal.set_editor_property(name, value)
save(seal)

offers = list(catalog.get_editor_property("offers"))
offers = [o for o in offers if str(o.get_editor_property("offer_id")) != "TemporarySeal"]
offer = unreal.ShopSupplyOffer()
for name, value in {"offer_id": "TemporarySeal", "display_name": unreal.Text("Temporary pressure seal"),
                    "kind": unreal.ShopSupplyKind.TEMPORARY_SEAL, "product": seal, "unit_cost_cents": 100}.items():
    offer.set_editor_property(name, value)
offers.append(offer)
for entry in offers:
    if entry.get_editor_property("kind") != unreal.ShopSupplyKind.COLA_CYLINDER:
        continue
    product = entry.get_editor_property("product")
    if not isinstance(product, unreal.InteractablePropDataAsset):
        product = unreal.load_asset(str(product))
    backup(product.get_path_name().split(".")[0])
    product.set_editor_property("custom_active_actor_class", hose_bp.generated_class())
    save(product)
catalog.set_editor_property("offers", offers)
save(catalog)

machine_bp = unreal.load_asset(folder + "/BP_ManualColaFillingMachine")
backup(folder + "/BP_ManualColaFillingMachine")
unreal.BlueprintEditorLibrary.compile_blueprint(machine_bp)
machine_default = unreal.get_default_object(machine_bp.generated_class())
station = machine_default.get_component_by_class(unreal.ShopProductionStationComponent)
station.set_editor_property("bOffersSupplies", False)
station.set_editor_property("temporary_seal_duration", 4.0)
machine_default.get_component_by_class(unreal.BottleFillingComponent).set_editor_property("target_content_level", 0.0)
unreal.BlueprintEditorLibrary.compile_blueprint(machine_bp)
if "ERROR" in str(machine_bp.get_editor_property("status")).upper():
    raise RuntimeError("Manual machine Blueprint failed compilation")
save(machine_bp)

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = {"physical_seal": seal_path, "hose_container": hose_bp_path, "maps": []}
for map_name in ("Lvl_NormalColaPrototype", "Lvl_NormalColaPrototype_Validation"):
    map_path = folder + "/" + map_name
    backup(map_path)
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if not world:
        raise RuntimeError("Cannot load " + map_path)
    all_actors = actors.get_all_level_actors()
    machines = [a for a in all_actors if isinstance(a, unreal.ManualColaFillingMachine)]
    if not machines:
        raise RuntimeError("No existing manual machine in " + map_name)
    starter = next((m for m in machines if m.get_actor_label() == "Supplies and Normal Cola workbench"), machines[0])
    depots = [a for a in all_actors if isinstance(a, unreal.ShopSupplyDepot)]
    if not depots:
        depot = actors.spawn_actor_from_class(unreal.ShopSupplyDepot, starter.get_actor_location() + unreal.Vector(-650, -350, 0))
        depot.set_actor_label("Supply depot - buy physical bottles caps seals and containers")
    else:
        depot = depots[0]
    for machine in machines:
        machine.get_component_by_class(unreal.ShopProductionStationComponent).set_editor_property("bOffersSupplies", False)
        machine.get_component_by_class(unreal.ShopProductionStationComponent).set_editor_property("temporary_seal_duration", 4.0)
        machine.get_component_by_class(unreal.BottleFillingComponent).set_editor_property("target_content_level", 0.0)
    for actor in all_actors:
        if actor.get_actor_label() == "Assembly line instructions":
            text = actor.get_component_by_class(unreal.TextRenderComponent)
            text.set_text("PHYSICAL BOTTLING\nBuy supplies at the depot. Load cap and seal trays.\nE: grab hose end; aim at input + E to plug.\nHold F. X stops liquid. Keep F held until pressure normal.\nR: fit/twist cap. Overfilling or unsafe release can launch the bottle.")
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
        raise RuntimeError("Cannot save " + map_path)
    report["maps"].append({"path": map_path, "machines": len(machines), "depots": 1})
(out / "AssetConfiguration.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("PHYSICAL_MANUAL_FILLING_CONFIGURED")
unreal.SystemLibrary.quit_editor()
