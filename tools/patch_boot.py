#!/usr/bin/env python3
from pathlib import Path

UF2_PATH = Path("../build/picostation_pico1.uf2")
PATCHES = {
    0x3C: bytes([0x04]),
    0x11C: bytes([0x07, 0x0B, 0x8F, 0xD5]),
}

def patch_file(path: Path):
    data = bytearray(path.read_bytes())
    for offset, new_bytes in PATCHES.items():
        old = data[offset:offset+len(new_bytes)]
        if old == new_bytes:
            print(f"[patch_uf2] 0x{offset:X}: already patched ({old.hex()})")
        else:
            print(f"[patch_uf2] 0x{offset:X}: {old.hex()} -> {new_bytes.hex()}")
            data[offset:offset+len(new_bytes)] = new_bytes
    path.write_bytes(data)
    print(f"[patch_uf2] Patched {path}")

if __name__ == "__main__":
    if not UF2_PATH.exists():
        raise SystemExit(f"[patch_uf2] File not found: {UF2_PATH}")
    patch_file(UF2_PATH)
