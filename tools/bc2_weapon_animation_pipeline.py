"""Read authored local tracks and evaluate explicit clip/skeleton pairs offline."""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
import math
import struct
from pathlib import Path
from bc2_granny_resource import Resource
from bc2_granny_curves import decode,IDENTITY

def identity():return [float(n//4==n%4) for n in range(16)]
def finite_matrix(a):
    if len(a)!=16 or not all(math.isfinite(v) for v in a):raise ValueError('Nonfinite matrix')
    return a
def multiply(a,b):
    finite_matrix(a);finite_matrix(b)
    return finite_matrix([sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)])
def transpose(a):return [a[c*4+r] for r in range(4) for c in range(4)]
def inverse_rigid(a):
    finite_matrix(a)
    r=[a[n] for n in (0,1,2,4,5,6,8,9,10)]
    if any(abs(sum(r[i*3+k]*r[j*3+k] for k in range(3))-float(i==j))>2e-4 for i in range(3) for j in range(3)):raise ValueError('Nonrigid evaluated transform')
    result=transpose(a)
    result[3]=result[7]=result[11]=0.
    result[12:15]=[-sum(a[12+k]*result[k*4+c] for k in range(3)) for c in range(3)]
    return finite_matrix(result)
def matrix(position,orientation,scale):
    x,y,z,w=orientation
    n=math.sqrt(x*x+y*y+z*z+w*w)
    if not .95<n<1.05:raise ValueError('Orientation norm')
    x,y,z,w=(v/n for v in orientation)
    # Row-vector transform, matching the stored inverse-world matrices.
    rotation=[1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y),0.,
              2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x),0.,
              2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y),0.,0.,0.,0.,1.]
    scaling=identity()
    for row in range(3):scaling[row*4:row*4+3]=scale[row*3:row*3+3]
    result=multiply(scaling,rotation);result[12:15]=position
    return result
def bind_matrix(value):
    flags=value['flags']
    return matrix(value['position'] if flags&1 else IDENTITY[3],value['orientation'] if flags&2 else IDENTITY[4],value['scale_shear'] if flags&4 else IDENTITY[9])
def canonical(a):
    finite_matrix(a)
    sign=(1.,1.,-1.,1.)
    return [a[r*4+c]*sign[r]*sign[c] for r in range(4) for c in range(4)]

def valid_basis(art):
    return art and art['UnitsPerMeter']==1. and art['Origin']==[0.,0.,0.] and art['RightVector']==[1.,0.,0.] and art['UpVector']==[0.,1.,0.] and art['BackVector']==[0.,0.,1.]

def rig_fingerprint(names,parents,inverse):
    value=14695981039346656037
    for name,parent,transform in zip(names,parents,inverse):
        # Match Bc2Rig canonicalization, including signed zero in the affine column.
        native=list(transform)
        for row in range(4):native[row*4+3]=float(row==3)
        for i in range(4):native[8+i]=-native[8+i];native[4*i+2]=-native[4*i+2]
        data=name.encode('ascii')+b'\0\0\0\0'+struct.pack('<i16f',parent,*native)
        for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return f'fnv1a64:{value:016x}'

class Skeleton:
    def __init__(self,data):
        root=Resource(data,'GrannyModel').read()
        if len(root['Skeletons'])!=1:raise ValueError('Exact single skeleton required')
        self.source=root['Skeletons'][0];self.bones=self.source['Bones'];self.names=[b['Name'] for b in self.bones]
        if not 0<len(self.bones)<=512 or len(set(self.names))!=len(self.names):raise ValueError('Skeleton names/count')
        self.parents=[b['ParentIndex'] for b in self.bones]
        if any(p>=n or p< -1 for n,p in enumerate(self.parents)):raise ValueError('Skeleton parent order')
        art=root['ArtToolInfo']
        if not valid_basis(art):raise ValueError('Unsupported skeleton basis/units')
        self.world=[];errors=[]
        for n,b in enumerate(self.bones):
            local=bind_matrix(b['Transform']);world=multiply(local,self.world[self.parents[n]]) if self.parents[n]>=0 else local
            self.world.append(world)
            inverse=b['InverseWorldTransform'];product=multiply(world,inverse)
            errors.append(max(abs(a-b) for a,b in zip(product,identity())))
        self.inverse_bind_max_error=max(errors)
        if self.inverse_bind_max_error>2e-4:raise ValueError('Bind hierarchy does not reproduce archive inverse matrices')
        self.sha256=hashlib.sha256(data).hexdigest()
        self.fingerprint=rig_fingerprint(self.names,self.parents,[b['InverseWorldTransform'] for b in self.bones])
    def metadata(self):
        return {'resource_sha256':self.sha256,'bone_count':len(self.bones),'rig_fingerprint':self.fingerprint,
                'fingerprint_algorithm':'SightRigFingerprint ordered names/parents/canonical float32 inverse bind',
                'inverse_bind_max_error':self.inverse_bind_max_error,
                'units_per_meter':1.,'archive_basis':'right +X, up +Y, back +Z',
                'bones':[{'name':name,'parent_index':parent} for name,parent in zip(self.names,self.parents)]}

