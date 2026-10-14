"""Verify and extract captured camera-dependent Steam vertex shaders."""
from pathlib import Path
import re
import struct
import zlib

data = Path('build/analysis/shader_unpacked.dat').read_bytes()
magic, count, offsets, _, names, sizes = struct.unpack_from('<4s5I', data)
assert magic == b'DAT\0'
expected = {int(crc, 16): int(size) for crc, size in re.findall(
    r'\{0x([0-9A-F]+)u,(\d+)\}', Path('src/ReflectionCamera.h').read_text())}
root = Path('tests/fixtures/reflection_camera')
root.mkdir(parents=True, exist_ok=True)
seen = set()
for i in range(count):
    offset = struct.unpack_from('<I', data, offsets + 4*i)[0]
    size = struct.unpack_from('<I', data, sizes + 4*i)[0]
    shader = data[offset:offset+size]
    crc = zlib.crc32(shader)
    if crc not in expected or crc in seen:
        continue
    assert size == expected[crc]
    (root / f'{crc:08X}.vso').write_bytes(shader)
    seen.add(crc)
assert seen == set(expected)
print(f'Extracted {len(seen)} exact native reflection camera vertex shaders.')
