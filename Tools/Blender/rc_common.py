"""Shared helpers for the Referee Career Blender scripts.

Works both inside Blender (blender --background --python build_all.py) and with the `bpy` module from PyPI.
Units: 1 Blender unit = 1 metre. The pitch length runs along +X, the pitch centre is the origin, the grass is z = 0.
Exported FBX files land in RefereeCareer/SourceArt/Meshes and are imported by Tools/Unreal/setup_project.py.
"""
import math
import os

import bpy  # must come first when running with the PyPI `bpy` module
import bmesh
from mathutils import Matrix, Vector

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MESH_DIR = os.path.join(REPO, "RefereeCareer", "SourceArt", "Meshes")
DATA_DIR = os.path.join(REPO, "RefereeCareer", "Content", "Data")
PREVIEW_DIR = os.path.join(REPO, "docs", "images")

# Material slot names are the contract with Unreal: setup_project.py creates a material instance per name.
PALETTE = {
    "MI_Concrete": ((0.52, 0.52, 0.50), 0.85, 0.0, None),
    "MI_ConcreteDark": ((0.22, 0.22, 0.23), 0.9, 0.0, None),
    "MI_StepEdge": ((0.85, 0.85, 0.82), 0.6, 0.0, None),
    "MI_SeatHome": ((0.62, 0.05, 0.07), 0.45, 0.0, None),
    "MI_SeatAway": ((0.05, 0.16, 0.45), 0.45, 0.0, None),
    "MI_SeatNeutral": ((0.75, 0.75, 0.78), 0.45, 0.0, None),
    "MI_Metal": ((0.62, 0.63, 0.66), 0.35, 1.0, None),
    "MI_MetalDark": ((0.08, 0.08, 0.09), 0.4, 1.0, None),
    "MI_RoofMembrane": ((0.92, 0.92, 0.9), 0.5, 0.0, None),
    "MI_Glass": ((0.55, 0.68, 0.75), 0.05, 0.0, None),
    "MI_LEDBoard": ((0.02, 0.02, 0.02), 0.3, 0.0, ((0.1, 0.45, 1.0), 6.0)),
    "MI_Screen": ((0.02, 0.02, 0.02), 0.2, 0.0, ((0.9, 0.9, 1.0), 4.0)),
    "MI_Floodlight": ((0.9, 0.9, 0.9), 0.2, 0.0, ((1.0, 0.97, 0.9), 40.0)),
    "MI_Track": ((0.36, 0.15, 0.1), 0.9, 0.0, None),
    "MI_Apron": ((0.14, 0.32, 0.12), 0.8, 0.0, None),
    "MI_Facade": ((0.80, 0.80, 0.78), 0.55, 0.3, None),
    "MI_GoalPost": ((0.97, 0.97, 0.97), 0.25, 0.0, None),
    "MI_Net": ((0.95, 0.95, 0.95), 0.6, 0.0, None),
    "MI_Flag": ((0.98, 0.84, 0.05), 0.6, 0.0, None),
    "MI_FlagRed": ((0.85, 0.08, 0.08), 0.6, 0.0, None),
    "MI_Plastic": ((0.08, 0.08, 0.08), 0.35, 0.0, None),
    "MI_CardYellow": ((1.0, 0.82, 0.0), 0.35, 0.0, None),
    "MI_CardRed": ((0.85, 0.02, 0.05), 0.35, 0.0, None),
    "MI_Wood": ((0.45, 0.30, 0.18), 0.7, 0.0, None),
    "MI_Dirt": ((0.42, 0.33, 0.22), 0.95, 0.0, None),
    "MI_Plaster": ((0.86, 0.80, 0.68), 0.85, 0.0, None),
    "MI_PlasterB": ((0.72, 0.62, 0.50), 0.85, 0.0, None),
    "MI_Window": ((0.10, 0.13, 0.16), 0.15, 0.2, ((1.0, 0.75, 0.45), 0.6)),
    "MI_ChainLink": ((0.55, 0.57, 0.6), 0.4, 1.0, None),
    "MI_Palm": ((0.12, 0.35, 0.10), 0.7, 0.0, None),
    "MI_Bark": ((0.36, 0.26, 0.17), 0.9, 0.0, None),
    "MI_Container": ((0.10, 0.36, 0.55), 0.6, 0.4, None),
    "MI_Skin": ((0.55, 0.38, 0.27), 0.6, 0.0, None),
    "MI_Shirt": ((0.75, 0.1, 0.1), 0.7, 0.0, None),
    "MI_Hair": ((0.05, 0.04, 0.03), 0.7, 0.0, None),
    "MI_Ball": ((0.95, 0.95, 0.95), 0.4, 0.0, None),
    "MI_BallPanel": ((0.06, 0.06, 0.08), 0.4, 0.0, None),
}


