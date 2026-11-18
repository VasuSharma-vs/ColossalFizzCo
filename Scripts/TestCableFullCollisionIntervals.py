"""Compare full-query solver intervals in an isolated SIE level process.

Requires -CableFullIntervalValidation -nullrhi -SaveToUserDir and a UserDir
inside Saved/CableFullIntervalValidation. Never runs in the live user editor.
Temporary fixtures are not saved. Component timings exclude Python diagnostics.
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

COMMAND = unreal.SystemLibrary.get_command_line()
if "-CableFullIntervalValidation" not in COMMAND or "-nullrhi" not in COMMAND.lower():
    raise RuntimeError("This test requires its own headless helper process")
ROOT = Path(unreal.Paths.project_dir()).resolve()
if ROOT.name == "IsolatedProject":
    ROOT = ROOT.parents[2]
OUT = ROOT / "Saved/CableFullIntervalValidation"
OUT.mkdir(parents=True, exist_ok=True)
SAVED = Path(unreal.Paths.project_saved_dir()).resolve()
if not SAVED.is_relative_to(OUT):
    raise RuntimeError("Save-game isolation failed: " + str(SAVED))
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MAP = "/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation"
if not EDITOR.get_editor_world().get_path_name().startswith(MAP):
    raise RuntimeError("Open the saved validation map in the helper")
if LEVEL.is_in_play_in_editor():
    raise RuntimeError("Unexpected existing play session")
try:
    performance = unreal.load_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
    performance.set_editor_property("throttle_cpu_when_not_foreground", False)
except Exception:
    unreal.log_warning("Could not disable helper foreground throttling")

CABLE_CLASS = unreal.load_class(None, "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C")
ORDER = [1, 4, 8, 8, 1, 4, 4, 8, 1]
FRAMES = 360
WARMUP = 30


def xyz(point):
    return [float(point.x), float(point.y), float(point.z)]


def stats(values):
    if not values:
        return None
    values = sorted(values)
    return {"count": len(values), "mean": statistics.mean(values),
            "median": statistics.median(values),
            "p95": values[min(len(values)-1, math.ceil(.95*len(values))-1)],
            "max": values[-1]}


class Session:
    def __init__(self):
        self.started = time.monotonic()
        self.phase = "prepare"
        self.trial_index = 0
        self.frame = 0
        self.fixtures = []
        self.specs = {}
        self.cases = {}
        self.errors = []
        self.world = None
        self.origin = unreal.Vector(20000, 0, 0)
        self.hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in [
            ROOT / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset",
            ROOT / "Content/NormalColaPrototype/Lvl_NormalColaPrototype_Validation.umap"]}
        self.report = {"level": MAP, "project_saved_dir": str(SAVED),
            "timing_scope": "LastSimulationTimeMs: native cable tick including visual-path queries; NullRHI, no GPU/FPS claim",
            "hardware": "AMD Ryzen 7 5700X; shared machine with user's editor left open",
            "order": ORDER, "frames_per_trial": FRAMES, "timing_warmup_frames": WARMUP,
            "settings": {"full_collision": True, "solver_iterations": 8, "substep_seconds": 1/120,
                "game_dt_seconds": 1/60, "segments": 32, "points": 33, "width_cm": 10,
                "skin_cm": .5, "visual_subdivisions": 4, "visual_smoothing": 1,
                "sleeping": False, "smooth_penetration_recovery": True},
            "trials": [], "errors": self.errors}
        self.raw = {interval: {"total": [], "cases": {}} for interval in [1,4,8]}
        self.write()

    def write(self):
        self.report.update(phase=self.phase, trial_index=self.trial_index, frame=self.frame,
                           elapsed_wall_seconds=time.monotonic()-self.started)
        (OUT / "Result.json").write_text(json.dumps(self.report, indent=2))

    def pos(self, x, y, z):
        return self.origin + unreal.Vector(x,y,z)

    def spawn(self, cls, location, scale=None):
        actor = ACTORS.spawn_actor_from_class(cls, location)
        actor.set_actor_location(location, False, True)
        self.fixtures.append(actor)
        if scale is not None:
            actor.set_actor_scale3d(scale)
        return actor

    def prepare(self):
        unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), "t.MaxFPS 60")
        cube = unreal.load_asset("/Engine/BasicShapes/Cube")
        for label, position, scale in [
                ("IntervalTestFloor", self.pos(0,0,-25), unreal.Vector(20,20,.5)),
                ("IntervalTestWall", self.pos(300,450,300), unreal.Vector(.1,10,6))]:
            actor = self.spawn(unreal.StaticMeshActor, position, scale)
            actor.set_actor_label(label)
            mesh = actor.static_mesh_component
            mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
            mesh.set_static_mesh(cube)
            mesh.set_collision_profile_name("BlockAll")
        for name, x, y, height, length, end, attach, gravity in [
                ("hanging", -600,-450,500,650,unreal.Vector(400,0,0),True,1),
                ("floor", -500,350,65,550,unreal.Vector(400,0,0),True,1),
                ("wall_motion", 100,650,400,300,unreal.Vector(0,0,-300),False,0),
                ("wall_attachment", 100,250,200,250,unreal.Vector(0,0,200),True,0)]:
            actor = self.spawn(CABLE_CLASS, self.pos(x,y,height))
            actor.tags = [unreal.Name("CableFullInterval_"+name)]
            props = {"num_segments": 32, "cable_length": length, "end_location": end,
                "attach_start": attach, "attach_end": attach, "cable_gravity_scale": gravity,
                "cable_force": unreal.Vector(), "enable_collision": True,
                "use_full_collision_queries": True, "full_collision_solver_interval": 1,
                "collision_method": unreal.CableCollisionMethod.SWEEPS, "collision_points_per_frame": 1,
                "solver_iterations": 8, "substep_time": 1/120, "max_substeps": 8,
                "enable_stiffness": False, "damping": 1., "enable_sleeping": False,
                "enable_self_collision": False, "enable_grabbing": False,
                "enable_movement_restrictions": False, "enable_network_sync": False,
                "enable_point_weight": True, "point_mass_kg": 1.,
                "cable_width": 10., "collision_skin": .5,
                "smooth_collision_recovery": True, "use_bounds_center_recovery": True,
                "enable_visual_subdivision": True, "visual_subdivisions": 4,
                "visual_smoothing": 1., "limit_visual_deviation": False,
                "keep_collision_awake": True}
            self.specs[name] = {"position":self.pos(x,y,height), "props":props}
            for key,value in props.items():
                cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
                cable.set_editor_property(key,value)
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            cable.set_relative_location(unreal.Vector(),False,True)
            cable.set_attach_end_to_component(None)
            for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
                mesh.set_visibility(False)
                mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        self.phase = "starting"
        self.write()
        LEVEL.editor_play_simulate()

    def find_game(self):
        self.world = EDITOR.get_game_world()
        if not self.world:
            return
        found = {}
        for actor in unreal.GameplayStatics.get_all_actors_of_class(self.world,CABLE_CLASS):
            for tag in actor.tags:
                if str(tag).startswith("CableFullInterval_"):
                    found[str(tag).removeprefix("CableFullInterval_")] = actor
        if any(name not in found for name in self.specs):
            return
        self.cases = {name:{"actor":found[name]} for name in self.specs}
        self.begin_trial()

    def begin_trial(self):
        self.frame = 0
        interval = ORDER[self.trial_index]
        self.trial = {"interval":interval, "cases":{}, "total_cpu":[]}
        self.report["trials"].append(self.trial)
        for name,case in self.cases.items():
            actor = case["actor"]
            actor.set_actor_location(self.specs[name]["position"],False,True)
            for key,value in self.specs[name]["props"].items():
                cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
                cable.set_editor_property(key,value)
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            cable.full_collision_solver_interval = interval
            cable.set_relative_location(unreal.Vector(),False,True)
            cable.set_relative_scale3d(unreal.Vector(1,1,1))
            cable.set_attach_end_to_component(None)
            cable.set_collision_profile_name("PhysicsActor")
            cable.set_cable_feature_enabled(unreal.CableFeature.WORLD_COLLISION,True)
            cable.reset_cable()
            cable.set_component_tick_enabled(True)
            for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
                mesh.set_visibility(False)
                mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            case.update(cable=cable,cpu=[],queries=[],render_queries=[],dt=[])
            data = {"point_count":len(cable.get_cable_particle_locations()),
                "render_point_count":len(cable.get_cable_render_locations()),
                "initial_path_clear":cable.is_cable_collision_free(),
                "minimum_sim_z":1e30, "maximum_sim_z":-1e30,
                "min_floor_clearance_cm":1e30, "min_visual_floor_clearance_cm":1e30,
                "max_wall_penetration_cm":0., "max_visual_wall_penetration_cm":0.,
                "floor_clipping_frames":0,"visual_floor_clipping_frames":0,
                "wall_clipping_frames":0,"visual_wall_clipping_frames":0,
                "capsule_checks":0,"capsule_failures":0,"coverage_failures":0,
                "attachment_blocked_frames":0,"first_failures":{},"samples":[]}
            if data["point_count"] != 33 or data["render_point_count"] != 129 or not data["initial_path_clear"]:
                raise RuntimeError(name+": invalid initial fixture "+json.dumps(data))
            case["data"] = data
            self.trial["cases"][name] = data
        self.last_game_time = unreal.GameplayStatics.get_time_seconds(self.world)
        self.phase = "simulate"
        self.write()

    def record_failure(self,data,key,points):
        data[key+"_frames"] += 1
        if key not in data["first_failures"]:
            data["first_failures"][key] = {"frame":self.frame,"points":points}

    def sample(self,dt):
        total_cpu = 0.
        for name,case in self.cases.items():
            cable = case["cable"]
            data = case["data"]
            points = [xyz(p) for p in cable.get_cable_particle_locations()]
            visual = [xyz(p) for p in cable.get_cable_render_locations()]
            data["minimum_sim_z"] = min(data["minimum_sim_z"],min(p[2] for p in points))
            data["maximum_sim_z"] = max(data["maximum_sim_z"],max(p[2] for p in points))
            radius = cable.cable_width*.5
            for path,prefix in [(points,""),(visual,"visual_")]:
                clearance = min(p[2] for p in path)-radius
                min_key = "min_"+prefix+"floor_clearance_cm"
                data[min_key] = min(data[min_key],clearance)
                if clearance < -.05:
                    self.record_failure(data,prefix+"floor_clipping",path)
                if name.startswith("wall_"):
                    penetration = max([0.]+[p[0]-self.origin.x-(295-radius) for p in path if -radius <= p[2] <= 600+radius])
                    max_key = "max_"+prefix+"wall_penetration_cm"
                    data[max_key] = max(data[max_key],penetration)
                    if penetration > .05:
                        self.record_failure(data,prefix+"wall_clipping",path)
            if self.frame % 5 == 0:
                data["capsule_checks"] += 1
                if not cable.is_cable_collision_free():
                    data["capsule_failures"] += 1
                    if "capsule" not in data["first_failures"]:
                        data["first_failures"]["capsule"] = {"frame":self.frame,"points":points}
            if cable.get_editor_property("attachment_blocked"):
                data["attachment_blocked_frames"] += 1
            if self.frame > WARMUP:
                indices = list(cable.get_editor_property("last_collision_point_indices"))
                if len(set(indices)) != 33 or not cable.use_full_collision_queries or cable.full_collision_solver_interval != ORDER[self.trial_index]:
                    data["coverage_failures"] += 1
                cpu = float(cable.get_editor_property("last_simulation_time_ms"))
                case["cpu"].append(cpu)
                case["queries"].append(int(cable.get_editor_property("collision_query_count")))
                case["render_queries"].append(int(cable.get_editor_property("render_collision_query_count")))
                case["dt"].append(dt)
                total_cpu += cpu
            if self.frame % 60 == 0:
                data["samples"].append({"frame":self.frame,"points":points,"visual_points":visual})
        if self.frame > WARMUP:
            self.trial["total_cpu"].append(total_cpu)

    def finish_trial(self):
        interval = ORDER[self.trial_index]
        raw = self.raw[interval]
        raw["total"].extend(self.trial.pop("total_cpu"))
        for name,case in self.cases.items():
            data = case["data"]
            for key in ["cpu","queries","render_queries","dt"]:
                data[key+"_stats"] = stats(case[key])
                raw["cases"].setdefault(name,{}).setdefault(key,[]).extend(case[key])
            if abs(data["dt_stats"]["median"]-1/60) > .001:
                self.errors.append("Game timestep was not 60 Hz")
            data["passed_collision"] = not any(data[key] for key in [
                "floor_clipping_frames","visual_floor_clipping_frames","wall_clipping_frames",
                "visual_wall_clipping_frames","capsule_failures","coverage_failures"])
            if name == "hanging":
                data["hangs_down"] = data["minimum_sim_z"] < 400 and data["maximum_sim_z"] <= 501
            case["cable"].set_component_tick_enabled(False)
        self.write()
        if self.trial_index+1 < len(ORDER):
            self.trial_index += 1
            self.begin_trial()
        else:
            self.phase = "stopping"
            LEVEL.editor_request_end_play()

    def finish(self):
        aggregate = {}
        for interval,raw in self.raw.items():
            aggregate[str(interval)] = {"total_four_cables_cpu_ms":stats(raw["total"]),
                "cases":{name:{key:stats(values) for key,values in fields.items()} for name,fields in raw["cases"].items()}}
        self.report["aggregate"] = aggregate
        self.report["input_asset_hashes"] = self.hashes
        self.report["input_assets_unchanged"] = all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in self.hashes.items())
        self.report["accepted"] = not self.errors and len(self.report["trials"]) == len(ORDER) and all(
            data.get("passed_collision",False) and data.get("hangs_down",True)
            for trial in self.report["trials"] for data in trial["cases"].values())
        self.phase = "complete" if not self.errors else "failed"
        self.write()
        unreal.unregister_slate_post_tick_callback(self.handle)
        for actor in reversed(self.fixtures):
            if unreal.SystemLibrary.is_valid(actor):
                ACTORS.destroy_actor(actor)
        unreal.log("CABLE_FULL_INTERVAL_TEST_FINISHED "+json.dumps({"accepted":self.report["accepted"],"errors":self.errors}))
        unreal.SystemLibrary.quit_editor()

    def tick(self,delta):
        try:
            if time.monotonic()-self.started > 600:
                raise RuntimeError("Full collision interval test timed out")
            if self.phase == "prepare":
                self.prepare()
            elif self.phase == "starting":
                self.find_game()
            elif self.phase == "simulate":
                current = unreal.GameplayStatics.get_time_seconds(self.world)
                if current <= self.last_game_time:
                    return
                dt = current-self.last_game_time
                self.last_game_time = current
                self.frame += 1
                self.sample(dt)
                if self.frame == 60:
                    self.cases["wall_motion"]["cable"].cable_force = unreal.Vector(1000,0,0)
                if 60 <= self.frame < 180:
                    self.cases["wall_attachment"]["actor"].set_actor_location(self.pos(100+5*(self.frame-59),250,200),False,True)
                if self.frame == 180:
                    self.cases["wall_motion"]["cable"].cable_force = unreal.Vector()
                if self.frame % 60 == 0:
                    self.write()
                if self.frame >= FRAMES:
                    self.finish_trial()
            elif self.phase == "stopping" and not LEVEL.is_in_play_in_editor():
                self.finish()
        except Exception:
            self.errors.append(traceback.format_exc())
            if LEVEL.is_in_play_in_editor():
                self.phase = "stopping"
                LEVEL.editor_request_end_play()
                self.write()
            else:
                self.finish()


builtins.cable_full_interval_session = Session()
builtins.cable_full_interval_session.handle = unreal.register_slate_post_tick_callback(builtins.cable_full_interval_session.tick)
unreal.log("CABLE_FULL_INTERVAL_TEST_STARTED")
