#!/usr/bin/env python3
"""Generate a night F1 circuit scene and export assets for the OpenGL lab loader.

Run: blender --background --python generate_f1_scene.py
"""
import json
import math
import os
import random
import re
import struct
import zlib
from bisect import bisect_right
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "models" / "f1_circuit"
TEX_SIZE = 512
TRACK_HALF = 7.0
CURB_WIDTH = 1.5
BARRIER_OFFSET = 10.0
WALL_HEIGHT = 1.4

# key: (diffuse png, specular png, Kd, Ks, Ns)
MATERIALS = {
    "track": ("asphalt.png", "asphalt_specular.png", (0.34, 0.34, 0.36), (0.85, 0.85, 0.85), 96),
    "curbs": ("curb.png", "curb.png", (0.90, 0.90, 0.90), (0.08, 0.08, 0.08), 32),
    "barriers": ("concrete.png", "concrete.png", (0.72, 0.72, 0.70), (0.12, 0.12, 0.12), 24),
    "pit_building": ("concrete.png", "concrete.png", (0.66, 0.66, 0.64), (0.10, 0.10, 0.10), 24),
    "grandstand": ("concrete.png", "concrete.png", (0.78, 0.78, 0.76), (0.10, 0.10, 0.10), 24),
    "start_bridge": ("concrete.png", "concrete.png", (0.55, 0.57, 0.60), (0.35, 0.35, 0.35), 64),
    "light_poles": ("concrete.png", "concrete.png", (0.50, 0.52, 0.55), (0.40, 0.40, 0.40), 64),
    "ground": ("grass.png", "grass.png", (0.55, 0.55, 0.55), (0.02, 0.02, 0.02), 8),
    "car_body": ("car_body.png", "car_body.png", (0.85, 0.85, 0.85), (0.90, 0.90, 0.90), 128),
    "wheels": ("asphalt.png", "asphalt_specular.png", (0.20, 0.20, 0.20), (0.05, 0.05, 0.05), 16),
}

EXPORTS = [
    "track", "curbs", "barriers", "pit_building", "grandstand", "start_bridge",
    "light_poles", "ground", "car_body", "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr",
]

WHEEL_POSITIONS = {
    "wheel_fl": (1.5, 0.95, 0.36),
    "wheel_fr": (1.5, -0.95, 0.36),
    "wheel_rl": (-1.5, 0.95, 0.36),
    "wheel_rr": (-1.5, -0.95, 0.36),
}


def _png(path, pixel):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(TEX_SIZE):
        raw.append(0)
        for x in range(TEX_SIZE):
            raw.extend(pixel(x, y))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", TEX_SIZE, TEX_SIZE, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def _hash01(x, y, seed):
    n = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def _grid(seed, cells):
    rng = random.Random(seed)
    return [[rng.random() for _ in range(cells)] for _ in range(cells)]


def _smooth(grid, x, y):
    cells = len(grid)
    gx, gy = x * cells / TEX_SIZE, y * cells / TEX_SIZE
    x0, y0 = int(gx), int(gy)
    fx, fy = gx - x0, gy - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
    x0, y0 = x0 % cells, y0 % cells
    a = grid[y0][x0] * (1 - fx) + grid[y0][x1] * fx
    b = grid[y1][x0] * (1 - fx) + grid[y1][x1] * fx
    return a * (1 - fy) + b * fy


def _clamp(v):
    return max(0, min(255, int(v)))


