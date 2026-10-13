import copy,json,math,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_sampled_magazine_path as path
from bc2_authored_grip_bindings import digest
from bc2_weapon_animation_pipeline import identity

def pose(t=0):
    m=identity();a=.2*t;m[0]=m[5]=math.cos(a);m[1]=math.sin(a);m[4]=-math.sin(a)
    m[12]=.04*t*t;m[13]=-.1*t;m[14]=.2;return m
def source():
    return dict(clip_sha256='1'*64,skeleton_sha256='2'*64,rig_fingerprint='fnv1a64:123456789abcdef0',
        magazine_bone='Magazine',static_clip_sha256='3'*64,mesh_sha256='4'*64,lod_sha256='5'*64,
        grip_binding_digest='6'*64,weapon={'resource':'Objects/Pistol.dbx','sha256':'7'*64,'instance_guid':'12345678-1234-1234-1234-123456789abc'})
def samples():return [{'time':n/60,'item':pose(n/10)} for n in range(13)]
def profile(r):
    s=r['source'];return dict(reload_clip={'sha256':s['clip_sha256']},skeleton_sha256=s['skeleton_sha256'],
        rig_fingerprint=s['rig_fingerprint'],bones={'magazine':s['magazine_bone']},static_clip_sha256=s['static_clip_sha256'],
        mesh_sha256=s['mesh_sha256'],lod_sha256=s['lod_sha256'],grip_binding_digest=s['grip_binding_digest'],
        weapon=s['weapon'],geometry={'attached_item':r['closed_item']})
def rehash(r):r['path_digest']=digest({k:v for k,v in r.items() if k!='path_digest'})

class SampledPathTests(unittest.TestCase):
    def test_measured_pistol_export_is_exact_and_remains_unadmitted(self):
        import bc2_authored_magazine_geometry as geometry
        folder=Path(__file__).resolve().parents[1]/'profiles/experimental-pistol-sampled242'
        report=json.loads((folder/'exact-geometry.json').read_text())
        self.assertEqual({p['native_asset_name'] for p in report['profiles']},{'M9','MP443_sp'})
        for p in report['profiles']:
            receipt=p['geometry']['sampled_path'];path.validate(receipt,p)
            self.assertEqual(len(receipt['points']),12)
            self.assertFalse(p['runtime_admitted']);self.assertFalse(receipt['runtime_admitted'])
            self.assertFalse(receipt['native_trajectory_verified'])
            self.assertEqual(receipt['withdrawal_samples'][-1]['time_seconds'],.8)
            self.assertEqual(receipt['points'][-1]['weapon_from_item'],p['geometry']['attached_item'])
        self.assertEqual(geometry.cpp_header(report,{'M9','MP443_sp'}),(folder/'PistolGeometry.h').read_text())
    def test_exact_reversed_source_and_endpoint(self):
        r=path.from_samples(samples(),pose(),source());self.assertIs(path.validate(r,profile(r)),r)
        self.assertEqual(r['points'][-1]['weapon_from_item'],pose())
        self.assertEqual([p['weapon_from_item'] for p in r['points']],list(reversed([s['weapon_from_item'] for s in r['withdrawal_samples']])))
        self.assertGreater(r['travel_m'],.1);self.assertEqual(r['points'][0]['arc_m'],0)
        self.assertFalse(r['runtime_admitted']);self.assertFalse(r['native_trajectory_verified'])
    def test_seated_anticipation_preserves_raw_prefix(self):
        raw=samples();tiny=pose();tiny[13]=.0002
        raw=[{'time':0,'item':pose()},{'time':1/60,'item':tiny},{'time':2/60,'item':pose()}]+[
            {'time':(n+3)/60,'item':s['item']} for n,s in enumerate(raw[1:])]
        r=path.from_samples(raw,pose(),source());self.assertEqual(r['source_prefix'][1]['weapon_from_item'],tiny)
        self.assertNotIn(tiny,[p['weapon_from_item'] for p in r['points']]);path.validate(r,profile(r))
    def test_rotation_exiting_seated_prefix_is_not_discarded(self):
        raw=samples();early=pose(.05);early[0]=early[5]=math.cos(.012);early[1]=math.sin(.012);early[4]=-math.sin(.012)
        raw.insert(1,{'time':1/60,'item':early})
        for n,s in enumerate(raw):s['time']=n/60
        r=path.from_samples(raw,pose(),source());self.assertEqual(r['withdrawal_samples'][1]['weapon_from_item'],early)
    def test_true_repeat_loop_or_jump_rejected(self):
        for bad in range(4):
            raw=samples()
            if bad==0:raw[5]['item']=copy.deepcopy(raw[2]['item'])
            if bad==1:raw[4]['item'][13]-=.3
            if bad==2:raw[4]['item'][12]=float('nan')
            if bad==3:raw[4]['time']+=.001
            with self.subTest(bad=bad),self.assertRaises((ValueError,TypeError)):path.from_samples(raw,pose(),source())
    def test_adjacent_stationary_source_sample_does_not_create_repeated_path_point(self):
        raw=samples();raw.insert(5,copy.deepcopy(raw[4]))
        for n,s in enumerate(raw):s['time']=n/60
        r=path.from_samples(raw,pose(),source());self.assertEqual(len(r['source_prefix']),len(r['withdrawal_samples'])+1)
        path.validate(r,profile(r))
    def test_pure_rotation_without_path_resolution_is_rejected(self):
        raw=samples();raw[4]['item']=copy.deepcopy(raw[3]['item']);a=.3
        raw[4]['item'][0]=raw[4]['item'][5]=math.cos(a);raw[4]['item'][1]=math.sin(a);raw[4]['item'][4]=-math.sin(a)
        with self.assertRaisesRegex(ValueError,'pure rotation'):path.from_samples(raw,pose(),source())
    def test_source_mismatch_cannot_be_rehashed_into_acceptance(self):
        r=path.from_samples(samples(),pose(),source());p=profile(r)
        for key in ['clip_sha256','skeleton_sha256','rig_fingerprint','magazine_bone','static_clip_sha256','mesh_sha256','lod_sha256','grip_binding_digest','weapon']:
            bad=copy.deepcopy(r);bad['source'][key]='wrong';rehash(bad)
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'source mismatch'):path.validate(bad,p)
    def test_export_tampering_and_false_admission_rejected(self):
        r=path.from_samples(samples(),pose(),source());p=profile(r)
        for bad in range(7):
            q=copy.deepcopy(r)
            if bad==0:q['points'][2]['arc_m']+=.001
            if bad==1:q['points'][2]['weapon_from_item'][12]+=.001
            if bad==2:q['points'].reverse()
            if bad==3:q['closed_item'][12]+=.001
            if bad==4:q['runtime_admitted']=True
            if bad==5:q['native_trajectory_verified']=True
            if bad==6:q['source_prefix'].pop(3)
            rehash(q)
            with self.subTest(bad=bad),self.assertRaises(ValueError):path.validate(q,p)
    def test_baseline_and_insufficient_motion_rejected(self):
        raw=samples();raw[0]['item'][12]+=.002
        with self.assertRaisesRegex(ValueError,'baseline'):path.from_samples(raw,pose(),source())
        with self.assertRaisesRegex(ValueError,'insufficient'):path.from_samples(samples()[:6],pose(),source())

if __name__=='__main__':unittest.main()
