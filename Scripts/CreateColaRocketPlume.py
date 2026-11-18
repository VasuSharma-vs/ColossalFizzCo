"""Author the bottle exhaust in UE 5.8 with -EnablePlugins=CascadeToNiagaraConverter.

Pass -ColaShared for the system used by the world subsystem, or -ColaBoth for both assets.
Pass -ColaRebuild to replace the emitters in an existing system at its original path.
Use -ColaPatchColor to update only the spawn color in existing hand-tuned emitters.
The converter provides editor graph authoring functions. The saved Niagara asset
uses standard engine modules and has no runtime dependency on the converter.
"""
import unreal as ue
import Paths as paths
from CascadeToNiagaraHelperMethods import create_script_input_random_range as random_range

fx = ue.FXConverterUtilitiesLibrary
stage = ue.ScriptExecutionCategory
folder = "/Game/Niagara/ColaRocket"
flags = ue.SystemLibrary.get_command_line().lower().split()
input_type = ue.NiagaraScriptInputType


def module(emitter, label, asset, category):
    args = ue.CreateScriptContextArgs(fx.create_asset_data(asset))
    return emitter.find_or_add_module_script(label, args, category)


def set_input(script, name, value, gated=False):
    if not script.set_parameter(name, value, gated, gated):
        raise ValueError(f"Niagara rejected module input {name}")


def dynamic(script_path, result_type, inputs):
    script = fx.create_script_context(ue.CreateScriptContextArgs(fx.create_asset_data(script_path)))
    for key, value in inputs.items():
        set_input(script, key, value)
    return fx.create_script_input_dynamic(script, result_type)


def nozzle_count():
    return fx.create_script_input_linked_parameter("User.NozzleCount", input_type.INT)


def shared_spawn_rate(rate):
    count = dynamic("/Niagara/DynamicInputs/Math/Min_Int.Min_Int", input_type.INT,
                    {"A": nozzle_count(), "B": fx.create_script_input_int(12)})
    return dynamic("/Niagara/DynamicInputs/Multiply/Multiply_FloatByInt.Multiply_FloatByInt",
                   input_type.FLOAT, {"Float": fx.create_script_input_float(rate), "Integer": count})


def nozzle_value(attribute):
    # UniqueID advances across frames. Round-robin allocation includes every nozzle,
    # even when a sparse bubble emitter spawns fewer particles than bottles per frame.
    count = dynamic("/Niagara/DynamicInputs/Math/Max_Int.Max_Int", input_type.INT,
                    {"A": nozzle_count(), "B": fx.create_script_input_int(1)})
    index = dynamic("/Niagara/DynamicInputs/Fmod/ModuloInt.ModuloInt", input_type.INT,
                    {"A": fx.create_script_input_linked_parameter("Particles.UniqueID", input_type.INT),
                     "B": count})
    if attribute == "position":
        script, output, name = "SelectPositionFromArray", input_type.POSITION, "User.NozzlePositions"
        pin, interface = "Position Array", ue.NiagaraDataInterfaceArrayPosition()
    elif attribute == "direction":
        script, output, name = "SelectVectorFromArray", input_type.VEC3, "User.NozzleDirections"
        pin, interface = "Vector Selection Array", ue.NiagaraDataInterfaceArrayFloat3()
    elif attribute == "scale":
        script, output, name = "SelectFloatFromArray", input_type.FLOAT, "User.NozzleScales"
        pin, interface = "Float Selection Array", ue.NiagaraDataInterfaceArrayFloat()
    elif attribute == "color":
        script, output, name = "SelectLinearColorFromArray", input_type.LINEAR_COLOR, "User.NozzleColors"
        pin, interface = "Color Selection Array", ue.NiagaraDataInterfaceArrayColor()
    else:
        raise ValueError(f"Unknown nozzle attribute {attribute}")
    array = fx.create_script_input_linked_parameter_ex(name, fx.create_script_input_di(interface))
    return dynamic(f"/Niagara/DynamicInputs/Arrays/{script}.{script}", output, {
        "Array Sampling Mode": fx.create_script_input_enum(paths.enum_niagara_array_sampling_mode, "Direct Set"),
        pin: array,
        "Direct Array Index": index,
    })


def mouth_scale(shared):
    if shared:
        return nozzle_value("scale")
    # RMS of the two lateral component scales also works on a mirrored bottle.
    axes = dynamic("/Niagara/DynamicInputs/Multiply/Multiply_Vector.Multiply_Vector", input_type.VEC3, {
        "A": fx.create_script_input_linked_parameter("Engine.Owner.Scale", input_type.VEC3),
        "B": fx.create_script_input_vector(ue.Vector(0.70710678, 0.70710678, 0)),
    })
    return dynamic("/Niagara/DynamicInputs/TypeConversions/VectorLengthSafe.VectorLengthSafe",
                   input_type.FLOAT, {"Vector": axes})