def gen_textures():
    OUT.mkdir(parents=True, exist_ok=True)
    g8, g16, g32 = _grid(11, 8), _grid(12, 16), _grid(13, 32)
    g4b, g16b, g64b = _grid(21, 4), _grid(22, 16), _grid(23, 64)

    def asphalt(x, y):
        v = 40 + 12 * _smooth(g8, x, y) + 10 * _smooth(g16, x, y) + 16 * _hash01(x, y, 3)
        c = _clamp(v)
        return (c, c, _clamp(c + 3), 255)

    def asphalt_spec(x, y):
        p = 0.55 * _smooth(g4b, x, y) + 0.30 * _smooth(g16b, x, y) + 0.15 * _smooth(g64b, x, y)
        v = _clamp(35 + 220 * max(0.0, p - 0.22) / 0.78 + 8 * _hash01(x, y, 31))
        return (v, v, v, 255)

    def curb(x, y):
        stripe = (x // 64) % 2
        n = _hash01(x, y, 41) * 18 - 9
        if stripe:
            rgb = (200 + n, 22 + n * 0.3, 20 + n * 0.3)
        else:
            rgb = (235 + n * 0.5, 235 + n * 0.5, 232 + n * 0.5)
        return tuple(_clamp(c) for c in rgb) + (255,)

    def concrete(x, y):
        v = 112 + 20 * _smooth(g32, x, y) + 12 * _hash01(x, y, 51)
        if y % 128 < 2 or x % 256 < 1:
            v -= 28
        c = _clamp(v)
        return (c, c, _clamp(c - 2), 255)

    def grass(x, y):
        v = 70 + 26 * _smooth(g8, x, y) + 20 * _smooth(g32, x, y) + 18 * _hash01(x, y, 61)
        return (_clamp(30 + v * 0.35), _clamp(45 + v * 0.75), _clamp(24 + v * 0.30), 255)

    def car_body(x, y):
        stripe = 30 <= y % 256 < 62
        n = _hash01(x, y, 71) * 16 - 8
        if stripe:
            rgb = (232 + n, 232 + n, 236 + n)
        else:
            rgb = (150 + n, 18 + n * 0.3, 24 + n * 0.4)
        return tuple(_clamp(c) for c in rgb) + (255,)

    _png(OUT / "asphalt.png", asphalt)
    _png(OUT / "asphalt_specular.png", asphalt_spec)
    _png(OUT / "curb.png", curb)
    _png(OUT / "concrete.png", concrete)
    _png(OUT / "grass.png", grass)
    _png(OUT / "car_body.png", car_body)

def to_gl(p):
    x, y, z = p
    return [round(x, 4), round(z, 4), round(-y, 4)]


def bbox_gl(verts):
    xs, ys, zs = zip(*verts)
    return {
        "min": [round(min(xs), 4), round(min(zs), 4), round(-max(ys), 4)],
        "max": [round(max(xs), 4), round(max(zs), 4), round(-min(ys), 4)],
    }


def new_object(name, verts, faces, material_key):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    mat = bpy.data.materials.get("mat_" + material_key)
    if mat is None:
        mat = bpy.data.materials.new("mat_" + material_key)
        mat.diffuse_color = (*MATERIALS[material_key][2], 1.0)
    me.materials.append(mat)
    return ob


def box_uv(ob, scale):
    # shortcut: box-projected UVs for props; swap for per-face unwrap if stretching matters.
    me = ob.data
    layer = me.uv_layers.new(name="UVMap")
    for poly in me.polygons:
        n = poly.normal
        ax, ay, az = abs(n.x), abs(n.y), abs(n.z)
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            if ax >= ay and ax >= az:
                u, v = co.y, co.z
            elif ay >= az:
                u, v = co.x, co.z
            else:
                u, v = co.x, co.y
            layer.data[li].uv = (u / scale, v / scale)


def explicit_uv(ob, uvs):
    me = ob.data
    layer = me.uv_layers.new(name="UVMap")
    for li in range(len(me.loops)):
        layer.data[li].uv = uvs[me.loops[li].vertex_index]


def recalc_normals(ob):
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()


CUBE_FACES = [
    [0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4],
    [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7],
]


def add_box(verts, faces, center, size, rot_z=0.0):
    cx, cy, cz = center
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    c, s = math.cos(rot_z), math.sin(rot_z)
    base = len(verts)
    for lx, ly, lz in [
        (-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
        (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz),
    ]:
        verts.append((cx + lx * c - ly * s, cy + lx * s + ly * c, cz + lz))
    faces.extend([[base + i for i in f] for f in CUBE_FACES])


def add_cylinder(verts, faces, center, radius, length, axis="Z", segments=16):
    base = len(verts)
    for k in range(segments):
        a = 2 * math.pi * k / segments
        u, v = math.cos(a) * radius, math.sin(a) * radius
        verts.append((u, v, -length / 2))
        verts.append((u, v, length / 2))
    for k in range(segments):
        k2 = (k + 1) % segments
        faces.append([base + 2 * k, base + 2 * k2, base + 2 * k2 + 1, base + 2 * k + 1])
    faces.append([base + 2 * k + 1 for k in range(segments)])
    faces.append([base + 2 * k for k in reversed(range(segments))])
    if axis == "Y":
        for i in range(base, len(verts)):
            x, y, z = verts[i]
            verts[i] = (x, z, -y)
    cx, cy, cz = center
    for i in range(base, len(verts)):
        x, y, z = verts[i]
        verts[i] = (x + cx, y + cy, z + cz)


def _normal(verts, face):
    nx = ny = nz = 0.0
    for i in range(len(face)):
        x1, y1, z1 = verts[face[i]]
        x2, y2, z2 = verts[face[(i + 1) % len(face)]]
        nx += (y1 - y2) * (z1 + z2)
        ny += (z1 - z2) * (x1 + x2)
        nz += (x1 - x2) * (y1 + y2)
    return nx, ny, nz


def _oriented(verts, face, target):
    n = _normal(verts, face)
    return face if n[0] * target[0] + n[1] * target[1] + n[2] * target[2] >= 0 else list(reversed(face))


WAYPOINTS = [
    (-160, -40), (-100, -40), (-40, -40), (20, -40), (90, -40), (150, -40),
    (190, -15), (200, 25), (185, 65), (150, 90), (105, 105), (60, 100),
    (20, 80), (-15, 55), (-45, 40), (-80, 50), (-115, 75), (-150, 85),
    (-180, 55), (-190, 15), (-180, -20),
]


def _catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return tuple(
        0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t
               + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
               + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3)
        for k in range(2)
    )


def build_nodes(step=4.0, per_segment=16):
    raw = []
    n = len(WAYPOINTS)
    for i in range(n):
        p0, p1 = WAYPOINTS[(i - 1) % n], WAYPOINTS[i]
        p2, p3 = WAYPOINTS[(i + 1) % n], WAYPOINTS[(i + 2) % n]
        for j in range(per_segment):
            raw.append(_catmull(p0, p1, p2, p3, j / per_segment))
    cum = [0.0]
    for i in range(len(raw)):
        x1, y1 = raw[i]
        x2, y2 = raw[(i + 1) % len(raw)]
        cum.append(cum[-1] + math.hypot(x2 - x1, y2 - y1))
    total = cum[-1]
    count = max(80, int(total / step))
    pts = []
    for k in range(count):
        d = k * total / count
        i = min(bisect_right(cum, d) - 1, len(raw) - 1)
        span = cum[i + 1] - cum[i]
        f = (d - cum[i]) / span if span else 0.0
        a, b = raw[i], raw[(i + 1) % len(raw)]
        pts.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f))
    start = min(range(count), key=lambda i: math.hypot(pts[i][0], pts[i][1] + 40))
    pts = pts[start:] + pts[:start]
    nodes = []
    for k, p in enumerate(pts):
        prev, nxt = pts[k - 1], pts[(k + 1) % count]
        tx, ty = nxt[0] - prev[0], nxt[1] - prev[1]
        length = math.hypot(tx, ty)
        tx, ty = tx / length, ty / length
        nodes.append((p[0], p[1], tx, ty, -ty, tx, k * total / count))
    return nodes, total


