"""Root-only: three 1280x720 CPU geometry PNGs, <12 MiB plus receipt."""
import argparse, json, math, struct, zlib
from pathlib import Path
import measure_mp443 as m
from bc2_mesh_geometry import position_bytes
WIDTH,HEIGHT=1280,720
TIMES=(0.,3.092190252495596,3.1333333333333333)
COLORS={'jntWpn_3':(50,225,245),'jntWpn_12':(240,195,55),'jntWpn_13':(255,160,35)}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--admitted-by-root',action='store_true',required=True);args=ap.parse_args()
    dest=args.output.resolve();allowed=(m.ROOT/'reports/coordinator-20261030-implementation17/pistol-geometry').resolve()
    if dest!=allowed:raise ValueError('Only assigned scratch directory allowed')
    if dest.exists() and any(dest.iterdir()):raise ValueError('Preserve prior render; assigned output must be empty')
    data=m.load();sk,clip,lod,raw,sections=(data[k] for k in ('sk','clip','lod','raw','skin'))
    proof=json.loads((m.HERE/'mp443-geometry.json').read_text(encoding='utf-8'))
    assert all(m.sha(v)==proof['resources'][n]['sha256'] for n,v in raw.items())
    known={m.bone_hash(n):n for n in sk.names}
    inverse={n:m.canonical(v['InverseWorldTransform']) for n,v in zip(sk.names,sk.bones)}
    vb=sum(s['vertices']*s['stride'] for s in sections);ib=sum(s['triangles']*6 for s in sections);start=len(raw[m.LOD])-vb-ib
    vertices=[];triangles=[];weighted=set()
    for s in sections:
        if s['name'].endswith('_ZOnly'):continue
        offset=len(vertices);posbytes=position_bytes(s)
        for i in range(s['vertices']):
            at=start+s['vertex_offset']+i*s['stride'];p=struct.unpack_from('<3e' if posbytes==8 else '<3f',raw[m.LOD],at)
            ids=raw[m.LOD][at+posbytes:at+posbytes+4];weights=raw[m.LOD][at+posbytes+4:at+posbytes+8]
            slot,=[k for k,w in enumerate(weights) if w];assert weights[slot]==255
            owner=known[lod['palette'][s['palette'][ids[slot]]]];weighted.add(owner)
            vertices.append((m.mag.point([p[0],p[1],-p[2]],inverse[owner]),owner))
        indices=struct.unpack_from('<'+'H'*(s['triangles']*3),raw[m.LOD],start+vb+s['first_index']*2)
        for i in range(0,len(indices),3):
            ids=tuple(offset+k for k in indices[i:i+3]);owners={vertices[k][1] for k in ids}
            assert len(owners)==1;triangles.append((ids,next(iter(owners))))
    assert len(triangles)==sum(r['visible_triangles'] for r in proof['parts'])
    dest.mkdir(parents=True,exist_ok=True);receipts=[]
    for number,t in enumerate(TIMES):
        pose=clip.evaluate(sk,t,sorted(weighted|{'LeftHand',*m.mag.FINGERS}))['weapon_relative']
        world=[m.mag.point(p,pose[owner]) for p,owner in vertices]
        pixels=bytearray((19,23,31)*(WIDTH*HEIGHT));depth=[-float('inf')]*(WIDTH*HEIGHT)
        def put(x,y,color):
            if 0<=x<WIDTH and 0<=y<HEIGHT:pixels[(y*WIDTH+x)*3:(y*WIDTH+x)*3+3]=bytes(color)
        def dot(p,color,radius=3):
            for dy in range(-radius,radius+1):
                for dx in range(-radius,radius+1):
                    if dx*dx+dy*dy<=radius*radius:put(round(p[0])+dx,round(p[1])+dy,color)
        def line(a,b,color):
            count=max(1,int(max(abs(a[0]-b[0]),abs(a[1]-b[1]))))
            for step in range(count+1):dot((a[0]+(b[0]-a[0])*step/count,a[1]+(b[1]-a[1])*step/count),color,1)
        for panel,view in enumerate(((1.,.45,.2),(-1.,.45,.2))):
            camera=m.mag.unit(view);right=m.mag.unit(m.mag.cross((0,1,0),camera));up=m.mag.cross(camera,right)
            basis=lambda p:(m.mag.dot(p,right),m.mag.dot(p,up),m.mag.dot(p,camera))
            projected=[basis(p) for p in world];landmarks=[pose[n][12:15] for n in ['LeftHand',*m.mag.FINGERS]]
            points=projected+[basis(p) for p in landmarks]
            lo=[min(p[k] for p in points) for k in range(2)];hi=[max(p[k] for p in points) for k in range(2)]
            factor=min(590/(hi[0]-lo[0]),610/(hi[1]-lo[1]));cx=panel*640+320;cy=345
            def screen(p):return (cx+(p[0]-(lo[0]+hi[0])/2)*factor,cy-(p[1]-(lo[1]+hi[1])/2)*factor,p[2])
            sp=[screen(p) for p in projected]
            for ids,owner in triangles:
                A,B,C=[sp[k] for k in ids];den=(B[1]-C[1])*(A[0]-C[0])+(C[0]-B[0])*(A[1]-C[1])
                if abs(den)<1e-9:continue
                lx=max(panel*640+10,int(min(A[0],B[0],C[0])));hx=min(panel*640+629,math.ceil(max(A[0],B[0],C[0])))
                ly=max(20,int(min(A[1],B[1],C[1])));hy=min(665,math.ceil(max(A[1],B[1],C[1])))
                normal=m.mag.cross(m.mag.sub(world[ids[1]],world[ids[0]]),m.mag.sub(world[ids[2]],world[ids[0]]));length=m.mag.norm(normal)
                shade=.5+.5*abs(m.mag.dot(normal,camera))/length if length else .65
                color=tuple(round(c*shade) for c in COLORS.get(owner,(125,135,150)))
                for y in range(ly,hy+1):
                    for x in range(lx,hx+1):
                        u=((B[1]-C[1])*(x-C[0])+(C[0]-B[0])*(y-C[1]))/den
                        v=((C[1]-A[1])*(x-C[0])+(A[0]-C[0])*(y-C[1]))/den;w=1-u-v
                        if min(u,v,w)<-1e-8:continue
                        z=u*A[2]+v*B[2]+w*C[2];at=y*WIDTH+x
                        if z>depth[at]:depth[at]=z;put(x,y,color)
            project=lambda p:screen(basis(p))
            wrist=pose['LeftHand'][12:15]
            for digit in ('Thumb','Index','Middle','Ring','Pinky'):
                chain=[wrist]+[pose['LeftHand'+digit+str(k)][12:15] for k in (1,2,3)]
                for first,second in zip(chain,chain[1:]):line(project(first),project(second),(245,245,245))
                for p in chain[1:]:dot(project(p),(245,245,245),2)
            origin=pose[m.SLIDE][12:15];dot(project(origin),(255,60,60),5)
            line(project(origin),project([origin[0],origin[1],origin[2]+.072471334581]),(255,60,60))
            for i,color in enumerate(((50,225,245),(240,195,55),(245,245,245),(255,60,60))):
                for y in range(687,700):
                    for x in range(panel*640+30+i*150,panel*640+130+i*150):put(x,y,color)
        scan=b''.join(b'\0'+pixels[y*WIDTH*3:(y+1)*WIDTH*3] for y in range(HEIGHT))
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',WIDTH,HEIGHT,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan,6))+chunk(b'IEND',b'')
        assert len(png)<4*1024*1024
        output=dest/f'frame{number:02d}.png'
        with output.open('xb') as f:f.write(png)
        receipts.append(dict(file=output.name,time_s=t,width=WIDTH,height=HEIGHT,bytes=len(png),sha256=m.sha(png)))
        print(json.dumps(receipts[-1]),flush=True)
    receipt=dict(evidence_sha256=m.sha((m.HERE/'mp443-geometry.json').read_bytes()),renderer_sha256=m.sha(Path(__file__).read_bytes()),
        visible_triangles=len(triangles),frames=receipts,legend={'cyan':'jntWpn_3','yellow':'jntWpn_12','orange':'jntWpn_13','white':'left hand joints','red':'jntWpn_3 origin and +Z rail'},native_playback=False,assets_exported=False)
    with (dest/'render-receipt.json').open('x',encoding='utf-8') as f:json.dump(receipt,f,indent=2);f.write('\n')

if __name__=='__main__':main()
