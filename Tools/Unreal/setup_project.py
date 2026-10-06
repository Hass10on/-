"""One-click asset setup for Referee Career inside the Unreal Editor (UE 5.4+).

Run it once after opening the project:
    Tools > Execute Python Script... > Tools/Unreal/setup_project.py
or from a terminal:
    UnrealEditor-Cmd.exe RefereeCareer/RefereeCareer.uproject -run=pythonscript -script="<repo>/Tools/Unreal/setup_project.py"

What it does (safe to run again; it replaces what it created before):
  1. Imports the Blender venues/props/crowd (RefereeCareer/SourceArt/Meshes/*.fbx), Nanite on for the big meshes.
  2. Creates the master materials (PBR, masked net/fence, glass, LED emissive, mown grass, kit, instanced crowd
     and seats driven by per-instance data, MPC_Crowd for crowd excitement) and one instance per Blender slot.
  3. Assigns the materials to every mesh slot by name.
  4. Imports the synthesised whistles / crowd sounds (looping where needed).
  5. Creates /Game/Maps/Stadium (the game builds the pitch, teams and lights at runtime).
Asset paths match Config/DefaultGame.ini [/Script/RefereeCareer.RCSettings].
"""
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
MESHES = os.path.join(REPO, "RefereeCareer", "SourceArt", "Meshes")
AUDIO = os.path.join(REPO, "RefereeCareer", "SourceArt", "Audio")
MAT_PATH = "/Game/Art/Materials"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary

# Same palette as Tools/Blender/rc_common.py: slot -> (base colour, roughness, metallic, emissive colour, emissive strength, kind)
PALETTE = {
    "MI_Concrete": ((0.52, 0.52, 0.50), 0.85, 0.0, None, 0, "opaque"),
    "MI_ConcreteDark": ((0.22, 0.22, 0.23), 0.9, 0.0, None, 0, "opaque"),
    "MI_StepEdge": ((0.85, 0.85, 0.82), 0.6, 0.0, None, 0, "opaque"),
    "MI_SeatHome": ((0.62, 0.05, 0.07), 0.45, 0.0, None, 0, "opaque"),
    "MI_SeatAway": ((0.05, 0.16, 0.45), 0.45, 0.0, None, 0, "opaque"),
    "MI_SeatNeutral": ((0.75, 0.75, 0.78), 0.45, 0.0, None, 0, "opaque"),
    "MI_Metal": ((0.62, 0.63, 0.66), 0.35, 1.0, None, 0, "opaque"),
    "MI_MetalDark": ((0.08, 0.08, 0.09), 0.4, 1.0, None, 0, "opaque"),
    "MI_RoofMembrane": ((0.92, 0.92, 0.9), 0.5, 0.0, None, 0, "opaque"),
    "MI_Glass": ((0.55, 0.68, 0.75), 0.05, 0.0, None, 0, "glass"),
    "MI_LEDBoard": ((0.02, 0.02, 0.02), 0.3, 0.0, (0.1, 0.45, 1.0), 25.0, "opaque"),
    "MI_Screen": ((0.02, 0.02, 0.02), 0.2, 0.0, (0.9, 0.9, 1.0), 12.0, "opaque"),
    "MI_Floodlight": ((0.9, 0.9, 0.9), 0.2, 0.0, (1.0, 0.97, 0.9), 200.0, "opaque"),
    "MI_Track": ((0.36, 0.15, 0.1), 0.9, 0.0, None, 0, "opaque"),
    "MI_Apron": ((0.14, 0.32, 0.12), 0.8, 0.0, None, 0, "opaque"),
    "MI_Facade": ((0.80, 0.80, 0.78), 0.55, 0.3, None, 0, "opaque"),
    "MI_GoalPost": ((0.97, 0.97, 0.97), 0.25, 0.0, None, 0, "opaque"),
    "MI_Net": ((0.95, 0.95, 0.95), 0.6, 0.0, None, 0, "net"),
    "MI_ChainLink": ((0.55, 0.57, 0.6), 0.4, 1.0, None, 0, "fence"),
    "MI_Flag": ((0.98, 0.84, 0.05), 0.6, 0.0, None, 0, "opaque"),
    "MI_FlagRed": ((0.85, 0.08, 0.08), 0.6, 0.0, None, 0, "opaque"),
    "MI_Plastic": ((0.08, 0.08, 0.08), 0.35, 0.0, None, 0, "opaque"),
    "MI_CardYellow": ((1.0, 0.82, 0.0), 0.35, 0.0, None, 0, "opaque"),
    "MI_CardRed": ((0.85, 0.02, 0.05), 0.35, 0.0, None, 0, "opaque"),
    "MI_Wood": ((0.45, 0.30, 0.18), 0.7, 0.0, None, 0, "opaque"),
    "MI_Dirt": ((0.42, 0.33, 0.22), 0.95, 0.0, None, 0, "opaque"),
    "MI_Plaster": ((0.86, 0.80, 0.68), 0.85, 0.0, None, 0, "opaque"),
    "MI_PlasterB": ((0.72, 0.62, 0.50), 0.85, 0.0, None, 0, "opaque"),
    "MI_Window": ((0.10, 0.13, 0.16), 0.15, 0.2, (1.0, 0.75, 0.45), 2.0, "opaque"),
    "MI_Palm": ((0.12, 0.35, 0.10), 0.7, 0.0, None, 0, "opaque"),
    "MI_Bark": ((0.36, 0.26, 0.17), 0.9, 0.0, None, 0, "opaque"),
    "MI_Container": ((0.10, 0.36, 0.55), 0.6, 0.4, None, 0, "opaque"),
    "MI_Ball": ((0.95, 0.95, 0.95), 0.4, 0.0, None, 0, "opaque"),
    "MI_BallPanel": ((0.06, 0.06, 0.08), 0.4, 0.0, None, 0, "opaque"),
}