def build_ribbon(nodes, inner_off, outer_off, z, u_scale, material_key, name, face_up=True):
    verts, uvs, faces = [], [], []
    n = len(nodes)
    for side in (1, -1):
        base = len(verts)
        for x, y, tx, ty, lx, ly, dist in nodes:
            a = (x + lx * side * inner_off, y + ly * side * inner_off, z)
            b = (x + lx * side * outer_off, y + ly * side * outer_off, z)
            verts.extend((a, b))
            uvs.extend(((dist / u_scale, 0.0), (dist / u_scale, 1.0)))
        for i in range(n):
            j = (i + 1) % n
            i0, i1 = base + 2 * i, base + 2 * i + 1
            j0, j1 = base + 2 * j, base + 2 * j + 1
            faces.append(_oriented(verts, [i0, j0, j1, i1], (0, 0, 1) if face_up else (0, 0, -1)))
    ob = new_object(name, verts, faces, material_key)
    explicit_uv(ob, uvs)
    return ob


def build_barriers(nodes):
    verts, faces, boxes = [], [], []
    n = len(nodes)
    for side in (1, -1):
        base = len(verts)
        for x, y, tx, ty, lx, ly, dist in nodes:
            for off in (side * (BARRIER_OFFSET - 0.2), side * (BARRIER_OFFSET + 0.2)):
                verts.append((x + lx * off, y + ly * off, 0.0))
                verts.append((x + lx * off, y + ly * off, WALL_HEIGHT))
        for i in range(n):
            j = (i + 1) % n
            i0, j0 = base + 4 * i, base + 4 * j
            ib_i, it_i, ob_i, ot_i = i0, i0 + 1, i0 + 2, i0 + 3
            ib_j, it_j, ob_j, ot_j = j0, j0 + 1, j0 + 2, j0 + 3
            lx, ly = nodes[i][4], nodes[i][5]
            faces.append(_oriented(verts, [ib_i, ib_j, it_j, it_i], (-side * lx, -side * ly, 0)))
            faces.append(_oriented(verts, [ob_i, ob_j, ot_j, ot_i], (side * lx, side * ly, 0)))
            faces.append(_oriented(verts, [it_i, it_j, ot_j, ot_i], (0, 0, 1)))
            seg = [verts[i0 + k] for k in range(4)] + [verts[j0 + k] for k in range(4)]
            boxes.append(bbox_gl(seg))
    ob = new_object("barriers", verts, faces, "barriers")
    box_uv(ob, 4.0)
    return ob, boxes


