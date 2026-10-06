"""Neighbourhood ground for the first tier: a 7-a-side pitch behind a chain-link fence, a small concrete
bleacher with a tin shade, a container changing room, lamp posts and the apartment blocks of the district.
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rc_common as rc  # noqa: E402

FX, FY = 40.0, 28.0  # fence half-extents around a 64 x 42 pitch


def fence(mb, link):
    def run(x0, y0, x1, y1):
        length = math.dist((x0, y0), (x1, y1))
        n = max(1, int(length / 3.0))
        rot = math.atan2(y1 - y0, x1 - x0)
        mb.box(((x0 + x1) / 2, (y0 + y1) / 2, 0.3), (length, 0.25, 0.6), "MI_Plaster", rot_z=rot)
        for k in range(n + 1):
            t = k / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            mb.cylinder((x, y, 0.6), 3.0, 0.04, "MI_Metal", segments=8)
        for k in range(n):
            t0, t1 = k / n, (k + 1) / n
            a = (x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0)
            b = (x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1)
            link.quad((a[0], a[1], 0.6), (b[0], b[1], 0.6), (b[0], b[1], 3.55), (a[0], a[1], 3.55), "MI_ChainLink")
        mb.cylinder_between((x0, y0, 3.6), (x1, y1, 3.6), 0.03, "MI_Metal")

    run(-FX, -FY, FX, -FY)
    run(FX, FY, -FX, FY)
    run(FX, -FY, FX, FY)
    run(-FX, FY, -FX, -6)   # gate gap on the west side
    run(-FX, -2, -FX, -FY)


def bleacher(mb):
    rows, depth, riser = 6, 0.8, 0.42
    y0 = FY + 0.8
    for i in range(rows):
        mb.box((0, y0 + i * depth + depth / 2, (i + 1) * riser / 2), (30, depth, (i + 1) * riser), "MI_Concrete")
        mb.box((0, y0 + i * depth + 0.2, (i + 1) * riser + 0.03), (30, 0.4, 0.06), "MI_StepEdge")
    back = y0 + rows * depth
    for x in range(-15, 16, 5):
        mb.cylinder((x, back + 0.2, 0.0), 5.4, 0.07, "MI_Metal", segments=10)
        mb.cylinder((x, y0 - 0.3, 0.0), 4.2, 0.07, "MI_Metal", segments=10)
    # Corrugated tin shade: alternating strips at two heights.
    strips = 40
    for k in range(strips):
        x0 = -16 + 32 * k / strips
        x1 = -16 + 32 * (k + 1) / strips
        dz = 0.05 if k % 2 else 0.0
        mb.quad((x0, y0 - 0.8, 4.2 + dz), (x1, y0 - 0.8, 4.2 + dz), (x1, back + 0.6, 5.4 + dz), (x0, back + 0.6, 5.4 + dz), "MI_Metal")
        mb.quad((x0, back + 0.6, 5.38 + dz), (x1, back + 0.6, 5.38 + dz), (x1, y0 - 0.8, 4.18 + dz), (x0, y0 - 0.8, 4.18 + dz), "MI_Metal")


def changing_room(mb):
    cx, cy = -30.0, -FY - 6.0
    mb.box((cx, cy, 1.3), (12.0, 2.45, 2.6), "MI_Container")
    for k in range(24):
        mb.box((cx - 5.9 + k * 0.5, cy - 1.24, 1.3), (0.08, 0.04, 2.5), "MI_Container")
    mb.box((cx + 3.0, cy - 1.25, 1.05), (0.95, 0.05, 2.0), "MI_Wood")
    mb.box((cx - 2.5, cy - 1.25, 1.6), (1.4, 0.05, 0.7), "MI_Window")
    mb.box((cx, cy - 2.2, 2.65), (12.5, 2.2, 0.08), "MI_Metal")


def lamps(mb):
    lights = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (FX - 1.0), sy * (FY - 1.0)
            mb.cylinder((x, y, 0.0), 12.0, 0.16, "MI_MetalDark", segments=10)
            mb.box((x - sx * 0.6, y - sy * 0.4, 12.2), (1.6, 1.0, 0.5), "MI_MetalDark")
            mb.box((x - sx * 0.6, y - sy * 0.4, 11.92), (1.4, 0.85, 0.06), "MI_Floodlight")
            lights.append([round(x - sx * 0.6, 2), round(y - sy * 0.4, 2), 11.8])
    return lights


def apartment(mb, cx, cy, w, d, floors, mat, rot, rng):
    h = floors * 3.1
    mb.box((cx, cy, h / 2), (w, d, h), mat, rot_z=rot)
    c, s = math.cos(rot), math.sin(rot)

    def local(x, y, z):
        return (cx + x * c - y * s, cy + x * s + y * c, z)

    cols = max(2, int(w / 3.2))
    for f in range(floors):
        z = 1.6 + f * 3.1
        for k in range(cols):
            x = -w / 2 + (k + 0.5) * w / cols
            lit = "MI_Window" if rng.random() < 0.6 else "MI_ConcreteDark"
            for side in (-1, 1):
                y = side * (d / 2 + 0.02)
                p = [local(x - 0.6, y, z - 0.7), local(x + 0.6, y, z - 0.7), local(x + 0.6, y, z + 0.7), local(x - 0.6, y, z + 0.7)]
                mb.poly(p if side > 0 else list(reversed(p)), lit)
            if f % 2 == 0 and k % 2 == 1:
                bx, by, bz = local(x, -d / 2 - 0.6, z - 0.75)
                mb.box((bx, by, bz), (2.4, 1.2, 0.15), "MI_ConcreteDark", rot_z=rot)
    # Water tanks and a dish on the roof.
    for k in range(rng.randint(1, 3)):
        tx, ty, _ = local(-w / 3 + k * 2.2, d / 4, 0)
        mb.cylinder((tx, ty, h), 1.4, 0.6, "MI_Plastic" if k % 2 else "MI_SeatNeutral", segments=12)
    dx, dy, _ = local(w / 3, -d / 4, 0)
    mb.cylinder((dx, dy, h), 0.8, 0.05, "MI_Metal", segments=6)
    mb.cylinder((dx, dy, h + 0.8), 0.08, 0.5, "MI_SeatNeutral", segments=14, axis="X")


def palm(mb, x, y, h, rng):
    segs = 8
    pts = []
    for k in range(segs + 1):
        t = k / segs
        pts.append((x + 0.4 * math.sin(t * 1.4), y, t * h))
    for a, b in zip(pts[:-1], pts[1:]):
        mb.cylinder_between(a, b, 0.22, "MI_Bark", segments=8)
    top = pts[-1]
    for k in range(9):
        ang = k * 2 * math.pi / 9 + rng.random() * 0.3
        dx, dy = math.cos(ang), math.sin(ang)
        mid = (top[0] + dx * 1.6, top[1] + dy * 1.6, top[2] + 0.4)
        tip = (top[0] + dx * 3.2, top[1] + dy * 3.2, top[2] - 0.9)
        px, py = -dy * 0.45, dx * 0.45
        mb.poly([top, (mid[0] + px, mid[1] + py, mid[2]), tip, (mid[0] - px, mid[1] - py, mid[2])], "MI_Palm")
        mb.poly([(mid[0] - px, mid[1] - py, mid[2]), tip, (mid[0] + px, mid[1] + py, mid[2]), top], "MI_Palm")


def main():
    rc.reset_scene()
    rng = random.Random(7)
    ground = rc.MeshBuilder("SM_CommunityGround")
    ground.quad((-FX - 30, -FY - 30, -0.03), (FX + 30, -FY - 30, -0.03), (FX + 30, FY + 30, -0.03), (-FX - 30, FY + 30, -0.03), "MI_Dirt")
    ground_obj = ground.finish()
    rc.box_project_uvs(ground_obj, 4.0)

    mb = rc.MeshBuilder("SM_Community")
    link = rc.MeshBuilder("SM_CommunityFence")
    fence(mb, link)
    bleacher(mb)
    changing_room(mb)
    lights = lamps(mb)
    for (bx, by) in ((-34, 6), (34, -6)):
        mb.box((bx, by, 0.45), (0.5, 4.0, 0.06), "MI_Wood")
        mb.box((bx, by, 0.22), (0.4, 3.8, 0.44), "MI_Metal")
    blocks = []
    for side in range(4):
        for k in range(5):
            w, d = rng.uniform(12, 20), rng.uniform(10, 14)
            floors = rng.randint(3, 7)
            if side == 0:
                cx, cy, rot = -60 + k * 30 + rng.uniform(-3, 3), -FY - 22 - rng.uniform(0, 8), 0.0
            elif side == 1:
                cx, cy, rot = -60 + k * 30 + rng.uniform(-3, 3), FY + 24 + rng.uniform(0, 8), math.pi
            elif side == 2:
                cx, cy, rot = FX + 26 + rng.uniform(0, 8), -36 + k * 18, math.pi / 2
            else:
                cx, cy, rot = -FX - 26 - rng.uniform(0, 8), -36 + k * 18, -math.pi / 2
            blocks.append((cx, cy, w, d, floors, "MI_Plaster" if (side + k) % 2 else "MI_PlasterB", rot))
    for b in blocks:
        apartment(mb, *b[:6], b[6], rng)
    for (px, py) in ((-FX - 4, -FY - 3), (FX + 4, FY + 3), (FX + 5, -FY - 4), (-FX - 5, FY + 6), (20, -FY - 5), (-12, FY + 9)):
        palm(mb, px, py, rng.uniform(7, 10), rng)
    community = mb.finish()
    rc.box_project_uvs(community, 2.0)
    fence_obj = link.finish()
    rc.box_project_uvs(fence_obj, 0.07)  # one UV tile per 7 cm chain-link cell
    print("community triangles:", rc.triangle_count([community, ground_obj, fence_obj]))
    rc.export_fbx([community], "SM_Community.fbx")
    rc.export_fbx([fence_obj], "SM_CommunityFence.fbx")
    rc.export_fbx([ground_obj], "SM_CommunityGround.fbx")
    rc.write_json(
        "venue_community.json",
        {
            "comment": "Generated by Tools/Blender/build_community.py. Metres, Blender axes.",
            "rows": [{"tier": "bleacher", "x0": -14.5, "x1": 14.5, "y": round(FY + 0.8 + i * 0.8 + 0.35, 3), "z": round((i + 1) * 0.42, 3)} for i in range(6)],
            "seatSpacing": 0.55,
            "floodlights": lights,
            "cameras": {"main": [0, -FY - 4, 9.0], "behind_goal_home": [FX + 6, 0, 6.0], "behind_goal_away": [-FX - 6, 0, 6.0]},
        },
    )
    return [community, ground_obj, fence_obj]


if __name__ == "__main__":
    main()
