import json
from collections import defaultdict
from itertools import combinations, pairwise
from pathlib import Path

import numpy as np
import shapely as sh
import trimesh
from shapely.geometry import Point, Polygon, box
from shapely.ops import polygonize

OUT = Path(__file__).resolve().parent
P = json.loads((OUT / "case_dimensions.json").read_text())
C = P["case"]
O = C["wall"] + C["fit_clearance"]
PW, PD = P["carrier"]["size"]
W, D = PW + 2 * O, PD + 2 * O
cam, pico, lcd = P["camera"], P["pico"], P["lcd"]
CAM_TIP = C["wall"] + cam["face_gap"]
CAM_FRONT = CAM_TIP + cam["lens_height"]
CAM_BACK = CAM_FRONT + cam["pcb_thickness"]
PICO_FACE = CAM_BACK + 1.6 + pico["component_gap"]
PCB_Z = PICO_FACE + pico["pcb_thickness"] + pico["header_height"] + pico["socket_height"]
PCB_TOP = PCB_Z + P["carrier"]["thickness"]
LCD_BOTTOM = PCB_TOP + lcd["rear_connector_depth"]
H = LCD_BOTTOM + lcd["pcb_thickness"] + lcd["glass_height"] + lcd["face_gap"]
TOP = H + C["face_thickness"]
i = P["carrier"]["screw_inset"] + O
SCREWS = [(i, i), (W - i, i), (i, D - i), (W - i, D - i)]
OUTLINE = [
    (O + 1, O),
    (W - O - 1, O),
    (W - O, O + 1),
    (W - O, D - O - 1),
    (W - O - 1, D - O),
    (O + 1, D - O),
    (O, D - O - 1),
    (O, O + 1),
]


def disk(x, y, radius):
    return Point(x, y).buffer(radius, quad_segs=12)


def rounded(x0, y0, x1, y1, radius):
    return box(x0 + radius, y0 + radius, x1 - radius, y1 - radius).buffer(
        radius, quad_segs=12
    )


def op(poly, z0, z1, cut=False):
    if z1 <= z0:
        raise ValueError(f"Nonpositive extrusion height: {z0}, {z1}")
    return ("cut" if cut else "add", poly, round(z0, 6), round(z1, 6))


def slices(ops):
    zs = sorted({z for _, _, a, b in ops for z in (a, b)})
    result = []
    for lo, hi in pairwise(zs):
        poly = Polygon()
        for action, footprint, a, b in ops:
            if a <= (lo + hi) / 2 <= b:
                poly = (
                    poly.union(footprint)
                    if action == "add"
                    else poly.difference(footprint)
                )
        result.append((lo, hi, poly))
    return result


def mesh_slices(layers):
    boundaries = sh.union_all([p.boundary for _, _, p in layers if not p.is_empty])
    xy = []
    for cell in polygonize(boundaries):
        for tri in sh.get_parts(sh.constrained_delaunay_triangles(cell)):
            coords = np.asarray(tri.exterior.coords[:3])
            if np.cross(coords[1] - coords[0], coords[2] - coords[0]) < 0:
                coords = coords[::-1]
            xy.append(coords)
    xy = np.asarray(xy)
    centers = sh.points(xy.mean(axis=1))
    occupied = np.array([sh.covers(p, centers) for _, _, p in layers])
    edges = defaultdict(list)
    for i, triangle in enumerate(xy):
        for e in range(3):
            key = tuple(
                sorted(tuple(np.round(v, 7)) for v in triangle[[e, (e + 1) % 3]])
            )
            edges[key].append((i, e))
    neighbors = {}
    for entries in edges.values():
        if len(entries) == 2:
            (a, e), (b, f) = entries
            neighbors[a, e], neighbors[b, f] = b, a
        elif len(entries) != 1:
            raise ValueError("Non-manifold planar subdivision")
    faces = []
    for k, (lo, hi, _) in enumerate(layers):
        for i in np.flatnonzero(occupied[k]):
            low = np.column_stack((xy[i], np.full(3, lo)))
            high = np.column_stack((xy[i], np.full(3, hi)))
            if k == 0 or not occupied[k - 1, i]:
                faces.append(low[::-1])
            if k == len(layers) - 1 or not occupied[k + 1, i]:
                faces.append(high)
            for e in range(3):
                other = neighbors.get((i, e))
                if other is None or not occupied[k, other]:
                    f = (e + 1) % 3
                    faces.extend(
                        ([low[e], low[f], high[f]], [low[e], high[f], high[e]])
                    )
    vertices = np.asarray(faces).reshape(-1, 3)
    return trimesh.Trimesh(
        vertices=vertices, faces=np.arange(len(vertices)).reshape(-1, 3), process=True
    )


