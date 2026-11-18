"""Interactive SIE validation in the user's saved validation level.

Run from Unreal's Python console. Transient editor fixtures are cloned into PIE
and removed after the simulation ends.
Captures rendered screenshots and measured particle/segment checks, then stops SIE.
No Blueprint or level is saved. Existing save-game files are restored after SIE.
"""
import builtins
import hashlib
import json
import math
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "Saved/CableLevelValidation"
OUT.mkdir(parents=True, exist_ok=True)
ISOLATED = "-CableRenderValidation" in unreal.SystemLibrary.get_command_line()
if ISOLATED:
    unreal.get_default_object(unreal.EditorPerformanceSettings).set_editor_property("throttle_cpu_when_not_foreground", False)
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_asset_editors()
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
previous = getattr(builtins, "cable_level_session", None)
if previous is not None:
    try: unreal.unregister_slate_post_tick_callback(previous.handle)
    except Exception: pass
    if not LEVEL.is_in_play_in_editor():
        for actor in reversed(getattr(previous, "editor_fixtures", [])):
            if unreal.SystemLibrary.is_valid(actor): ACTORS.destroy_actor(actor)
        for path,data in getattr(previous, "saves", {}).items():
            if not path.exists() or path.read_bytes() != data: path.write_bytes(data)
EXPECTED_MAP = "/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation"
if LEVEL.is_in_play_in_editor():
    raise RuntimeError("Stop the existing play session before running cable validation")
if not EDITOR.get_editor_world().get_path_name().startswith(EXPECTED_MAP):
    raise RuntimeError("Open the saved validation level before running this script")

def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]