MESH_FOLDERS = {
    "SM_Stadium": ("/Game/Art/Venues", True),
    "SM_StadiumFloor": ("/Game/Art/Venues", True),
    "SM_Community": ("/Game/Art/Venues", True),
    "SM_CommunityFence": ("/Game/Art/Venues", False),
    "SM_CommunityGround": ("/Game/Art/Venues", True),
    "SM_CrowdSeated": ("/Game/Art/Crowd", True),
    "SM_CrowdCheer": ("/Game/Art/Crowd", True),
    "SM_CrowdScarf": ("/Game/Art/Crowd", True),
    "SM_Seat": ("/Game/Art/Props", True),
}


def log(msg):
    unreal.log("[RefereeCareer setup] " + msg)


def get_or_create(name, path, cls, factory):
    """Returns (asset, created). Existing assets are reused so re-running never breaks references."""
    full = f"{path}/{name}"
    if eal.does_asset_exist(full):
        return unreal.load_asset(full), False
    return asset_tools.create_asset(name, path, cls, factory), True


def new_material(name):
    """A material whose graph still has to be built, or None when it already exists (keep user edits)."""
    mat, created = get_or_create(name, MAT_PATH, unreal.Material, unreal.MaterialFactoryNew())
    return mat if created else None


# ---------------------------------------------------------------- materials

def expr(mat, cls, x, y, **props):
    e = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(mat, name, default, x, y):
    return expr(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=default)


def vector(mat, name, rgb, x, y):
    return expr(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgb, 1.0))


def connect(a, b, a_out="", b_in=""):
    mel.connect_material_expressions(a, a_out, b, b_in)


def binary(mat, cls, a, b, x, y, a_out="", b_out=""):
    node = expr(mat, cls, x, y)
    connect(a, node, a_out, "A")
    connect(b, node, b_out, "B")
    return node


