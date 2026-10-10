"""Read-only verification of build .36's effect patches against the Steam PE."""
import pathlib
import re
import struct
import sys
import pefile

pe = pefile.PE(sys.argv[1])
assert pe.FILE_HEADER.TimeDateStamp == 0x591AD330, "unsupported executable"
source = (pathlib.Path(__file__).resolve().parents[1] / "src/D3D9Hooks.cpp").read_text()
sites_text = source.split("kEffectClearSites[] = {", 1)[1].split("};", 1)[0]
sites = [(int(a, 16), int(b, 16)) for a, b in
         re.findall(r"\{(0x[0-9A-F]+),(0x[0-9A-F]+)\}", sites_text)]
assert len(sites) == 21 and len(set(a for a, _ in sites)) == 21
reset_text = source.split("resetCode[] = {", 1)[1].split("};", 1)[0]
reset = bytes(int(x, 16) for x in re.findall(r"0x[0-9A-F]+", reset_text))
assert pe.get_data(0x85F7A0, len(reset)) == reset, "reset function signature mismatch"
for rva, opcode in sites:
    data = pe.get_data(rva, 5)
    assert data[0] == opcode, f"opcode mismatch at {rva:X}"
    assert rva + 5 + struct.unpack_from("<i", data, 1)[0] == 0x85F7A0, f"target mismatch at {rva:X}"
    # Relocation-independent, for a module at either the preferred or another base.
    for base in (pe.OPTIONAL_HEADER.ImageBase, 0x10000000):
        assert base + rva + 5 + struct.unpack_from("<i", data, 1)[0] == base + 0x85F7A0
print(f"effect hook signatures: {len(sites)} sites verified ({sum(b == 0xE9 for _, b in sites)} tail jumps), reset body verified")