def nozzle_face_position(shared):
    center = nozzle_value("position") if shared else fx.create_script_input_linked_parameter(
        "Engine.Owner.Position", input_type.POSITION)
    normal = nozzle_value("direction") if shared else fx.create_script_input_linked_parameter(
        "Engine.Owner.SystemZAxis", input_type.VEC3)
    # Crossing an isotropic random direction with the nozzle axis gives a random
    # azimuth on the lip plane. Use a zero offset if the two happen to be parallel.
    direction = dynamic("/Niagara/DynamicInputs/Random/V2/RandomVector.RandomVector", input_type.VEC3, {})
    tangent = dynamic("/Niagara/DynamicInputs/Vectors/CrossProduct.CrossProduct", input_type.VEC3,
                      {"Vector A": normal, "Vector B": direction})
    unit_tangent = dynamic("/Niagara/DynamicInputs/Helpers/SafeNormalizeVector.SafeNormalizeVector", input_type.VEC3,
                           {"Vector To Normalize": tangent, "Fallback Vector": fx.create_script_input_vector(ue.Vector(0, 0, 0))})
    # sqrt(U) makes equal areas of the opening equally likely. 10.3 cm is the
    # measured inner lip radius on the production plastic bottle at native scale.
    distance_fraction = dynamic("/Niagara/DynamicInputs/Power/Power.Power", input_type.FLOAT,
                                {"a": random_range(0.0, 1.0), "b": fx.create_script_input_float(0.5)})
    scale = dynamic("/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float", input_type.FLOAT,
                    {"A": fx.create_script_input_float(10.3), "B": mouth_scale(shared)})
    radius = dynamic("/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float", input_type.FLOAT,
                     {"A": scale, "B": distance_fraction})
    offset = dynamic("/Niagara/DynamicInputs/Multiply/Multiply_VectorByFloat.Multiply_VectorByFloat",
                     input_type.VEC3, {"Vector": unit_tangent, "Float": radius})
    return dynamic("/Niagara/DynamicInputs/Vectors/Position/AddVectorToPosition.AddVectorToPosition",
                   input_type.POSITION, {"Position": center, "Vector": offset})


def set_liquid_color(init, shared):
    color_mode = "/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode"
    set_input(init, "Color Mode", fx.create_script_input_enum(color_mode, "Direct Set"))
    # Capture the liquid color once: after a nozzle leaves the array its old
    # droplets retain their color throughout the rest of their lifetime.
    color = nozzle_value("color") if shared else fx.create_script_input_linked_parameter(
        "User.ColaSprayColor", input_type.LINEAR_COLOR)
    set_input(init, "Color", color)


def build_emitter(context, shared, name, material, rate, burst, lifetime, speed, cone_degrees,
                  drag, vertical_accel, size_range, streak=False, spawn_on_face=False):
    emitter = context.add_empty_emitter(name)
    emitter.set_local_space(False)
    module(emitter, "EmitterState", "/Niagara/Modules/Emitter/EmitterState.EmitterState",
           stage.EMITTER_UPDATE)
    spawn = module(emitter, "SpawnRate", paths.script_spawnrate, stage.EMITTER_UPDATE)
    set_input(spawn, "SpawnRate", shared_spawn_rate(rate) if shared else fx.create_script_input_float(rate))
    if burst and not shared:
        initial = module(emitter, "OpeningBurst", paths.script_spawnburst, stage.EMITTER_UPDATE)
        set_input(initial, "Spawn Count", fx.create_script_input_int(burst))
        set_input(initial, "Loop Count Limit", fx.create_script_input_int(1), gated=True)

    init = module(emitter, "InitializeParticle", paths.script_initialize_particle, stage.PARTICLE_SPAWN)
    set_input(init, "Lifetime", random_range(*lifetime))
    if shared or spawn_on_face:
        set_input(init, "Position Mode", fx.create_script_input_enum(paths.enum_niagara_position_initialization_mode, "Direct Set"))
        position = nozzle_face_position(shared) if spawn_on_face else nozzle_value("position")
        set_input(init, "Position", position)
    if spawn_on_face:
        set_liquid_color(init, shared)
    size_mode = "Non-Uniform" if streak else "Uniform"
    set_input(init, "Sprite Size Mode", fx.create_script_input_enum(paths.enum_niagara_size_scale_mode, size_mode))
    if streak:
        size = random_range(ue.Vector2D(*size_range[0]), ue.Vector2D(*size_range[1]))
        if spawn_on_face:
            size = dynamic("/Niagara/DynamicInputs/Multiply/Multiply_Vector2D_ByFloat.Multiply_Vector2D_ByFloat",
                           input_type.VEC2, {"Vector2D": size, "Float": mouth_scale(shared)})
        set_input(init, "Sprite Size", size, gated=True)
    else:
        set_input(init, "Uniform Sprite Size", random_range(*size_range))

    velocity = module(emitter, "AddVelocity", paths.script_add_velocity, stage.PARTICLE_SPAWN)
    set_input(velocity, "Velocity Mode", fx.create_script_input_enum(paths.enum_niagara_velocity_mode, "In Cone"))
    set_input(velocity, "Cone Angle Mode", fx.create_script_input_enum(paths.enum_niagara_angle_input, "Degrees"))
    set_input(velocity, "Cone Angle", fx.create_script_input_float(cone_degrees))
    axis = nozzle_value("direction") if shared else fx.create_script_input_vector(ue.Vector(0, 0, 1))
    set_input(velocity, "Cone Axis", axis)
    coord_space = "World" if shared else "Local"
    set_input(velocity, "Rotation Coordinate Space", fx.create_script_input_enum(paths.enum_niagara_coordinate_space, coord_space))
    set_input(velocity, "Velocity Speed", random_range(*speed))

    module(emitter, "ParticleState", paths.script_particle_state, stage.PARTICLE_UPDATE)
    resistance = module(emitter, "Drag", paths.script_drag, stage.PARTICLE_UPDATE)
    set_input(resistance, "Drag", fx.create_script_input_float(drag))
    force = module(emitter, "AccelerationForce", paths.script_acceleration_force, stage.PARTICLE_UPDATE)
    set_input(force, "Acceleration", fx.create_script_input_vector(ue.Vector(0, 0, vertical_accel)))
    set_input(force, "Coordinate Space", fx.create_script_input_enum(paths.enum_niagara_coordinate_space, "World"))
    module(emitter, "SolveForcesAndVelocity", paths.script_solve_forces_and_velocity, stage.PARTICLE_UPDATE)

    renderer = ue.NiagaraSpriteRendererProperties()
    renderer.set_editor_property("Material", material)
    if streak:
        renderer.set_editor_property("Alignment", ue.NiagaraSpriteAlignment.VELOCITY_ALIGNED)
    emitter.add_renderer("SpriteRenderer", renderer)


