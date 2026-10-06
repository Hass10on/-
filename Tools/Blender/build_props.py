"""Match props: goals, corner flags, the assistant's flag, whistle, cards, ball, VAR monitor, bench, crowd figures.

Every prop is exported to its own FBX so Unreal can place them per pitch size and instance the crowd.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rc_common as rc  # noqa: E402
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402


def goal(name, width, height, depth):
    """Goal centred on its line at x = 0, opening towards -X (Unreal mirrors it for the other end)."""
    frame = rc.MeshBuilder(name)
    r = 0.06
    hw = width / 2
    frame.cylinder((0, -hw, 0), height + r, r, "MI_GoalPost", segments=20)
    frame.cylinder((0, hw, 0), height + r, r, "MI_GoalPost", segments=20)
    frame.cylinder((0, -hw - r, height + r * 0.5), width + 2 * r, r, "MI_GoalPost", segments=20, axis="Y")
    # Back frame and stanchions.
    frame.cylinder_between((depth, -hw, 0.02), (depth, hw, 0.02), 0.025, "MI_GoalPost")
    for s in (-1, 1):
        frame.cylinder_between((0.0, s * hw, height), (depth * 0.35, s * hw, height), 0.025, "MI_GoalPost")
        frame.cylinder_between((depth * 0.35, s * hw, height), (depth, s * hw, 0.02), 0.025, "MI_GoalPost")
        frame.cylinder_between((0.0, s * hw, 0.02), (depth, s * hw, 0.02), 0.025, "MI_GoalPost")
    frame.cylinder_between((depth * 0.35, -hw, height), (depth * 0.35, hw, height), 0.025, "MI_GoalPost")
    frame_obj = frame.finish(smooth=True)

    net = rc.MeshBuilder(name + "_Net")
    d1 = depth * 0.35
    net.quad((0.02, -hw, height), (0.02, hw, height), (d1, hw, height), (d1, -hw, height), "MI_Net")
    net.quad((d1, -hw, height), (d1, hw, height), (depth, hw, 0.02), (depth, -hw, 0.02), "MI_Net")
    for s in (-1, 1):
        net.poly([(0.02, s * hw, 0.02), (depth, s * hw, 0.02), (d1, s * hw, height), (0.02, s * hw, height)], "MI_Net")
    net_obj = net.finish()
    rc.box_project_uvs(net_obj, 0.12)  # one UV tile per 12 cm mesh cell; the Unreal net material masks with frac(UV)
    return [frame_obj, net_obj]


def corner_flag():
    mb = rc.MeshBuilder("SM_CornerFlag")
    mb.cylinder((0, 0, 0), 1.55, 0.017, "MI_GoalPost", segments=10)
    mb.box((0.0, 0.0, 1.56), (0.04, 0.04, 0.03), "MI_Plastic")
    mb.poly([(0.0, 0.0, 1.52), (0.0, 0.42, 1.48), (0.0, 0.40, 1.18), (0.0, 0.0, 1.22)], "MI_FlagRed")
    mb.poly([(0.0, 0.0, 1.22), (0.0, 0.40, 1.18), (0.0, 0.42, 1.48), (0.0, 0.0, 1.52)], "MI_FlagRed")
    return [mb.finish()]


def ar_flag():
    """Assistant referee flag: handle along +Z, red/yellow quarters."""
    mb = rc.MeshBuilder("SM_ARFlag")
    mb.cylinder((0, 0, -0.18), 0.62, 0.014, "MI_Plastic", segments=10)
    mb.box((0, 0, -0.12), (0.035, 0.035, 0.14), "MI_Plastic", bevel=0.008)
    w, h, z0 = 0.42, 0.34, 0.08
    for i in range(2):
        for j in range(2):
            mat = "MI_FlagRed" if (i + j) % 2 == 0 else "MI_Flag"
            y0, y1 = i * w / 2, (i + 1) * w / 2
            zz0, zz1 = z0 + j * h / 2, z0 + (j + 1) * h / 2
            mb.quad((0, y0, zz0), (0, y1, zz0), (0, y1, zz1), (0, y0, zz1), mat)
            mb.quad((0, y0, zz1), (0, y1, zz1), (0, y1, zz0), (0, y0, zz0), mat)
    return [mb.finish()]


def whistle():
    mb = rc.MeshBuilder("SM_Whistle")
    mb.box((0.0, 0.0, 0.0), (0.05, 0.022, 0.016), "MI_Plastic", bevel=0.004)
    mb.cylinder((0.012, -0.011, 0.0), 0.022, 0.013, "MI_Plastic", segments=16, axis="Y")
    mb.cylinder((-0.03, -0.004, 0.0), 0.008, 0.007, "MI_Metal", segments=10, axis="Y")
    return [mb.finish(smooth=True)]


def cards():
    out = []
    for name, mat in (("SM_CardYellow", "MI_CardYellow"), ("SM_CardRed", "MI_CardRed")):
        mb = rc.MeshBuilder(name)
        mb.box((0, 0, 0), (0.002, 0.085, 0.12), mat, bevel=0.0008)
        out.append(mb.finish())
    return out


def ball():
    """Truncated icosahedron (12 pentagons, 20 hexagons) subdivided and projected onto a 22 cm sphere."""
    radius = 0.11
    tmp = bmesh.new()
    bmesh.ops.create_icosphere(tmp, subdivisions=1, radius=1.0)
    ico_v = [Vector(v.co) for v in tmp.verts]
    ico_f = [[v.index for v in f.verts] for f in tmp.faces]
    tmp.free()
    # bmesh's subdivisions=1 is a plain icosahedron (12 verts, 20 faces).
    bm = bmesh.new()
    edge_pts = {}

    def third(a, b):
        key = (a, b)
        if key not in edge_pts:
            edge_pts[key] = bm.verts.new(ico_v[a] + (ico_v[b] - ico_v[a]) / 3.0)
        return edge_pts[key]

    faces_mat = []
    for f in ico_f:
        a, b, c = f
        hexa = [third(a, b), third(b, a), third(b, c), third(c, b), third(c, a), third(a, c)]
        faces_mat.append((bm.faces.new(hexa), 0))
    neighbours = {i: set() for i in range(len(ico_v))}
    for f in ico_f:
        for i in range(3):
            neighbours[f[i]].update(f[:i] + f[i + 1:])
    for v, ns in neighbours.items():
        pts = [third(v, n) for n in ns]
        normal = ico_v[v].normalized()
        ref = (pts[0].co - ico_v[v]).normalized()
        side = normal.cross(ref)
        pts.sort(key=lambda p: math.atan2((p.co - ico_v[v]).dot(side), (p.co - ico_v[v]).dot(ref)))
        faces_mat.append((bm.faces.new(pts), 1))
    for face, m in faces_mat:
        face.material_index = m
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=3, use_grid_fill=True)
    for v in bm.verts:
        v.co = v.co.normalized() * radius
    mesh = bpy.data.meshes.new("SM_Ball")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(rc.material("MI_Ball"))
    mesh.materials.append(rc.material("MI_BallPanel"))
    for p in mesh.polygons:
        p.use_smooth = True
    obj = bpy.data.objects.new("SM_Ball", mesh)
    bpy.context.scene.collection.objects.link(obj)
    return [obj]


def var_monitor():
    mb = rc.MeshBuilder("SM_VARMonitor")
    mb.box((0, 0, 0.03), (0.6, 0.6, 0.06), "MI_MetalDark")
    mb.box((0, 0, 0.65), (0.1, 0.1, 1.25), "MI_MetalDark")
    mb.box((0, 0, 1.45), (0.08, 0.78, 0.5), "MI_Plastic", bevel=0.01)
    mb.box((-0.045, 0, 1.45), (0.01, 0.72, 0.43), "MI_Screen")
    mb.box((-0.2, 0, 1.75), (0.5, 0.86, 0.04), "MI_MetalDark")
    for s in (-1, 1):
        mb.box((-0.2, s * 0.43, 1.55), (0.5, 0.03, 0.4), "MI_MetalDark")
    return [mb.finish()]


def sub_board():
    mb = rc.MeshBuilder("SM_SubBoard")
    mb.box((0, 0, 0), (0.06, 0.5, 0.32), "MI_Plastic", bevel=0.01)
    for s in (-1, 1):
        mb.box((-0.032, s * 0.12, 0.0), (0.005, 0.2, 0.22), "MI_Screen")
    mb.cylinder((0, 0, -0.42), 0.26, 0.018, "MI_Plastic", segments=10)
    return [mb.finish()]


def bench(name="SM_Bench", length=4.0):
    mb = rc.MeshBuilder(name)
    for k in range(int(length / 0.5)):
        x = -length / 2 + 0.25 + k * 0.5
        mb.box((x, 0, 0.45), (0.46, 0.45, 0.06), "MI_SeatNeutral", bevel=0.01)
        mb.box((x, 0.2, 0.75), (0.46, 0.05, 0.5), "MI_SeatNeutral", bevel=0.01)
    for s in (-1, 1):
        mb.box((s * (length / 2 - 0.1), 0.0, 0.22), (0.06, 0.45, 0.44), "MI_Metal")
    mb.box((0, 0.0, 0.42), (length, 0.08, 0.04), "MI_Metal")
    return [mb.finish()]


def camera_rig():
    mb = rc.MeshBuilder("SM_BroadcastCamera")
    for a in range(3):
        ang = a * 2 * math.pi / 3
        mb.cylinder_between((0, 0, 1.2), (0.55 * math.cos(ang), 0.55 * math.sin(ang), 0.0), 0.02, "MI_MetalDark")
    mb.box((0, 0, 1.3), (0.25, 0.25, 0.15), "MI_MetalDark")
    mb.box((0.05, 0, 1.55), (0.55, 0.22, 0.3), "MI_Plastic", bevel=0.02)
    mb.cylinder((0.32, 0, 1.58), 0.45, 0.075, "MI_MetalDark", segments=16, axis="X")
    mb.box((-0.35, 0, 1.6), (0.12, 0.2, 0.18), "MI_Screen")
    return [mb.finish()]


def crowd_figure(name, pose):
    """Low-poly spectator (~400 triangles). Slots: MI_Skin, MI_Shirt (team colour in Unreal), MI_Hair, MI_Plastic (trousers)."""
    mb = rc.MeshBuilder(name)
    seated = pose in ("seated", "cheer")
    hip_z = 0.45 if seated else 0.95
    # Legs
    for s in (-1, 1):
        y = s * 0.1
        if seated:
            mb.beam((0.0, y, hip_z), (0.42, y, hip_z), 0.13, "MI_Plastic")
            mb.beam((0.42, y, hip_z), (0.45, y, 0.02), 0.11, "MI_Plastic")
        else:
            mb.beam((0.0, y, hip_z), (0.03, y, 0.02), 0.13, "MI_Plastic")
    # Torso and shoulders
    mb.box((0.0, 0.0, hip_z + 0.3), (0.24, 0.38, 0.6), "MI_Shirt", bevel=0.04)
    neck_z = hip_z + 0.62
    mb.cylinder((0.0, 0.0, neck_z - 0.02), 0.07, 0.05, "MI_Skin", segments=8)
    # Head
    head = Vector((0.0, 0.0, neck_z + 0.14))
    mb.box(tuple(head), (0.2, 0.18, 0.23), "MI_Skin", bevel=0.05)
    mb.box((head.x - 0.02, head.y, head.z + 0.1), (0.21, 0.19, 0.07), "MI_Hair", bevel=0.02)
    # Arms
    for s in (-1, 1):
        sh = Vector((0.0, s * 0.22, neck_z - 0.05))
        if pose == "cheer":
            elbow = sh + Vector((0.05, s * 0.08, 0.28))
            hand = elbow + Vector((0.02, s * 0.02, 0.28))
        elif pose == "scarf":
            elbow = sh + Vector((0.18, s * 0.05, 0.12))
            hand = elbow + Vector((0.05, -s * 0.12, 0.28))
        else:
            elbow = sh + Vector((0.12, s * 0.03, -0.25))
            hand = elbow + Vector((0.26, -s * 0.05, 0.0))
        mb.beam(tuple(sh), tuple(elbow), 0.09, "MI_Shirt")
        mb.beam(tuple(elbow), tuple(hand), 0.075, "MI_Skin")
    if pose == "scarf":
        mb.beam((0.22, -0.3, neck_z + 0.3), (0.22, 0.3, neck_z + 0.3), 0.04, "MI_Shirt", height=0.18)
    return [mb.finish()]


def stadium_seat():
    """Tip-up bucket seat, origin at the floor of its row, facing +X. Instanced ~45,000 times in Unreal."""
    mb = rc.MeshBuilder("SM_Seat")
    mb.box((0.05, 0.0, 0.42), (0.42, 0.44, 0.05), "MI_SeatHome", bevel=0.015)
    mb.beam((-0.16, -0.2, 0.44), (-0.2, -0.2, 0.86), 0.04, "MI_SeatHome", height=0.4)
    mb.box((-0.19, 0.0, 0.66), (0.05, 0.44, 0.42), "MI_SeatHome", bevel=0.015)
    mb.box((0.0, 0.0, 0.2), (0.08, 0.3, 0.4), "MI_MetalDark")
    return [mb.finish()]


def main():
    rc.reset_scene()
    groups = {
        "SM_Goal": goal("SM_Goal", 7.32, 2.44, 2.2),
        "SM_GoalSmall": goal("SM_GoalSmall", 5.0, 2.0, 1.5),
        "SM_CornerFlag": corner_flag(),
        "SM_ARFlag": ar_flag(),
        "SM_Whistle": whistle(),
        "SM_Cards": cards(),
        "SM_Ball": ball(),
        "SM_VARMonitor": var_monitor(),
        "SM_SubBoard": sub_board(),
        "SM_Bench": bench(),
        "SM_Seat": stadium_seat(),
        "SM_BroadcastCamera": camera_rig(),
        "SM_CrowdSeated": crowd_figure("SM_CrowdSeated", "seated"),
        "SM_CrowdCheer": crowd_figure("SM_CrowdCheer", "cheer"),
        "SM_CrowdScarf": crowd_figure("SM_CrowdScarf", "scarf"),
    }
    for name, objs in groups.items():
        if name == "SM_Cards":
            for o in objs:
                rc.export_fbx([o], o.name + ".fbx")
        elif name.startswith("SM_Goal"):
            rc.export_fbx([objs[0]], name + ".fbx")
            rc.export_fbx([objs[1]], name + "_Net.fbx")
        else:
            rc.export_fbx(objs, name + ".fbx")
    return groups


if __name__ == "__main__":
    main()
