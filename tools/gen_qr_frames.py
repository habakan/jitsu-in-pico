# /// script
# dependencies = ["segno", "pillow", "numpy"]
# ///
"""UR アニメーション QR の 1 フレームを、QVGA グレースケールのカメラ画像風に合成して C ヘッダへ出す。"""
import random, sys
import numpy as np, segno
from PIL import Image, ImageFilter

W, H = 320, 240
BYTEWORDS = "ABDEGHJKLMNPSTWYZ"  # minimal bytewords に現れる文字の近似。英大文字なので alphanumeric モードになる

def ur_frame(frag_bytes, rng):
    body = "".join(rng.choice(BYTEWORDS) for _ in range(frag_bytes * 2 + 8))
    return f"UR:CRYPTO-PSBT/12-30/{body}"

def camera_shot(qr, span, angle, blur, noise, rng):
    img = Image.fromarray((1 - np.array(qr.matrix, dtype=np.uint8)) * 255, "L").resize((span, span), Image.NEAREST)
    bg = Image.new("L", (W, H), 255)
    bg.paste(img, ((W - span) // 2, (H - span) // 2))
    bg = bg.rotate(angle, resample=Image.BILINEAR, fillcolor=255)
    bg = bg.filter(ImageFilter.GaussianBlur(blur))
    a = np.asarray(bg, dtype=np.float32) / 255.0
    light = np.linspace(0.75, 1.0, W)[None, :] * np.linspace(0.85, 1.0, H)[:, None]
    a = (40 + a * 170) * light + np.array(rng.normal(0, noise, (H, W)))
    return np.clip(a, 0, 255).astype(np.uint8)

def main():
    rng = random.Random(1)
    nrng = np.random.default_rng(1)
    cases = []
    for frag in (50, 100, 200):
        text = ur_frame(frag, rng)
        qr = segno.make(text, error="l", boost_error=False)
        modules = len(qr.matrix)
        for span in (220, 190, 160):
            for blur in (0.5, 1.0):
                name = f"f{frag}_s{span}_b{int(blur * 10)}"
                cases.append((name, qr.version, modules, span, text, camera_shot(qr, span, 5, blur, 4, nrng)))
    out = open(sys.argv[1], "w")
    out.write(f"/* tools/gen_qr_frames.py が生成 */\n#define FRAME_W {W}\n#define FRAME_H {H}\n")
    out.write("struct frame { const char *name; int version; const char *text; const unsigned char *pix; };\n")
    for i, (name, ver, modules, span, text, pix) in enumerate(cases):
        out.write(f"static const unsigned char frame{i}[] = {{" + ",".join(map(str, pix.flatten())) + "};\n")
        Image.fromarray(pix).save(sys.argv[1].replace(".h", f"_{name}.png"))
        print(f"{name}: version {ver}, {modules} modules, {span / modules:.1f} px/module, {len(text)} chars")
    out.write("static const struct frame frames[] = {\n")
    for i, (name, ver, _, _, text, _) in enumerate(cases):
        out.write(f'    {{"{name}", {ver}, "{text}", frame{i}}},\n')
    out.write("};\n")

main()
