import base64
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

EXPLODE = {
    "base": 0,
    "camera": 45,
    "retainer": 58,
    "pico": 76,
    "board": 94,
    "lcd": 118,
    "button": 134,
    "lid": 160,
}
FONT = "/System/Library/Fonts/Hiragino Sans GB.ttc"


def font(size):
    return (
        ImageFont.truetype(FONT, size)
        if Path(FONT).exists()
        else ImageFont.load_default(size=size)
    )


def rotation(azimuth, inclination):
    a, e = math.radians(azimuth), math.radians(inclination)
    return np.array(
        [
            [math.cos(a), -math.sin(a), 0],
            [math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), -math.sin(e)],
            [math.sin(a) * math.sin(e), math.cos(a) * math.sin(e), math.cos(e)],
        ]
    )


def render(parts, width, height, mode):
    rot = rotation(-16, 65 if mode == "exploded" else 24)
    triangles, colors = [], []
    light = np.array([-0.35, -0.55, 1])
    light /= np.linalg.norm(light)
    for p in parts:
        if mode == "inside" and p["group"] in ("base", "lid"):
            continue
        mesh = p["mesh"]
        v = mesh.triangles.copy()
        if mode == "exploded":
            v[:, :, 2] += EXPLODE[p["group"]]
        triangles.append(v @ rot.T)
        rgb = np.array([int(p["color"][i : i + 2], 16) for i in (1, 3, 5)])
        lighting = 0.58 + 0.42 * np.maximum(0, mesh.face_normals @ light)
        colors.append(rgb[None, :] * lighting[:, None])
    tri = np.concatenate(triangles)
    colors = np.concatenate(colors)
    low, high = tri.reshape(-1, 3).min(axis=0), tri.reshape(-1, 3).max(axis=0)
    scale = min((width - 100) / (high[0] - low[0]), (height - 110) / (high[1] - low[1]))
    origin = np.array([width / 2, height / 2]) - scale * (low[:2] + high[:2]) / 2
    tri[:, :, :2] = tri[:, :, :2] * scale + origin
    rgb = np.full((height, width, 3), [247, 247, 242], dtype=np.uint8)
    depth = np.full((height, width), -np.inf)
    for t, color in zip(tri, colors):
        xmin, ymin = np.maximum(np.floor(t[:, :2].min(axis=0)).astype(int), 0)
        xmax, ymax = np.minimum(
            np.ceil(t[:, :2].max(axis=0)).astype(int), [width - 1, height - 1]
        )
        if xmax < xmin or ymax < ymin:
            continue
        xx, yy = np.meshgrid(
            np.arange(xmin, xmax + 1) + 0.5, np.arange(ymin, ymax + 1) + 0.5
        )
        a, b, c = t
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-10:
            continue
        u = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / den
        v = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / den
        w = 1 - u - v
        z = u * a[2] + v * b[2] + w * c[2]
        view = depth[ymin : ymax + 1, xmin : xmax + 1]
        mask = (u >= -1e-8) & (v >= -1e-8) & (w >= -1e-8) & (z > view)
        view[mask] = z[mask]
        rgb[ymin : ymax + 1, xmin : xmax + 1][mask] = color.astype(np.uint8)
    im = Image.fromarray(rgb)
    draw = ImageDraw.Draw(im)
    labels = [
        ("lcd_active", "LCD", -15, -35),
        ("camera_glass", "OV7675", 30, -20),
        ("pico", "Pico 2 H", 15, 22),
        ("carrier", "KiCad PCB", 90, 50),
    ]
    if mode != "assembled":
        for name, label, dx, dy in labels:
            p = next(p for p in parts if p["name"] == name)
            anchor = p["mesh"].bounds.mean(axis=0)
            if mode == "exploded":
                anchor[2] += EXPLODE[p["group"]]
            projected = (anchor @ rot.T)[:2] * scale + origin
            x, y = projected
            tx = min(max(x + dx, 15), width - 120)
            ty = min(max(y + dy, 15), height - 40)
            draw.line([(x, y), (tx, ty + 12)], fill="#455761", width=2)
            text_box = draw.textbbox((tx, ty), label, font=font(19))
            draw.rounded_rectangle(
                (text_box[0] - 7, text_box[1] - 5, text_box[2] + 7, text_box[3] + 5),
                radius=5,
                fill="#f7f7f2",
            )
            draw.text((tx, ty), label, font=font(19), fill="#203842")
    return im


def export_views(parts, size, out):
    width, depth, height = size
    dimensions = f"{width:g} × {depth:g} × {height:g} mm"
    image = Image.new("RGB", (1600, 1000), "#f7f7f2")
    draw = ImageDraw.Draw(image)
    draw.text((55, 32), "実印  /  Pico 2 H ケース試作", font=font(38), fill="#203842")
    draw.text(
        (57, 94),
        f"REV C   ·   {dimensions}   ·   同じ形状データからSTLと完成イメージを生成",
        font=font(20),
        fill="#57686c",
    )
    for i, (mode, label) in enumerate(
        [
            ("assembled", "01  完成イメージ"),
            ("inside", "02  内部配置（ケースを非表示）"),
        ]
    ):
        image.paste(render(parts, 760, 650, mode), (40 + 800 * i, 155))
        draw.text((60 + 800 * i, 153), label, font=font(25), fill="#203842")
    draw.line((55, 826, 1545, 826), fill="#d5ddd9", width=2)
    for x, title, note in [
        (60, "液晶 / カメラ", "液晶は前カバー、カメラは背面ケースへ固定しハーネス接続"),
        (820, "Pico / ボタン", "Picoは基板裏のソケットへ。押し子でスイッチを操作"),
    ]:
        draw.text((x, 856), title, font=font(25), fill="#203842")
        draw.text((x, 899), note, font=font(19), fill="#52636a")
    draw.text(
        (60, 955),
        "調整用の試作：レンズ高さ・コネクタ・取付面は未実測。キャリア基板は未配線。",
        font=font(19),
        fill="#836334",
    )
    image.save(out / "assembly_preview.png")
    exploded = Image.new("RGB", (1100, 1200), "#f7f7f2")
    d = ImageDraw.Draw(exploded)
    d.text((45, 28), "分解イメージ / 部品と固定方法", font=font(34), fill="#203842")
    exploded.paste(render(parts, 1050, 1000, "exploded"), (25, 100))
    d.text(
        (45, 1130),
        "上から：前カバー → 押し子 → LCD → 基板 → Pico → 裏押さえ → カメラ → 背面ケース",
        font=font(16),
        fill="#52636a",
    )
    exploded.save(out / "assembly_exploded.png")
    records = []
    for p in parts:
        mesh = p["mesh"]
        data = np.concatenate(
            (mesh.triangles, np.repeat(mesh.face_normals[:, None, :], 3, axis=1)),
            axis=2,
        ).astype("<f4")
        records.append(
            {
                "name": p["name"],
                "label": p["label"],
                "group": p["group"],
                "color": p["color"],
                "count": len(mesh.faces) * 3,
                "data": base64.b64encode(data.tobytes()).decode(),
                "anchor": mesh.bounds.mean(axis=0).round(3).tolist(),
            }
        )
    template = (out / "viewer_template.html").read_text()
    payload = json.dumps(
        {
            "parts": records,
            "explode": EXPLODE,
            "width": width,
            "depth": depth,
            "height": height,
        },
        ensure_ascii=False,
        separators=(",", ":"),
    )
    (out / "assembly.html").write_text(
        template.replace("__DIMENSIONS__", dimensions).replace(
            "__SCENE_DATA__", payload
        )
    )