class Clip:
    def __init__(self,data):
        self.root=Resource(data).read_root_fields(('ArtToolInfo','Animations'))
        art=self.root['ArtToolInfo']
        if art is not None and not valid_basis(art):raise ValueError('Unsupported clip basis/units')
        self.basis='explicit_matching_clip_art_metadata' if art else 'not_stored_in_clip; inherited_from_explicitly_paired_skeleton'
        if len(self.root['Animations'])!=1:raise ValueError('Exact single animation required')
        animation=self.root['Animations'][0]
        if len(animation['TrackGroups'])!=1:raise ValueError('Exact single track group required')
        self.group=animation['TrackGroups'][0];self.animation=animation
        self.duration=animation['Duration']
        if not 0<self.duration<=600:raise ValueError('Clip duration')
        self.tracks={};self.unsupported={};self.formats=Counter()
        for track in self.group['TransformTracks']:
            name=track['Name']
            if name in self.tracks or name in self.unsupported:raise ValueError('Duplicate track name')
            try:
                if track['Flags']!=0:raise ValueError('Unsupported track flags')
                curves=tuple(decode(track[channel],dim) for channel,dim in (('PositionCurve',3),('OrientationCurve',4),('ScaleShearCurve',9)))
                for c in curves:
                    if c.knots[-1]>self.duration+max(.001,self.duration*.01):raise ValueError('Curve outside clip duration')
                self.tracks[name]=curves
                self.formats.update(c.format for c in curves)
            except (ValueError,KeyError,TypeError) as exc:self.unsupported[name]=str(exc)
        self.sha256=hashlib.sha256(data).hexdigest()
    def summary(self):
        return {'duration_seconds':self.duration,'time_step':self.animation['TimeStep'],'oversampling':self.animation['Oversampling'],
                'track_group':self.group['Name'],'named_tracks':list(self.tracks)+list(self.unsupported),
                'tracks':[{'name':name,'position_curve_type':curves[0].format,'orientation_curve_type':curves[1].format,'scale_curve_type':curves[2].format,
                           'payload_decoded':True,'motion_status':'constant_controls' if all(all(c.controls[0]==v for v in c.controls) for c in curves) else 'varying_controls_not_mechanism_proof'}
                          for name,curves in self.tracks.items()]+[{'name':name,'payload_decoded':False,'motion_status':'unsupported','reason':reason} for name,reason in self.unsupported.items()],
                'supported_tracks':len(self.tracks),'unsupported_tracks':self.unsupported,'curve_formats':dict(self.formats),
                'all_track_payloads_decoded':not self.unsupported,
                'clip_basis_provenance':self.basis,'clip_skeleton_pairing':'explicit caller-selected skeleton; named-track mapping; not active native mesh proof',
                'target_runtime_interpolation_verified':False}
    def evaluate(self,skeleton,time,names,weapon='jntWpn_1'):
        if not 0<=time<=self.duration:raise ValueError('Time outside clip')
        needed=set(names)|{weapon};indices={name:n for n,name in enumerate(skeleton.names)}
        if not needed<=indices.keys():raise ValueError('Requested bone absent from exact skeleton')
        for name in list(needed):
            parent=skeleton.parents[indices[name]]
            while parent>=0:needed.add(skeleton.names[parent]);parent=skeleton.parents[parent]
        absent=needed-self.tracks.keys()
        if absent:raise ValueError('Required authored tracks absent/unsupported: '+','.join(sorted(absent)))
        world={};static=True;constant_chain={}
        for n,name in enumerate(skeleton.names):
            if name not in needed:continue
            curves=self.tracks[name];local=matrix(*(curve.evaluate(time) for curve in curves))
            constant=all(all(c.controls[0]==v for v in c.controls) for c in curves)
            parent=skeleton.parents[n];constant_chain[name]=constant and (constant_chain[skeleton.names[parent]] if parent>=0 else True)
            static &= constant
            world[name]=multiply(local,world[skeleton.names[parent]]) if parent>=0 else local
        inverse=inverse_rigid(world[weapon])
        return {'time_seconds':time,'coordinate_convention':'canonical reflected-Z row-vector metres; named bone relative to '+weapon,
                'all_required_controls_constant':static,'evaluation_status':'static_authored_pose' if static else 'decoded_spline_candidate',
                'clip_skeleton_pairing':'explicit caller-selected skeleton; named-track mapping','clip_basis_provenance':self.basis,
                'target_runtime_interpolation_verified':False,
                'bone_evaluation_status':{name:'static_authored_pose' if constant_chain[name] and constant_chain[weapon] else 'decoded_spline_candidate' for name in names},
                'weapon_relative':{name:canonical(multiply(world[name],inverse)) for name in names}}

