"""Isolated SIE test of the saved cable Blueprint in the saved validation level.

Run in a separate -nullrhi editor with -SaveToUserDir and an isolated -UserDir.
Never run this script in the user's interactive editor. No assets are saved.
"""
import builtins
import hashlib
import json
import math
import statistics
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
COMMAND = unreal.SystemLibrary.get_command_line()
FULL_CONTROL = "-CableFullPointControl" in COMMAND
OUT = ROOT / "Saved/CableOnePointLevelValidation"
if FULL_CONTROL:
    OUT = OUT / "FullPointControl"
OUT.mkdir(parents=True, exist_ok=True)
if "-CableOnePointIsolated" not in COMMAND or "-nullrhi" not in COMMAND.lower():
    raise RuntimeError("This test must use its own headless editor session")
SAVED = Path(unreal.Paths.project_saved_dir()).resolve()
if not SAVED.is_relative_to(OUT):
    raise RuntimeError("Save-game isolation failed: ProjectSavedDir=" + str(SAVED))
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MAP = "/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation"
if not EDITOR.get_editor_world().get_path_name().startswith(MAP):
    raise RuntimeError("Expected the saved validation level")
if LEVEL.is_in_play_in_editor():
    raise RuntimeError("An existing simulation is already running")
CABLE_CLASS = unreal.load_class(None, "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C")
MODES = [("LineTraces", unreal.CableCollisionMethod.LINE_TRACES),
         ("Sweeps", unreal.CableCollisionMethod.SWEEPS)]


def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]


def summary(values):
    if not values:
        return None
    values = sorted(values)
    return {"count": len(values), "median": statistics.median(values),
            "p95": values[min(len(values)-1, math.ceil(.95*len(values))-1)],
            "max": values[-1]}