def ensure_addons():
    try:
        import addon_utils

        addon_utils.enable("io_scene_fbx", default_set=True)
    except Exception as exc:  # inside a full Blender the add-on is usually already on
        print("FBX add-on:", exc)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ensure_addons()
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    return scene


def material(name):
    mat = bpy.data.materials.get(name)
    if mat:
        return mat
    color, rough, metal, emission = PALETTE.get(name, ((0.7, 0.7, 0.7), 0.6, 0.0, None))
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*emission[0], 1.0)
        bsdf.inputs["Emission Strength"].default_value = emission[1]
    if name in ("MI_Glass",):
        bsdf.inputs["Transmission Weight"].default_value = 0.9
    if name in ("MI_Net", "MI_ChainLink"):
        _grid_alpha(mat, bsdf, 0.14 if name == "MI_Net" else 0.22)
    mat.diffuse_color = (*color, 1.0)
    return mat


def _grid_alpha(mat, bsdf, line_width):
    """Net / chain-link: opaque only along the lines of the UV grid (one UV tile = one mesh cell)."""
    nt = mat.node_tree
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs["UV"], sep.inputs[0])
    lines = []
    for axis in ("X", "Y"):
        fr = nt.nodes.new("ShaderNodeMath")
        fr.operation = "FRACT"
        nt.links.new(sep.outputs[axis], fr.inputs[0])
        lt = nt.nodes.new("ShaderNodeMath")
        lt.operation = "LESS_THAN"
        lt.inputs[1].default_value = line_width
        nt.links.new(fr.outputs[0], lt.inputs[0])
        lines.append(lt)
    mx = nt.nodes.new("ShaderNodeMath")
    mx.operation = "MAXIMUM"
    nt.links.new(lines[0].outputs[0], mx.inputs[0])
    nt.links.new(lines[1].outputs[0], mx.inputs[1])
    nt.links.new(mx.outputs[0], bsdf.inputs["Alpha"])


