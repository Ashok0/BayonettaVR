"""Read-only extraction of installed human-costume WMBs for mask generation.
Usage: python tools/extract_first_person_costumes.py [game-data-directory]
"""
from pathlib import Path
import struct, json, sys

ROOT=Path(sys.argv[1]) if __name__=='__main__' and len(sys.argv)>1 else Path(r'C:\Program Files (x86)\Steam\steamapps\common\Bayonetta\data')
OUT=Path('build/analysis/first_person_costumes')
if __name__=='__main__':OUT.mkdir(parents=True,exist_ok=True)

def utf(d):
    assert d[:4]==b'@UTF'
    _,rows,strings,blob,table,cols,width,count=struct.unpack_from('>5I2HI',d,4)
    rows+=8;strings+=8;blob+=8
    def s(o):return d[strings+o:].split(b'\0',1)[0].decode()
    def value(t,p):
        fmt={0:'B',1:'b',2:'H',3:'h',4:'I',5:'i',6:'Q',7:'q',8:'f',10:'I',11:'2I'}[t]
        v=struct.unpack_from('>'+fmt,d,p); n=struct.calcsize('>'+fmt)
        return (s(v[0]) if t==10 else d[blob+v[0]:blob+v[0]+v[1]] if t==11 else v[0]),p+n
    p=32; columns=[]
    for i in range(cols):
        flags=d[p];name=s(struct.unpack_from('>I',d,p+1)[0]);p+=5
        constant=None
        if flags&0xf0==0x30:constant,p=value(flags&15,p)
        columns.append((name,flags,constant))
    results=[]
    for i in range(count):
        p=rows+i*width;r={}
        for name,flags,constant in columns:
            if flags&0xf0==0x50:r[name],p=value(flags&15,p)
            else:r[name]=constant if flags&0xf0==0x30 else 0
        results.append(r)
    return results

def packet(f,o):
    f.seek(o);h=f.read(16);size=struct.unpack_from('<Q',h,8)[0]
    return utf(f.read(size))

def unpack(d):
    if d[:8]!=b'CRILAYLA':return d
    size,header=struct.unpack_from('<II',d,8)
    out=bytearray(size+256);out[:256]=d[header+16:header+272]
    pos=header+15;bits=0;available=0;dst=len(out)-1
    def read(n):
        nonlocal pos,bits,available
        while available<n:
            bits=(bits<<8)|d[pos];pos-=1;available+=8
        available-=n
        v=(bits>>available)&((1<<n)-1)
        bits&=(1<<available)-1
        return v
    while dst>=256:
        if not read(1):out[dst]=read(8);dst-=1;continue
        back=read(13)+3;length=3
        for n in (2,3,5,8):
            v=read(n);length+=v
            if v<(1<<n)-1:break
        else:
            while True:
                v=read(8);length+=v
                if v!=255:break
        assert dst-length+1>=256 and dst+back<len(out)
        for i in range(length):out[dst]=out[dst+back];dst-=1
    return bytes(out)

def entries(d):
    magic,count,offsets,ext,names,sizes=struct.unpack_from('<4s5I',d)
    assert magic==b'DAT\0'
    width=struct.unpack_from('<I',d,names)[0]
    for i in range(count):
        name=d[names+4+i*width:names+4+(i+1)*width].split(b'\0')[0].decode()
        offset=struct.unpack_from('<I',d,offsets+i*4)[0]
        size=struct.unpack_from('<I',d,sizes+i*4)[0]
        yield name,d[offset:offset+size]

def meshes(d):
    assert d[:4]==b'WMB\0'
    count,offsets,base=struct.unpack_from('<3I',d,0x50)
    result=[]
    for i in range(count):
        p=base+struct.unpack_from('<I',d,offsets+4*i)[0]
        name=d[p+32:p+64].split(b'\0')[0].decode()
        result.append(dict(name=name,offset=p,header=d[p:p+32].hex(),bounds=struct.unpack_from('<9f',d,p+64)))
    return result

if __name__=='__main__':
    found=[]
    for path in sorted(ROOT.glob('*.cpk')):
        with path.open('rb') as f:
            h=packet(f,0)[0];toc=packet(f,h['TocOffset']);base=min(h['TocOffset'],h['ContentOffset'])
            matches=[r for r in toc if r['FileName'].startswith('pl') and r['FileName'].endswith('.dat')]
            print(path.name,len(toc),'player files',len(matches))
            for r in matches:
                r.update(archive=path.name,offset=base+r['FileOffset']);found.append(r)
    (OUT/'index.json').write_text(json.dumps(found,indent=2))
    print([(r['FileName'],r['FileSize'],r['ExtractSize']) for r in found])
    models={}
    for r in found:
        if r['archive']!='data12.cpk' and r['FileName'] not in ('pl0020.dat','pl0030.dat'):continue
        with (ROOT/r['archive']).open('rb') as f:
            f.seek(r['offset']);d=unpack(f.read(r['FileSize']))
        assert len(d)==r['ExtractSize']
        for name,wmb in entries(d):
            if name.endswith('.wmb'):
                (OUT/name).write_bytes(wmb)
                models[name]=meshes(wmb)
                print(name,[m['name'] for m in models[name]],flush=True)
    (OUT/'meshes.json').write_text(json.dumps(models,indent=2))
