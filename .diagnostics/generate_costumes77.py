from pathlib import Path
import sys,struct,json,collections
sys.path.insert(0,'.diagnostics')
from inspect_costumes77 import meshes
root=Path('.diagnostics/costumes77')
def fnv(names):
    h=14695981039346656037
    for name in names:
        for b in name.encode()+b'\0':h=((h^b)*1099511628211)&0xffffffffffffffff
    return h
profiles=[]
for path in sorted(root.glob('pl*.wmb')):
    d=path.read_bytes();ms=meshes(d)
    if len(ms)<30 or not ms[0]['name'].startswith('body'):continue
    u=lambda p:struct.unpack_from('<I',d,p)[0]
    parents=struct.unpack_from('<'+'h'*u(48),d,u(52))
    def head(b):
        for _ in parents:
            if b==4:return True
            if b<0:return False
            b=parents[b]
        raise ValueError('bone cycle')
    headbones=[head(i) for i in range(len(parents))]
    vo=u(24);stride=(u(28)-vo)//u(12)
    assert stride==32
    rules=[];hide=set()
    for meshno,mesh in enumerate(ms):
        p=mesh['offset'];num=struct.unpack_from('<H',d,p+2)[0];bt=p+u(p+8)
        mesh_rules=[];mesh_kept=mesh_removed=0
        for j in range(num):
            b=bt+u(bt+4*j);vmin,vmax,kind,io,ic=struct.unpack_from('<5I',d,b+12)
            assert kind in (4,5)
            indices=struct.unpack_from('<'+'H'*ic,d,b+io)
            palette=d[b+68:b+68+u(b+64)]
            def ishead(i):
                ids=d[vo+i*32+24:vo+i*32+28];weights=d[vo+i*32+28:vo+i*32+32]
                return sum(w for v,w in zip(ids,weights) if w and headbones[palette[v]])>=128
            veil=meshno==0 and ((path.stem=='pl0089' and j in (4,5)) or (path.stem=='pl0074' and j in (3,4)))
            face=meshno in (7,8,9,10,11,12,13,27,28,29) or (meshno==6 and path.stem!='pl0075')
            flags=[];removed=kept=0
            step=1 if kind==5 else 3
            for k in range(0,ic-2,step):
                tri=indices[k:k+3];degenerate=len(set(tri))<3
                hidden=veil or face or any(ishead(i) for i in tri)
                flags.append(not hidden or degenerate)
                if not degenerate:
                    if hidden:removed+=1
                    else:kept+=1
            mesh_kept+=kept;mesh_removed+=removed
            if not removed:continue
            ranges=[];k=0
            while k<len(flags):
                if not flags[k]:k+=1;continue
                end=k+1
                while end<len(flags) and flags[end]:end+=1
                if any(len(set(indices[t*step:t*step+3]))==3 for t in range(k,end)):
                    ranges.append([k*step,end-k])
                k=end
            mesh_rules.append(dict(mesh=meshno,mesh_batches=num,batch=j,vmin=vmin,vmax=vmax,kind=kind,indices=ic,ranges=ranges,removed=removed,kept=kept))
        if mesh_removed and not mesh_kept:hide.add(meshno)
        else:rules+=mesh_rules
    names=[m['name'] for m in ms]
    profiles.append(dict(model=path.stem,hash=fnv(names),names=names,vertices=u(12),polygons=u(92),shaders=u(96),mesh_data=u(88),mask=sum(1<<i for i in hide),rules=rules))
out=root/'profiles.json';out.write_text(json.dumps(profiles,indent=2))
print('models',len(profiles),'body rules',sum(len(p['rules']) for p in profiles),'ranges',sum(len(r['ranges']) for p in profiles for r in p['rules']))
for p in profiles:
 print(p['model'],[(r['batch'],r['removed'],r['kept'],len(r['ranges'])) for r in p['rules']])

lines=['// Generated from installed Steam WMB topology; see tools/generate_first_person_costumes.py.',
       '#pragma once','#include <cstdint>','namespace bvr {',
       'struct FirstPersonDrawRange { unsigned offset, primitives; };',
       'struct FirstPersonBodyRule { unsigned mesh, meshBatches, batch, vertexMin, vertexMax, primitiveType, indexCount, rangeStart, rangeCount; };',
       'struct FirstPersonCostume { uint64_t namesHash, headMask; unsigned meshCount, vertices, polygons, shaders, meshData, ruleStart, ruleCount; const char* model; };',
       'inline constexpr FirstPersonDrawRange kFirstPersonRanges[]={']
ri=bi=0;rs=[];ps=[]
for p in profiles:
    for r in p['rules']:
        for a,n in r['ranges']:lines.append(f'    {{{a},{n}}},')
        rs.append('    {%d,%d,%d,%d,%d,%d,%d,%d,%d},'%(r['mesh'],r['mesh_batches'],r['batch'],r['vmin'],r['vmax'],r['kind'],r['indices'],ri,len(r['ranges'])))
        ri+=len(r['ranges'])
    ps.append('    {0x%016Xull,0x%016Xull,%d,%d,%d,%d,%d,%d,%d,"%s"},'%(p['hash'],p['mask'],len(p['names']),p['vertices'],p['polygons'],p['shaders'],p['mesh_data'],bi,len(p['rules']),p['model']))
    bi+=len(p['rules'])
lines+=['};','inline constexpr FirstPersonBodyRule kFirstPersonBodyRules[]={']+rs+['};','inline constexpr FirstPersonCostume kFirstPersonCostumes[]={']+ps+['};','}']
Path('src/FirstPersonCostumes.h').write_text('\n'.join(lines)+'\n')
