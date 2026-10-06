"""Renders preview images of the generated venues and props with Cycles (CPU is fine) into docs/images.

Adds what Unreal adds at runtime so the previews are honest: the striped pitch with markings, goals,
corner flags, floodlights and a crowd placed on the exported seat rows.
"""
import json
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rc_common as rc  # noqa: E402
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import build_community  # noqa: E402
import build_props  # noqa: E402
import build_stadium  # noqa: E402

FAST = "--fast" in sys.argv


def pitch(length, width):
    mb = rc.MeshBuilder("Pitch")
    stripes = 16
    margin_x, margin_y = 6.0, 5.0
    x0, x1 = -length / 2 - margin_x, length / 2 + margin_x
    for k in range(stripes):
        a = x0 + (x1 - x0) * k / stripes
        b = x0 + (x1 - x0) * (k + 1) / stripes
        mb.quad((a, -width / 2 - margin_y, 0), (b, -width / 2 - margin_y, 0), (b, width / 2 + margin_y, 0), (a, width / 2 + margin_y, 0),
                "Grass_A" if k % 2 else "Grass_B")
    w = 0.12

    def line(p, q):
        mb.beam((p[0], p[1], 0.006), (q[0], q[1], 0.006), w, "Line", height=0.004)

    hl, hw = length / 2, width / 2
    for s in (-1, 1):
        line((-hl, s * hw), (hl, s * hw))
        line((s * hl, -hw), (s * hl, hw))
        box_d, box_w = (16.5, 20.16) if length > 80 else (9.0, 12.0)
        six_d, six_w = (5.5, 9.16) if length > 80 else (3.0, 6.0)
        line((s * hl, -box_w), (s * (hl - box_d), -box_w))
        line((s * hl, box_w), (s * (hl - box_d), box_w))
        line((s * (hl - box_d), -box_w), (s * (hl - box_d), box_w))
        line((s * hl, -six_w), (s * (hl - six_d), -six_w))
        line((s * hl, six_w), (s * (hl - six_d), six_w))
        line((s * (hl - six_d), -six_w), (s * (hl - six_d), six_w))
    line((0, -hw), (0, hw))
    r = 9.15 if length > 80 else 6.0
    pts = [(r * math.cos(2 * math.pi * k / 48), r * math.sin(2 * math.pi * k / 48)) for k in range(49)]
    for p, q in zip(pts[:-1], pts[1:]):
        line(p, q)
    obj = mb.finish()
    for name, col in (("Grass_A", (0.10, 0.32, 0.07)), ("Grass_B", (0.13, 0.40, 0.09)), ("Line", (0.95, 0.95, 0.95))):
        m = bpy.data.materials.get(name)
        bsdf = m.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value = (*col, 1)
        bsdf.inputs["Roughness"].default_value = 0.9
    return obj


def place_goals(length, small):
    name = "SM_GoalSmall" if small else "SM_Goal"
    frame, net = bpy.data.objects[name], bpy.data.objects[name + "_Net"]
    for o in (frame, net):
        o.location = (length / 2, 0, 0)
    for o in (frame, net):
        dup = o.copy()
        dup.location = (-length / 2, 0, 0)
        dup.rotation_euler.z = math.pi
        bpy.context.scene.collection.objects.link(dup)


def crowd_shirt_material():
    m = rc.material("MI_Shirt")
    nt = m.node_tree
    info = nt.nodes.new("ShaderNodeObjectInfo")
    nt.links.new(info.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])


def place_crowd(seats, density, home, away, rng):
    crowd_shirt_material()
    meshes = [bpy.data.objects[n].data for n in ("SM_CrowdSeated", "SM_CrowdCheer", "SM_CrowdScarf")]
    for n in ("SM_CrowdSeated", "SM_CrowdCheer", "SM_CrowdScarf"):
        bpy.data.objects[n].hide_render = True
        bpy.data.objects[n].hide_viewport = True
    coll = bpy.data.collections.new("Crowd")
    bpy.context.scene.collection.children.link(coll)
    count = 0
    for (x, y, z, yaw) in seats:
        if rng.random() > density:
            continue
        mesh = meshes[0] if rng.random() < 0.7 else meshes[1 + int(rng.random() * 2)]
        o = bpy.data.objects.new("Fan", mesh)
        o.location = (x, y, z)
        o.rotation_euler.z = yaw
        team = home if x > 0 else away
        o.color = (*team, 1) if rng.random() < 0.8 else (0.9, 0.9, 0.9, 1)
        coll.objects.link(o)
        count += 1
    print("crowd figures:", count)


