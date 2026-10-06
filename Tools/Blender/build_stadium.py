"""Procedural 45,000-seat bowl stadium for the professional tiers.

The bowl is built from stepped rows swept along rounded rectangles, so the same numbers drive the
geometry here and the seat/crowd placement in Unreal (written to Content/Data/venue_stadium.json).
Run: python Tools/Blender/build_all.py   (or inside Blender: Text editor > Run Script)
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rc_common as rc  # noqa: E402

# Bowl footprint around a 105 x 68 pitch (half-extents measured to the front wall).
A0, B0, R0 = 62.0, 44.0, 16.0
FRONT_WALL = 1.3
LOWER = dict(rows=24, depth=0.80, riser=0.42)
LEDGE = 1.6          # concourse walkway between tiers
BAND = 3.6           # hospitality / glazed band height
UPPER = dict(rows=20, depth=0.86, riser=0.60, parapet=1.1)
ROOF_INNER = 5.0     # roof leading edge offset from the bowl front
CORNER_SEG = 14


def path(off, step=None):
    return rc.rounded_rect_path(A0 + off, B0 + off, R0 + off, CORNER_SEG, step)


def ring_strip(mb, off_a, z_a, off_b, z_b, mat):
    """Quad strip between two offset paths (same topology)."""
    pa, pb = path(off_a), path(off_b)
    n = len(pa)
    for i in range(n):
        j = (i + 1) % n
        mb.quad((pa[i][0], pa[i][1], z_a), (pa[j][0], pa[j][1], z_a), (pb[j][0], pb[j][1], z_b), (pb[i][0], pb[i][1], z_b), mat)


def build_bowl(mb):
    seat_rows = []
    # Pitch-side wall.
    ring_strip(mb, 0.0, 0.0, 0.0, FRONT_WALL, "MI_ConcreteDark")
    z = FRONT_WALL
    off = 0.0
    for i in range(LOWER["rows"]):
        nxt = off + LOWER["depth"]
        ring_strip(mb, off, z, nxt - 0.06, z, "MI_Concrete")
        ring_strip(mb, nxt - 0.06, z, nxt, z, "MI_StepEdge")
        seat_rows.append({"tier": "lower", "offset": off + LOWER["depth"] * 0.42, "z": z})
        if i < LOWER["rows"] - 1:
            ring_strip(mb, nxt, z, nxt, z + LOWER["riser"], "MI_Concrete")
            z += LOWER["riser"]
        off = nxt
    lower_top, lower_back = z, off
    # Concourse ledge, then the glazed hospitality band.
    ring_strip(mb, lower_back, lower_top, lower_back, lower_top + 0.4, "MI_Concrete")
    ring_strip(mb, lower_back, lower_top + 0.4, lower_back + LEDGE, lower_top + 0.4, "MI_ConcreteDark")
    band_base = lower_top + 0.4
    ring_strip(mb, lower_back + LEDGE, band_base, lower_back + LEDGE, band_base + BAND, "MI_Glass")
    # LED ribbon on the upper tier fascia.
    up_front = lower_back + LEDGE
    z = band_base + BAND
    ring_strip(mb, up_front, z, up_front, z + 0.8, "MI_LEDBoard")
    ring_strip(mb, up_front, z + 0.8, up_front, z + UPPER["parapet"], "MI_ConcreteDark")
    ring_strip(mb, up_front, z + UPPER["parapet"], up_front + 0.25, z + UPPER["parapet"], "MI_StepEdge")
    off = up_front + 0.25
    z = z + 0.5
    for i in range(UPPER["rows"]):
        nxt = off + UPPER["depth"]
        ring_strip(mb, off, z, nxt - 0.06, z, "MI_Concrete")
        ring_strip(mb, nxt - 0.06, z, nxt, z, "MI_StepEdge")
        seat_rows.append({"tier": "upper", "offset": off + UPPER["depth"] * 0.42, "z": z})
        ring_strip(mb, nxt, z, nxt, z + UPPER["riser"], "MI_Concrete")
        z += UPPER["riser"]
        off = nxt
    upper_top, upper_back = z, off
    # Rear wall up to the roof line and outer facade fins.
    ring_strip(mb, upper_back, upper_top, upper_back, upper_top + 2.5, "MI_ConcreteDark")
    facade = upper_back + 1.2
    ring_strip(mb, facade, 0.0, facade, upper_top + 2.5, "MI_Facade")
    ring_strip(mb, upper_back, upper_top + 2.5, facade, upper_top + 2.5, "MI_ConcreteDark")
    for (x, y) in path(facade + 0.5, step=3.0):
        mb.box((x, y, (upper_top + 4.0) / 2), (0.35, 0.35, upper_top + 4.0), "MI_Metal", rot_z=math.atan2(y, x))
    return seat_rows, upper_top, upper_back


def build_roof(mb, upper_top, upper_back):
    inner, outer = ROOF_INNER, upper_back + 2.0
    z_in, z_out = upper_top + 6.5, upper_top + 4.0
    # Membrane (top and underside) and a fascia band carrying the floodlight strip.
    ring_strip(mb, inner, z_in, outer, z_out, "MI_RoofMembrane")
    ring_strip(mb, outer, z_out - 0.5, inner, z_in - 0.5, "MI_RoofMembrane")
    ring_strip(mb, inner, z_in - 2.2, inner, z_in, "MI_MetalDark")
    ring_strip(mb, outer, z_out - 0.5, outer, z_out, "MI_MetalDark")
    floodlights = []
    lights = path(inner + 0.4, step=2.6)
    for idx, (x, y) in enumerate(lights):
        ang = math.atan2(y, x)
        mb.box((x, y, z_in - 2.5), (1.3, 0.5, 0.45), "MI_Floodlight", rot_z=ang)
        if idx % max(1, len(lights) // 16) == 0:
            floodlights.append([round(x, 2), round(y, 2), round(z_in - 2.6, 2)])
    # Exposed radial trusses on top of the roof and columns at the back.
    inner_pts, outer_pts = path(inner, step=12.0), path(outer, step=12.0)
    n = min(len(inner_pts), len(outer_pts))
    for k in range(n):
        i = inner_pts[k * len(inner_pts) // n]
        o = outer_pts[k * len(outer_pts) // n]
        mb.beam((i[0], i[1], z_in + 0.1), (o[0], o[1], z_out + 0.1), 0.5, "MI_Metal", height=0.9)
        mb.beam((i[0], i[1], z_in + 0.9), (o[0], o[1], z_out + 3.2), 0.3, "MI_Metal")
        mb.beam((o[0], o[1], z_out + 0.1), (o[0], o[1], z_out + 3.2), 0.3, "MI_Metal")
        mb.cylinder((o[0], o[1], 0.0), z_out, 0.45, "MI_Metal", segments=12)
    return floodlights, z_in


def build_pitchside(mb):
    # Perimeter advertising boards, angled towards the pitch, with gaps behind the goals.
    def board(x0, y0, x1, y1, facing):
        length = math.dist((x0, y0), (x1, y1))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        rot = math.atan2(y1 - y0, x1 - x0)
        mb.box((cx, cy, 0.45), (length, 0.25, 0.9), "MI_MetalDark", rot_z=rot)
        nx, ny = math.cos(facing), math.sin(facing)
        mb.box((cx + nx * 0.14, cy + ny * 0.14, 0.47), (length - 0.1, 0.03, 0.82), "MI_LEDBoard", rot_z=rot)

    for sign in (-1, 1):
        board(-50, sign * 39.5, -22, sign * 39.5, -sign * math.pi / 2)
        board(22, sign * 39.5, 50, sign * 39.5, -sign * math.pi / 2)
        if sign > 0:
            board(-22, 39.5, 22, 39.5, -math.pi / 2)
        board(sign * 57.5, -32, sign * 57.5, -7, math.pi if sign > 0 else 0)
        board(sign * 57.5, 7, sign * 57.5, 32, math.pi if sign > 0 else 0)

    # Dugouts and the technical area on the main-stand side (negative Y).
    for sx in (-1, 1):
        cx = sx * 11.0
        mb.box((cx, -41.6, 0.15), (9.0, 2.6, 0.3), "MI_ConcreteDark")
        mb.box((cx, -42.8, 1.3), (9.0, 0.15, 2.3), "MI_MetalDark")
        for k in range(12):
            mb.box((cx - 4.0 + k * 0.72, -42.2, 0.75), (0.55, 0.55, 0.12), "MI_SeatNeutral")
            mb.box((cx - 4.0 + k * 0.72, -42.45, 1.15), (0.55, 0.08, 0.7), "MI_SeatNeutral")
        mb.box((cx, -41.6, 2.45), (9.1, 2.8, 0.06), "MI_Glass")
        mb.box((cx - 4.55, -41.6, 1.3), (0.06, 2.6, 2.3), "MI_Glass")
        mb.box((cx + 4.55, -41.6, 1.3), (0.06, 2.6, 2.3), "MI_Glass")
    # Players' tunnel canopy.
    mb.box((0, -42.0, 1.6), (3.6, 4.0, 0.12), "MI_MetalDark")
    mb.box((-1.75, -42.0, 0.8), (0.1, 4.0, 1.6), "MI_MetalDark")
    mb.box((1.75, -42.0, 0.8), (0.1, 4.0, 1.6), "MI_MetalDark")
    mb.box((0, -42.0, 2.6), (3.8, 4.2, 0.1), "MI_Glass")
    # Fourth official's table and the referee review area (VAR monitor).
    mb.box((0, -38.6, 0.75), (1.8, 0.7, 0.06), "MI_MetalDark")
    mb.box((0, -38.6, 0.37), (1.7, 0.6, 0.74), "MI_Plastic")
    mb.box((4.5, -38.7, 0.65), (0.12, 0.12, 1.3), "MI_MetalDark")
    mb.box((4.5, -38.7, 1.45), (0.75, 0.08, 0.48), "MI_Screen", rot_z=0.0)
    mb.box((4.5, -38.75, 1.75), (0.85, 0.5, 0.05), "MI_MetalDark")


def build_screens_and_gantry(mb, roof_z):
    for sx, sy in ((1, 1), (-1, -1)):
        cx, cy = sx * (A0 + 3.5), sy * (B0 + 3.5)
        ang = math.atan2(-cy, -cx)
        mb.box((cx, cy, roof_z - 9.0), (14.0, 0.8, 7.5), "MI_MetalDark", rot_z=ang + math.pi / 2)
        nx, ny = math.cos(ang), math.sin(ang)
        mb.box((cx + nx * 0.45, cy + ny * 0.45, roof_z - 9.0), (13.2, 0.05, 6.9), "MI_Screen", rot_z=ang + math.pi / 2)
    # TV gantry hanging under the main-stand roof.
    mb.box((0, -(B0 + 6.5), roof_z - 3.6), (24.0, 2.4, 0.25), "MI_MetalDark")
    mb.box((0, -(B0 + 5.4), roof_z - 3.0), (24.0, 0.08, 1.0), "MI_Metal")
    for k in range(5):
        x = -8 + k * 4
        mb.box((x, -(B0 + 6.0), roof_z - 3.1), (0.5, 0.9, 0.6), "MI_Plastic")


def build_floor(mb):
    pts = path(0.0)
    mb.poly([(x, y, -0.03) for (x, y) in pts], "MI_Apron")
    ring = path(-2.0)
    n = len(ring)
    for i in range(n):
        j = (i + 1) % n
        a, b = ring[i], ring[j]
        c, d = pts[j], pts[i]
        mb.quad((a[0], a[1], -0.02), (b[0], b[1], -0.02), (c[0], c[1], -0.02), (d[0], d[1], -0.02), "MI_Track")


def main():
    rc.reset_scene()
    mb = rc.MeshBuilder("SM_Stadium")
    seat_rows, upper_top, upper_back = build_bowl(mb)
    floodlights, roof_z = build_roof(mb, upper_top, upper_back)
    build_pitchside(mb)
    build_screens_and_gantry(mb, roof_z)
    stadium = mb.finish()
    rc.box_project_uvs(stadium, 2.0)

    floor = rc.MeshBuilder("SM_StadiumFloor")
    build_floor(floor)
    floor_obj = floor.finish()
    rc.box_project_uvs(floor_obj, 2.0)

    objs = [stadium, floor_obj]
    print("stadium triangles:", rc.triangle_count(objs))
    rc.export_fbx([stadium], "SM_Stadium.fbx")
    rc.export_fbx([floor_obj], "SM_StadiumFloor.fbx")
    rc.write_json(
        "venue_stadium.json",
        {
            "comment": "Generated by Tools/Blender/build_stadium.py. Metres, Blender axes (X = pitch length).",
            "path": {"a": A0, "b": B0, "r": R0},
            "seatSpacing": 0.52,
            "aisleEvery": 16,
            "aisleGap": 1.2,
            "rows": [{"tier": r["tier"], "offset": round(r["offset"], 3), "z": round(r["z"], 3)} for r in seat_rows],
            "floodlights": floodlights,
            "roofHeight": round(roof_z, 2),
            "cameras": {
                "main": [0, -(B0 + 6.5), round(roof_z - 4.0, 2)],
                "high_left": [-(A0 - 8), -(B0 + 18), 30.0],
                "high_right": [A0 - 8, -(B0 + 18), 30.0],
                "behind_goal_home": [A0 + 14, 0, 14.0],
                "behind_goal_away": [-(A0 + 14), 0, 14.0],
                "reverse": [0, B0 + 10, 12.0],
            },
        },
    )
    return objs


if __name__ == "__main__":
    main()
