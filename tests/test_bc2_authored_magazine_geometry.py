from pathlib import Path
import copy,json,math,struct,sys,unittest
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_authored_magazine_geometry as p

def at(x=0,y=0,z=0):
    m=p.identity();m[12:15]=[x,y,z];return m
def fixture():
    points=[[x,y,z] for x in (-.015,.015) for y in (-.09,.09) for z in (-.03,.03)]
    part={'points':points,'triangles':48,'mixed_triangles':0,'sections':[],'extent':[.03,.18,.06]}
    attached=at(0,-.03,-.4);hand=at(.08,-.05,-.43)
    fingers={}
    for name,x,y in [('Thumb',.05,.025),('Index',.03,.06),('Middle',.01,.07),('Ring',-.01,.065),('Pinky',-.03,.055)]:
        for n in (1,2,3):fingers[f'LeftHand{name}{n}']=p.multiply(at(x,y+(n-1)*.02,0),hand)
    body=[p.point([0,.105,0],attached)]
    return part,attached,body,hand,fingers

def profile():
    row={'native_asset_name':'SyntheticRifle','configured_mesh_path':'Objects/ExactMesh','grip_binding_digest':'1'*64,
         'mesh_sha256':'2'*64,'lod_sha256':'3'*64,'skeleton_sha256':'4'*64,'static_clip_sha256':'5'*64,
         'rig_fingerprint':'fnv1a64:0123456789abcdef','role_evidence':{'native_role_verified':False},
         'bones':{'weapon':'Root','magazine':'RigidBox','wrist':'LeftHand','fingers':p.FINGERS},
         'geometry':p.design(*fixture()),'runtime_admitted':False}
    row['profile_digest']=p.digest(row);return row
