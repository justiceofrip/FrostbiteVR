"""Root-only bounded offline diagnostic: five PNGs, no mesh/clip export or GPU."""
import argparse, json, math, struct, zlib
from pathlib import Path
import measure_geometry as m
from bc2_mesh_geometry import position_bytes

WIDTH, HEIGHT = 1280, 720
TIMES = (0., .8333333333, 2.1, 4.1333333333, 6.730160063)
COLORS = {'jntWpn_9':(245,207,60),'jntWpn_4':(45,220,235),'jntWpn_6':(224,85,202),
          'jntWpn_12':(255,114,45),'jntWpn_13':(255,155,45),
          'jntWpnwpnJnt_14':(160,225,62),'jntWpn_15':(70,200,90)}
FONT = {
'0':'111/101/101/101/111','1':'010/110/010/010/111','2':'111/001/111/100/111',
'3':'111/001/111/001/111','4':'101/101/111/001/001','5':'111/100/111/001/111',
'6':'111/100/111/101/111','7':'111/001/010/010/010','8':'111/101/111/101/111',
'9':'111/101/111/001/111','A':'010/101/111/101/101','B':'110/101/110/101/110',
'C':'111/100/100/100/111','D':'110/101/101/101/110','E':'111/100/110/100/111',
'F':'111/100/110/100/100','G':'111/100/101/101/111','H':'101/101/111/101/101',
'I':'111/010/010/010/111','J':'001/001/001/101/111','K':'101/101/110/101/101',
'L':'100/100/100/100/111','M':'101/111/111/101/101','N':'101/111/111/111/101',
'O':'111/101/101/101/111','P':'111/101/111/100/100','Q':'111/101/101/111/001',
'R':'110/101/110/101/101','S':'111/100/111/001/111','T':'111/010/010/010/010',
'U':'101/101/101/101/111','V':'101/101/101/101/010','W':'101/101/111/111/101',
'X':'101/101/010/101/101','Y':'101/101/010/010/010','Z':'111/001/010/100/111',
'-':'000/000/111/000/000','.':'000/000/000/000/010',':':'000/010/000/010/000',
'+':'000/010/111/010/000','/':'001/001/010/100/100',' ':'000/000/000/000/000'}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--admitted-by-root',action='store_true',required=True)
    args=ap.parse_args()
    allowed=(m.ROOT/'reports/coordinator-20261030-implementation15/m60-geometry').resolve()
    dest=args.output.resolve()
    if dest!=allowed: raise ValueError('Only the assigned private report directory is permitted')
    if dest.exists() and any(dest.iterdir()): raise ValueError('Use the empty assigned directory; preserve earlier evidence')
    p=m.profile();a=m.Archive(m.GAME/m.PROOF['archive']);b=m.Archive(m.GAME/m.PROOF['authored_reload_animation']['archive'])
    assert a.index_sha256==m.PROOF['archive_index_sha256']
    assert b.index_sha256==m.PROOF['authored_reload_animation']['archive_index_sha256']
    sr=a.read_selected([m.SKEL])[m.SKEL];raw=b.read_selected([m.MESH,m.LOD,m.CLIP])
    proof=json.loads((m.HERE/'geometry-evidence.json').read_text(encoding='utf-8'))
    assert m.sha(sr)==p['skeleton_sha256']
    assert all(m.sha(v)==proof['resources'][n] for n,v in raw.items())
    sk=m.Skeleton(sr);clip=m.Clip(raw[m.CLIP]);lod,=m.metadata(raw[m.MESH])
    known={m.bone_hash(n):n for n in sk.names};sections=m.geometry(raw[m.LOD],lod,known)
    inverse={n:m.canonical(v['InverseWorldTransform']) for n,v in zip(sk.names,sk.bones)}
    vb=sum(s['vertices']*s['stride'] for s in sections);ib=sum(s['triangles']*6 for s in sections);start=len(raw[m.LOD])-vb-ib
    vertices=[];triangles=[];weighted=set()
    for s in sections:
        if s['name'].endswith('_ZOnly'):continue
        offset=len(vertices);posbytes=position_bytes(s)
        for i in range(s['vertices']):
            at=start+s['vertex_offset']+i*s['stride'];point=struct.unpack_from('<3e' if posbytes==8 else '<3f',raw[m.LOD],at)
            ids=raw[m.LOD][at+posbytes:at+posbytes+4];weights=raw[m.LOD][at+posbytes+4:at+posbytes+8]
            slot,= [k for k,w in enumerate(weights) if w];assert weights[slot]==255
            owner=known[lod['palette'][s['palette'][ids[slot]]]];weighted.add(owner)
            vertices.append((m.mag.point([point[0],point[1],-point[2]],inverse[owner]),owner))
        indices=struct.unpack_from('<'+'H'*(s['triangles']*3),raw[m.LOD],start+vb+s['first_index']*2)
        for i in range(0,len(indices),3):
            ids=tuple(offset+k for k in indices[i:i+3]);owners={vertices[k][1] for k in ids}
            assert len(owners)==1;triangles.append((ids,next(iter(owners))))
    assert len(triangles)==8501 and len(weighted)==14
    dest.mkdir(parents=True,exist_ok=True);receipts=[]
    for number,time in enumerate(TIMES):
        pose=clip.evaluate(sk,time,sorted(weighted|{'LeftHand',*m.mag.FINGERS}))['weapon_relative']
        world=[m.mag.point(point,pose[owner]) for point,owner in vertices]
        pixels=bytearray((19,23,31)*(WIDTH*HEIGHT));depth=[-float('inf')]*(WIDTH*HEIGHT)
        def put(x,y,color):
            if 0<=x<WIDTH and 0<=y<HEIGHT:
                at=(y*WIDTH+x)*3;pixels[at:at+3]=bytes(color)
        def text(x,y,value,color=(235,238,245),size=2):
            for char in value:
                for row,line in enumerate(FONT.get(char.upper(),FONT[' ']).split('/')):
                    for col,bit in enumerate(line):
                        if bit=='1':
                            for dy in range(size):
                                for dx in range(size):put(x+col*size+dx,y+row*size+dy,color)
                x+=4*size
        def line(a,b,color,width=2):
            count=max(1,int(max(abs(a[0]-b[0]),abs(a[1]-b[1]))))
            for step in range(count+1):
                x=round(a[0]+(b[0]-a[0])*step/count);y=round(a[1]+(b[1]-a[1])*step/count)
                for dy in range(-width,width+1):
                    for dx in range(-width,width+1):put(x+dx,y+dy,color)
        def dot(a,color,radius=4):
            for dy in range(-radius,radius+1):
                for dx in range(-radius,radius+1):
                    if dx*dx+dy*dy<=radius*radius:put(round(a[0])+dx,round(a[1])+dy,color)
        text(20,16,f'M60 SP RELOAD  T {time:.3f} S  -  OFFLINE DECODER',size=3)
        text(20,44,'PART COLORS ARE NAMED BONE IDS. WHITE LINES ARE AUTHORED LEFT HAND JOINTS.',size=2)
        for panel,view in enumerate(((1.,.65,.25),(-1.,.65,.25))):
            camera=m.mag.unit(view);right=m.mag.unit(m.mag.cross((0,1,0),camera));up=m.mag.cross(camera,right)
            # Show the receiver region plus detached parts. Exclude front barrel/bipod only.
            keep=[t for t in triangles if any(world[k][2]>-.65 for k in t[0])]
            used={k for ids,_ in keep for k in ids};landmarks=[pose[n][12:15] for n in ['LeftHand',*m.mag.FINGERS]]
            basis=lambda q:(m.mag.dot(q,right),m.mag.dot(q,up),m.mag.dot(q,camera))
            projected=[basis(q) for q in world];points=[projected[k] for k in used]+[basis(q) for q in landmarks]
            lo=[min(q[k] for q in points) for k in range(2)];hi=[max(q[k] for q in points) for k in range(2)]
            factor=min(590/(hi[0]-lo[0]),520/(hi[1]-lo[1]));cx=panel*640+320;cy=340
            def screen(q):return (cx+(q[0]-(lo[0]+hi[0])/2)*factor,cy-(q[1]-(lo[1]+hi[1])/2)*factor,q[2])
            screen_points=[screen(q) for q in projected]
            for ids,owner in keep:
                A,B,C=[screen_points[k] for k in ids];den=(B[1]-C[1])*(A[0]-C[0])+(C[0]-B[0])*(A[1]-C[1])
                if abs(den)<1e-9:continue
                loX=max(panel*640+10,int(min(A[0],B[0],C[0])));hiX=min(panel*640+629,math.ceil(max(A[0],B[0],C[0])))
                loY=max(85,int(min(A[1],B[1],C[1])));hiY=min(615,math.ceil(max(A[1],B[1],C[1])))
                normal=m.mag.cross(m.mag.sub(world[ids[1]],world[ids[0]]),m.mag.sub(world[ids[2]],world[ids[0]]))
                nlen=m.mag.norm(normal);shade=.5+.5*abs(m.mag.dot(normal,camera))/nlen if nlen else .65
                color=tuple(round(c*shade) for c in COLORS.get(owner,(125,135,150)))
                for y in range(loY,hiY+1):
                    for x in range(loX,hiX+1):
                        u=((B[1]-C[1])*(x-C[0])+(C[0]-B[0])*(y-C[1]))/den
                        v=((C[1]-A[1])*(x-C[0])+(A[0]-C[0])*(y-C[1]))/den;w=1-u-v
                        if min(u,v,w)<-1e-8:continue
                        z=u*A[2]+v*B[2]+w*C[2];at=y*WIDTH+x
                        if z>depth[at]:depth[at]=z;put(x,y,color)
            def project(q):return screen(basis(q))
            wrist=pose['LeftHand'][12:15]
            for digit in ('Thumb','Index','Middle','Ring','Pinky'):
                chain=[wrist]+[pose['LeftHand'+digit+str(k)][12:15] for k in (1,2,3)]
                for first,second in zip(chain,chain[1:]):line(project(first),project(second),(245,245,245),1)
                for q in chain[1:]:dot(project(q),(245,245,245),2)
            knuckle=[sum(pose['LeftHand'+d+'1'][12+k] for d in ('Index','Middle','Ring','Pinky'))/4 for k in range(3)]
            dot(project(knuckle),(255,65,65),5)
            for name in COLORS:
                point=project(pose[name][12:15]);dot(point,COLORS[name],3)
                label=name.split('_')[-1];text(round(point[0])+5,round(point[1])-5,label,COLORS[name],2)
            text(panel*640+20,69,'VIEW +X' if panel==0 else 'VIEW -X',size=2)
        text(20,632,'9 HINGED PANEL   4 SLIDER   6 FABRICS PART   12/13/14/15 AMMO CHAIN',size=2)
        x=20
        for name,color in COLORS.items():text(x,656,'ID '+name.split('_')[-1],color,3);x+=170
        text(20,692,'RECEIVER CROP. NO NATIVE PLAYBACK, GRIP, ROLE OR GAMEPLAY ADMISSION.',size=2)
        rows=b''.join(b'\0'+pixels[y*WIDTH*3:(y+1)*WIDTH*3] for y in range(HEIGHT))
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',WIDTH,HEIGHT,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,6))+chunk(b'IEND',b'')
        assert len(png)<4*1024*1024
        output=dest/f'frame{number:02d}.png'
        with output.open('xb') as f:f.write(png)
        receipts.append(dict(file=output.name,time_s=time,width=WIDTH,height=HEIGHT,bytes=len(png),sha256=m.sha(png)))
        print(json.dumps(receipts[-1]),flush=True)
    receipt=dict(source_evidence_sha256=m.sha((m.HERE/'geometry-evidence.json').read_bytes()),
        renderer_sha256=m.sha(Path(__file__).read_bytes()),visible_triangles=8501,frames=receipts,
        native_playback=False,original_assets_exported=False)
    (dest/'render-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
