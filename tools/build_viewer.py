# /// script
# dependencies = []
# ///
"""PSBT ビューアを単一 HTML にまとめる。wasm を base64 で埋め込むので、file:// でもオフラインで動く。
使い方: uv run tools/build_viewer.py build/viewer.html

埋め込む parser.wasm は実機に積んでいるものと同じバイト列。ハッシュをページに出して突き合わせられる。"""
import base64
import hashlib
import re
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "build/viewer.html"
parser = open("build/parser.wasm", "rb").read()
address = open("build/address.wasm", "rb").read()
qr = open("build/qr.wasm", "rb").read()
jsqr = open("apps/viewer/vendor/jsQR.min.js").read()  # Safari には内蔵デコーダが無いので同梱する
# 解析器のホスト。単一 HTML は file:// で開くので ES モジュールにできない（CORS で弾かれる）。
# export を落として普通のスクリプトとして埋める
host = re.sub(r"^export ", "", open("components/parser/hosts/js/parser.mjs").read(), flags=re.M)
page = open("apps/viewer/viewer.html").read()

html = (page.replace("__PARSER_WASM__", base64.b64encode(parser).decode())
            .replace("__ADDRESS_WASM__", base64.b64encode(address).decode())
            .replace("__PARSER_SHA256__", hashlib.sha256(parser).hexdigest())
            .replace("__QR_WASM__", base64.b64encode(qr).decode())
            .replace("__ADDRESS_SHA256__", hashlib.sha256(address).hexdigest())
            .replace("__QR_SHA256__", hashlib.sha256(qr).hexdigest())
            .replace("__JSQR_JS__", jsqr)
            .replace("__PARSER_HOST_JS__", host))
open(OUT, "w").write(html)
print(f"{OUT}: {len(html) / 1024:.0f}KB "
      f"(parser.wasm {len(parser)}B, address.wasm {len(address)}B, qr.wasm {len(qr)}B)")
print(f"parser.wasm sha256 {hashlib.sha256(parser).hexdigest()}")
