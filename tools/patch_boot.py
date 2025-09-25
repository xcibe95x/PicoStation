#!/usr/bin/env python3
from pathlib import Path

UF2_PATH = Path("../build/picostation_pico1.uf2")

PATCHES = {
    0x1000003C: bytes([0x04]),
    0x1000011C: bytes([0x07, 0x0B, 0x8F, 0xD5]),
}

BLOCK_SIZE = 512
HEADER_SIZE = 32
PAYLOAD_SIZE = 476
ADDR_OFFSET = 12  # target address in header

def patch_uf2(path: Path):
    data = bytearray(path.read_bytes())
    patched = 0
    skipped = 0

    for i in range(0, len(data), BLOCK_SIZE):
        block = data[i:i+BLOCK_SIZE]
        target_addr = int.from_bytes(block[ADDR_OFFSET:ADDR_OFFSET+4], "little")
        for addr, new_bytes in PATCHES.items():
            if target_addr <= addr < target_addr + PAYLOAD_SIZE:
                offset_in_payload = addr - target_addr
                file_offset = i + HEADER_SIZE + offset_in_payload
                old_bytes = data[file_offset:file_offset+len(new_bytes)]

                if old_bytes == new_bytes:
                    print(f"[patch_uf2] {addr:#x}: already patched ({old_bytes.hex()})")
                    skipped += 1
                else:
                    print(f"[patch_uf2] {addr:#x}: {old_bytes.hex()} -> {new_bytes.hex()}")
                    data[file_offset:file_offset+len(new_bytes)] = new_bytes
                    patched += 1

    if patched:
        path.write_bytes(data)
        print(f"[patch_uf2] Patched {patched} location(s) in {path}")
    elif skipped:
        print(f"[patch_uf2] No changes needed — {skipped} location(s) already patched.")
    else:
        print(f"[patch_uf2] WARNING: No matching addresses found!")

if __name__ == "__main__":
    if not UF2_PATH.exists():
        raise SystemExit(f"[patch_uf2] File not found: {UF2_PATH}")
    patch_uf2(UF2_PATH)
