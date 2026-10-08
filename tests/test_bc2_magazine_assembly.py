from pathlib import Path
import copy,importlib.util,json,math,struct,sys,unittest
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_magazine_assembly as a
import bc2_authored_magazine_geometry as p
import bc2_magazine_assembly_cache as cache
from test_bc2_authored_magazine_geometry import fixture,profile,report,at,PairedClip,paired_rig

def setup():
    sk=SimpleNamespace(names=['jntWpn_1','Box','EmptyJoint','Follower'],parents=[-1,0,1,2],sha256='a'*64,fingerprint='fnv1a64:0123456789abcdef')
    closed={'weapon_relative':{'jntWpn_1':at(),'Box':at(0,0,.2),'EmptyJoint':at(0,.02,.2),'Follower':at(0,.04,.2)},
            'bone_evaluation_status':{n:'static_authored_pose' for n in sk.names}}
    controls=lambda:tuple(SimpleNamespace(controls=((0,0,0),)) for _ in range(3))
    clip=SimpleNamespace(duration=.5,sha256='b'*64,tracks={n:controls() for n in sk.names})
    def evaluate(sk,time,names):
        move=at(time*2,0,0)
        return {'weapon_relative':{n:p.multiply(closed['weapon_relative'][n],move) for n in names}}
    clip.evaluate=evaluate
    parts={'Box':fixture()[0],'Follower':dict(fixture()[0],triangles=24)}
    return parts,sk,closed,clip