class Session:
    def __init__(self):
        self.started = time.monotonic()
        self.phase = "prepare"
        self.mode_index = 0
        self.frame = 0
        self.fixtures = []
        self.specs = {}
        self.cases = {}
        self.results = {}
        self.errors = []
        self.world = None
        self.last_game_time = None
        self.origin = unreal.Vector(20000, 0, 0)
        self.hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [
            ROOT / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset",
            ROOT / "Content/NormalColaPrototype/Lvl_NormalColaPrototype_Validation.umap"]}
        self.report = {"level": MAP, "project_saved_dir": str(SAVED),
                       "rendering": "NullRHI; component CPU timing only, no GPU/FPS claim",
                       "point_budget": "all" if FULL_CONTROL else 1, "results": self.results, "errors": self.errors}
        self.write()

    def write(self):
        self.report.update(phase=self.phase, mode=MODES[self.mode_index][0], frame=self.frame)
        (OUT / "Result.json").write_text(json.dumps(self.report, indent=2))

    def pos(self, x, y, z):
        return self.origin + unreal.Vector(x, y, z)

    def spawn(self, cls, location, scale=None):
        actor = ACTORS.spawn_actor_from_class(cls, location)
        self.fixtures.append(actor)
        if scale is not None:
            actor.set_actor_scale3d(scale)
        return actor

    def prepare(self):
        unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), "t.MaxFPS 60")
        cube = unreal.load_asset("/Engine/BasicShapes/Cube")
        for location, scale in [(self.pos(0,0,-25), unreal.Vector(20,20,.5)),
                                (self.pos(300,450,300), unreal.Vector(.1,10,6))]:
            actor = self.spawn(unreal.StaticMeshActor, location, scale)
            mesh = actor.static_mesh_component
            mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
            mesh.set_static_mesh(cube)
            mesh.set_collision_profile_name("BlockAll")
        specs = [
            ("hanging", -500, -450, 500, 800, unreal.Vector(), True, True, 1),
            ("floor", -500, 350, 65, 500, unreal.Vector(), True, True, 1),
            ("wall_motion", 100, 650, 400, 300, unreal.Vector(0,0,-300), False, False, 0),
            ("wall_attachment", 100, 250, 200, 250, unreal.Vector(0,0,200), True, True, 0)]
        for name,x,y,height,length,end,pin_start,pin_end,gravity in specs:
            actor = self.spawn(CABLE_CLASS, self.pos(x,y,0))
            actor.set_editor_property("tags", [unreal.Name("CableOnePoint_" + name)])
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            saved_segments = cable.get_editor_property("num_segments")
            cable.set_relative_location(unreal.Vector(0,0,height), False, True)
            props = {"cable_length": length, "end_location": end, "attach_start": pin_start,
                     "attach_end": pin_end, "cable_gravity_scale": gravity, "enable_collision": True,
                     "collision_points_per_frame": 257 if FULL_CONTROL else 1, "collision_method": MODES[0][1],
                     "cable_force": unreal.Vector()}
            for prop,value in props.items():
                cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
                cable.set_editor_property(prop, value)
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            self.specs[name] = {"location": self.pos(x,y,0), "props": props, "mount_z": height,
                                "saved_segments": saved_segments, "editor_segments": cable.get_editor_property("num_segments")}
        self.phase = "starting"
        self.write()
        LEVEL.editor_play_simulate()

    def find_clones(self):
        self.world = EDITOR.get_game_world()
        if not self.world:
            return False
        found = {}
        for actor in unreal.GameplayStatics.get_all_actors_of_class(self.world, CABLE_CLASS):
            name = None
            for tag in actor.tags:
                if str(tag).startswith("CableOnePoint_"):
                    name = str(tag)[len("CableOnePoint_"):]
            if name is None:
                name = "placed_" + actor.get_name()
            found[name] = actor
        if any(name not in found for name in self.specs):
            return False
        self.cases = {name: {"actor": actor} for name,actor in found.items()}
        self.begin_mode()
        return True

    def begin_mode(self):
        mode_name,mode = MODES[self.mode_index]
        self.frame = 0
        self.last_game_time = None
        self.results[mode_name] = {}
        for name,case in self.cases.items():
            actor = case["actor"]
            if name in self.specs:
                actor.set_actor_location(self.specs[name]["location"], False, True)
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            if name in self.specs:
                for prop,value in self.specs[name]["props"].items():
                    setattr(cable, prop, value)
            cable.collision_method = mode
            cable.collision_points_per_frame = 257 if FULL_CONTROL else 1
            cable.keep_collision_awake = True
            cable.set_component_tick_enabled(True)
            cable.reset_cable()
            case["cable"] = cable
            case["cpu"] = []
            case["queries"] = []
            case["dt"] = []
            points = cable.get_cable_particle_locations()
            data = {"actor": actor.get_path_name(), "point_count": len(points),
                    "num_segments": cable.get_editor_property("num_segments"),
                    "length_cm": cable.cable_length, "mount": xyz(cable.get_world_location()),
                    "min_z": 1e30, "max_z": -1e30, "min_floor_clearance_cm": 1e30,
                    "max_wall_penetration_cm": 0, "floor_crossing_frames": 0,
                    "wall_crossing_frames": 0, "full_diagnostic_failures": 0,
                    "full_diagnostic_checks": 0, "budget_failures": 0,
                    "attachment_blocked_frames": 0, "samples": [], "first_floor_crossing": None,
                    "first_wall_crossing": None, "initial_points": [xyz(p) for p in points]}
            if name in self.specs:
                data["saved_blueprint_segments"] = self.specs[name]["saved_segments"]
                data["editor_segments"] = self.specs[name]["editor_segments"]
            case["data"] = data
            self.results[mode_name][name] = data
        self.phase = "simulate"
        self.write()

    def sample(self, dt):
        for name,case in self.cases.items():
            cable = case["actor"].get_components_by_class(unreal.CollisionCableComponent)[0]
            case["cable"] = cable
            points = cable.get_cable_particle_locations()
            if not points:
                raise RuntimeError(name + ": empty runtime cable")
            data = case["data"]
            radius = cable.cable_width * .5
            minimum,maximum = min(p.z for p in points),max(p.z for p in points)
            data["min_z"] = min(data["min_z"], minimum)
            data["max_z"] = max(data["max_z"], maximum)
            if self.frame > 15:
                indices = list(cable.get_editor_property("last_collision_point_indices"))
                expected = len(points) if FULL_CONTROL else 1
                if len(indices) != expected or len(set(indices)) != expected or any(i < 0 or i >= len(points) for i in indices):
                    data["budget_failures"] += 1
                case["cpu"].append(cable.get_editor_property("last_simulation_time_ms"))
                case["queries"].append(cable.get_editor_property("collision_query_count"))
                case["dt"].append(dt)
            if self.frame % 5 == 0:
                data["full_diagnostic_checks"] += 1
                if not cable.is_cable_collision_free():
                    data["full_diagnostic_failures"] += 1
            if cable.get_editor_property("attachment_blocked"):
                data["attachment_blocked_frames"] += 1
            if name in self.specs:
                clearance = minimum - radius
                data["min_floor_clearance_cm"] = min(data["min_floor_clearance_cm"], clearance)
                if clearance < -.05:
                    data["floor_crossing_frames"] += 1
                    if data["first_floor_crossing"] is None:
                        data["first_floor_crossing"] = {"frame": self.frame, "points": [xyz(p) for p in points]}
            if name.startswith("wall_"):
                in_height = [p for p in points if -radius <= p.z <= 600+radius]
                penetration = max([0.] + [p.x-self.origin.x-(295-radius) for p in in_height])
                data["max_wall_penetration_cm"] = max(data["max_wall_penetration_cm"], penetration)
                if penetration > .05:
                    data["wall_crossing_frames"] += 1
                    if data["first_wall_crossing"] is None:
                        data["first_wall_crossing"] = {"frame": self.frame, "points": [xyz(p) for p in points]}
            if self.frame % 30 == 0:
                data["samples"].append({"frame": self.frame, "min_z": minimum, "max_z": maximum,
                                        "points": [xyz(p) for p in points]})

    def finish_mode(self):
        for name,case in self.cases.items():
            data = case["data"]
            data["component_cpu_ms"] = summary(case["cpu"])
            data["collision_queries_per_tick"] = summary(case["queries"])
            data["game_delta_seconds"] = summary(case["dt"])
            if data["game_delta_seconds"] and abs(data["game_delta_seconds"]["median"]-1/60) > .001:
                self.errors.append(name + ": the simulation did not use 60 Hz game timesteps")
            data["passed_collision_checks"] = not (data["floor_crossing_frames"] or data["wall_crossing_frames"] or
                                                    data["full_diagnostic_failures"] or data["budget_failures"])
            if name == "hanging":
                mount_z = data["mount"][2]
                data["hangs_down"] = data["min_z"] < mount_z-100 and data["max_z"] <= mount_z+15
            cable = case["cable"]
            cable.keep_collision_awake = False
            cable.set_component_tick_enabled(False)
        self.write()
        if self.mode_index + 1 < len(MODES):
            self.mode_index += 1
            self.begin_mode()
        else:
            self.phase = "stopping"
            self.write()
            LEVEL.editor_request_end_play()

    def restore(self):
        for actor in reversed(self.fixtures):
            if unreal.SystemLibrary.is_valid(actor):
                ACTORS.destroy_actor(actor)
        self.fixtures = []
        self.phase = "complete"
        self.report["input_asset_hashes"] = self.hashes
        self.report["input_assets_unchanged_during_test"] = all(
            hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p,h in self.hashes.items())
        self.report["elapsed_wall_seconds"] = time.monotonic()-self.started
        self.report["accepted"] = not self.errors and len(self.results) == 2 and all(
            c.get("passed_collision_checks",False) and c.get("hangs_down",True)
            for cases in self.results.values() for c in cases.values())
        self.write()
        unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.log("CABLE_ONE_POINT_LEVEL_TEST_COMPLETE " + json.dumps({k:self.report[k] for k in ["accepted","errors"]}))
        unreal.SystemLibrary.quit_editor()

    def tick(self, delta):
        try:
            if time.monotonic()-self.started > 180:
                raise RuntimeError("Isolated level test timed out")
            if self.phase == "prepare":
                self.prepare()
            elif self.phase == "starting":
                self.find_clones()
            elif self.phase == "simulate":
                current_time = unreal.GameplayStatics.get_time_seconds(self.world)
                if self.last_game_time is not None and current_time <= self.last_game_time:
                    return
                dt = current_time-self.last_game_time if self.last_game_time is not None else 1/60
                self.last_game_time = current_time
                self.frame += 1
                self.sample(dt)
                if 60 <= self.frame < 140:
                    self.cases["wall_motion"]["cable"].cable_force = unreal.Vector(1000,0,0)
                    self.cases["wall_attachment"]["actor"].set_actor_location(
                        self.pos(100+5*(self.frame-59),250,0),False,True)
                elif self.frame == 140:
                    self.cases["wall_motion"]["cable"].cable_force = unreal.Vector()
                if self.frame % 30 == 0:
                    self.write()
                if self.frame >= 300:
                    self.finish_mode()
            elif self.phase == "stopping" and not LEVEL.is_in_play_in_editor():
                self.restore()
        except Exception:
            self.errors.append(traceback.format_exc())
            self.write()
            if LEVEL.is_in_play_in_editor():
                self.phase = "stopping"
                LEVEL.editor_request_end_play()
            else:
                self.restore()


builtins.cable_one_point_level_session = Session()
builtins.cable_one_point_level_session.handle = unreal.register_slate_post_tick_callback(
    builtins.cable_one_point_level_session.tick)
unreal.log("CABLE_ONE_POINT_LEVEL_TEST_STARTED")
