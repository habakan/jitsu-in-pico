# /// script
# dependencies = []
# ///
"""PSBT ビューアを単一 HTML にまとめる。wasm を base64 で埋め込むので、file:// でもオフラインで動く。
使い方: uv run tools/build_viewer.py build/viewer.html

埋め込む parser.wasm は実機に積んでいるものと同じバイト列。ハッシュをページに出して突き合わせられる。"""
import base64
import hashlib
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "build/viewer.html"
parser = open("build/parser.wasm", "rb").read()
address = open("build/address.wasm", "rb").read()
page = open("web/viewer.html").read()

html = (page.replace("__PARSER_WASM__", base64.b64encode(parser).decode())
            .replace("__ADDRESS_WASM__", base64.b64encode(address).decode())
            .replace("__PARSER_SHA256__", hashlib.sha256(parser).hexdigest())
            .replace("__ADDRESS_SHA256__", hashlib.sha256(address).hexdigest()))
open(OUT, "w").write(html)
print(f"{OUT}: {len(html) / 1024:.0f}KB (parser.wasm {len(parser)}B, address.wasm {len(address)}B)")
print(f"parser.wasm sha256 {hashlib.sha256(parser).hexdigest()}")