class Assembly(unittest.TestCase):
    def test_complete_subtree_includes_unweighted_intermediary_and_all_points(self):
        parts,sk,closed,clip=setup();before=copy.deepcopy(parts)
        merged,r=a.derive_assembly(parts,sk,closed,clip,'Box')
        self.assertEqual(parts,before);self.assertEqual(merged['triangles'],72)
        self.assertEqual([x['bone'] for x in r['members']],['EmptyJoint','Follower'])
        self.assertEqual([x['triangles'] for x in r['members']],[0,24])
        self.assertEqual(merged['points'][8],p.point(parts['Follower']['points'][0],at(0,.04,0)))
        self.assertFalse(r['runtime_admitted']);self.assertFalse(r['body_prop_cache_verified'])
    def test_leaf_is_original_object_and_no_receipt(self):
        parts,sk,closed,clip=setup();sk.names=sk.names[:2];sk.parents=sk.parents[:2]
        part,receipt=a.derive_assembly(parts,sk,closed,clip,'Box')
        self.assertIs(part,parts['Box']);self.assertIsNone(receipt)
    def test_varying_controls_rejected_even_when_every_sample_would_look_constant(self):
        parts,sk,closed,clip=setup();clip.tracks['Follower'][0].controls=((0,0,0),(0,.001,0),(0,0,0))
        with self.assertRaisesRegex(ValueError,'varying authored controls'):a.derive_assembly(parts,sk,closed,clip,'Box')
    def test_exact_closed_reload_relation_required(self):
        parts,sk,closed,clip=setup();original=clip.evaluate
        def shifted(*args):
            out=original(*args);out['weapon_relative']['Follower'][13]+=.001;return out
        clip.evaluate=shifted
        with self.assertRaisesRegex(ValueError,'relative transform differs'):a.derive_assembly(parts,sk,closed,clip,'Box')
    def test_missing_tracks_closed_member_mixed_skin_or_nonrigid_reject(self):
        for bad in ('track','closed','weighted','matrix','closed_motion'):
            parts,sk,closed,clip=setup()
            if bad=='track':del clip.tracks['EmptyJoint']
            if bad=='closed':del closed['weapon_relative']['EmptyJoint']
            if bad=='weighted':parts['Follower']['mixed_triangles']=1
            if bad=='matrix':closed['weapon_relative']['Follower'][0]=2
            if bad=='closed_motion':closed['bone_evaluation_status']['EmptyJoint']='decoded_spline_candidate'
            with self.assertRaises((ValueError,KeyError),msg=bad):a.derive_assembly(parts,sk,closed,clip,'Box')
    def test_cycles_parent_range_duplicate_names_and_bound_reject(self):
        for bad in ('cycle','range','duplicate','bound'):
            parts,sk,closed,clip=setup()
            if bad=='cycle':sk.parents[2]=3
            if bad=='range':sk.parents[2]=99
            if bad=='duplicate':sk.names[2]='Box'
            if bad=='bound':sk.names+=['Extra'+str(n) for n in range(7)];sk.parents += [1]*7
            with self.assertRaises(ValueError,msg=bad):a.subtree(sk,'Box')
    def test_strict_skin_reports_blends_that_have_no_rigid_triangle_owner(self):
        vertices=b''.join(struct.pack('<4e4B4B',*v,1,0,1,0,0,128,127,0,0) for v in ((1,2,3),(2,2,3),(1,3,3)))
        indices=struct.pack('<3H',0,1,2);vb=len(vertices)
        raw=b'\x01\0\0\0\0\0\1part\0'+struct.pack('<4I4BH',1,3,0,0,16,3,2,2,2)+struct.pack('<2H',7,8)+struct.pack('<3I',vb,len(indices),0)+vertices+indices
        lod={'section_count':1,'palette':{7:p.bone_hash('Box'),8:p.bone_hash('Follower')},'buffer_bytes':vb+len(indices)+12}
        sk=SimpleNamespace(names=['Box','Follower'],bones=[{'InverseWorldTransform':at()}]*2)
        self.assertEqual(p.rigid_parts(raw,lod,sk),{})
        parts=p.rigid_parts(raw,lod,sk,True)
        self.assertEqual({n:v['mixed_triangles'] for n,v in parts.items()},{'Box':1,'Follower':1})
        self.assertFalse(parts['Follower']['points'])
    def test_emitter_receipt_must_match_exact_sources_and_order(self):
        parts,sk,closed,clip=setup();_,receipt=a.derive_assembly(parts,sk,closed,clip,'Box')
        row=profile();row['bones']['magazine']='Box';row['skeleton_sha256']=sk.sha256;row['reload_clip']={'sha256':clip.sha256};row['assembly']=receipt
        def emitted(v):
            v['profile_digest']=p.digest({k:x for k,x in v.items() if k!='profile_digest'});return p.cpp_header(report(v),{'SyntheticRifle'})
        self.assertIn('g.assemblyCount=2',emitted(row))
        for bad in ('digest','source','order','native','duplicate','motion'):
            wrong=copy.deepcopy(row);r=wrong['assembly']
            if bad=='digest':r['assembly_digest']='0'*64
            if bad=='source':r['skeleton_sha256']='0'*64
            if bad=='order':r['members'].reverse()
            if bad=='native':r['runtime_admitted']=True
            if bad=='duplicate':r['members'][1]['bone']=r['members'][0]['bone']
            if bad=='motion':r['members'][0]['maximum_relative_error_m']=.02
            if bad!='digest':r['assembly_digest']=p.digest({k:v for k,v in r.items() if k!='assembly_digest'})
            with self.assertRaises(ValueError,msg=bad):emitted(wrong)
    def test_leaf_header_emits_no_assembly_assignments(self):
        self.assertNotIn('g.assembly',p.cpp_header(report(),{'SyntheticRifle'}))
    def test_contact_diagnostics_cannot_change_selected_pose_or_receipt(self):
        args=(PairedClip(),paired_rig(),'Magazine',at(),fixture()[0]);detail={}
        plain=p.paired_reload_grasp(*args);observed=p.paired_reload_grasp(*args,diagnostics=detail)
        self.assertEqual(json.dumps(plain,sort_keys=True),json.dumps(observed,sort_keys=True))
        self.assertEqual(detail['eligible_windows'],plain['receipt']['eligible_windows'])
        self.assertFalse(detail['runtime_admitted']);self.assertTrue(detail['windows'])
    def test_rejected_authored_finger_motion_remains_explicit(self):
        detail={}
        with self.assertRaisesRegex(ValueError,'No stable'):
            p.paired_reload_grasp(PairedClip('finger_motion'),paired_rig(),'Magazine',at(),fixture()[0],detail)
        self.assertEqual(detail['eligible_windows'],0)
        self.assertEqual(detail['failure_counts']['finger_angle_rad'],len(detail['windows']))
        self.assertTrue(all('finger_angle_rad' in w['failed'] for w in detail['windows']))
    def test_assembly_inspection_requires_explicit_paired_contact_opt_in(self):
        with self.assertRaisesRegex(ValueError,'explicitly paired contact'):
            p.derive(Path('.'),{'schema':'fvr.bc2.authored_grip_bindings','schema_version':1,'profiles':[]},
                     {'schema':'fvr.bc2.authored_weapon_mesh_bindings'},{'Test'},rigid_assembly_assets={'Test'})
    def test_cache_bakes_all_members_to_root_local_and_excludes_unrelated_part(self):
        parts,sk,closed,clip=setup();sk.names+=['Other'];sk.parents+=[0]
        sk.bones=[{'InverseWorldTransform':at()} for _ in sk.names]
        closed['weapon_relative']['Other']=at();closed['bone_evaluation_status']['Other']='static_authored_pose'
        # The source includes one root, one child, and one unrelated triangle.
        vertices=b''.join(struct.pack('<3f8B',x,y,z,owner,0,0,0,255,0,0,0)
            + bytes(12) for owner in range(3) for x,y,z in ((0,0,0),(.01,0,0),(0,.01,0)))
        indices=struct.pack('<9H',*range(9));vb=len(vertices)
        data=b'\x01\0\0\0\0\0\1part\0'+struct.pack('<4I4BH',3,9,0,0,32,3,1,3,3)+struct.pack('<3H',7,8,9)+struct.pack('<3I',vb,len(indices),0)+vertices+indices
        lod={'section_count':1,'palette':{7:p.bone_hash('Box'),8:p.bone_hash('Follower'),9:p.bone_hash('Other')},'buffer_bytes':vb+len(indices)+12}
        parts=p.rigid_parts(data,lod,sk,True)
        _,receipt=a.derive_assembly(parts,sk,closed,clip,'Box')
        row=profile();row.update(bones=dict(row['bones'],magazine='Box'),assembly=receipt,
            skeleton_sha256=sk.sha256,mesh_sha256=p.sha(b'metadata'),lod_sha256=p.sha(data),
            static_clip_sha256='c'*64,reload_clip={'sha256':clip.sha256})
        row['profile_digest']=p.digest({k:v for k,v in row.items() if k!='profile_digest'})
        static=SimpleNamespace(sha256='c'*64,evaluate=lambda *args:closed)
        with patch.object(cache,'metadata',return_value=[lod]):
            catalog,packed=cache.derive_cache(b'metadata',data,sk,static,clip,row)
        self.assertEqual(packed[:8],b'BC2PROP1');self.assertEqual(catalog['inverse_bind'],at())
        self.assertEqual(catalog['sections'][0]['part_triangles'],2)
        # Header has three names, rig/count, then section name and five u32.
        cursor=12
        for _ in range(3):cursor+=2+struct.unpack_from('<H',packed,cursor)[0]
        cursor+=12;cursor+=2+struct.unpack_from('<H',packed,cursor)[0]
        nv,ni,width,offset,base=struct.unpack_from('<5I',packed,cursor);cursor+=20
        self.assertEqual((nv,ni,width,offset,base),(120,12,2,0,0))
        root=struct.unpack_from('<3f',packed,cursor);child=struct.unpack_from('<3f',packed,cursor+60)
        self.assertEqual(root,(0.,0.,0.));self.assertAlmostEqual(child[1],.04,places=7)
        # Mutating a source cannot regenerate a descriptor under the old pins.
        with self.assertRaisesRegex(ValueError,'exact source mismatch'):
            cache.derive_cache(b'metadata',data+b'x',sk,static,clip,row)
        with self.assertRaisesRegex(ValueError,'exact source mismatch'):
            cache.derive_cache(b'other',data,sk,static,clip,row)

if __name__=='__main__':unittest.main()
