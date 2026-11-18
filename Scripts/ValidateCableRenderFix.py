"""Offscreen, isolated editor check of one-sided normals and visible rounding.

Run on /Engine/Maps/Entry with -CableRenderFixValidation -RenderOffscreen.
Creates only unsaved editor fixtures. Does not start PIE or change user assets.
"""
import builtins
import hashlib
import json
import math
import time
import traceback
from pathlib import Path
import unreal

command = unreal.SystemLibrary.get_command_line()
if "-CableRenderFixValidation" not in command or "-renderoffscreen" not in command.lower():
    raise RuntimeError("This check requires its own offscreen validation helper")
root = Path(unreal.Paths.project_dir()).resolve()
if root.name == "IsolatedProject":
    root = root.parents[2]
out = root / "Saved/CableRenderFixValidation/Rendering"
out.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()


class Session:
    def __init__(self):
        self.started = time.monotonic()
        self.frame = 0
        self.phase = "normal_setup"
        self.fixtures = []
        self.errors = []
        self.result = {"errors": self.errors}
        self.asset_file = root / "Content/Assest/Blueprints/BP_Cable_CollisionSafe.uasset"
        self.asset_hash = hashlib.sha256(self.asset_file.read_bytes()).hexdigest()
        self.prepare()
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def spawn(self, cls, position):
        actor = actors.spawn_actor_from_class(cls, position)
        actor.set_actor_location(position, False, True)
        self.fixtures.append(actor)
        return actor

    def cable(self, label, points, subdivisions):
        cls = unreal.load_class(None, "/Game/Assest/Blueprints/BP_Cable_CollisionSafe.BP_Cable_CollisionSafe_C")
        actor = self.spawn(cls, unreal.Vector())
        actor.set_actor_label(label)
        settings = {"num_segments": len(points)-1, "cable_length": sum((points[i]-points[i-1]).length() for i in range(1,len(points))),
                    "attach_start": False, "attach_end": False, "enable_collision": False,
                    "enable_sleeping": False, "enable_self_collision": False, "enable_network_sync": False,
                    "enable_point_weight": False, "enable_stiffness": False, "cable_gravity_scale": 0.,
                    "cable_force": unreal.Vector(), "cable_width": 16., "use_full_collision_queries": False,
                    "enable_visual_subdivision": subdivisions > 1, "visual_subdivisions": max(1,subdivisions),
                    "visual_smoothing": 1., "limit_visual_deviation": False}
        for key, value in settings.items():
            component = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
            component.set_editor_property(key, value)
        component = actor.get_components_by_class(unreal.CollisionCableComponent)[0]
        component.set_relative_location(unreal.Vector(), False, True)
        component.set_relative_scale3d(unreal.Vector(1,1,1))
        component.set_attach_end_to_component(None)
        if not component.set_cable_particle_locations(points):
            raise RuntimeError("Could not seed the render fixture")
        component.set_component_tick_enabled(False)
        component.set_visibility(True)
        material = unreal.load_asset("/Engine/EngineMaterials/DefaultMaterial")
        if material:
            component.set_material(0, material)
            if material.get_editor_property("two_sided"):
                raise RuntimeError("The normal test requires a one-sided material")
        for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh.set_visibility(False)
            mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        return actor, component

    def prepare(self):
        self.probe_actor, self.probe = self.cable("OneSidedNormalProbe", [unreal.Vector(-50,0,400),unreal.Vector(50,0,400)], 1)
        self.polygon_actor, self.polygon = self.cable("TwelvePoints_NoSubdivision", self.loop(-230), 1)
        self.smooth_actor, self.smooth = self.cable("TwelvePoints_Subdivision8", self.loop(230), 8)
        self.camera = self.spawn(unreal.SceneCapture2D, unreal.Vector(0,-200,400))
        self.capture = self.camera.get_component_by_class(unreal.SceneCaptureComponent2D)
        self.capture.set_editor_property("capture_every_frame", False)
        self.capture.set_editor_property("capture_on_movement", False)
        self.capture.set_editor_property("primitive_render_mode", unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        unreal.SystemLibrary.execute_console_command(world, "t.MaxFPS 30")

    def loop(self, center_x):
        return [unreal.Vector(center_x+150*math.cos(2*math.pi*i/12),150*math.sin(2*math.pi*i/12),100) for i in range(12)]

    def write(self):
        self.result.update(phase=self.phase, asset_unchanged=self.asset_hash == hashlib.sha256(self.asset_file.read_bytes()).hexdigest())
        (out / "Result.json").write_text(json.dumps(self.result, indent=2))

    def finish(self):
        self.phase = "complete" if not self.errors else "failed"
        self.write()
        unreal.unregister_slate_post_tick_callback(self.handle)
        for actor in reversed(self.fixtures):
            if unreal.SystemLibrary.is_valid(actor):
                actors.destroy_actor(actor)
        unreal.SystemLibrary.quit_editor()

    def tick(self, delta):
        try:
            self.frame += 1
            if time.monotonic()-self.started > 180:
                raise RuntimeError("Render validation timed out")
            if self.frame < 15:
                return
            if self.phase == "normal_setup":
                self.camera.set_actor_location(unreal.Vector(0,-200,400),False,True)
                self.camera.set_actor_rotation(unreal.Rotator(pitch=0,yaw=90,roll=0),True)
                self.normal_target = unreal.RenderingLibrary.create_render_target2d(world,600,400,unreal.TextureRenderTargetFormat.RTF_RGBA16F)
                self.capture.set_editor_property("capture_source",unreal.SceneCaptureSource.SCS_NORMAL)
                self.capture.set_editor_property("texture_target",self.normal_target)
                self.capture.clear_show_only_components()
                self.capture.show_only_actor_components(self.probe_actor)
                self.phase = "normal_capture"
            elif self.phase == "normal_capture":
                self.capture.capture_scene()
                self.phase = "normal_read"
            elif self.phase == "normal_read":
                normal = unreal.RenderingLibrary.read_render_target_raw_pixel(world,self.normal_target,300,200,False)
                self.result["front_surface_world_normal"] = [normal.r,normal.g,normal.b]
                if normal.g > -.8:
                    self.errors.append("Visible one-sided tube does not expose an outward normal facing the camera")
                self.camera.set_actor_location(unreal.Vector(0,-650,900),False,True)
                self.camera.set_actor_rotation(unreal.Rotator(pitch=-51.6,yaw=90,roll=0),True)
                self.color_target = unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8)
                self.capture.set_editor_property("capture_source",unreal.SceneCaptureSource.SCS_BASE_COLOR)
                self.capture.set_editor_property("texture_target",self.color_target)
                self.capture.clear_show_only_components()
                self.capture.show_only_actor_components(self.polygon_actor)
                self.capture.show_only_actor_components(self.smooth_actor)
                self.phase = "comparison_capture"
            elif self.phase == "comparison_capture":
                self.capture.capture_scene()
                self.phase = "comparison_export"
            elif self.phase == "comparison_export":
                self.filename = "SubdivisionComparison_"+str(int(time.time()))+".png"
                unreal.RenderingLibrary.export_render_target(world,self.color_target,str(out),self.filename)
                self.result["polygon"] = {"simulation_points":len(self.polygon.get_cable_particle_locations()),"render_points":len(self.polygon.get_cable_render_locations())}
                self.result["smooth"] = {"simulation_points":len(self.smooth.get_cable_particle_locations()),"render_points":len(self.smooth.get_cable_render_locations())}
                if self.result["smooth"] != {"simulation_points":12,"render_points":89}:
                    self.errors.append("Subdivision changed simulation resolution or produced the wrong visual count")
                self.result["screenshot"] = str(out / self.filename)
                self.phase = "complete_export"
            elif self.phase == "complete_export":
                if (out / self.filename).exists():
                    self.finish()
            self.write()
        except Exception:
            self.errors.append(traceback.format_exc())
            self.finish()


try:
    builtins.cable_render_fix_session = Session()
except Exception:
    (out / "Result.json").write_text(json.dumps({"phase":"failed","errors":[traceback.format_exc()]},indent=2))
    unreal.SystemLibrary.quit_editor()