def pbr_inputs(mat, x=-700):
    color = vector(mat, "Color", (0.5, 0.5, 0.5), x, -200)
    rough = scalar(mat, "Roughness", 0.6, x, 0)
    metal = scalar(mat, "Metallic", 0.0, x, 100)
    emis = vector(mat, "EmissiveColor", (0.0, 0.0, 0.0), x, 220)
    emis_s = scalar(mat, "EmissiveStrength", 0.0, x, 380)
    mel.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    e = binary(mat, unreal.MaterialExpressionMultiply, emis, emis_s, x + 260, 260, "RGB", "")
    mel.connect_material_property(e, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return color


def grid_mask(mat, line_width, x, y):
    """1 on the lines of the UV grid (net cells / chain-link), 0 in the holes."""
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y)
    out = []
    for i, ch in enumerate(("r", "g")):
        mask = expr(mat, unreal.MaterialExpressionComponentMask, x + 200, y + i * 120, r=(ch == "r"), g=(ch == "g"), b=False, a=False)
        connect(uv, mask)
        frac = expr(mat, unreal.MaterialExpressionFrac, x + 380, y + i * 120)
        connect(mask, frac)
        shift = expr(mat, unreal.MaterialExpressionAdd, x + 540, y + i * 120, const_b=0.5 - line_width)
        connect(frac, shift, "", "A")
        rnd = expr(mat, unreal.MaterialExpressionRound, x + 700, y + i * 120)
        connect(shift, rnd)
        inv = expr(mat, unreal.MaterialExpressionOneMinus, x + 860, y + i * 120)
        connect(rnd, inv)
        out.append(inv)
    return binary(mat, unreal.MaterialExpressionMax, out[0], out[1], x + 1020, y + 60)


def make_master(name, kind):
    mat = new_material(name)
    if not mat:
        return unreal.load_asset(f"{MAT_PATH}/{name}")
    pbr_inputs(mat)
    if kind in ("net", "fence"):
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("two_sided", True)
        mask = grid_mask(mat, 0.14 if kind == "net" else 0.22, -900, 520)
        mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    elif kind == "glass":
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("two_sided", True)
        op = expr(mat, unreal.MaterialExpressionConstant, -300, 520, r=0.3)
        mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    return mat


