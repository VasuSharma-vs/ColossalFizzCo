"""Separate offscreen-editor smoke test: cached native mesh + sleeping BP cable.

Run on /Engine/Maps/Entry with -CableFeatureRender -RenderOffscreen, isolated
-SaveToUserDir/-UserDir, and no live project editor automation. Saves no assets.
"""
import builtins
import hashlib
import json
import time
import traceback
from pathlib import Path
import unreal

COMMAND = unreal.SystemLibrary.get_command_line()
if "-CableFeatureRender" not in COMMAND or "-renderoffscreen" not in COMMAND.lower():
    raise RuntimeError("This script requires a separate offscreen-rendering helper")
ROOT = Path(unreal.Paths.project_dir()).resolve()
if ROOT.name == "IsolatedProject":
    ROOT = ROOT.parents[2]
OUT = ROOT / "Saved/CableFeatureValidation/Rendering"
OUT.mkdir(parents=True, exist_ok=True)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
try:
    performance = unreal.load_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
    if performance:
        performance.set_editor_property("throttle_cpu_when_not_foreground", False)
except Exception:
    unreal.log_warning("Foreground throttle setting is unavailable in this helper")


class Session:
    def __init__(self):
        self.started = time.monotonic()
        self.phase = "starting"
        self.fixtures = []
        self.frames = 0
        self.errors = []
        self.stable_revision = None
        self.configured = False
        self.specs = {}
        self.result = {"errors": self.errors}
        self.asset_file = ROOT / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset"
        self.asset_hash = hashlib.sha256(self.asset_file.read_bytes()).hexdigest()
        self.prepare()
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def spawn(self, cls, position):
        actor = ACTORS.spawn_actor_from_class(cls, position)
        actor.set_actor_location(position, False, True)
        self.fixtures.append(actor)
        return actor

    def prepare(self):
        cube = unreal.load_asset("/Engine/BasicShapes/Cube")
        floor = self.spawn(unreal.StaticMeshActor, unreal.Vector(200, 0, -25))
        floor.set_actor_scale3d(unreal.Vector(10, 7, .5))
        floor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        floor.static_mesh_component.set_static_mesh(cube)
        floor.static_mesh_component.set_collision_profile_name("BlockAll")
        sun = self.spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 600))
        sun.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=-35, roll=0), False)
        sun.light_component.set_editor_property("intensity", 5.)
        sky = self.spawn(unreal.SkyLight, unreal.Vector(200, 0, 400))
        sky.light_component.set_editor_property("intensity", 1.)
        cls = unreal.load_class(None, "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C")
        self.sources = {}
        for name, y, z, length, end, attach in [
                ("Hanging", 0, 320, 520, unreal.Vector(400, 0, 0), True),
                ("Sleeping", -100, 5.51, 400, unreal.Vector(400, 0, 0), False)]:
            actor = self.spawn(cls, unreal.Vector(0, y, z))
            actor.tags = [unreal.Name("CableFeatureRender_" + name)]
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            props = {"num_segments": 11, "cable_length": length, "end_location": end,
                     "attach_start": attach, "attach_end": attach, "enable_stiffness": False,
                     "visual_subdivisions": 4, "enable_visual_subdivision": True,
                     "enable_sleeping": True, "sleep_delay": .3, "damping": 4.,
                     "enable_collision": True, "collision_method": unreal.CableCollisionMethod.SWEEPS,
                     "collision_points_per_frame": 12, "smooth_collision_recovery": False,
                     "cable_width": 10., "collision_skin": .5, "cable_gravity_scale": 1.,
                     "cable_force": unreal.Vector(), "enable_network_sync": False}
            self.specs[name] = props
            for key, value in props.items():
                cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
                cable.set_editor_property(key, value)
            cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            cable.set_relative_location(unreal.Vector(), False, True)
            cable.set_attach_end_to_component(None)
            cable.reset_cable()
            # Hide original nozzle/mount meshes in this renderer-only fixture.
            for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
                mesh.set_visibility(False)
            self.sources[name] = actor.get_name()
        camera = self.spawn(unreal.SceneCapture2D, unreal.Vector(200, -850, 420))
        camera.set_actor_rotation(unreal.Rotator(pitch=-17, yaw=90, roll=0), False)
        camera.tags = [unreal.Name("CableFeatureCamera")]
        capture = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
        capture.set_editor_property("capture_every_frame", False)
        capture.set_editor_property("capture_on_movement", False)
        capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_BASE_COLOR)
        EDITOR.set_level_viewport_camera_info(unreal.Vector(200, -850, 420), unreal.Rotator(pitch=-17, yaw=90, roll=0))
        unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), "t.MaxFPS 30")
        LEVEL.editor_play_simulate()

    def write(self):
        self.result["phase"] = self.phase
        self.result["asset_unchanged"] = self.asset_hash == hashlib.sha256(self.asset_file.read_bytes()).hexdigest()
        (OUT / "Result.json").write_text(json.dumps(self.result, indent=2))

    def finish(self):
        self.phase = "complete" if not self.errors else "failed"
        self.write()
        unreal.unregister_slate_post_tick_callback(self.handle)
        if LEVEL.is_in_play_in_editor():
            LEVEL.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()

    def tick(self, delta):
        try:
            if time.monotonic() - self.started > 300:
                raise RuntimeError("Offscreen cable render test timed out")
            world = EDITOR.get_game_world()
            if not world:
                return
            cables = {}
            camera = None
            for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
                for tag in actor.tags:
                    label = str(tag)
                    if label == "CableFeatureCamera":
                        camera = actor
                    if label.startswith("CableFeatureRender_"):
                        cables[label.removeprefix("CableFeatureRender_")] = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            if len(cables) != 2:
                return
            self.frames += 1
            hanging, sleeping = cables["Hanging"], cables["Sleeping"]
            if not self.configured:
                # PIE can reconstruct SCS components from the Blueprint template.
                # Restore the fixture overrides on the actual game instances.
                for name, cable in list(cables.items()):
                    actor = cable.get_owner()
                    for key, value in self.specs[name].items():
                        cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
                        cable.set_editor_property(key, value)
                    cable = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
                    cable.set_relative_location(unreal.Vector(), False, True)
                    cable.set_relative_scale3d(unreal.Vector(1, 1, 1))
                    cable.set_attach_end_to_component(None)
                    if name == "Sleeping":
                        cable.set_cable_particle_locations([unreal.Vector(400. * i / 11, -100, 5.51) for i in range(12)])
                    else:
                        cable.reset_cable()
                self.configured = True
                self.settle_started = time.monotonic()
                self.game_start = unreal.GameplayStatics.get_time_seconds(world)
                return
            age = unreal.GameplayStatics.get_time_seconds(world) - self.game_start
            if self.phase == "starting" and age > 4 and (sleeping.get_editor_property("cable_sleeping") or time.monotonic() - self.settle_started > 90):
                sim = hanging.get_cable_particle_locations()
                visual = hanging.get_cable_render_locations()
                self.result.update(simulation_points=len(sim), render_points=len(visual),
                                   hanging_min_z=min(p.z for p in sim), hanging_max_z=max(p.z for p in sim),
                                   sleeping=sleeping.get_editor_property("cable_sleeping"),
                                   sleeping_queries=sleeping.get_editor_property("collision_query_count"),
                                   sleeping_points=sleeping.get_editor_property("sleeping_point_count"),
                                   floor_parameters={key: sleeping.get_editor_property(key) for key in
                                       ["cable_length", "cable_width", "attach_start", "attach_end", "enable_stiffness", "sleep_delay"]},
                                   floor_points=[[p.x, p.y, p.z] for p in sleeping.get_cable_particle_locations()],
                                   floor_collision_clear=sleeping.is_cable_collision_free())
                if len(sim) != 12 or len(visual) != 45:
                    self.errors.append("Visual subdivision changed the simulation count or produced the wrong sample count")
                if min(p.z for p in sim) > 300 or max(p.z for p in sim) > 321:
                    self.errors.append("Hanging cable did not sag downward from its mounts")
                if not sleeping.get_editor_property("cable_sleeping"):
                    self.errors.append("Floor cable failed to sleep")
                self.stable_revision = sleeping.get_editor_property("render_geometry_revision")
                self.stable_frame = self.frames
                self.phase = "stable"
                self.write()
            elif self.phase == "stable" and self.frames - self.stable_frame > 30:
                self.result["sleeping_geometry_unchanged"] = self.stable_revision == sleeping.get_editor_property("render_geometry_revision")
                if not self.result["sleeping_geometry_unchanged"]:
                    self.errors.append("Sleeping geometry continued rebuilding")
                if not camera:
                    raise RuntimeError("The game-world capture camera was not duplicated into PIE")
                self.target = unreal.RenderingLibrary.create_render_target2d(world, 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
                component = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
                camera.set_actor_location(unreal.Vector(200, -850, 420), False, True)
                camera.set_actor_rotation(unreal.Rotator(pitch=-17, yaw=90, roll=0), True)
                rotation = camera.get_actor_rotation()
                self.result["capture_rotation"] = [rotation.pitch, rotation.yaw, rotation.roll]
                self.result["capture_location"] = [camera.get_actor_location().x, camera.get_actor_location().y, camera.get_actor_location().z]
                forward = camera.get_actor_forward_vector()
                self.result["capture_forward"] = [forward.x, forward.y, forward.z]
                component.set_editor_property("capture_every_frame", False)
                component.set_editor_property("capture_on_movement", False)
                component.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_BASE_COLOR)
                component.set_editor_property("texture_target", self.target)
                self.capture_component = component
                self.capture = OUT / ("CableFeaturesCapture_" + str(int(time.time())) + ".png")
                self.result["capture_mode"] = "DX11 scene capture, base color; no editor-viewport dependency"
                self.phase = "capture_request"
                self.write()
            elif self.phase == "capture_request":
                self.capture_component.capture_scene()
                self.phase = "capture_export"
            elif self.phase == "capture_export":
                unreal.RenderingLibrary.export_render_target(world, self.target, str(OUT), self.capture.name)
                self.phase = "capture"
                self.write()
            elif self.phase == "capture":
                images = [p for p in OUT.glob(self.capture.stem + "*") if p.is_file() and p.stat().st_size > 100]
                if images:
                    self.result["screenshot"] = str(images[0])
                    self.finish()
        except Exception:
            self.errors.append(traceback.format_exc())
            self.finish()


try:
    builtins.cable_feature_render_session = Session()
except Exception:
    (OUT / "Result.json").write_text(json.dumps({"phase": "failed", "errors": [traceback.format_exc()]}, indent=2))
    unreal.SystemLibrary.quit_editor()