def report(row=None):return {'schema':'fvr.bc2.authored_magazine_geometry','schema_version':1,'profiles':[row or profile()]}
class Geometry(unittest.TestCase):
    def test_synthetic_carry_uses_geometry_and_own_fingers(self):
        args=fixture();g=p.design(*args)
        self.assertEqual(g['design']['status'],'experimental_geometry_based_estimate')
        self.assertFalse(g['design']['authored_reload_grasp']);self.assertEqual(g['design']['inward_direction'],[0,1,0])
        self.assertEqual(g['design']['long_axis'],1);self.assertEqual(g['design']['short_axis'],0)
        for n,m in g['wrist_from_fingers'].items():
            self.assertEqual(m,p.multiply(args[4][n],p.inverse_rigid(args[3])))
    def test_seat_roundtrip_recovers_exact_closed_frame(self):
        g=p.design(*fixture());seat=list(g['weapon_from_entry'])
        seat[12:15]=p.add(seat[12:15],p.scale(seat[8:11],p.UX['travel_m']))
        restored=p.multiply(p.inverse_rigid(g['item_from_insertion']),seat)
        self.assertLess(max(abs(a-b) for a,b in zip(restored,g['attached_item'])),1e-10)
    def test_palm_contacts_surface_lower_third(self):
        g=p.design(*fixture());knuckles=[g['wrist_from_fingers'][f'LeftHand{d}1'][12:15] for d in ('Index','Middle','Ring','Pinky')]
        center=[sum(v[k] for v in knuckles)/4 for k in range(3)];placed=p.point(center,g['item_from_hand'])
        self.assertAlmostEqual(placed[0],.015);self.assertAlmostEqual(placed[1],-.03);self.assertAlmostEqual(placed[2],0)
    def test_grasp_stacks_fingers_along_magazine_instead_of_pointing_from_fist(self):
        # Contact at the same lower-third landmark is insufficient: all fingers
        # must run along the exposed magazine, not bunch at its bottom edge.
        for side in (-1,1):
            for inward in (-1,1):
                part,attached,body,hand,fingers=fixture()
                body=[p.point([0,inward*.105,0],attached)]
                move=at((side-1)*.08,0,0)
                hand=p.multiply(hand,move);fingers={n:p.multiply(v,move) for n,v in fingers.items()}
                g=p.design(part,attached,body,hand,fingers)
                joints={n:p.point(v[12:15],g['item_from_hand']) for n,v in g['wrist_from_fingers'].items()}
                index=joints['LeftHandIndex1'];pinky=joints['LeftHandPinky1']
                self.assertGreater(inward*(index[1]-pinky[1]),.05)
                # Broad-face contact stays at the same lower-third landmark;
                # the new orientation does not change insertion geometry.
                center=[sum(joints[f'LeftHand{d}1'][k] for d in ('Index','Middle','Ring','Pinky'))/4 for k in range(3)]
                self.assertAlmostEqual(center[0],side*.015)
                self.assertAlmostEqual(center[1],-inward*.03)
                seat=list(g['weapon_from_entry']);seat[12:15]=p.add(seat[12:15],p.scale(seat[8:11],p.UX['travel_m']))
                restored=p.multiply(p.inverse_rigid(g['item_from_insertion']),seat)
                self.assertLess(max(abs(a-b) for a,b in zip(restored,attached)),1e-10)
    def test_opposite_authored_hand_side_changes_contact_side(self):
        part,attached,body,hand,fingers=fixture();translation=at(-.16,0,0)
        movedhand=p.multiply(hand,translation);movedfingers={n:p.multiply(m,translation) for n,m in fingers.items()}
        g=p.design(part,attached,body,movedhand,movedfingers)
        knuckles=[g['wrist_from_fingers'][f'LeftHand{d}1'][12:15] for d in ('Index','Middle','Ring','Pinky')]
        center=[sum(v[k] for v in knuckles)/4 for k in range(3)]
        self.assertAlmostEqual(p.point(center,g['item_from_hand'])[0],-.015)
    def test_rotated_closed_frame_preserves_local_design(self):
        args=list(fixture());r=at(3,4,5);r[:12]=[0,0,-1,0,0,1,0,0,1,0,0,0]
        baseline=p.design(*args);args[1]=p.multiply(args[1],r);args[2]=[p.point(v,r) for v in args[2]]
        args[3]=p.multiply(args[3],r);args[4]={n:p.multiply(m,r) for n,m in args[4].items()}
        other=p.design(*args)
        self.assertLess(max(abs(a-b) for a,b in zip(baseline['item_from_hand'],other['item_from_hand'])),1e-10)
        self.assertLess(max(abs(a-b) for a,b in zip(p.multiply(baseline['weapon_from_entry'],r),other['weapon_from_entry'])),1e-10)
    def test_body_facing_end_can_be_negative_axis(self):
        args=list(fixture());args[2]=[p.point([0,-.11,0],args[1])]
        self.assertEqual(p.design(*args)['design']['inward_direction'],[0,-1,0])
    def test_ambiguous_or_absent_body_rejects(self):
        for points in ([],[[0,-.03,-.4]]):
            args=list(fixture());args[2]=points
            with self.assertRaises(ValueError):p.design(*args)
    def test_missing_or_degenerate_finger_shape_rejects(self):
        args=list(fixture());del args[4][p.FINGERS[0]]
        with self.assertRaises(ValueError):p.design(*args)
        args=list(fixture());args[4]={n:at() for n in p.FINGERS}
        with self.assertRaises(ValueError):p.design(*args)
    def test_nonfinite_or_degenerate_part_rejects(self):
        args=list(fixture());args[0]['points'][0][0]=math.nan
        with self.assertRaises(ValueError):p.design(*args)
        args=list(fixture());args[0]['points']=[[0,0,0]]
        with self.assertRaises(ValueError):p.design(*args)
    def test_role_is_not_bone_number_or_reload_enum(self):
        skeleton=SimpleNamespace(names=['Weapon','ArbitraryBox'],parents=[-1,0]);part=fixture()[0]
        closed={'bone_evaluation_status':{'ArbitraryBox':'static_authored_pose'}}
        moving=SimpleNamespace(controls=((0,0,0),(0,.1,0)))
        candidates,rejected=p.role_candidates({'ArbitraryBox':part},skeleton,closed,SimpleNamespace(tracks={'ArbitraryBox':(moving,)}),'Weapon')
        self.assertEqual([r['bone'] for r in candidates],['ArbitraryBox']);self.assertFalse(candidates[0]['native_role_verified'])
        stationary=SimpleNamespace(controls=((0,0,0),))
        self.assertFalse(p.role_candidates({'ArbitraryBox':part},skeleton,closed,SimpleNamespace(tracks={'ArbitraryBox':(stationary,)}),'Weapon')[0])
    def test_role_requires_rigid_leaf_shape_and_static_closed_pose(self):
        skeleton=SimpleNamespace(names=['Weapon','Box'],parents=[-1,0]);part=fixture()[0]
        clip=SimpleNamespace(tracks={'Box':(SimpleNamespace(controls=((0,0,0),(0,.1,0))),)})
        for change in ('weights','thin','animated','child'):
            current=copy.deepcopy(part);rig=copy.deepcopy(skeleton);closed={'bone_evaluation_status':{'Box':'static_authored_pose'}}
            if change=='weights':current['mixed_triangles']=1
            if change=='thin':current['extent']=[.001,.18,.06]
            if change=='animated':closed['bone_evaluation_status']['Box']='decoded_spline_candidate'
            if change=='child':rig.names+=['Child'];rig.parents+=[1]
            self.assertFalse(p.role_candidates({'Box':current},rig,closed,clip,'Weapon')[0])
    def test_raw_vertex_reflection_precedes_canonical_inverse_bind(self):
        vertices=b''.join(struct.pack('<4e4B4B',*v,1,0,0,0,0,255,0,0,0) for v in ((1,2,3),(2,2,3),(1,3,3)))
        vb=len(vertices);indices=struct.pack('<3H',0,1,2)
        raw=b'\x01\0\0\0\0\0\1part\0'+struct.pack('<4I4BH',1,3,0,0,16,3,1,1,1)+struct.pack('<H',7)+struct.pack('<3I',vb,len(indices),0)+vertices+indices
        lod={'section_count':1,'palette':{7:p.bone_hash('Box')},'buffer_bytes':vb+len(indices)+12}
        skeleton=SimpleNamespace(names=['Box'],bones=[{'InverseWorldTransform':at(.1,.2,.3)}])
        part=p.rigid_parts(raw,lod,skeleton)['Box']
        self.assertEqual(part['triangles'],1);self.assertEqual(part['points'][0],[1.1,2.2,-3.3])
    def test_header_is_explicit_experimental_allowlist(self):
        with self.assertRaises(ValueError):p.cpp_header(report(),set())
        with self.assertRaises(ValueError):p.cpp_header(report(),{'Missing'})
        h=p.cpp_header(report(),{'SyntheticRifle'})
        self.assertIn('std::array<MagazineGeometryProfile,1>',h);self.assertIn('ExperimentalMagazineGeometry',h)
        self.assertIn('SelectedMeshKind::Unknown',h);self.assertNotIn('FindMagazineGeometry',h)
    def test_header_rejects_changed_digest_or_native_claim(self):
        row=profile();row['geometry']['item_from_hand'][12]+=.01
        with self.assertRaisesRegex(ValueError,'digest'):p.cpp_header(report(row),{'SyntheticRifle'})
        for key in ('runtime_admitted','role'):
            row=profile()
            if key=='role':row['role_evidence']['native_role_verified']=True
            else:row[key]=True
            row['profile_digest']=p.digest({k:v for k,v in row.items() if k!='profile_digest'})
            with self.assertRaisesRegex(ValueError,'native'):p.cpp_header(report(row),{'SyntheticRifle'})
    def test_header_rejects_ambiguous_asset(self):
        out=report();out['profiles'].append(copy.deepcopy(out['profiles'][0]))
        with self.assertRaisesRegex(ValueError,'Ambiguous'):p.cpp_header(out,{'SyntheticRifle'})
    def test_same_name_variants_emit_exact_configuration_paths(self):
        def exact(resource):
            row=profile();row['weapon']={'resource':resource,'sha256':'a'*64,
                'instance_guid':'00000000-0000-0000-0000-000000000001'}
            row['profile_digest']=p.digest({k:v for k,v in row.items() if k!='profile_digest'});return row
        a=exact('Objects/WeaponA.dbx');b=exact('Objects/WeaponB.dbx')
        out=report(a);out['profiles'].append(b)
        header=p.cpp_header(out,{'SyntheticRifle'})
        self.assertIn('g.configurationPath="Objects/WeaponA";',header)
        self.assertIn('g.configurationPath="Objects/WeaponB";',header)
        out['profiles'].append(profile())
        with self.assertRaisesRegex(ValueError,'Ambiguous legacy'):p.cpp_header(out,{'SyntheticRifle'})
        for resource in ('Objects/../Weapon.dbx','/Objects/Weapon.dbx','Objects/Weapon','Objects//Weapon.dbx'):
            with self.assertRaisesRegex(ValueError,'configuration resource'):
                p.cpp_header(report(exact(resource)),{'SyntheticRifle'})
    def test_header_cannot_silently_replace_documented_design_defaults(self):
        row=copy.deepcopy(profile());row['geometry']['design']['ux_defaults']['travel_m']+=.01
        row['profile_digest']=p.digest({k:v for k,v in row.items() if k!='profile_digest'})
        with self.assertRaisesRegex(ValueError,'defaults'):p.cpp_header(report(row),{'SyntheticRifle'})
    def test_skeleton_same_guid_cannot_borrow_different_resource(self):
        identifier='00000000-0000-0000-0000-000000000001';receipt={'resource':'Characters/Skeleton.dbx','instance_guid':identifier}
        self.assertTrue(p.bound_reference('characters/skeleton/'+identifier,receipt))
        with self.assertRaisesRegex(ValueError,'resource/GUID'):p.bound_reference('characters/different/'+identifier,receipt)
        with self.assertRaises(ValueError):p.bound_reference(identifier,receipt)
    def test_mesh_binding_includes_exact_weapon_document_hash(self):
        identifier='00000000-0000-0000-0000-000000000001';row={'native_asset_name':'Rifle','weapon':{'instance_guid':identifier,'resource':'Objects/Rifle.dbx','sha256':'a'*64}}
        weapon={'native_name':'Rifle','instance_guid':identifier,'resource':'Objects/Rifle.dbx','resource_sha256':'a'*64}
        self.assertTrue(p.mesh_weapon_matches(weapon,row))
        for key,value in (('resource','Objects/Other.dbx'),('resource_sha256','b'*64)):
            wrong=dict(weapon);wrong[key]=value;self.assertFalse(p.mesh_weapon_matches(wrong,row))