def make_grass():
    """Mown stripes every 5.5 m across the pitch, slight colour variation, rough."""
    mat = new_material("M_Grass")
    if not mat:
        return unreal.load_asset(f"{MAT_PATH}/M_Grass")
    wp = expr(mat, unreal.MaterialExpressionWorldPosition, -1400, 0)
    x = expr(mat, unreal.MaterialExpressionComponentMask, -1200, 0, r=True, g=False, b=False, a=False)
    connect(wp, x)
    scaled = expr(mat, unreal.MaterialExpressionDivide, -1000, 0, const_b=1100.0)
    connect(x, scaled, "", "A")
    frac = expr(mat, unreal.MaterialExpressionFrac, -820, 0)
    connect(scaled, frac)
    stripe = expr(mat, unreal.MaterialExpressionRound, -660, 0)
    connect(frac, stripe)
    light = vector(mat, "LightGreen", (0.11, 0.36, 0.07), -660, -200)
    dark = vector(mat, "DarkGreen", (0.075, 0.27, 0.05), -660, -360)
    lerp = expr(mat, unreal.MaterialExpressionLinearInterpolate, -380, -200)
    connect(dark, lerp, "RGB", "A")
    connect(light, lerp, "RGB", "B")
    connect(stripe, lerp, "", "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(mat, unreal.MaterialExpressionConstant, -380, 100, r=0.92)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    return mat


def make_simple(name, rgb, rough):
    mat = new_material(name)
    if not mat:
        return unreal.load_asset(f"{MAT_PATH}/{name}")
    c = pbr_inputs(mat)
    c.set_editor_property("default_value", unreal.LinearColor(*rgb, 1.0))
    mel.recompile_material(mat)
    return mat


def make_kit():
    mat = new_material("M_Kit")
    if not mat:
        return unreal.load_asset(f"{MAT_PATH}/M_Kit")
    kit = vector(mat, "KitColor", (0.7, 0.1, 0.1), -600, -100)
    vector(mat, "KitSecondary", (1.0, 1.0, 1.0), -600, 100)
    mel.connect_material_property(kit, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(mat, unreal.MaterialExpressionConstant, -400, 200, r=0.75)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    return mat


def make_mpc():
    mpc, created = get_or_create("MPC_Crowd", MAT_PATH, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    if not created:
        return mpc
    p = unreal.CollectionScalarParameter()
    p.set_editor_property("parameter_name", "Excitement")
    p.set_editor_property("default_value", 0.3)
    mpc.set_editor_property("scalar_parameters", [p])
    return mpc


def make_instanced(name, mpc, crowd):
    """Seats / fans: colour from per-instance custom data 0-2; fans also bounce with MPC_Crowd.Excitement."""
    mat = new_material(name)
    if not mat:
        return unreal.load_asset(f"{MAT_PATH}/{name}")
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    r = expr(mat, unreal.MaterialExpressionPerInstanceCustomData, -1000, -300, data_index=0)
    g = expr(mat, unreal.MaterialExpressionPerInstanceCustomData, -1000, -200, data_index=1)
    b = expr(mat, unreal.MaterialExpressionPerInstanceCustomData, -1000, -100, data_index=2)
    rg = binary(mat, unreal.MaterialExpressionAppendVector, r, g, -800, -250)
    rgb = binary(mat, unreal.MaterialExpressionAppendVector, rg, b, -620, -200)
    tint = vector(mat, "Tint", (0.6, 0.42, 0.3), -620, -420)
    use_custom = scalar(mat, "UseInstanceColor", 1.0, -620, -40)
    lerp = expr(mat, unreal.MaterialExpressionLinearInterpolate, -400, -260)
    connect(tint, lerp, "RGB", "A")
    connect(rgb, lerp, "", "B")
    connect(use_custom, lerp, "", "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(mat, unreal.MaterialExpressionConstant, -400, 0, r=0.7)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    if crowd:
        # Z offset = sin(time * 9 + phase * 6.28) * excitement * 5 cm (everyone in the same figure moves together).
        phase = expr(mat, unreal.MaterialExpressionPerInstanceCustomData, -1200, 300, data_index=3)
        phase2 = expr(mat, unreal.MaterialExpressionMultiply, -1020, 300, const_b=6.28)
        connect(phase, phase2, "", "A")
        time = expr(mat, unreal.MaterialExpressionTime, -1200, 420)
        speed = expr(mat, unreal.MaterialExpressionMultiply, -1020, 420, const_b=9.0)
        connect(time, speed, "", "A")
        arg = binary(mat, unreal.MaterialExpressionAdd, phase2, speed, -840, 360)
        sine = expr(mat, unreal.MaterialExpressionSine, -680, 360, period=6.283)
        connect(arg, sine)
        pos = expr(mat, unreal.MaterialExpressionAbs, -540, 360)
        connect(sine, pos)
        exc = expr(mat, unreal.MaterialExpressionCollectionParameter, -680, 500, collection=mpc, parameter_name="Excitement")
        amp = binary(mat, unreal.MaterialExpressionMultiply, pos, exc, -400, 420)
        cm = expr(mat, unreal.MaterialExpressionMultiply, -240, 420, const_b=5.0)
        connect(amp, cm, "", "A")
        zero = expr(mat, unreal.MaterialExpressionConstant2Vector, -240, 560, r=0.0, g=0.0)
        wpo = binary(mat, unreal.MaterialExpressionAppendVector, zero, cm, -60, 480)
        mel.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(mat)
    return mat


def make_instance(name, parent, params):
    mi, _ = get_or_create(name, MAT_PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    for k, v in params.items():
        if isinstance(v, tuple):
            mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1.0))
        else:
            mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    mel.update_material_instance(mi)
    return mi


def build_materials():
    masters = {k: make_master("M_RC_" + k.capitalize(), k) for k in ("opaque", "net", "fence", "glass")}
    slots = {}
    for slot, (rgb, rough, metal, ecol, estr, kind) in PALETTE.items():
        params = {"Color": rgb, "Roughness": rough, "Metallic": metal}
        if ecol:
            params["EmissiveColor"] = ecol
            params["EmissiveStrength"] = estr
        slots[slot] = make_instance(slot, masters[kind], params)
    make_grass()
    make_simple("M_Line", (0.92, 0.92, 0.92), 0.8)
    make_kit()
    mpc = make_mpc()
    crowd = make_instanced("M_CrowdInstanced", mpc, True)
    seat = make_instanced("M_SeatInstanced", mpc, False)
    # Fan slots: shirt = team colour from instance data; skin/hair/trousers fixed tints, all bouncing together.
    slots["Crowd:MI_Shirt"] = make_instance("MI_CrowdShirt", crowd, {"UseInstanceColor": 1.0})
    slots["Crowd:MI_Skin"] = make_instance("MI_CrowdSkin", crowd, {"UseInstanceColor": 0.0, "Tint": (0.55, 0.38, 0.27)})
    slots["Crowd:MI_Hair"] = make_instance("MI_CrowdHair", crowd, {"UseInstanceColor": 0.0, "Tint": (0.04, 0.03, 0.02)})
    slots["Crowd:MI_Plastic"] = make_instance("MI_CrowdTrousers", crowd, {"UseInstanceColor": 0.0, "Tint": (0.07, 0.08, 0.1)})
    slots["Seat:MI_SeatHome"] = make_instance("MI_SeatInstanced", seat, {"UseInstanceColor": 1.0})
    slots["Seat:MI_MetalDark"] = make_instance("MI_SeatFrame", seat, {"UseInstanceColor": 0.0, "Tint": (0.08, 0.08, 0.09)})
    return slots


# ---------------------------------------------------------------- meshes

def import_fbx(path, dest, name, nanite):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("generate_lightmap_u_vs", False)
    data.set_editor_property("auto_generate_collision", False)
    try:
        data.set_editor_property("build_nanite", nanite)
    except Exception:
        pass
    task.set_editor_property("options", ui)
    asset_tools.import_asset_tasks([task])
    mesh = unreal.load_asset(f"{dest}/{name}")
    if mesh and nanite:
        # The Interchange FBX pipeline (default in recent versions) ignores FbxImportUI; set Nanite explicitly.
        try:
            ns = mesh.get_editor_property("nanite_settings")
            if not ns.get_editor_property("enabled"):
                ns.set_editor_property("enabled", True)
                mesh.set_editor_property("nanite_settings", ns)
        except Exception as exc:
            log(f"Nanite not set on {name}: {exc}")
    return mesh


def assign_slots(mesh, name, slots):
    prefix = "Crowd:" if name.startswith("SM_Crowd") else "Seat:" if name == "SM_Seat" else ""
    materials = mesh.get_editor_property("static_materials")
    for i, sm in enumerate(materials):
        slot = str(sm.get_editor_property("material_slot_name"))
        # Interchange may suffix slot names; match on the MI_* stem.
        stem = slot.split(".")[0]
        for candidate in (prefix + stem, stem):
            if candidate in slots:
                mesh.set_material(i, slots[candidate])
                break
        else:
            log(f"{name}: no material for slot '{slot}'")


def import_meshes(slots):
    for file in sorted(os.listdir(MESHES)):
        if not file.lower().endswith(".fbx"):
            continue
        name = os.path.splitext(file)[0]
        dest, nanite = MESH_FOLDERS.get(name, ("/Game/Art/Props", False))
        mesh = import_fbx(os.path.join(MESHES, file), dest, name, nanite)
        if not mesh:
            log(f"import failed: {file}")
            continue
        assign_slots(mesh, name, slots)
        eal.save_loaded_asset(mesh)
        log(f"imported {dest}/{name}")


# ---------------------------------------------------------------- audio & map

def import_audio():
    for file in sorted(os.listdir(AUDIO)):
        if not file.lower().endswith(".wav"):
            continue
        name = os.path.splitext(file)[0]
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(AUDIO, file))
        task.set_editor_property("destination_path", "/Game/Audio")
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        asset_tools.import_asset_tasks([task])
        sound = unreal.load_asset(f"/Game/Audio/{name}")
        if sound and name in ("S_CrowdLoop", "S_Heartbeat"):
            sound.set_editor_property("looping", True)
        if sound:
            eal.save_loaded_asset(sound)
            log(f"imported /Game/Audio/{name}")


def create_map():
    path = "/Game/Maps/Stadium"
    if eal.does_asset_exist(path):
        log("map already exists: " + path)
        return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.new_level(path)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, -4000.0, 200.0))
    les.save_current_level()
    log("created " + path + " (the game builds the venue at runtime)")


def main():
    with unreal.ScopedSlowTask(4, "Referee Career: setting up assets") as task:
        task.make_dialog(True)
        task.enter_progress_frame(1, "Materials")
        slots = build_materials()
        task.enter_progress_frame(1, "Meshes from Blender")
        import_meshes(slots)
        task.enter_progress_frame(1, "Audio")
        import_audio()
        task.enter_progress_frame(1, "Map")
        create_map()
    eal.save_directory("/Game/Art", only_if_is_dirty=True, recursive=True)
    eal.save_directory("/Game/Audio", only_if_is_dirty=True, recursive=True)
    log("done. Press Play.")


main()
