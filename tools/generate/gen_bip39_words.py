# /// script
# dependencies = ["embit"]
# ///
"""BIP39（英語）の単語表を C の配列にする。SeedQR の数字から単語を引くために実機へ持つ。
使い方: uv run tools/generate/gen_bip39_words.py build/bip39_words.h

単語表は BIP39 の仕様そのものなので、公式の SHA-256 と照合してから書き出す。"""
import hashlib
import sys

from embit.wordlists.bip39 import WORDLIST

# https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt の SHA-256
OFFICIAL = "2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda"

assert len(WORDLIST) == 2048, len(WORDLIST)
digest = hashlib.sha256(("\n".join(WORDLIST) + "\n").encode()).hexdigest()
assert digest == OFFICIAL, f"wordlist mismatch: {digest}"
assert max(len(w) for w in WORDLIST) == 8

with open(sys.argv[1], "w") as f:
    f.write("/* tools/generate/gen_bip39_words.py が生成。BIP39 英語の単語表（公式の SHA-256 と照合済み） */\n")
    f.write("static const char bip39_words[2048][9] = {\n")
    for i in range(0, 2048, 8):
        f.write("    " + " ".join(f'"{w}",' for w in WORDLIST[i:i + 8]) + "\n")
    f.write("};\n")
print(f"{sys.argv[1]}: 2048 words, sha256 {digest[:16]}...")
