import math
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILYID_PRESENT = 0x00002000
UF2_FAMILY_SAMD21 = 0x68ED2B88
PAYLOAD_SIZE = 256


def build_block(target_addr: int, payload: bytes, block_no: int, num_blocks: int) -> bytes:
    payload = payload.ljust(PAYLOAD_SIZE, b"\xFF")
    header = struct.pack(
        "<IIIIIIII",
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        UF2_FLAG_FAMILYID_PRESENT,
        target_addr,
        PAYLOAD_SIZE,
        block_no,
        num_blocks,
        UF2_FAMILY_SAMD21,
    )
    data = payload + bytes(476 - PAYLOAD_SIZE)
    return header + data + struct.pack("<I", UF2_MAGIC_END)


def load_blocks(bin_path: str, base_addr: int):
    with open(bin_path, "rb") as f:
        data = f.read()

    blocks = []
    count = int(math.ceil(len(data) / PAYLOAD_SIZE))
    for i in range(count):
        chunk = data[i * PAYLOAD_SIZE:(i + 1) * PAYLOAD_SIZE]
        blocks.append((base_addr + i * PAYLOAD_SIZE, chunk))
    return blocks


def main() -> int:
    if len(sys.argv) != 6:
        print("usage: make_dual_update.py <bin1> <base1> <bin2> <base2> <output.uf2>")
        return 1

    bin1, base1, bin2, base2, out_path = sys.argv[1:6]
    base1 = int(base1, 0)
    base2 = int(base2, 0)

    entries = load_blocks(bin1, base1) + load_blocks(bin2, base2)
    num_blocks = len(entries)

    out = bytearray()
    for i, (addr, payload) in enumerate(entries):
        out.extend(build_block(addr, payload, i, num_blocks))

    with open(out_path, "wb") as f:
        f.write(out)

    print(f"Wrote {len(out)} bytes to {out_path}")
    print(f"Legacy bridge at : 0x{base1:08X}")
    print(f"Main updater at  : 0x{base2:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