def build_ground():
    verts = [(-1000, -1000, 0), (1000, -1000, 0), (1000, 1000, 0), (-1000, 1000, 0)]
    faces = [[0, 1, 2, 3]]
    ob = new_object("ground", verts, faces, "ground")
    me = ob.data
    layer = me.uv_layers.new(name="UVMap")
    for li in range(len(me.loops)):
        co = me.vertices[me.loops[li].vertex_index].co
        layer.data[li].uv = (co.x / 20.0, co.y / 20.0)
    return ob

def build_pit_building():
    verts, faces = [], []
    add_box(verts, faces, (0, -68, 4.5), (140, 16, 9))
    add_box(verts, faces, (0, -68, 9.5), (146, 18, 1.0))
    for i in range(10):
        add_box(verts, faces, (-63 + i * 14, -59.7, 2.2), (11, 0.6, 4.2))
    ob = new_object("pit_building", verts, faces, "pit_building")
    box_uv(ob, 4.0)
    return ob, bbox_gl(verts)


def build_grandstand():
    verts, faces = [], []
    for i in range(6):
        h = 0.9 * (i + 1)
        add_box(verts, faces, (0, -24 + i * 3, h / 2), (160, 3, h))
    ob = new_object("grandstand", verts, faces, "grandstand")
    box_uv(ob, 4.0)
    return ob, bbox_gl(verts)


def build_start_bridge():
    verts, faces = [], []
    for y in (-52.5, -27.5):
        add_box(verts, faces, (0, y, 4.5), (1.4, 1.4, 9))
    add_box(verts, faces, (0, -40, 9.4), (2.4, 30, 1.2))
    for i in range(5):
        add_box(verts, faces, (0, -48 + i * 4, 8.35), (2.6, 1.8, 0.7))
    ob = new_object("start_bridge", verts, faces, "start_bridge")
    box_uv(ob, 4.0)
    return ob, bbox_gl(verts)


