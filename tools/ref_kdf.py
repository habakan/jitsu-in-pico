# /// script
# dependencies = ["embit"]
# ///
"""Independently calculate the BIP39 and BIP84 expected values in tests/host/vectors.h."""
import hashlib
from embit import bip32
m12 = " ".join(["abandon"] * 11 + ["about"]); m24 = " ".join(["abandon"] * 23 + ["art"])
s = lambda m, p: hashlib.pbkdf2_hmac("sha512", m.encode(), b"mnemonic" + p.encode(), 2048)
print("seed12", s(m12, "TREZOR").hex()); print("seed24", s(m24, "TREZOR").hex())
print("bip84_pub", bip32.HDKey.from_seed(s(m12, "")).derive("m/84h/0h/0h/0/0").get_public_key().sec().hex())
