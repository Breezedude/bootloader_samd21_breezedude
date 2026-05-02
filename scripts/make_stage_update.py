import math
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILYID_PRESENT = 0x00002000
UF2_FAMILY_SAMD21 = 0x68ED2B88
OTA_AB_MAGIC = 0x42544455
OTA_AB_VERSION = 1
OTA_ACTIVE_SLOT = 0
OTA_PENDING_SLOT = 1
OTA_CHECKSUM_XOR = 0xA5A55A5A
OTA_SLOT1_START = 0x00020000
OTA_FLAG_ADDRESS = 0x0003EF00
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
    footer = struct.pack("<I", UF2_MAGIC_END)
    return header + data + footer


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: make_stage_update.py <input-bin> <output-uf2>")
        return 1

    input_bin = sys.argv[1]
    output_uf2 = sys.argv[2]

    with open(input_bin, "rb") as f:
        app = f.read()

    app_blocks = int(math.ceil(len(app) / PAYLOAD_SIZE))
    total_blocks = app_blocks + 1  # final block writes OTA flags

    out = bytearray()

    for i in range(app_blocks):
        chunk = app[i * PAYLOAD_SIZE:(i + 1) * PAYLOAD_SIZE]
        out.extend(build_block(OTA_SLOT1_START + i * PAYLOAD_SIZE, chunk, i, total_blocks))

    checksum = OTA_AB_MAGIC ^ OTA_AB_VERSION ^ OTA_ACTIVE_SLOT ^ OTA_PENDING_SLOT ^ OTA_CHECKSUM_XOR
    flag_payload = struct.pack(
        "<IIIII",
        OTA_AB_MAGIC,
        OTA_AB_VERSION,
        OTA_ACTIVE_SLOT,
        OTA_PENDING_SLOT,
        checksum,
    )
    out.extend(build_block(OTA_FLAG_ADDRESS, flag_payload, app_blocks, total_blocks))

    with open(output_uf2, "wb") as f:
        f.write(out)

    print(f"Wrote {len(out)} bytes to {output_uf2}")
    print(f"Stage target start: 0x{OTA_SLOT1_START:08X}")
    print(f"Flag target row   : 0x{OTA_FLAG_ADDRESS:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