def build_light_poles(nodes):
    verts, faces, heads = [], [], []
    fractions = (0.38, 0.47, 0.56, 0.65, 0.74, 0.84)
    for frac in fractions:
        x, y, tx, ty, lx, ly, _ = nodes[int(frac * len(nodes)) % len(nodes)]
        bx, by = x + lx * 20, y + ly * 20
        add_cylinder(verts, faces, (bx, by, 9), 0.28, 18, axis="Z", segments=12)
        hx, hy = bx - lx * 0.7, by - ly * 0.7
        add_box(verts, faces, (hx, hy, 18.3), (2.0, 1.0, 0.6), rot_z=math.atan2(ty, tx))
        heads.append(to_gl((hx, hy, 18.3)))
    ob = new_object("light_poles", verts, faces, "light_poles")
    recalc_normals(ob)
    box_uv(ob, 4.0)
    return ob, heads


def build_car_body():
    verts, faces = [], []
    add_box(verts, faces, (2.75, 0, 0.18), (0.5, 1.95, 0.06))
    add_box(verts, faces, (2.25, 0, 0.34), (0.9, 0.7, 0.16))
    add_box(verts, faces, (0, 0, 0.35), (4.4, 1.1, 0.24))
    add_box(verts, faces, (0.2, 0.62, 0.43), (1.5, 0.36, 0.28))
    add_box(verts, faces, (0.2, -0.62, 0.43), (1.5, 0.36, 0.28))
    add_box(verts, faces, (-0.25, 0, 0.62), (1.2, 0.7, 0.34))
    add_box(verts, faces, (-1.3, 0, 0.57), (1.5, 0.65, 0.3))
    add_box(verts, faces, (-2.2, 0, 1.02), (0.5, 1.7, 0.07))
    add_box(verts, faces, (-2.2, 0.7, 0.78), (0.16, 0.16, 0.46))
    add_box(verts, faces, (-2.2, -0.7, 0.78), (0.16, 0.16, 0.46))
    ob = new_object("car_body", verts, faces, "car_body")
    box_uv(ob, 2.0)
    return ob


def build_wheel(name):
    verts, faces = [], []
    add_cylinder(verts, faces, (0, 0, 0), 0.36, 0.3, axis="Y", segments=16)
    ob = new_object(name, verts, faces, "wheels")
    recalc_normals(ob)
    box_uv(ob, 1.0)
    return ob

def build_track(nodes):
    verts, uvs, faces = [], [], []
    for x, y, tx, ty, lx, ly, dist in nodes:
        verts.append((x + lx * TRACK_HALF, y + ly * TRACK_HALF, 0.02))
        verts.append((x - lx * TRACK_HALF, y - ly * TRACK_HALF, 0.02))
        uvs.extend(((dist / 6.0, 0.0), (dist / 6.0, 1.0)))
    n = len(nodes)
    for i in range(n):
        j = (i + 1) % n
        faces.append([2 * i, 2 * i + 1, 2 * j + 1, 2 * j])
    ob = new_object("track", verts, faces, "track")
    explicit_uv(ob, uvs)
    return ob


def merge_boxes(boxes):
    return {
        "min": [min(b["min"][i] for b in boxes) for i in range(3)],
        "max": [max(b["max"][i] for b in boxes) for i in range(3)],
    }


def export_obj(ob, path):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    if bpy.app.version >= (4, 0, 0):
        bpy.ops.wm.obj_export(
            filepath=str(path), export_selected_objects=True, export_uv=True,
            export_normals=True, export_materials=True, export_triangulated_mesh=True,
            apply_modifiers=True, forward_axis="NEGATIVE_Z", up_axis="Y",
        )
    else:
        bpy.ops.export_scene.obj(
            filepath=str(path), use_selection=True, use_uvs=True, use_normals=True,
            use_materials=True, use_triangles=True, axis_forward="-Z", axis_up="Y",
        )