def make_parts():
    parts = []

    def add(name, label, ops, color, group, printable=False):
        parts.append(
            {
                "name": name,
                "label": label,
                "ops": ops,
                "color": color,
                "group": group,
                "printable": printable,
            }
        )

    wall = C["wall"]
    base = [
        op(rounded(0, 0, W, D, 4), 0, H),
        op(rounded(wall, wall, W - wall, D - wall, 1.6), wall, H + 1, True),
    ]
    for x, y in SCREWS:
        base.extend(
            [
                op(disk(x, y, 3.6), wall, PCB_Z),
                op(disk(x, y, 1), PCB_Z - 10, PCB_Z + 0.1, True),
            ]
        )
    px, py = pico["xy"][0] + O, pico["xy"][1] + O
    usb_y = py + 10.5
    base.append(
        op(box(-1, usb_y - 8, wall + 1, usb_y + 8), PICO_FACE - 7, PICO_FACE + 2.6, True)
    )

    cx, cy = cam["xy"][0] + O, cam["xy"][1] + O
    size = cam["size"]
    center = (cx + cam["lens_offset"][0], cy + cam["lens_offset"][1])
    base.append(op(disk(*center, cam["window_diameter"] / 2), -1, wall + 1, True))
    cam_screws = [
        (x, y) for x in (cx - 3, cx + size + 3) for y in (cy + 8, cy + size - 8)
    ]
    clamp_poly = box(cx - 0.7, cy - 0.7, cx + size + 0.7, cy + size + 0.7).difference(
        box(cx + 1.2, cy + 1.2, cx + size - 1.2, cy + size + 0.8)
    )
    for x, y in cam_screws:
        side = cx if x < cx else cx + size
        clamp_poly = clamp_poly.union(disk(x, y, 2.2)).union(
            box(min(x, side), y - 1.4, max(x, side), y + 1.4)
        )
        base.extend(
            [
                op(disk(x, y, 2.2), wall, CAM_BACK),
                op(disk(x, y, 0.8), wall + 0.6, CAM_BACK + 0.1, True),
            ]
        )
    for x in (cx, cx + size - 1.2):
        for y in (cy, cy + size - 1.2):
            base.append(op(box(x, y, x + 1.2, y + 1.2), wall, CAM_FRONT))
    add("base", "背面ケース・カメラ受け", base, "#455c68", "base", True)

    clamp = [op(clamp_poly, CAM_BACK, CAM_BACK + 1.6)]
    for x, y in cam_screws:
        clamp.append(op(disk(x, y, 1.1), CAM_BACK - 0.1, CAM_BACK + 1.7, True))
    add(
        "camera_retainer",
        "カメラ裏押さえ・M2ねじ4本",
        clamp,
        "#83999c",
        "retainer",
        True,
    )
    add(
        "camera_board",
        "OV7675 B0070・背面向き",
        [op(box(cx, cy, cx + size, cy + size), CAM_FRONT, CAM_BACK)],
        "#d74238",
        "camera",
    )
    add(
        "camera_lens",
        "カメラレンズ・高さは仮値",
        [op(disk(*center, cam["lens_diameter"] / 2), CAM_TIP + 0.15, CAM_FRONT)],
        "#202b36",
        "camera",
    )
    add(
        "camera_glass",
        "カメラ光学面",
        [op(disk(*center, cam["lens_diameter"] * 0.29), CAM_TIP, CAM_TIP + 0.15)],
        "#397986",
        "camera",
    )
    add(
        "camera_header",
        "カメラヘッダ・上辺と仮定",
        [
            op(
                box(cx + 2.55, cy + size - 4.8, cx + 27.95, cy + size + 0.3),
                CAM_BACK,
                CAM_BACK + cam["rear_connector_depth"],
            )
        ],
        "#252c32",
        "camera",
    )

    lid = [op(rounded(0, 0, W, D, 4), H, TOP)]
    clearance = C["fit_clearance"]
    lip = rounded(
        wall + clearance,
        wall + clearance,
        W - wall - clearance,
        D - wall - clearance,
        2,
    )
    lip = lip.difference(
        rounded(
            wall + clearance + 1.2,
            wall + clearance + 1.2,
            W - wall - clearance - 1.2,
            D - wall - clearance - 1.2,
            1,
        )
    )
    buttons = [(x + O, y + O) for x, y in P["buttons"]["centers"]]
    for x, y in SCREWS:
        lip = lip.difference(disk(x, y, 4))
    for x, y in buttons:
        lip = lip.difference(disk(x, y, 4.5))
    lid.append(op(lip, H - 1.8, H))
    for x, y in SCREWS:
        lid.extend(
            [
                op(disk(x, y, 3), PCB_TOP, H),
                op(disk(x, y, 1.4), PCB_TOP - 0.1, TOP + 1, True),
                op(disk(x, y, 2.65), TOP - 1.2, TOP + 1, True),
            ]
        )

    lx, ly = lcd["xy"][0] + O, lcd["xy"][1] + O
    lw, ld = lcd["size"]
    ltop = LCD_BOTTOM + lcd["pcb_thickness"]
    lcd_holes = [(lx + x, ly + y) for x in (2.5, lw - 2.5) for y in (2.5, ld - 2.5)]
    lcd_ops = [op(box(lx, ly, lx + lw, ly + ld), LCD_BOTTOM, ltop)]
    for x, y in lcd_holes:
        lcd_ops.append(op(disk(x, y, 1), LCD_BOTTOM - 0.1, ltop + 0.1, True))
        lid.extend(
            [
                op(disk(x, y, 2.3), ltop, H),
                op(disk(x, y, 0.65), ltop - 0.1, TOP - 0.6, True),
            ]
        )
    ax, ay = lx + lcd["active_origin"][0], ly + lcd["active_origin"][1]
    aa, margin = lcd["active_size"], lcd["window_margin"]
    lid.append(
        op(
            box(ax - margin, ay - margin, ax + aa + margin, ay + aa + margin),
            H - 1,
            TOP + 1,
            True,
        )
    )
    add("lcd_board", "LCD基板・M1.6ねじ4本", lcd_ops, "#175989", "lcd")
    add(
        "lcd_glass",
        "LCDガラス",
        [
            op(
                box(lx + 5, ly + 0.24, lx + 38.72, ly + 31.76),
                ltop,
                H - lcd["face_gap"] - 0.03,
            )
        ],
        "#131d27",
        "lcd",
    )
    add(
        "lcd_active",
        "表示域 27.72 mm角",
        [
            op(
                box(ax, ay, ax + aa, ay + aa),
                H - lcd["face_gap"] - 0.03,
                H - lcd["face_gap"],
            )
        ],
        "#91c7ce",
        "lcd",
    )
    add(
        "lcd_header",
        "LCD背面ヘッダ・仮寸法",
        [op(box(lx + 0.2, ly + 5.2, lx + 2.8, ly + 25.5), PCB_TOP, LCD_BOTTOM)],
        "#252c32",
        "lcd",
    )

    board = [op(Polygon(OUTLINE), PCB_Z, PCB_TOP)]
    for x, y in SCREWS:
        board.append(op(disk(x, y, 1.35), PCB_Z - 0.1, PCB_TOP + 0.1, True))
    for i, (bx, by) in enumerate(buttons):
        lid.extend(
            [
                op(disk(bx, by, 4.2), H - 2.5, H),
                op(disk(bx, by, 3.7), H - 2.6, H, True),
                op(disk(bx, by, 2.7), H, TOP + 1, True),
            ]
        )
        tip = PCB_TOP + P["buttons"]["switch_height"]
        button = [
            op(disk(bx, by, 1.5), tip + P["buttons"]["free_gap"], H - 0.8),
            op(disk(bx, by, 3.4), H - 1.3, H - 0.1),
            op(disk(bx, by, 2.4), H - 0.2, TOP + 1),
        ]
        add(
            f"button_{i + 1}",
            ["次へボタン・押し子", "承認ボタン・押し子"][i],
            button,
            ["#cae0e1", "#e5ac56"][i],
            "button",
            True,
        )
        add(
            f"switch_{i + 1}",
            "TVDP01-G73BB",
            [
                op(box(bx - 3, by - 3, bx + 3, by + 3), PCB_TOP, PCB_TOP + 3.8),
                op(disk(bx, by, 1.7), PCB_TOP + 3.8, tip),
            ],
            "#232c34",
            "board",
        )
    add("lid", "前カバー・液晶の固定柱", lid, "#e4e6dc", "lid", True)
    add("carrier", "KiCadキャリア基板・ケースねじで挟む", board, "#287567", "board")

    socket_z = PCB_Z - pico["socket_height"]
    pico_z = PICO_FACE + pico["pcb_thickness"]
    sockets, headers = [], []
    for y in (py + 1.61, py + 19.39):
        row = box(px + 0.1, y - 1.25, px + 50.9, y + 1.25)
        sockets.append(op(row, socket_z, PCB_Z))
        headers.append(op(row, pico_z, socket_z))
    add("pico_sockets", "Pico用ソケット 1×20 ×2・基板裏", sockets, "#252c32", "board")
    add("pico_headers", "Pico 2 Hの実装済みヘッダ", headers, "#343b42", "pico")
    pico_ops = [op(box(px, py, px + 51, py + 21), PICO_FACE, pico_z)]
    for x in (px + 2, px + 49):
        for y in (py + 4.8, py + 16.2):
            pico_ops.append(op(disk(x, y, 1.05), PICO_FACE - 0.1, pico_z + 0.1, True))
    add("pico", "Pico 2 H・部品面は背面側", pico_ops, "#32976d", "pico")
    add(
        "rp2350",
        "RP2350",
        [op(box(px + 23, py + 7, px + 30, py + 14), PICO_FACE - 1.2, PICO_FACE)],
        "#233038",
        "pico",
    )
    add(
        "bootsel",
        "BOOTSEL・基板を外して操作",
        [op(box(px + 9, py + 7.6, px + 12, py + 13.4), PICO_FACE - 1.5, PICO_FACE)],
        "#f1eade",
        "pico",
    )
    usb_z = PICO_FACE - pico["usb_height"]
    usb = [
        op(box(px - 1.3, usb_y - 4, px + 4.5, usb_y + 4), usb_z, PICO_FACE),
        op(
            box(px - 1.4, usb_y - 3.25, px + 3, usb_y + 3.25),
            usb_z + 0.45,
            PICO_FACE - 0.45,
            True,
        ),
    ]
    add("usb", "Micro-USB・開口16 mm幅", usb, "#aeb8bb", "pico")
    connectors = []
    for c in P["connectors"].values():
        x, y, w, d = c["rect"]
        z0, z1 = (PCB_TOP, PCB_TOP + 8.5) if c["side"] == "front" else (socket_z, PCB_Z)
        connectors.append(op(box(x + O, y + O, x + O + w, y + O + d), z0, z1))
    add(
        "harness_sockets", "J3 LCD / J5 UART / J4カメラ(裏)", connectors, "#252c32", "board"
    )
    add(
        "case_screw_heads",
        "M2.5×25 ケース・基板固定ねじ",
        [op(disk(x, y, 2.3), TOP - 1.2, TOP + 0.4) for x, y in SCREWS],
        "#a6b3b9",
        "lid",
    )
    return parts