class PairedClip:
    duration=1.;sha256='6'*64
    def __init__(self,mode='valid'):self.mode=mode
    def evaluate(self,skeleton,time,names):
        item=at(.3+time*.1,0,0)
        if self.mode=='not_removed':item=at(.01,0,0)
        hand=at(.015,-.06,-.04)
        if self.mode=='disconnected':hand=at(1,0,0)
        if self.mode=='sliding':hand=at(time,-.06,-.04)
        wrist=p.multiply(hand,item);fingers=fixture()[4]
        local={n:p.multiply(m,p.inverse_rigid(fixture()[3])) for n,m in fingers.items()}
        if self.mode=='finger_motion':
            c,s=math.cos(time*3),math.sin(time*3);rotation=at();rotation[:12]=[c,s,0,0,-s,c,0,0,0,0,1,0]
            local={n:p.multiply(m,rotation) for n,m in local.items()}
        result={'LeftHand':wrist,'Magazine':item,**{n:p.multiply(m,wrist) for n,m in local.items()}}
        if self.mode=='missing':del result[p.FINGERS[0]]
        return {'weapon_relative':result,'evaluation_status':'decoded_spline_candidate'}

def paired_rig():
    names=['LeftHand','Magazine',*p.FINGERS];parents=[-1,-1]
    for digit in ('Thumb','Index','Middle','Ring','Pinky'):
        for n in (1,2,3):parents.append(0 if n==1 else names.index(f'LeftHand{digit}{n-1}'))
    return SimpleNamespace(names=names,parents=parents,sha256='4'*64,fingerprint='fnv1a64:0123456789abcdef')