class CableLevelSession:
    def __init__(self):
        self.phase = "capture_existing"
        self.started = time.monotonic()
        self.frame = 0
        self.cases = {}
        self.failures = []
        self.samples = []
        self.snapshots = []
        self.game_world = None
        self.capture_folder = OUT / time.strftime("Rendered_%Y%m%d_%H%M%S")
        self.capture_folder.mkdir(parents=True, exist_ok=True)
        self.editor_fixtures = []
        self.source_cases = {}
        self.origin = unreal.Vector(20000, 0, 0)
        self.saves = {p: p.read_bytes() for p in (ROOT / "Saved/SaveGames").glob("*.sav")}
        self.asset_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in [
            ROOT / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset",
            ROOT / "Content/NormalColaPrototype/Lvl_NormalColaPrototype_Validation.umap"]}
        self.existing = []
        for actor in unreal.GameplayStatics.get_all_actors_of_class(EDITOR.get_editor_world(), unreal.Actor):
            for cable in actor.get_components_by_class(unreal.CollisionCableComponent):
                points = cable.get_cable_particle_locations()
                self.existing.append({"actor": actor.get_actor_label(), "component": cable.get_path_name(),
                    "mount": xyz(cable.get_world_location()), "min_z": min(p.z for p in points),
                    "max_z": max(p.z for p in points), "clear": cable.is_cable_collision_free()})
        self.request_capture("ExistingCable")
        self.write()

    def request_capture(self, name):
        filename = self.capture_folder / (name+".png")
        self.capture_requested = time.monotonic()
        self.pending_capture = filename
        if ISOLATED:
            unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), "r.ScreenshotDelegate 0")
            unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), 'HighResShot 1280x720 filename="'+str(filename).replace('\\','/')+'"')
        else:
            self.screenshot_task = unreal.AutomationLibrary.take_high_res_screenshot(1280,720,str(filename),delay=.2)
        self.snapshots.append(str(filename))

    def capture_done(self):
        if not ISOLATED: return self.screenshot_task.is_task_done()
        if self.pending_capture.exists() and self.pending_capture.stat().st_size > 100: return True
        if time.monotonic()-self.capture_requested > 20:
            self.failures.append("Rendered screenshot was not written: "+str(self.pending_capture))
            return True
        return False

    def write(self):
        data = {"phase": self.phase, "frame": self.frame, "level": EXPECTED_MAP,
                "existing_components": self.existing, "failures": self.failures,
                "samples": self.samples, "screenshots": self.snapshots,
                "cases": {name: {k: v for k, v in case.items() if k not in ["actor", "cable"]}
                          for name, case in self.cases.items()}}
        (OUT / "LiveResult.json").write_text(json.dumps(data, indent=2))

    def spawn(self, actor_class, location, scale=None):
        actor = ACTORS.spawn_actor_from_class(actor_class, location)
        self.editor_fixtures.append(actor)
        if scale is not None: actor.set_actor_scale3d(scale)
        return actor

    def offset(self, x, y, z):
        return self.origin + unreal.Vector(x,y,z)

    def prepare(self):
        cube = unreal.load_asset("/Engine/BasicShapes/Cube")
        floor = self.spawn(unreal.StaticMeshActor, self.offset(0,0,-25), unreal.Vector(20,20,.5))
        floor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        floor.static_mesh_component.set_static_mesh(cube)
        floor.static_mesh_component.set_collision_profile_name("BlockAll")
        wall = self.spawn(unreal.StaticMeshActor, self.offset(300,450,300), unreal.Vector(.1,10,6))
        wall.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        wall.static_mesh_component.set_static_mesh(cube)
        wall.static_mesh_component.set_collision_profile_name("BlockAll")
        cable_class = unreal.load_class(None, "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C")
        specifications = [
            ("hanging", -500,-450,450,500,unreal.Vector(),True,True,1),
            ("floor", -500,350,65,500,unreal.Vector(),True,True,1),
            ("wall_motion", 100,650,400,300,unreal.Vector(0,0,-300),False,False,0),
            ("wall_attachment", 100,250,200,250,unreal.Vector(0,0,200),True,True,0)]
        for name,x,y,height,length,end,pin_start,pin_end,gravity in specifications:
            actor = self.spawn(cable_class, self.offset(x,y,0))
            actor.set_editor_property("tags", [unreal.Name("CableLevelValidation_"+name)])
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            cable.set_relative_location(unreal.Vector(0,0,height), False, True)
            for prop,value in {"cable_length": length,"end_location":end,"attach_start":pin_start,
                "attach_end":pin_end,"cable_gravity_scale":gravity,"enable_collision":True,
                "enable_stiffness":True,"cable_force":unreal.Vector()}.items():
                cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
                cable.set_editor_property(prop,value)
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            cable.reset_cable()
            self.source_cases[name] = {"actor_name":actor.get_name(),"mount_z":height}

    def setup(self):
        cable_class = unreal.load_class(None, "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C")
        clones = {}
        for actor in unreal.GameplayStatics.get_all_actors_of_class(self.game_world,cable_class):
            for tag in actor.tags:
                if str(tag).startswith("CableLevelValidation_"):
                    clones[str(tag)[len("CableLevelValidation_"):]] = actor
        if any(name not in clones for name in self.source_cases): return False
        for name,source in self.source_cases.items():
            actor = clones[name]
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            self.cases[name] = {"actor":actor,"cable":cable,"path":actor.get_path_name(),
                "mount_z":source["mount_z"],"min_z":1e30,"max_z":-1e30,"max_x":-1e30,
                "collision_failures":0,"floor_crossings":0,"wall_crossings":0,
                "floor_contact":False,"wall_contact":False,"attachment_blocked":False}
        self.frame = 0
        self.phase = "simulate"
        self.write()
        return True

    def sample(self):
        for name, case in self.cases.items():
            cable = case["actor"].get_components_by_class(unreal.CollisionCableComponent)[0]
            case["cable"] = cable
            points = cable.get_cable_particle_locations()
            minimum = min(p.z for p in points)
            maximum = max(p.z for p in points)
            max_x = max(p.x-self.origin.x for p in points)
            radius = cable.get_editor_property("cable_width")*.5
            clear = cable.is_cable_collision_free() and not cable.get_editor_property("has_unresolved_collision")
            case["min_z"] = min(case["min_z"], minimum)
            case["max_z"] = max(case["max_z"], maximum)
            case["max_x"] = max(case["max_x"],max_x)
            if not clear: case["collision_failures"] += 1
            if minimum < radius-.05: case["floor_crossings"] += 1
            if name.startswith("wall_") and max_x > 295-radius+.05: case["wall_crossings"] += 1
            if name == "floor" and minimum < radius+2: case["floor_contact"] = True
            if name == "wall_motion" and max_x > 295-radius-2: case["wall_contact"] = True
            case["attachment_blocked"] |= cable.get_editor_property("attachment_blocked")
            if self.frame % 30 == 0:
                self.samples.append({"frame": self.frame,"case":name,"min_z":minimum,
                    "max_z":maximum,"max_x":max_x,"clear":clear})

    def take_case_screenshot(self, name):
        views = {
            "hanging":((-500,-1100,430),(-500,-450,300)),
            "floor":((-950,-250,300),(-500,350,75)),
            "wall":((-550,-500,600),(260,450,250))}
        location, target = views[name]
        location = self.offset(*location)
        target = self.offset(*target)
        rotation = unreal.MathLibrary.find_look_at_rotation(location,target)
        unreal.EditorLevelLibrary.set_level_viewport_camera_info(location, rotation)
        self.request_capture("Live_"+name)

    def finish(self):
        for name,case in self.cases.items():
            if case["collision_failures"] or case["floor_crossings"] or case["wall_crossings"]:
                self.failures.append(name+": collision or surface crossing")
        hang = self.cases["hanging"]
        if hang["min_z"] > 350 or hang["max_z"] > 460: self.failures.append("hanging: rope did not hang below mount")
        if not self.cases["floor"]["floor_contact"]: self.failures.append("floor: did not reach floor")
        if not self.cases["wall_motion"]["wall_contact"]: self.failures.append("wall: did not reach wall")
        if not self.cases["wall_attachment"]["attachment_blocked"]: self.failures.append("attachment: wall did not block anchor")
        self.phase = "stopping"
        self.write()
        LEVEL.editor_request_end_play()

    def restore(self):
        restored = []
        for actor in reversed(self.editor_fixtures):
            if unreal.SystemLibrary.is_valid(actor): ACTORS.destroy_actor(actor)
        self.editor_fixtures = []
        for path,data in self.saves.items():
            if not path.exists() or path.read_bytes() != data:
                path.write_bytes(data); restored.append(path.name)
        self.phase = "complete"
        self.write()
        report = json.loads((OUT / "LiveResult.json").read_text())
        report.update(accepted=not self.failures, original_assets_unchanged=all(
            hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in self.asset_hashes.items()),
            original_saves_preserved=all(p.read_bytes()==data for p,data in self.saves.items()),
            restored_saves=restored)
        (OUT / "LiveResult.json").write_text(json.dumps(report,indent=2))
        unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.log("CABLE_LEVEL_VALIDATION_COMPLETE " + json.dumps({k:report[k] for k in ["accepted","failures","frame"]}))

    def tick(self, delta):
        try:
            if time.monotonic()-self.started > 180: raise RuntimeError("Level test timed out")
            if self.phase == "capture_existing":
                if not self.capture_done(): return
                self.prepare()
                self.phase = "starting"; self.write(); LEVEL.editor_play_simulate(); return
            if self.phase == "starting":
                self.game_world = EDITOR.get_game_world()
                if not self.game_world: return
                self.setup(); return
            if self.phase == "simulate":
                if not EDITOR.get_game_world(): raise RuntimeError("Simulation stopped before validation finished")
                self.frame += 1
                if self.frame == 60:
                    # Runtime assignment, matching a Blueprint variable set.
                    # set_editor_property would reconstruct the SCS component.
                    self.cases["wall_motion"]["cable"].cable_force = unreal.Vector(2500000,0,0)
                    self.cases["wall_attachment"]["actor"].set_actor_location(self.offset(500,250,0),False,True)
                self.sample()
                if self.frame % 30 == 0: self.write()
                if self.frame >= 240:
                    self.phase = "shot_hanging"; self.take_case_screenshot("hanging"); self.write()
            elif self.phase.startswith("shot_"):
                if not self.capture_done(): return
                if self.phase == "shot_hanging": self.phase="shot_floor"; self.take_case_screenshot("floor")
                elif self.phase == "shot_floor": self.phase="shot_wall"; self.take_case_screenshot("wall")
                else: self.finish()
            elif self.phase == "stopping":
                if not LEVEL.is_in_play_in_editor(): self.restore()
        except Exception:
            self.failures.append(traceback.format_exc())
            self.write()
            if LEVEL.is_in_play_in_editor():
                self.phase="stopping"; LEVEL.editor_request_end_play()
            else: self.restore()

builtins.cable_level_session = CableLevelSession()
builtins.cable_level_session.handle = unreal.register_slate_post_tick_callback(builtins.cable_level_session.tick)
unreal.log("CABLE_LEVEL_VALIDATION_STARTED")
