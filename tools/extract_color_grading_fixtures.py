"""Extract the exact two grading shaders from the unpacked Steam shader.dat."""
from pathlib import Path
import struct
import zlib

data=Path('build/analysis/shader_unpacked.dat').read_bytes()
magic,count,offsets,extensions,names,sizes=struct.unpack_from('<4s5I',data)
assert magic==b'DAT\0'
width=struct.unpack_from('<I',data,names)[0]
wanted={'filter00.pso':(0x6E722DE9,760),'filter04.pso':(0x373A4BB7,500)}
destination=Path('tests/fixtures/color_grading')
destination.mkdir(parents=True,exist_ok=True)
for i in range(count):
    name=data[names+4+i*width:names+4+(i+1)*width].split(b'\0')[0].decode()
    if name not in wanted:continue
    offset=struct.unpack_from('<I',data,offsets+4*i)[0]
    size=struct.unpack_from('<I',data,sizes+4*i)[0]
    shader=data[offset:offset+size]
    assert (zlib.crc32(shader),size)==wanted.pop(name)
    (destination/name).write_bytes(shader)
    print(f'{name}: {size} bytes CRC32 {zlib.crc32(shader):08X}')
assert not wanted