def create(shared):
    system_name = "NS_ColaRocketThrustShared" if shared else "NS_ColaRocketThrust"
    materials = [ue.load_asset(f"{folder}/M_Cola{part}") for part in ("Jet", "Foam", "Bubble")]
    if not all(materials):
        raise RuntimeError("Create the three cola sprite materials before building the plume.")
    if ue.EditorAssetLibrary.does_asset_exist(f"{folder}/{system_name}"):
        system = ue.load_asset(f"{folder}/{system_name}")
        if "-colapatchcolor" not in flags:
            if "-colarebuild" not in flags:
                raise RuntimeError("The system already exists. Use -ColaRebuild to replace its emitters.")
            ue.ColaRocketPlumeEditorLibrary.reset_plume_emitters(system)
    else:
        if "-colapatchcolor" in flags:
            raise RuntimeError(f"Missing existing system {system_name} for the color patch")
        system = ue.AssetToolsHelpers.get_asset_tools().create_asset(
            system_name, folder, ue.NiagaraSystem, ue.NiagaraSystemFactoryNew())
    if shared and not ue.ColaRocketPlumeEditorLibrary.add_nozzle_parameters(system):
        raise RuntimeError("Could not create the Niagara nozzle inputs")
    if not shared and not ue.ColaRocketPlumeEditorLibrary.add_standalone_color_parameter(system):
        raise RuntimeError("Could not create the standalone spray color")
    if "-colapatchcolor" in flags:
        init = fx.create_script_context(ue.CreateScriptContextArgs(fx.create_asset_data(paths.script_initialize_particle)))
        set_liquid_color(init, shared)
        if not ue.ColaLiquidColorPatchLibrary.patch_liquid_spawn_color(system, init):
            raise RuntimeError("Could not patch the two liquid spawn colors")
        if not ue.EditorAssetLibrary.save_loaded_asset(system, False):
            raise RuntimeError("Could not save color changes")
        ue.log(f"COLA_PLUME_COLOR_SAVED {system.get_path_name()}")
        return
    context = fx.create_system_conversion_context(system)
    try:
        build_emitter(context, shared, "ColaJet", materials[0], 480, 24, (0.16, 0.34), (700.0, 1020.0),
                      3.0, 0.85, -540, ((3.0, 14.0), (5.2, 25.0)), streak=True, spawn_on_face=True)
        build_emitter(context, shared, "ColaFan", materials[0], 52, 8, (0.24, 0.56), (360.0, 740.0),
                      29.0, 1.7, -630, ((1.8, 4.5), (4.5, 12.0)), streak=True, spawn_on_face=True)
        build_emitter(context, shared, "FoamSpray", materials[1], 68, 14, (0.40, 0.95), (220.0, 580.0),
                      16.0, 2.2, -420, (1.3, 4.8))
        build_emitter(context, shared, "FizzBubbles", materials[2], 90, 18, (2.0, 4.0), (520.0, 900.0),
                      27.0, 4.8, 82, (2.0, 6.0))
        build_emitter(context, shared, "LingeringBubbles", materials[2], 10, 2, (6.0, 8.0), (490.0, 840.0),
                      35.0, 5.3, 90, (3.0, 7.4))
        context.finalize()
        if not ue.EditorAssetLibrary.save_loaded_asset(system, False):
            raise RuntimeError("Failed to save the Niagara plume")
        ue.log(f"COLA_PLUME_SAVED {system.get_path_name()}")
    finally:
        context.cleanup()


if "-colaboth" in flags:
    create(shared=True)
    create(shared=False)
else:
    create(shared="-colashared" in flags)
