#!/usr/bin/env python3
"""Generate extra F1 circuit assets: crowd, billboards, trees, containers, tower.
Exports OBJ + MTL with single material per file, power-of-2 texture refs, clean paths."""
import json
import math
import os
import random
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "models" / "f1_circuit"
TEX_SIZE = 512

# Materials: (diffuse_png, specular_png, Kd, Ks, Ns)
MATERIALS = {
    "crowd": ("crowd.png", "crowd_specular.png", (0.55, 0.45, 0.40), (0.05, 0.05, 0.05), 8),
    "billboards": ("billboards.png", "billboards_specular.png", (0.85, 0.85, 0.85), (0.30, 0.30, 0.30), 32),
    "trees": ("trees.png", "trees_specular.png", (0.25, 0.35, 0.20), (0.02, 0.02, 0.02), 4),
    "containers": ("containers.png", "containers_specular.png", (0.50, 0.48, 0.45), (0.12, 0.12, 0.12), 16),
    "tower": ("tower.png", "tower_specular.png", (0.40, 0.42, 0.45), (0.20, 0.20, 0.20), 24),
}

EXPORTS = ["crowd", "billboards", "trees", "containers", "tower"]

# Load existing scene_coords for track bounds
with open(OUT / "scene_coords.json") as f:
    SCENE_COORDS = json.load(f)

TRACK_HALF = 7.0
BARRIER_OFFSET = 10.0

WAYPOINTS = SCENE_COORDS["waypoints_gl"]
BARRIER_AABB = SCENE_COORDS["aabb"]["barriers"]
GRANDSTAND_AABB = SCENE_COORDS["aabb"]["grandstand"]
PIT_AABB = SCENE_COORDS["aabb"]["pit_building"]


