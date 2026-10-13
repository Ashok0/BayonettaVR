"""Label a captured native scene trace with original Steam shader names."""
from pathlib import Path
import collections
import re
import struct
import sys
import zlib

data=Path('build/analysis/shader_unpacked.dat').read_bytes()
_,count,offsets,_,names,sizes=struct.unpack_from('<4s5I',data)
width=struct.unpack_from('<I',data,names)[0]
shaders={}
for i in range(count):
    offset=struct.unpack_from('<I',data,offsets+4*i)[0]
    size=struct.unpack_from('<I',data,sizes+4*i)[0]
    name=data[names+4+i*width:names+4+(i+1)*width].split(b'\0')[0].decode()
    shaders[f'{zlib.crc32(data[offset:offset+size]):08X}']=name
root=Path(sys.argv[1])
trace=(root/'draws.txt').read_text()
draws=re.split(r'(?=^draw=)',trace,flags=re.M)[1:]
counts=collections.Counter()
last=None
lines=[]
for block in draws:
    first=block.splitlines()[0]
    values=dict(re.findall(r'(\w+)=([^ ]+)',first))
    ps=values['ps'];vs=values['vs'];counts[ps]+=1
    key=(vs,ps,values['rt'],values['blend'],values['depth'])
    if key!=last:
        lines.append(f"{first}\n  {shaders.get(vs,vs)} / {shaders.get(ps,ps)}")
        last=key
    if list(root.glob(f"draw{int(values['draw']):04d}_scene.bmp")):
        lines.append(f"  scene checkpoint BEFORE draw {values['draw']}")
out='Shader draw counts:\n'+'\n'.join(f'{n:4} {crc} {shaders.get(crc,crc)}' for crc,n in counts.items())
out+='\n\nPass transitions:\n'+'\n'.join(lines)+'\n'
(root/'summary.txt').write_text(out)
print(out)