def stadium_seats(spacing_mult):
    with open(os.path.join(rc.DATA_DIR, "venue_stadium.json"), encoding="utf-8") as f:
        v = json.load(f)
    a, b, r = v["path"]["a"], v["path"]["b"], v["path"]["r"]
    out = []
    for row in v["rows"]:
        off = row["offset"]
        pts = rc.rounded_rect_path(a + off, b + off, r + off, 14, step=v["seatSpacing"] * spacing_mult)
        for i, (x, y) in enumerate(pts):
            if (i % (v["aisleEvery"] + 2)) >= v["aisleEvery"]:
                continue
            nx, ny = pts[(i + 1) % len(pts)]
            yaw = math.atan2(ny - y, nx - x) + math.pi / 2  # face the pitch (path runs counter-clockwise)
            out.append((x, y, row["z"], yaw))
    return out


def world(night):
    w = bpy.data.worlds.new("World")
    bpy.context.scene.world = w
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes["Background"]
    if night:
        bg.inputs["Color"].default_value = (0.012, 0.02, 0.045, 1)
        bg.inputs["Strength"].default_value = 1.0
        return
    sky = nt.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = "NISHITA"
    except TypeError:
        pass
    if hasattr(sky, "sun_elevation"):
        sky.sun_elevation = math.radians(16)
        sky.sun_rotation = math.radians(200)
    nt.links.new(sky.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 0.35


def sun(elev, azim, strength):
    light = bpy.data.lights.new("Sun", "SUN")
    light.energy = strength
    light.angle = math.radians(1.0)
    o = bpy.data.objects.new("Sun", light)
    o.rotation_euler = (math.radians(90 - elev), 0, math.radians(azim))
    bpy.context.scene.collection.objects.link(o)


def floodlights(points, target=(0, 0, 0), power=9000):
    for i, p in enumerate(points):
        light = bpy.data.lights.new(f"Flood{i}", "SPOT")
        light.energy = power
        light.spot_size = math.radians(75)
        light.spot_blend = 0.4
        light.shadow_soft_size = 1.2
        o = bpy.data.objects.new(f"Flood{i}", light)
        o.location = p
        d = Vector(target) - Vector(p)
        o.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        bpy.context.scene.collection.objects.link(o)


def camera(loc, look, lens=24):
    cam = bpy.data.cameras.new("Cam")
    cam.lens = lens
    cam.clip_end = 2000
    o = bpy.data.objects.new("Cam", cam)
    o.location = loc
    o.rotation_euler = (Vector(look) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.collection.objects.link(o)
    bpy.context.scene.camera = o
    return o


def render(name, samples=48, res=(1280, 720)):
    s = bpy.context.scene
    s.render.engine = "CYCLES"
    s.cycles.device = "CPU"
    s.cycles.samples = 12 if FAST else samples
    s.cycles.use_denoising = True
    s.cycles.max_bounces = 4
    s.render.resolution_x, s.render.resolution_y = res if not FAST else (640, 360)
    s.render.image_settings.file_format = "PNG"
    s.view_settings.view_transform = "AgX"
    try:
        s.view_settings.look = "AgX - Punchy"
    except TypeError:
        pass
    os.makedirs(rc.PREVIEW_DIR, exist_ok=True)
    s.render.filepath = os.path.join(rc.PREVIEW_DIR, name)
    bpy.ops.render.render(write_still=True)
    print("rendered", s.render.filepath)


def build_props_into_scene():
    """Builds the props into the current scene (without build_props.main(), which would reset it)."""
    keep = set(bpy.data.objects.keys())
    for fn in (lambda: build_props.goal("SM_Goal", 7.32, 2.44, 2.2), lambda: build_props.goal("SM_GoalSmall", 5.0, 2.0, 1.5),
               build_props.corner_flag, build_props.ball, build_props.var_monitor, build_props.bench, build_props.camera_rig,
               lambda: build_props.crowd_figure("SM_CrowdSeated", "seated"), lambda: build_props.crowd_figure("SM_CrowdCheer", "cheer"),
               lambda: build_props.crowd_figure("SM_CrowdScarf", "scarf"), build_props.ar_flag, build_props.cards, build_props.whistle):
        fn()
    return [o for o in bpy.context.scene.objects if o.name not in keep]


def stadium_shots():
    build_stadium.main()
    build_props_into_scene()
    for o in bpy.data.objects:
        if o.name.startswith(("SM_Ball", "SM_VARMonitor", "SM_Bench", "SM_BroadcastCamera", "SM_ARFlag", "SM_Card", "SM_Whistle", "SM_GoalSmall")):
            o.hide_render = True
    pitch(105, 68)
    place_goals(105, small=False)
    flag = bpy.data.objects["SM_CornerFlag"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            f = flag.copy()
            f.location = (sx * 52.5, sy * 34, 0)
            bpy.context.scene.collection.objects.link(f)
    flag.hide_render = True
    rng = random.Random(3)
    place_crowd(stadium_seats(1.0 if not FAST else 3.0), 0.85, (0.62, 0.04, 0.06), (0.06, 0.2, 0.62), rng)
    with open(os.path.join(rc.DATA_DIR, "venue_stadium.json"), encoding="utf-8") as f:
        v = json.load(f)
    world(night=True)
    floodlights(v["floodlights"], power=14000)
    camera((0, -48.6, 31.6), (0, 4, 0), lens=17)
    render("stadium_broadcast.png")
    camera((49, -29, 1.7), (20, 30, 10), lens=18)
    render("stadium_pitchside.png")
    camera((-150, -150, 130), (0, 0, 0), lens=32)
    world(night=False)
    for o in list(bpy.data.objects):
        if o.name.startswith("Flood"):
            bpy.data.objects.remove(o)
    sun(28, 210, 3.5)
    render("stadium_aerial.png", samples=32)


def community_shots():
    build_community.main()
    build_props_into_scene()
    for o in bpy.data.objects:
        if o.name.startswith(("SM_Ball", "SM_VARMonitor", "SM_BroadcastCamera", "SM_ARFlag", "SM_Card", "SM_Whistle", "SM_Goal_", "SM_Crowd")) or o.name in ("SM_Goal", "SM_Goal_Net"):
            o.hide_render = True
    pitch(64, 42)
    place_goals(64, small=True)
    world(night=False)
    sun(14, 240, 3.0)
    camera((-46, -40, 9), (0, 4, 0), lens=24)
    render("community_evening.png", samples=40)


def props_shot():
    rc.reset_scene()
    build_props_into_scene()
    layout = {
        "SM_Goal": (0, 6, 0), "SM_Goal_Net": (0, 6, 0), "SM_CornerFlag": (-3, -1, 0), "SM_ARFlag": (-1.6, -1.2, 1.0),
        "SM_Ball": (-1.0, -0.6, 0.11), "SM_VARMonitor": (2.5, -1.5, 0), "SM_Bench": (4, 3, 0), "SM_BroadcastCamera": (6.5, -0.5, 0),
        "SM_CrowdSeated": (-5, 3, 0), "SM_CrowdCheer": (-5, 4.2, 0), "SM_CrowdScarf": (-5, 5.4, 0),
        "SM_CardYellow": (-0.4, -1.6, 1.1), "SM_CardRed": (-0.2, -1.6, 1.1), "SM_Whistle": (0.0, -1.6, 1.1),
    }
    for name, loc in layout.items():
        if name in bpy.data.objects:
            bpy.data.objects[name].location = loc
    for name in ("SM_GoalSmall", "SM_GoalSmall_Net"):
        if name in bpy.data.objects:
            bpy.data.objects[name].hide_render = True
    floor = rc.MeshBuilder("Floor")
    floor.quad((-30, -30, 0), (30, -30, 0), (30, 30, 0), (-30, 30, 0), "MI_Apron")
    floor.finish()
    world(night=False)
    sun(40, 160, 3.0)
    camera((-4.5, -7.5, 2.6), (0.0, 2.0, 0.9), lens=26)
    render("props.png", samples=40)


if __name__ == "__main__":
    which = [a for a in sys.argv[1:] if not a.startswith("--")] or ["stadium", "community", "props"]
    if "stadium" in which:
        stadium_shots()
    if "community" in which:
        community_shots()
    if "props" in which:
        props_shot()
