"""Extract the captured reflection shader from the local Steam shader archive."""
from pathlib import Path
import struct
import zlib

data = Path('build/analysis/shader_unpacked.dat').read_bytes()
magic, count, offsets, _, names, sizes = struct.unpack_from('<4s5I', data)
assert magic == b'DAT\0'
width = struct.unpack_from('<I', data, names)[0]
for i in range(count):
    name = data[names+4+i*width:names+4+(i+1)*width].split(b'\0')[0].decode()
    if name != 'espshadershimmer.pso':
        continue
    offset = struct.unpack_from('<I', data, offsets+4*i)[0]
    size = struct.unpack_from('<I', data, sizes+4*i)[0]
    shader = data[offset:offset+size]
    assert (zlib.crc32(shader), size) == (0xDD5F8DF1, 476)
    destination = Path('tests/fixtures/practice_reflection')
    destination.mkdir(parents=True, exist_ok=True)
    (destination/name).write_bytes(shader)
    print(f'{name}: {size} bytes, CRC32 {zlib.crc32(shader):08X}')
    break
else:
    raise RuntimeError('Reflection shader not found')