def overlap(a, b):
    volume = 0.0
    for lo, hi, p in a:
        for low, high, q in b:
            height = min(hi, high) - max(lo, low)
            if height > 1e-7:
                volume += p.intersection(q).area * height
    return volume


def scad_source(parts):
    source = 'part = "assembly";\n// Generated from case_dimensions.json. Units: mm.\n'
    for part in parts:
        source += f"module {part['name']}() {{ union() {{\n"
        for lo, hi, footprint in part["layers"]:
            for poly in sh.get_parts(footprint):
                if poly.is_empty:
                    continue
                rings = [list(poly.exterior.coords)[:-1]] + [
                    list(r.coords)[:-1] for r in poly.interiors
                ]
                points, paths = [], []
                for ring in rings:
                    paths.append(list(range(len(points), len(points) + len(ring))))
                    points.extend([[round(x, 6), round(y, 6)] for x, y in ring])
                source += f"translate([0,0,{lo}]) linear_extrude({hi - lo}) polygon(points={json.dumps(points)},paths={json.dumps(paths)});\n"
        source += "} }\n"
    source += 'if (part == "assembly") {\n'
    for part in parts:
        source += f'color("{part["color"]}") {part["name"]}();\n'
    source += "}\n"
    for part in parts:
        if part["printable"]:
            bounds = part["mesh"].bounds
            if part["name"] == "lid" or part["name"].startswith("button_"):
                transform = f"translate([{-bounds[0, 0]},{bounds[1, 1]},{bounds[1, 2]}]) rotate([180,0,0]) "
            else:
                transform = f"translate({(-bounds[0]).tolist()}) "
            source += f'if(part == "{part["name"]}") {transform}{part["name"]}();\n'
    return source


