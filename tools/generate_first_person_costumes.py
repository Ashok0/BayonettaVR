"""Generate costume masks from extracted Steam WMB skin weights and topology.
Head triangles touch a vertex with >=128/255 weight on head bone 4 or its
descendants. Verified face/hair slots are hidden whole; nun veil batches are
hidden whole even where their drape is weighted to shoulders and torso.
Mixed batches retain original index order and winding. No assets are edited.
Usage: python tools/generate_first_person_costumes.py [extracted-wmb-directory]
"""
from pathlib import Path
import sys,struct,json,collections
from extract_first_person_costumes import meshes
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/analysis/first_person_costumes')
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

# Capture original geometry and an independent triangle-list reference for
# GPU regression. The reference preserves all non-head triangles, including
# the Witch hat's shared waist trim, and removes only classified triangles.
import hashlib
fixtures=Path('tests/fixtures/firstperson_costumes');fixtures.mkdir(parents=True,exist_ok=True)
names=['// Ordered names extracted from installed Steam costume WMBs.','#pragma once','static const char* const kCostumeFixtureNames[][64]={']
for p in profiles:names.append('    {'+','.join(json.dumps(n) for n in p['names'])+'}, // '+p['model'])
Path('tests/FirstPersonCostumeFixture.h').write_text('\n'.join(names+['};'])+'\n')
cases=[('pl008a',0,2),('pl008a',0,0),('pl0089',0,4),('pl0089',0,6),('pl002c',0,1),('pl0030',1,1),('pl0075',0,7),('pl0064',1,1)]
binary=bytearray(b'FPC7'+struct.pack('<I',len(cases)))
for model,meshno,batchno in cases:
    pn=next(i for i,p in enumerate(profiles) if p['model']==model);p=profiles[pn]
    rn=next(i for i,r in enumerate(p['rules']) if r['mesh']==meshno and r['batch']==batchno);r=p['rules'][rn]
    d=(root/(model+'.wmb')).read_bytes();u=lambda o:struct.unpack_from('<I',d,o)[0]
    m=meshes(d)[meshno]['offset'];bt=m+u(m+8);b=bt+u(bt+4*batchno)
    original=list(struct.unpack_from('<'+'H'*r['indices'],d,b+u(b+24)))
    palette=d[b+68:b+68+u(b+64)];parents=struct.unpack_from('<'+'h'*u(48),d,u(52))
    descendants={4}
    for i in range(len(parents)):
        k=i
        while k>=0 and k not in descendants:k=parents[k]
        if k in descendants:descendants.add(i)
    vo=u(24);unique=sorted(set(original));mapping={v:i for i,v in enumerate(unique)}
    reference=[];step=1 if r['kind']==5 else 3
    for t in range(0,len(original)-2,step):
        tri=original[t:t+3]
        if len(set(tri))<3:continue
        weights=[]
        for v in tri:
            ids=d[vo+32*v+24:vo+32*v+28];ws=d[vo+32*v+28:vo+32*v+32]
            weights.append(sum(w for bone,w in zip(ids,ws) if w and palette[bone] in descendants))
        if max(weights)>=128 or (model=='pl0089' and meshno==0 and batchno==4):continue
        if r['kind']==5 and t&1:tri=[tri[1],tri[0],tri[2]]
        reference+=tri
    assert len(reference)//3==r['kept']
    binary+=struct.pack('<5I',pn,rn,len(unique),len(original),len(reference))
    for v in unique:binary+=d[vo+32*v:vo+32*v+12]
    binary+=struct.pack('<'+'H'*len(original),*(mapping[v] for v in original))
    binary+=struct.pack('<'+'H'*len(reference),*(mapping[v] for v in reference))
(fixtures/'geometry.bin').write_bytes(binary)
provenance=[dict(model=p['model'],sha256=hashlib.sha256((root/(p['model']+'.wmb')).read_bytes()).hexdigest(),meshes=len(p['names']),vertices=p['vertices'],hidden_meshes=[n for i,n in enumerate(p['names']) if p['mask']>>i&1]) for p in profiles]
(fixtures/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
# Equal runtime signatures must always produce identical rules.
seen={}
for p in profiles:
    key=(p['hash'],p['vertices'],p['polygons'],p['shaders'],p['mesh_data'])
    if key in seen:assert (p['mask'],p['rules'])==(seen[key]['mask'],seen[key]['rules'])
    seen[key]=p