def _png(path, pixel_fn):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(TEX_SIZE):
        raw.append(0)
        for x in range(TEX_SIZE):
            raw.extend(pixel_fn(x, y))
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
    g8, g16, g32 = _grid(31, 8), _grid(32, 16), _grid(33, 32)
    g4b, g16b, g64b = _grid(41, 4), _grid(42, 16), _grid(43, 64)

    def crowd_diffuse(x, y):
        # Low-poly crowd: varied skin/clothing tones in vertical strips
        strip = (y // 32) % 6
        n = _hash01(x, y, 71) * 18 - 9
        if strip == 0:
            rgb = (180 + n, 150 + n * 0.5, 130 + n * 0.4)
        elif strip == 1:
            rgb = (200 + n, 170 + n * 0.4, 145 + n * 0.3)
        elif strip == 2:
            rgb = (160 + n, 120 + n * 0.6, 100 + n * 0.5)
        elif strip == 3:
            rgb = (210 + n, 185 + n * 0.3, 160 + n * 0.3)
        elif strip == 4:
            rgb = (140 + n, 110 + n * 0.7, 90 + n * 0.6)
        else:
            rgb = (190 + n, 160 + n * 0.4, 140 + n * 0.4)
        return tuple(_clamp(c) for c in rgb) + (255,)

    def crowd_spec(x, y):
        v = _clamp(15 + 40 * _hash01(x, y, 73))
        return (v, v, v, 255)

    def billboards_diffuse(x, y):
        # Advertising panels: bright colors, logos simulated by blocks
        panel_x = x // 128
        panel_y = y // 128
        n = _hash01(x, y, 81) * 25
        base_hue = (panel_x * 7 + panel_y * 11) % 6
        if base_hue == 0:
            rgb = (220 + n, 50 + n * 0.5, 50 + n * 0.5)
        elif base_hue == 1:
            rgb = (50 + n * 0.5, 200 + n, 60 + n * 0.5)
        elif base_hue == 2:
            rgb = (60 + n * 0.5, 80 + n * 0.5, 230 + n)
        elif base_hue == 3:
            rgb = (230 + n, 200 + n, 40 + n * 0.5)
        elif base_hue == 4:
            rgb = (200 + n, 80 + n * 0.5, 200 + n)
        else:
            rgb = (240 + n, 140 + n * 0.5, 40 + n * 0.5)
        return tuple(_clamp(c) for c in rgb) + (255,)

    def billboards_spec(x, y):
        v = _clamp(60 + 100 * _hash01(x, y, 83))
        return (v, v, v, 255)

    def trees_diffuse(x, y):
        # Foliage: green variations with trunk brown
        if y < TEX_SIZE * 0.15:
            # Trunk area
            v = 70 + 30 * _smooth(g8, x, y) + 15 * _hash01(x, y, 91)
            c = _clamp(v)
            return (c, _clamp(c * 0.6), _clamp(c * 0.4), 255)
        v = 50 + 40 * _smooth(g16, x, y) + 30 * _smooth(g32, x, y) + 25 * _hash01(x, y, 93)
        return (_clamp(25 + v * 0.4), _clamp(70 + v * 0.8), _clamp(20 + v * 0.3), 255)

    def trees_spec(x, y):
        v = _clamp(5 + 15 * _hash01(x, y, 95))
        return (v, v, v, 255)

    def containers_diffuse(x, y):
        # Shipping containers: corrugated metal look
        ridge = (x // 16) % 2
        n = _hash01(x, y, 101) * 20 - 10
        base = 100 + 30 * _smooth(g16, x, y)
        if ridge:
            base -= 15
        c = _clamp(base + n)
        return (c, _clamp(c * 0.98), _clamp(c * 0.95), 255)

    def containers_spec(x, y):
        v = _clamp(25 + 50 * _hash01(x, y, 103))
        return (v, v, v, 255)

    def tower_diffuse(x, y):
        # Tower/structure: concrete with metal accents
        band = (y // 64) % 4
        n = _hash01(x, y, 111) * 15 - 7
        if band == 0:
            v = 90 + n
        elif band == 1:
            v = 110 + n
        elif band == 2:
            v = 85 + n
        else:
            v = 100 + n
        c = _clamp(v)
        return (c, _clamp(c * 1.02), _clamp(c * 1.05), 255)

    def tower_spec(x, y):
        v = _clamp(40 + 80 * _hash01(x, y, 113))
        return (v, v, v, 255)

    _png(OUT / "crowd.png", crowd_diffuse)
    _png(OUT / "crowd_specular.png", crowd_spec)
    _png(OUT / "billboards.png", billboards_diffuse)
    _png(OUT / "billboards_specular.png", billboards_spec)
    _png(OUT / "trees.png", trees_diffuse)
    _png(OUT / "trees_specular.png", trees_spec)
    _png(OUT / "containers.png", containers_diffuse)
    _png(OUT / "containers_specular.png", containers_spec)
    _png(OUT / "tower.png", tower_diffuse)
    _png(OUT / "tower_specular.png", tower_spec)


# ---------- OBJ generation helpers ----------
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


def add_pyramid(verts, faces, center, base_size, height):
    cx, cy, cz = center
    hx, hy = base_size[0] / 2, base_size[1] / 2
    base = len(verts)
    verts.extend([
        (cx - hx, cy - hy, cz), (cx + hx, cy - hy, cz),
        (cx + hx, cy + hy, cz), (cx - hx, cy + hy, cz),
        (cx, cy, cz + height),
    ])
    faces.extend([
        [base + 3, base + 2, base + 1, base + 0],  # base
        [base + 0, base + 1, base + 4],            # front
        [base + 1, base + 2, base + 4],            # right
        [base + 2, base + 3, base + 4],            # back
        [base + 3, base + 0, base + 4],            # left
    ])


def add_cylinder(verts, faces, center, radius, length, axis="Z", segments=12):
    base = len(verts)
    for k in range(segments):
        a = 2 * math.pi * k / segments
        u, v = math.cos(a) * radius, math.sin(a) * radius
        verts.append((u, v, -length / 2))
        verts.append((u, v, length / 2))
    for k in range(segments):
        k2 = (k + 1) % segments
        faces.append([base + 2 * k, base + 2 * k2, base + 2 * k2 + 1, base + 2 * k + 1])
    faces.append([base + 2 * k + 1 for k in range(segments)][::-1])
    faces.append([base + 2 * k for k in range(segments)])
    if axis == "Y":
        for i in range(base, len(verts)):
            x, y, z = verts[i]
            verts[i] = (x, z, -y)
    elif axis == "X":
        for i in range(base, len(verts)):
            x, y, z = verts[i]
            verts[i] = (z, y, -x)
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


def write_obj(name, verts, faces, material_key):
    """Write OBJ with single material, clean paths, triangulated quads."""
    path = OUT / f"{name}.obj"
    mtl_name = f"{material_key}.mtl"
    with open(path, "w") as f:
        f.write(f"# generated by generate_extra_assets.py\n")
        f.write(f"mtllib {mtl_name}\n")
        f.write(f"o {name}\n")
        for v in verts:
            f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
        # Simple planar UV projection for all (box-projected)
        for v in verts:
            x, y, z = v
            ax, ay, az = abs(x), abs(y), abs(z)
            if ax >= ay and ax >= az:
                u, v_uv = y / 4.0, z / 4.0
            elif ay >= az:
                u, v_uv = x / 4.0, z / 4.0
            else:
                u, v_uv = x / 4.0, y / 4.0
            f.write(f"vt {u:.6f} {v_uv:.6f}\n")
        # Compute face normals
        for face in faces:
            n = _normal(verts, face)
            f.write(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n")
        f.write(f"usemtl mat_{material_key}\n")
        s = 0
        for face in faces:
            s += 1
            tri_faces = []
            if len(face) == 4:
                tri_faces = [[face[0], face[1], face[2]], [face[0], face[2], face[3]]]
            elif len(face) == 3:
                tri_faces = [face]
            else:
                # fan triangulation
                for i in range(1, len(face) - 1):
                    tri_faces.append([face[0], face[i], face[i + 1]])
            for tri in tri_faces:
                f.write("f")
                for vi in tri:
                    # OBJ indices are 1-based, UV and VN same index here
                    f.write(f" {vi + 1}/{vi + 1}/{vi + 1}")
                f.write("\n")
    return path


def write_mtl(name, material_key):
    """Write clean MTL with single material, relative texture paths."""
    tex, spec, kd, ks, ns = MATERIALS[material_key]
    path = OUT / f"{name}.mtl"
    with open(path, "w") as f:
        f.write("# generated by generate_extra_assets.py\n")
        f.write(f"newmtl mat_{material_key}\n")
        f.write(f"Ns {ns}.000000\n")
        f.write("Ka 1.000000 1.000000 1.000000\n")
        f.write(f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}\n")
        f.write(f"Ks {ks[0]:.6f} {ks[1]:.6f} {ks[2]:.6f}\n")
        f.write("Ke 0.000000 0.000000 0.000000\n")
        f.write("Ni 1.000000\n")
        f.write("d 1.000000\n")
        f.write("illum 2\n")
        f.write(f"map_Kd {tex}\n")
        f.write(f"map_Ks {spec}\n")


def bbox_from_verts(verts):
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    return {
        "min": [round(min(xs), 4), round(min(ys), 4), round(min(zs), 4)],
        "max": [round(max(xs), 4), round(max(ys), 4), round(max(zs), 4)],
    }


# ---------- Asset builders ----------
def build_crowd():
    """Low-poly crowd figures on grandstand steps.
    Grandstand: 6 tiers, each 160x3m, at Y=0.9, 1.8, 2.7, 3.6, 4.5, 5.4, Z from 25.5 to 7.5"""
    verts, faces = [], []
    rng = random.Random(2025)
    # Grandstand bounds from existing model: X in [-80, 80], Z in [7.5, 25.5], tiers at Y = 0.9 * (i+1)
    tier_heights = [0.9, 1.8, 2.7, 3.6, 4.5, 5.4]
    tier_depths = [25.5, 22.5, 19.5, 16.5, 13.5, 10.5]  # Z centers of each tier step
    for tier_idx, (y_level, z_center) in enumerate(zip(tier_heights, tier_depths)):
        # Two rows per tier: front (z_center + 1.0) and back (z_center - 1.0)
        for row in [1, -1]:
            z = z_center + row * 1.0
            # People every ~2.5m along X, slight random offset
            num_people = 60
            for i in range(num_people):
                x = -78 + i * 2.6 + rng.uniform(-0.3, 0.3)
                if x > 78:
                    continue
                # Simple low-poly person: body box + head sphere (box approx)
                h = rng.uniform(1.55, 1.85)
                body_w = rng.uniform(0.35, 0.5)
                body_d = rng.uniform(0.25, 0.35)
                # Body
                add_box(verts, faces, (x, y_level + h * 0.4, z), (body_w, body_d, h * 0.7))
                # Head
                head_r = rng.uniform(0.12, 0.16)
                add_box(verts, faces, (x, y_level + h * 0.75 + head_r, z), (head_r * 2, head_r * 2, head_r * 2))
    ob = write_obj("crowd", verts, faces, "crowd")
    write_mtl("crowd", "crowd")
    return ob, bbox_from_verts(verts)


def build_billboards():
    """Advertising billboards around track outer barrier (~10-12m from centerline)."""
    verts, faces = [], []
    rng = random.Random(2026)
    # Use waypoints to place billboards every ~30m along track
    step = max(1, len(WAYPOINTS) // 35)
    for i in range(0, len(WAYPOINTS), step):
        wp = WAYPOINTS[i]
        x, y, z = wp[0], wp[1], wp[2]  # note: waypoints_gl are X, Y=0, Z
        # Tangent/normal from next waypoint
        nxt = WAYPOINTS[(i + 1) % len(WAYPOINTS)]
        tx, tz = nxt[0] - x, nxt[2] - z
        length = math.hypot(tx, tz)
        if length < 1e-3:
            continue
        tx, tz = tx / length, tz / length
        # Left/right normal (pointing outward)
        nx, nz = -tz, tx
        # Place on both sides, slightly outside barriers
        for side in [1, -1]:
            if rng.random() > 0.7:  # 70% chance
                continue
            bx = x + nx * (BARRIER_OFFSET + 4 + rng.uniform(0, 3))
            bz = z + nz * (BARRIER_OFFSET + 4 + rng.uniform(0, 3))
            by = 4.0 + rng.uniform(0, 3)
            # Billboard: vertical panel
            w = rng.uniform(6, 10)
            h = rng.uniform(3, 5)
            # Face perpendicular to track
            rot = math.atan2(nz * side, nx * side)
            add_box(verts, faces, (bx, by + h / 2, bz), (0.3, h, w), rot_z=rot)
            # Support pole
            add_cylinder(verts, faces, (bx - nx * side * 0.5, by / 2, bz - nz * side * 0.5), 0.2, by, axis="Y", segments=8)
    ob = write_obj("billboards", verts, faces, "billboards")
    write_mtl("billboards", "billboards")
    return ob, bbox_from_verts(verts)


def build_trees():
    """Low-poly trees and bushes around track perimeter."""
    verts, faces = [], []
    rng = random.Random(2027)
    # Inner and outer rings around track
    for ring in [1, -1]:
        offset = BARRIER_OFFSET + 8 + (ring > 0) * 15
        for i in range(0, len(WAYPOINTS), max(1, len(WAYPOINTS) // 120)):
            wp = WAYPOINTS[i]
            x, z = wp[0], wp[2]
            nxt = WAYPOINTS[(i + 1) % len(WAYPOINTS)]
            tx, tz = nxt[0] - x, nxt[2] - z
            length = math.hypot(tx, tz)
            if length < 1e-3:
                continue
            tx, tz = tx / length, tz / length
            nx, nz = -tz * ring, tx * ring
            # Jitter position
            bx = x + nx * offset + rng.uniform(-4, 4)
            bz = z + nz * offset + rng.uniform(-4, 4)
            by = 0.0
            if rng.random() < 0.6:
                # Tree: trunk + foliage cone
                h_trunk = rng.uniform(1.5, 3.0)
                r_trunk = rng.uniform(0.15, 0.3)
                add_cylinder(verts, faces, (bx, by + h_trunk / 2, bz), r_trunk, h_trunk, axis="Y", segments=6)
                h_foliage = rng.uniform(2.5, 5.0)
                r_foliage = rng.uniform(1.5, 2.5)
                # Foliage as stacked cones (low poly)
                for layer in range(3):
                    ly = by + h_trunk + layer * h_foliage / 3
                    lr = r_foliage * (1.0 - layer * 0.25)
                    add_cylinder(verts, faces, (bx, ly + h_foliage / 6, bz), lr, h_foliage / 3, axis="Y", segments=6)
            else:
                # Bush: low rounded box
                bw = rng.uniform(1.5, 3.0)
                bh = rng.uniform(0.6, 1.2)
                bd = rng.uniform(1.5, 3.0)
                add_box(verts, faces, (bx, by + bh / 2, bz), (bw, bh, bd))
    ob = write_obj("trees", verts, faces, "trees")
    write_mtl("trees", "trees")
    return ob, bbox_from_verts(verts)


def build_containers():
    """Shipping containers and service buildings near pit lane."""
    verts, faces = [], []
    rng = random.Random(2028)
    # Pit building area: around X in [-70, 70], Z in [-70, -55] (from pit_building AABB)
    pit_min_x, pit_max_x = PIT_AABB["min"][0], PIT_AABB["max"][0]
    pit_min_z, pit_max_z = PIT_AABB["min"][2], PIT_AABB["max"][2]
    # Containers along pit lane behind building
    for i in range(12):
        x = pit_min_x - 15 + i * 12 + rng.uniform(-1, 1)
        z = pit_min_z - 8 - rng.uniform(0, 10)
        # Container: 12m x 2.4m x 2.6m (standard 40ft)
        w, d, h = 12.0, 2.4, 2.6
        if rng.random() < 0.3:
            # Stacked
            add_box(verts, faces, (x, h / 2, z), (w, d, h))
            add_box(verts, faces, (x, h * 1.5, z), (w, d, h))
        else:
            add_box(verts, faces, (x, h / 2, z), (w, d, h))
    # Service buildings near pit entry/exit
    for _ in range(4):
        x = rng.uniform(pit_min_x - 30, pit_max_x + 30)
        z = rng.uniform(pit_min_z - 25, pit_min_z - 5)
        bw = rng.uniform(8, 15)
        bd = rng.uniform(6, 10)
        bh = rng.uniform(4, 7)
        add_box(verts, faces, (x, bh / 2, z), (bw, bd, bh))
        # Roof
        add_pyramid(verts, faces, (x, bh + bh * 0.15, z), (bw * 1.1, bd * 1.1), bh * 0.3)
    ob = write_obj("containers", verts, faces, "containers")
    write_mtl("containers", "containers")
    return ob, bbox_from_verts(verts)


def build_tower():
    """Control tower / grandstand structure near start/finish."""
    verts, faces = [], []
    rng = random.Random(2029)
    # Tower near start bridge (X ~ 0, Z ~ -40 to -50)
    tx, tz = 0.0, -48.0
    # Main tower shaft
    shaft_w, shaft_d, shaft_h = 6.0, 6.0, 25.0
    add_box(verts, faces, (tx, shaft_h / 2, tz), (shaft_w, shaft_d, shaft_h))
    # Observation deck
    deck_w, deck_d, deck_h = 10.0, 10.0, 3.0
    add_box(verts, faces, (tx, shaft_h + deck_h / 2, tz), (deck_w, deck_d, deck_h))
    # Roof
    add_pyramid(verts, faces, (tx, shaft_h + deck_h + 1.5, tz), (deck_w * 1.2, deck_d * 1.2), 3.0)
    # Antennas
    for _ in range(3):
        ax = tx + rng.uniform(-2, 2)
        az = tz + rng.uniform(-2, 2)
        add_cylinder(verts, faces, (ax, shaft_h + deck_h + 4, az), 0.1, 4.0, axis="Y", segments=6)
    # Stairs / access
    for i in range(5):
        add_box(verts, faces, (tx - 4, 1.5 + i * 5, tz - 8), (1.5, 3, 2.0))
    ob = write_obj("tower", verts, faces, "tower")
    write_mtl("tower", "tower")
    return ob, bbox_from_verts(verts)


def update_scene_coords(new_boxes):
    """Append new AABBs to scene_coords.json."""
    coords = SCENE_COORDS
    for name, box in new_boxes.items():
        coords["aabb"][name] = box
    # Also merge into barriers merged box
    all_min = coords["aabb"]["barriers"]["min"][:]
    all_max = coords["aabb"]["barriers"]["max"][:]
    for box in new_boxes.values():
        for i in range(3):
            all_min[i] = min(all_min[i], box["min"][i])
            all_max[i] = max(all_max[i], box["max"][i])
    coords["aabb"]["barriers"]["min"] = all_min
    coords["aabb"]["barriers"]["max"] = all_max
    (OUT / "scene_coords.json").write_text(json.dumps(coords, indent=2) + "\n")
    print("Updated scene_coords.json")


def main():
    print("Generating textures...")
    gen_textures()

    print("Building crowd...")
    _, crowd_box = build_crowd()

    print("Building billboards...")
    _, billboards_box = build_billboards()

    print("Building trees...")
    _, trees_box = build_trees()

    print("Building containers...")
    _, containers_box = build_containers()

    print("Building tower...")
    _, tower_box = build_tower()

    new_boxes = {
        "crowd": crowd_box,
        "billboards": billboards_box,
        "trees": trees_box,
        "containers": containers_box,
        "tower": tower_box,
    }
    update_scene_coords(new_boxes)

    print("Verifying...")
    for name in EXPORTS:
        obj_path = OUT / f"{name}.obj"
        mtl_path = OUT / f"{name}.mtl"
        assert obj_path.exists(), f"Missing {name}.obj"
        assert mtl_path.exists(), f"Missing {name}.mtl"
        text = obj_path.read_text()
        assert "usemtl" in text, f"No usemtl in {name}.obj"
        mtl_text = mtl_path.read_text()
        assert f"map_Kd {MATERIALS[name][0]}" in mtl_text, f"Bad map_Kd in {name}.mtl"
        assert f"map_Ks {MATERIALS[name][1]}" in mtl_text, f"Bad map_Ks in {name}.mtl"
    for tex_name in ["crowd.png", "crowd_specular.png", "billboards.png", "billboards_specular.png",
                     "trees.png", "trees_specular.png", "containers.png", "containers_specular.png",
                     "tower.png", "tower_specular.png"]:
        p = OUT / tex_name
        assert p.exists(), f"Missing texture {tex_name}"
        data = p.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", f"Bad PNG {tex_name}"
        w, h = struct.unpack(">II", data[16:24])
        assert w == h == TEX_SIZE and (w & (w - 1)) == 0, f"Non-power-of-2 {tex_name}: {w}x{h}"
    print("All assets generated and verified.")


if __name__ == "__main__":
    main()