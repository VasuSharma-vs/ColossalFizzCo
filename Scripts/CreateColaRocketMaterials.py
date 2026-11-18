"""Create the bottle rocket's texture-free Niagara sprite materials in Unreal Editor."""
import hashlib
import unreal


DEST = "/Game/Niagara/ColaRocket"
LIB = unreal.MaterialEditingLibrary

JET = """
float2 p = UV * 2 - 1;
p.x += 0.07 * sin(4 * p.y + 3 * Age);
float taper = 0.72 + 0.20 * p.y;
float2 drop = float2(p.x / taper, p.y);
float silhouette = 1 - smoothstep(0.81, 0.99, length(drop));
float surface = sqrt(saturate(1 - dot(drop, drop)));
float light = saturate(0.14 + 0.66 * surface - 0.36 * drop.x);
// ParticleColor is the liquid's linear RGB. Calibrate the light response
// against the catalog's amber default without baking brown into other drinks.
float3 liquid = max(Tint, 0);
float3 shadowToLight = lerp(float3(0.52, 0.4375, 0.60),
                            float3(1.92, 2.125, 2.75), light);
float3 body = liquid * shadowToLight;
float highlightX = drop.x + 0.32 + 0.07 * sin(4.0 * p.y + Age);
float highlight = exp(-40.0 * highlightX * highlightX - 3.2 * p.y * p.y);
float3 reflection = lerp(float3(0.27, 0.30, 0.24), sqrt(liquid), 0.66);
body += reflection * highlight;
float coverage = lerp(0.92, 1.0, smoothstep(0.18, 0.70, Age));
float alpha = silhouette * coverage * (1 - smoothstep(0.92, 1, Age));
return float4(body, alpha * Opacity);
"""

FOAM = """
float2 p = UV * 2 - 1;
float2 a = (p - float2(-0.22, 0.14)) / 0.62;
float2 b = (p - float2(0.32, 0.13)) / 0.41;
float2 c = (p - float2(0.09, -0.35)) / 0.48;
float d = min(length(a), min(length(b), length(c)));
float alpha = (1 - smoothstep(0.85, 1, d)) * (1 - smoothstep(0.68, 1, Age));
float3 tan = float3(0.48, 0.24, 0.076);
float3 cream = float3(0.94, 0.76, 0.48);
float light = saturate(1 - 0.48 * d - 0.37 * p.y);
return float4(lerp(tan, cream, light) * Tint, alpha * Opacity * 0.92);
"""

BUBBLE = """
float pop = smoothstep(0.92, 1, Age);
float2 p = (UV * 2 - 1) / lerp(0.81, 0.98, pop);
float r = length(p);
float ring = smoothstep(0.74, 0.92, r) * (1 - smoothstep(0.93, 1, r));
float inside = 1 - smoothstep(0.91, 1, r);
float2 hi = (p - float2(-0.37, -0.48)) * float2(3.4, 7);
float2 lo = (p - float2(0.42, 0.43)) * float2(5, 8);
float glint = exp(-dot(hi, hi)) + 0.42 * exp(-dot(lo, lo));
float alpha = saturate(0.11 * inside + 0.72 * ring + 0.98 * glint);
float3 color = lerp(float3(0.28, 0.082, 0.019), float3(0.89, 0.61, 0.32), ring);
color = lerp(color, float3(1.16, 1.01, 0.82), saturate(glint));
// The intact film gives way to several bright arcs as it expands and ruptures.
float angle = atan2(p.y, p.x);
float arcs = smoothstep(-0.35, 0.35, sin(5 * angle + 0.7));
alpha *= lerp(1, arcs * ring, smoothstep(0.08, 0.64, pop)) * (1 - pop);
return float4(color * Tint, alpha * Opacity);
"""


def build(name, description, shader):
    assets = unreal.EditorAssetLibrary
    path = DEST + "/" + name
    if assets.does_asset_exist(path):
        mat = assets.load_asset(path)
        if not isinstance(mat, unreal.Material):
            raise RuntimeError("Expected a material at " + path)
        # Maintain only the shader node authored here. Leave the artist's graph,
        # parameter connections and material settings intact.
        shapes = [expr for expr in LIB.get_material_expressions(mat)
                  if isinstance(expr, unreal.MaterialExpressionCustom)
                  and expr.get_editor_property("description") == description]
        if len(shapes) != 1:
            raise RuntimeError("Cannot identify the original shader node in " + path)
        current = shapes[0].get_editor_property("code")
        if current == shader:
            return
        known_jet_revisions = {
            "74e3aa900efa41a02b206c07220cdd3cfe1e76456c93ffdd33736c582ec50155",
            "93aaef504c3a824c1938d7b93963cfe18c8616e8574cfc9aec0c00449281123f",
            "b8203fb5c976281c3142c665bc04d2f691eb8bec673a1a723ebee6a521731846",
        }
        if name != "M_ColaJet" or hashlib.sha256(current.encode()).hexdigest() not in known_jet_revisions:
            unreal.log_warning("Preserving edited particle shader in " + path)
            return
        shapes[0].set_editor_property("code", shader)
        LIB.recompile_material(mat)
        if not assets.save_loaded_asset(mat, False):
            raise RuntimeError("Could not save " + path)
        unreal.log("COLA_ROCKET_MATERIAL_UPDATED " + path)
        return
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    LIB.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)

    def node(cls, x, y):
        return LIB.create_material_expression(mat, cls, x, y)

    def link(src, pin, dst, input_name):
        if not LIB.connect_material_expressions(src, pin, dst, input_name):
            raise RuntimeError(f"Cannot connect {src.get_name()}.{pin} to {input_name}")

    tex = node(unreal.MaterialExpressionTextureCoordinate, -880, -160)
    particle = node(unreal.MaterialExpressionParticleColor, -880, 0)
    age = node(unreal.MaterialExpressionParticleRelativeTime, -880, 200)
    shape = node(unreal.MaterialExpressionCustom, -570, -80)
    shape.set_editor_property("description", description)
    shape.set_editor_property("code", shader)
    shape.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for input_name in ("UV", "Tint", "Opacity", "Age"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    shape.set_editor_property("inputs", inputs)
    link(tex, "", shape, "UV")
    link(particle, "RGB", shape, "Tint")
    link(particle, "A", shape, "Opacity")
    link(age, "", shape, "Age")
    for rgb, y, prop in [(True, -120, unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                         (False, 80, unreal.MaterialProperty.MP_OPACITY)]:
        mask = node(unreal.MaterialExpressionComponentMask, -230, y)
        for channel in ("r", "g", "b", "a"):
            mask.set_editor_property(channel, rgb if channel != "a" else not rgb)
        link(shape, "", mask, "")
        if not LIB.connect_material_property(mask, "", prop):
            raise RuntimeError(f"Cannot connect material output {prop}")
    LIB.recompile_material(mat)
    if not assets.save_loaded_asset(mat, False):
        raise RuntimeError("Could not save " + path)
    unreal.log("COLA_ROCKET_MATERIAL_SAVED " + path)


for material_name, caption, code in [
    ("M_ColaJet", "Glossy amber cola droplet", JET),
    ("M_ColaFoam", "Three aerated foam beads", FOAM),
    ("M_ColaBubble", "Cola bubble film and late pop", BUBBLE),
]:
    build(material_name, caption, code)