class MeshBuilder:
    """Accumulates geometry with per-face material names, then becomes one object."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.mat_index = {}

    def _mi(self, mat_name):
        if mat_name not in self.mat_index:
            self.mat_index[mat_name] = len(self.mat_index)
        return self.mat_index[mat_name]

    def quad(self, a, b, c, d, mat):
        vs = [self.bm.verts.new(p) for p in (a, b, c, d)]
        f = self.bm.faces.new(vs)
        f.material_index = self._mi(mat)
        return f

    def poly(self, pts, mat):
        vs = [self.bm.verts.new(p) for p in pts]
        f = self.bm.faces.new(vs)
        f.material_index = self._mi(mat)
        return f

    def box(self, center, size, mat, rot_z=0.0, bevel=0.0):
        """Axis-aligned box (optionally rotated about Z)."""
        cx, cy, cz = center
        sx, sy, sz = (size[0] / 2, size[1] / 2, size[2] / 2)
        rot = Matrix.Rotation(rot_z, 3, "Z")
        corners = []
        for z in (-sz, sz):
            for x, y in ((-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)):
                v = rot @ Vector((x, y, z))
                corners.append(Vector((cx, cy, cz)) + v)
        vs = [self.bm.verts.new(c) for c in corners]
        idx = self._mi(mat)
        faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        made = []
        for f in faces:
            face = self.bm.faces.new([vs[i] for i in f])
            face.material_index = idx
            made.append(face)
        if bevel > 0:
            edges = list({e for face in made for e in face.edges})
            bmesh.ops.bevel(self.bm, geom=edges, offset=bevel, segments=2, affect="EDGES", profile=0.5)
        return made

    def beam(self, p0, p1, thickness, mat, height=None):
        """Box from p0 to p1 with a square (or thickness x height) section."""
        p0, p1 = Vector(p0), Vector(p1)
        d = p1 - p0
        length = d.length
        if length < 1e-6:
            return
        h = height if height is not None else thickness
        z = d.normalized()
        up = Vector((0, 0, 1)) if abs(z.z) < 0.95 else Vector((1, 0, 0))
        x = up.cross(z).normalized()
        y = z.cross(x).normalized()
        corners = []
        for t in (0.0, 1.0):
            base = p0 + d * t
            for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                corners.append(base + x * (sx * thickness / 2) + y * (sy * h / 2))
        vs = [self.bm.verts.new(c) for c in corners]
        idx = self._mi(mat)
        for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
            face = self.bm.faces.new([vs[i] for i in f])
            face.material_index = idx

    def cylinder(self, base, height, radius, mat, segments=16, axis="Z", cap=True):
        base = Vector(base)
        if axis == "Z":
            up, u, v = Vector((0, 0, 1)), Vector((1, 0, 0)), Vector((0, 1, 0))
        elif axis == "X":
            up, u, v = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
        else:
            up, u, v = Vector((0, 1, 0)), Vector((0, 0, 1)), Vector((1, 0, 0))
        bottom, top = [], []
        for i in range(segments):
            a = 2 * math.pi * i / segments
            off = u * (math.cos(a) * radius) + v * (math.sin(a) * radius)
            bottom.append(self.bm.verts.new(base + off))
            top.append(self.bm.verts.new(base + up * height + off))
        idx = self._mi(mat)
        for i in range(segments):
            j = (i + 1) % segments
            f = self.bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
            f.material_index = idx
        if cap:
            f = self.bm.faces.new(list(reversed(bottom)))
            f.material_index = idx
            f = self.bm.faces.new(top)
            f.material_index = idx

    def tube(self, points, radius, mat, segments=8):
        for a, b in zip(points[:-1], points[1:]):
            d = Vector(b) - Vector(a)
            if d.length < 1e-6:
                continue
            self.cylinder_between(a, b, radius, mat, segments)

    def cylinder_between(self, a, b, radius, mat, segments=8):
        a, b = Vector(a), Vector(b)
        z = (b - a).normalized()
        up = Vector((0, 0, 1)) if abs(z.z) < 0.95 else Vector((1, 0, 0))
        x = up.cross(z).normalized()
        y = z.cross(x).normalized()
        ring_a, ring_b = [], []
        for i in range(segments):
            ang = 2 * math.pi * i / segments
            off = x * (math.cos(ang) * radius) + y * (math.sin(ang) * radius)
            ring_a.append(self.bm.verts.new(a + off))
            ring_b.append(self.bm.verts.new(b + off))
        idx = self._mi(mat)
        for i in range(segments):
            j = (i + 1) % segments
            f = self.bm.faces.new((ring_a[i], ring_a[j], ring_b[j], ring_b[i]))
            f.material_index = idx

    def finish(self, collection=None, smooth=False):
        bmesh.ops.remove_doubles(self.bm, verts=self.bm.verts, dist=0.0005)
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        mesh = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(mesh)
        self.bm.free()
        for name, _ in sorted(self.mat_index.items(), key=lambda kv: kv[1]):
            mesh.materials.append(material(name))
        if smooth:
            for p in mesh.polygons:
                p.use_smooth = True
        obj = bpy.data.objects.new(self.name, mesh)
        (collection or bpy.context.scene.collection).objects.link(obj)
        return obj


def rounded_rect_path(a, b, r, corner_segments=12, step=None):
    """Closed path (list of (x, y, tangent_angle)) around a rounded rectangle with half-extents a, b and corner radius r."""
    r = max(0.01, min(r, a, b))
    pts = []
    corners = [((a - r, b - r), 0.0), ((-(a - r), b - r), math.pi / 2), ((-(a - r), -(b - r)), math.pi), ((a - r, -(b - r)), 1.5 * math.pi)]
    for (cx, cy), start in corners:
        for i in range(corner_segments + 1):
            ang = start + (math.pi / 2) * i / corner_segments
            pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    if step:
        dense = []
        for i, p in enumerate(pts):
            q = pts[(i + 1) % len(pts)]
            d = math.dist(p, q)
            n = max(1, int(d / step))
            for k in range(n):
                t = k / n
                dense.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t))
        pts = dense
    return pts


def export_fbx(objects, filename):
    os.makedirs(MESH_DIR, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    path = os.path.join(MESH_DIR, filename)
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        apply_scale_options="FBX_SCALE_UNITS",
        object_types={"MESH"},
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="STRIP",
    )
    print("exported", path, os.path.getsize(path) // 1024, "KB")
    return path


def triangle_count(objects):
    total = 0
    for o in objects:
        if o.type == "MESH":
            total += sum(len(p.vertices) - 2 for p in o.data.polygons)
    return total


def box_project_uvs(obj, metres_per_uv=1.0):
    """World-aligned box projection so tiling textures in Unreal keep a constant real-world size."""
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    uv = bm.loops.layers.uv.verify()
    for face in bm.faces:
        n = face.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for loop in face.loops:
            co = obj.matrix_world @ loop.vert.co
            if ax == 0:
                u, v = co.y, co.z
            elif ax == 1:
                u, v = co.x, co.z
            else:
                u, v = co.x, co.y
            loop[uv].uv = (u / metres_per_uv, v / metres_per_uv)
    bm.to_mesh(mesh)
    bm.free()


def write_json(name, data):
    import json

    os.makedirs(DATA_DIR, exist_ok=True)
    path = os.path.join(DATA_DIR, name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print("wrote", path)