def finalize_mtl(obj_path, material_key):
    text = obj_path.read_text(encoding="utf-8", errors="ignore")
    mtllib = re.search(r"^mtllib\s+(.+?)\s*$", text, re.M)
    usemtl = re.search(r"^usemtl\s+(.+?)\s*$", text, re.M)
    mat_name = usemtl.group(1).strip() if usemtl else material_key
    mtl_name = os.path.basename((mtllib.group(1).strip() if mtllib else material_key + ".mtl").replace("\\", "/"))
    prefix = ""
    if mtllib and mtllib.group(1).strip() != mtl_name:
        text = re.sub(r"^mtllib\s+.+?$", "mtllib " + mtl_name, text, count=1, flags=re.M)
        obj_path.write_text(text, encoding="utf-8")
    if not mtllib:
        prefix += "mtllib " + mtl_name + "\n"
    if not usemtl:
        prefix += "usemtl " + mat_name + "\n"
    if prefix:
        obj_path.write_text(prefix + text, encoding="utf-8")
    tex, spec, kd, ks, ns = MATERIALS[material_key]
    (OUT / mtl_name).write_text(
        "# generated by generate_f1_scene.py\n"
        f"newmtl {mat_name}\n"
        f"Ns {ns}.000000\n"
        "Ka 1.000000 1.000000 1.000000\n"
        f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}\n"
        f"Ks {ks[0]:.6f} {ks[1]:.6f} {ks[2]:.6f}\n"
        "Ke 0.000000 0.000000 0.000000\n"
        "Ni 1.000000\n"
        "d 1.000000\n"
        "illum 2\n"
        f"map_Kd {tex}\n"
        f"map_Ks {spec}\n",
        encoding="utf-8",
    )


def strip_mtl_paths():
    for mtl in OUT.glob("*.mtl"):
        lines = []
        for line in mtl.read_text(encoding="utf-8", errors="ignore").splitlines():
            s = line.strip()
            if s.startswith(("map_Kd", "map_Ks")):
                parts = s.split()
                if len(parts) >= 2:
                    parts[-1] = os.path.basename(parts[-1].replace("\\", "/"))
                    line = " ".join(parts)
            lines.append(line)
        mtl.write_text("\n".join(lines) + "\n", encoding="utf-8")

def setup_night_world():
    world = bpy.data.worlds.new("night")
    bpy.context.scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs[0].default_value = (0.004, 0.006, 0.02, 1.0)
        bg.inputs[1].default_value = 1.0


def add_lights(heads):
    scene = bpy.context.scene
    for i, hp in enumerate(heads):
        data = bpy.data.lights.new(f"flood_{i}", type="POINT")
        data.energy = 1800
        data.color = (1.0, 0.92, 0.78)
        data.shadow_soft_size = 0.6
        ob = bpy.data.objects.new(f"flood_{i}", data)
        ob.location = (hp[0], -hp[2], hp[1])
        scene.collection.objects.link(ob)
    moon = bpy.data.lights.new("moon", type="SUN")
    moon.energy = 0.15
    moon.color = (0.5, 0.62, 1.0)
    ob = bpy.data.objects.new("moon", moon)
    ob.rotation_euler = (0.7, 0.0, 0.4)
    scene.collection.objects.link(ob)