def main():
    from export_formats import export_formats
    from render_case import export_views

    parts = make_parts()
    report = {
        "units": "mm",
        "assumptions": P["assumptions"],
        "parts": [],
        "interferences": [],
    }
    scene = trimesh.Scene()
    for part in parts:
        part["layers"] = slices(part["ops"])
        mesh = mesh_slices(part["layers"])
        part["mesh"] = mesh
        if not mesh.is_watertight or not mesh.is_winding_consistent or mesh.volume <= 0:
            raise ValueError(f"Invalid mesh: {part['name']}")
        expected = sum((hi - lo) * p.area for lo, hi, p in part["layers"])
        if not np.isclose(mesh.volume, expected, rtol=1e-6, atol=1e-5):
            raise ValueError(f"Volume mismatch: {part['name']}")
        mesh.visual.face_colors = [
            int(part["color"][i : i + 2], 16) for i in (1, 3, 5)
        ] + [255]
        scene.add_geometry(mesh, node_name=part["name"], geom_name=part["name"])
        if part["printable"]:
            if (
                len(
                    trimesh.graph.connected_components(
                        mesh.face_adjacency,
                        nodes=np.arange(len(mesh.faces)),
                        engine="networkx",
                    )
                )
                != 1
            ):
                raise ValueError(f"Disconnected printable: {part['name']}")
            printed = mesh.copy()
            if part["name"] == "lid" or part["name"].startswith("button_"):
                printed.apply_transform(
                    trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0])
                )
            printed.apply_translation(-printed.bounds[0])
            part["print_mesh"] = printed
            name = "front" if part["name"] == "lid" else part["name"]
            printed.export(OUT / f"pico2h_case_{name}.stl")
            exported = trimesh.load_mesh(OUT / f"pico2h_case_{name}.stl", process=True)
            if not exported.is_watertight or not exported.is_winding_consistent:
                raise ValueError(f"STL roundtrip failed: {name}")
        report["parts"].append(
            {
                "name": part["name"],
                "watertight": bool(mesh.is_watertight),
                "volume_mm3": round(mesh.volume, 3),
                "bounds": mesh.bounds.round(3).tolist(),
                "printable": part["printable"],
            }
        )
        print(part["name"], len(mesh.faces), "triangles", flush=True)
    checked = 0
    for a, b in combinations(parts, 2):
        if a["name"] == "case_screw_heads" or b["name"] == "case_screw_heads":
            continue
        checked += 1
        volume = overlap(a["layers"], b["layers"])
        if volume > 1e-4:
            report["interferences"].append(
                {"a": a["name"], "b": b["name"], "volume_mm3": round(volume, 4)}
            )
    report["checked_pairs"] = checked
    report["scope"] = (
        "Nominal simplified solid envelopes only. Wiring bends, insertion paths, optical field and physical tolerances need physical confirmation."
    )
    (OUT / "fit_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    )
    if report["interferences"]:
        raise ValueError(report["interferences"])
    (OUT / "pico2h_case.scad").write_text(scad_source(parts))
    scene.export(OUT / "assembly.glb")
    export_formats(parts, OUT, (O, O, PCB_TOP))
    export_views(parts, (W, D, TOP), OUT)
    print(
        f"PASS: {checked} nominal body pairs, closed printable meshes and STL roundtrips"
    )


if __name__ == "__main__":
    main()