class PairedGrasp(unittest.TestCase):
    def pair(self,mode='valid'):
        return p.paired_reload_grasp(PairedClip(mode),paired_rig(),'Magazine',at(),fixture()[0])
    def test_same_frame_relation_transfers_to_arbitrary_vr_wrist(self):
        pair=self.pair();w=pair['item_from_hand']
        self.assertLess(max(abs(a-b) for a,b in zip(w,at(.015,-.06,-.04))),1e-9)
        vr=at(3,4,5);vr[:12]=[0,1,0,0,-1,0,0,0,0,0,1,0]
        world_item=p.multiply(p.inverse_rigid(w),vr)
        self.assertLess(max(abs(a-b) for a,b in zip(p.multiply(w,world_item),vr)),1e-9)
        original=PairedClip().evaluate(paired_rig(),pair['receipt']['window']['time_seconds'],[])['weapon_relative']
        for name,local in pair['wrist_from_fingers'].items():
            # Each finger and the item share the same original authored frame.
            actual=p.multiply(local,vr)
            expected=p.multiply(p.multiply(original[name],p.inverse_rigid(original['Magazine'])),world_item)
            self.assertLess(max(abs(a-b) for a,b in zip(actual,expected)),1e-9)
    def test_pair_changes_only_carry_and_fingers(self):
        geom=p.design(*fixture());before=copy.deepcopy(geom);pair=self.pair();p.apply_paired_reload_grasp(geom,pair)
        for key in ('attached_item','item_from_insertion','weapon_from_entry','measured'):
            self.assertEqual(geom[key],before[key])
        self.assertEqual(geom['design']['ux_defaults'],before['design']['ux_defaults'])
        self.assertEqual(geom['item_from_hand'],pair['item_from_hand'])
        self.assertEqual(geom['wrist_from_fingers'],pair['wrist_from_fingers'])
        self.assertFalse(pair['receipt']['runtime_interpolation_verified'])
        self.assertFalse(pair['receipt']['active_native_animation_verified'])
    def test_disconnected_unremoved_and_unstable_pose_reject(self):
        for mode in ('disconnected','not_removed','sliding','finger_motion'):
            with self.subTest(mode=mode),self.assertRaisesRegex(ValueError,'No stable'):self.pair(mode)
    def test_duration_and_hierarchy_are_bounded(self):
        for duration in (.1,11,math.inf,math.nan):
            clip=PairedClip();clip.duration=duration
            with self.assertRaisesRegex(ValueError,'duration'):p.paired_reload_grasp(clip,paired_rig(),'Magazine',at(),fixture()[0])
        rig=paired_rig();rig.parents[-1]=0
        with self.assertRaisesRegex(ValueError,'hierarchy'):p.paired_reload_grasp(PairedClip(),rig,'Magazine',at(),fixture()[0])
    def test_missing_finger_cannot_make_partial_pair(self):
        with self.assertRaises((ValueError,KeyError)):self.pair('missing')
    def test_header_requires_coherent_pair_receipt(self):
        row=profile();row['bones']['magazine']='Magazine';row['reload_clip']={'sha256':'6'*64}
        p.apply_paired_reload_grasp(row['geometry'],self.pair())
        row['profile_digest']=p.digest({k:v for k,v in row.items() if k!='profile_digest'})
        self.assertIn('ExperimentalMagazineGeometry',p.cpp_header(report(row),{'SyntheticRifle'}))
        for failure in ('transform','finger','clip','rig','native_claim','missing_both'):
            changed=copy.deepcopy(row)
            if failure=='transform':changed['geometry']['item_from_hand'][12]+=.01
            if failure=='finger':changed['geometry']['wrist_from_fingers'][p.FINGERS[0]][12]+=.01
            if failure=='clip':changed['reload_clip']['sha256']='a'*64
            if failure=='rig':changed['rig_fingerprint']='fnv1a64:0000000000000001'
            if failure=='native_claim':changed['geometry']['design']['paired_reload_grasp']['active_native_animation_verified']=True
            if failure=='missing_both':
                del changed['reload_clip']['sha256']
                del changed['geometry']['design']['paired_reload_grasp']['clip_sha256']
            changed['profile_digest']=p.digest({k:v for k,v in changed.items() if k!='profile_digest'})
            with self.subTest(failure=failure),self.assertRaisesRegex(ValueError,'coherence'):
                p.cpp_header(report(changed),{'SyntheticRifle'})


if __name__=='__main__':unittest.main()