def verify():
    textures = ["asphalt.png", "asphalt_specular.png", "curb.png", "concrete.png", "grass.png", "car_body.png"]
    for name in textures:
        path = OUT / name
        assert path.exists(), name
        data = path.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", name
        w, h = struct.unpack(">II", data[16:24])
        assert w == h == TEX_SIZE and (w & (w - 1)) == 0, (name, w, h)
    for name in EXPORTS:
        obj_path = OUT / f"{name}.obj"
        assert obj_path.exists(), name
        text = obj_path.read_text(encoding="utf-8", errors="ignore")
        usemtl = set(re.findall(r"^usemtl\s+(.+?)\s*$", text, re.M))
        assert len(usemtl) == 1, (name, usemtl)
        mtllib = re.findall(r"^mtllib\s+(.+?)\s*$", text, re.M)
        assert len(mtllib) == 1, name
        assert "/" not in mtllib[0] and "\\" not in mtllib[0], (name, mtllib)
        assert re.search(r"^vt ", text, re.M), name
        assert re.search(r"^vn ", text, re.M), name
        mtl_path = OUT / mtllib[0]
        assert mtl_path.exists(), mtl_path
        mtl = mtl_path.read_text(encoding="utf-8")
        assert len(re.findall(r"^newmtl\s+", mtl, re.M)) == 1, mtl_path
        maps = 0
        for line in mtl.splitlines():
            if line.startswith(("map_Kd", "map_Ks")):
                maps += 1
                tex = line.split(maxsplit=1)[1].strip()
                assert tex == os.path.basename(tex), (mtl_path, tex)
                assert (OUT / tex).exists(), (mtl_path, tex)
        assert maps == 2, mtl_path
    for name in WHEEL_POSITIONS:
        text = (OUT / f"{name}.obj").read_text(encoding="utf-8", errors="ignore")
        vs = [tuple(map(float, line.split()[1:4])) for line in text.splitlines() if line.startswith("v ")]
        for axis in range(3):
            center = (min(v[axis] for v in vs) + max(v[axis] for v in vs)) / 2
            assert abs(center) < 1e-3, (name, axis, center)
    print(f"verify OK: {len(EXPORTS)} obj, {len(list(OUT.glob('*.mtl')))} mtl, {len(textures)} png")


def main():
    print("Blender", bpy.app.version_string)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    OUT.mkdir(parents=True, exist_ok=True)
    gen_textures()
    setup_night_world()

    nodes, total = build_nodes()
    objs = {}
    objs["track"] = build_track(nodes)
    objs["curbs"] = build_ribbon(nodes, TRACK_HALF, TRACK_HALF + CURB_WIDTH, 0.03, 8.0, "curbs", "curbs")
    objs["barriers"], barrier_boxes = build_barriers(nodes)
    objs["pit_building"], pit_box = build_pit_building()
    objs["grandstand"], grandstand_box = build_grandstand()
    objs["start_bridge"], bridge_box = build_start_bridge()
    objs["light_poles"], heads = build_light_poles(nodes)
    objs["ground"] = build_ground()
    objs["car_body"] = build_car_body()
    for name in WHEEL_POSITIONS:
        objs[name] = build_wheel(name)

    for name in EXPORTS:
        export_obj(objs[name], OUT / f"{name}.obj")
        finalize_mtl(OUT / f"{name}.obj", "wheels" if name.startswith("wheel_") else name)
    strip_mtl_paths()

    objs["car_body"].location = (0, -40, 0)
    for name, pos in WHEEL_POSITIONS.items():
        objs[name].location = (pos[0], -40 + pos[1], pos[2])
    add_lights(heads)

    step = max(1, len(nodes) // 16)
    waypoints = [to_gl((nodes[i][0], nodes[i][1], 0.0)) for i in range(0, len(nodes), step)][:16]
    coords = {
        "coordinate_system": "OpenGL: X'=X, Y'=Z, Z'=-Y (same axes as the exported OBJ files)",
        "track": {
            "width": TRACK_HALF * 2,
            "length": round(total, 3),
            "start_line_gl": to_gl((0, -40, 0)),
        },
        "car_start_gl": to_gl((0, -40, 0)),
        "car_forward_gl": [1.0, 0.0, 0.0],
        "light_heads_gl": heads,
        "wheel_centers_gl": {name: to_gl(pos) for name, pos in WHEEL_POSITIONS.items()},
        "wheel_axle_gl": [0.0, 0.0, 1.0],
        "waypoints_gl": waypoints,
        "aabb": {
            "barriers": merge_boxes(barrier_boxes),
            "barrier_segments": barrier_boxes,
            "pit_building": pit_box,
            "grandstand": grandstand_box,
            "start_bridge": bridge_box,
        },
    }
    (OUT / "scene_coords.json").write_text(json.dumps(coords, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(coords, indent=2))
    verify()


if __name__ == "__main__":
    main()