def inspect_animation(data):
    """Stable in-memory metadata API for batch jobs; no binary parsing elsewhere."""
    clip=Clip(data)
    return {'schema':'fvr.bc2.authored_animation','schema_version':1,'resource_sha256':clip.sha256,**clip.summary()}

def archive_identity(path,game_root=None):
    result={'archive':str(path)}
    if game_root is not None:
        # Exact path join; never infer a match from an archive basename.
        result['archive_relative']=Path(path).resolve().relative_to(Path(game_root).resolve()).as_posix()
    return result

def inspect_archive(path,game_root=None):
    """One bounded inflation; metadata only, explicit unsupported resource rows."""
    from inspect_bc2_mesh_asset import Archive
    a=Archive(path);entries=[e for e in a.entries if e.flags==65536 and e.kind in ('GrannyAnimation','DeltaAnimation')]
    if len(entries)>256:raise ValueError('Per-archive clip limit')
    blobs=a.read_selected([e.name for e in entries]);clips=[]
    for entry in entries:
        row={**archive_identity(path,game_root),'archive_index_sha256':a.index_sha256,'resource':entry.name,'resource_kind':entry.kind,'resource_sha256':hashlib.sha256(blobs[entry.name]).hexdigest()}
        try:
            if entry.kind!='GrannyAnimation':raise ValueError('DeltaAnimation is not an absolute authored pose')
            row.update(inspect_animation(blobs[entry.name]));row['status']='decoded' if row['all_track_payloads_decoded'] else 'partial'
        except (ValueError,KeyError,TypeError) as exc:row.update(status='unsupported',reason=str(exc),tracks=[])
        clips.append(row)
    return {'schema':'fvr.bc2.authored_animation_batch','schema_version':1,'clips':clips,'runtime_admission':False}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive',type=Path,required=True);p.add_argument('--clip',required=True)
    p.add_argument('--game-root',type=Path,help='Emit exact archive_relative for inventory joins')
    p.add_argument('--skeleton-archive',type=Path);p.add_argument('--skeleton-resource')
    p.add_argument('--bone',action='append',default=[]);p.add_argument('--time',type=float,action='append',default=[])
    p.add_argument('--weapon-bone',default='jntWpn_1');p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    from inspect_bc2_mesh_asset import Archive
    a=Archive(args.archive);e=next((e for e in a.entries if e.name==args.clip and e.flags==65536),None)
    if e is None or e.kind!='GrannyAnimation':raise ValueError('Exact GrannyAnimation resource required')
    clip=Clip(a.read_selected([e.name])[e.name])
    result={'schema':'fvr.bc2.authored_animation','schema_version':1,**archive_identity(args.archive,args.game_root),'archive_index_sha256':a.index_sha256,
            'resource':e.name,'resource_sha256':clip.sha256,**clip.summary(),'runtime_admission':False,'evaluated':[]}
    if args.bone or args.time:
        if not args.skeleton_archive or not args.skeleton_resource:raise ValueError('Evaluation requires explicit skeleton archive/resource')
        s=Archive(args.skeleton_archive);e=next((e for e in s.entries if e.name==args.skeleton_resource and e.flags==65536),None)
        if e is None or e.kind!='GrannyModel':raise ValueError('Exact GrannyModel skeleton required')
        skeleton=Skeleton(s.read_selected([e.name])[e.name])
        result['skeleton']={**archive_identity(args.skeleton_archive,args.game_root),'archive_index_sha256':s.index_sha256,
                            'resource':e.name,**skeleton.metadata()}
        result['evaluated']=[clip.evaluate(skeleton,t,args.bone,args.weapon_bone) for t in (args.time or [0.])]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'output':str(args.output),'supported_tracks':len(clip.tracks),'unsupported_tracks':len(clip.unsupported),'evaluated':len(result['evaluated'])}))

if __name__=='__main__':main()
