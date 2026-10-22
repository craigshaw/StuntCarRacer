"""Read the original ADF; write reproducible metadata and boot-block derivatives."""
import hashlib
import json
from pathlib import Path
import re
import struct

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Stunt Car Racer (MicroStyle).adf"
EXPECTED_SHA256 = "548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06"
OUT = ROOT / "analysis" / "disk"


def main():
    data = SOURCE.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256 or len(data) != 901120:
        raise SystemExit("Source ADF differs from the recorded original; inspect before proceeding.")
    boot = data[:1024]
    checksum = 0
    for value in struct.unpack(">256I", boot):
        checksum += value
        checksum = (checksum & 0xFFFFFFFF) + (checksum >> 32)
    root_index = struct.unpack_from(">I", boot, 8)[0]
    root = data[root_index * 512:(root_index + 1) * 512]
    words = struct.unpack(">128I", root) if len(root) == 512 else None
    root_valid = bool(words and words[0] == 2 and words[-1] == 1
                      and sum(words) & 0xFFFFFFFF == 0)
    report = {
        "source": SOURCE.name,
        "sha256": digest,
        "size_bytes": len(data),
        "geometry": {"cylinders": 80, "heads": 2, "sectors_per_track": 11, "sector_bytes": 512},
        "boot_signature_hex": boot[:4].hex(),
        "boot_checksum_valid": checksum == 0xFFFFFFFF,
        "declared_root_block": root_index,
        "root_block_matches_amigados_structure": root_valid,
        "boot_strings": [{"file_offset": m.start(), "text": m.group().decode("ascii")}
                         for m in re.finditer(rb"[ -~]{8,}", boot)],
        "interpretation": "Custom disk layout suspected; DOS signature alone does not establish an AmigaDOS filesystem.",
        "disassembly_note": "Linear MC68000 decoding from boot offset 12; addresses are file offsets, not observed runtime addresses. Embedded data may decode as instructions.",
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (OUT / "bootblock.bin").write_bytes(boot)
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    listing = ["; File offsets only. Linear decode can include embedded data."]
    for ins in decoder.disasm(boot[12:], 12):
        listing.append(f"{ins.address:06x}: {ins.bytes.hex():<24} {ins.mnemonic:<12} {ins.op_str}")
    (OUT / "bootblock.asm").write_text("\n".join(listing) + "\n", encoding="utf-8")
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != digest:
        raise SystemExit("Source changed during inspection.")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
