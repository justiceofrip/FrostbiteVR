from pathlib import Path
import copy,json,sys,contextlib,unittest
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import bc2_magazine_contact_batch as batch
import bc2_authored_grip_bindings as grip
import bc2_authored_magazine_geometry as mag
from test_bc2_authored_grip_bindings import fixture as grip_fixture
from test_bc2_authored_magazine_geometry import fixture,PairedClip,paired_rig

class ReloadReferences(unittest.TestCase):
    def varying(self):
        args=grip_fixture();hands=args[4]['animations/different/handsikpose.res']['hands']
        hands['evaluation_status']='decoded_spline_candidate';hands['all_required_controls_constant']=False
        hands['bone_evaluation_status']['LeftHand']='decoded_spline_candidate'
        args[3]['clips'][0]['hands']=copy.deepcopy(hands)
        return args
    def test_reload_schema_cannot_be_exported_as_static_runtime_grip(self):
        args=self.varying();self.assertFalse(grip.build(*args)['profiles'])
        refs=grip.build(*args,reload_references_only=True);self.assertEqual(len(refs['profiles']),1)
        p=refs['profiles'][0];self.assertFalse(p['all_required_controls_constant']);self.assertFalse(p['authored_rifle_support'])
        self.assertEqual(p['left_hand_status'],'decoded_spline_candidate');self.assertFalse(p['runtime_accepted'])
        with self.assertRaisesRegex(ValueError,'schema'):grip.cpp_header(refs,{'Rifle_sp'})
    def test_reload_reference_keeps_exact_ownership_guards(self):
        for fault in ('hash','saved','rig'):
            a=self.varying()
            if fault=='hash':a[0]['resolved_weapons'][0]['resource_sha256']='0'*64
            if fault=='saved':a[3]['clips'][0]['hands']['weapon_relative']['LeftHand'][12]+=.1
            if fault=='rig':a[3]['skeleton']['rig_fingerprint']='fnv1a64:0000000000000000'
            self.assertFalse(grip.build(*a,reload_references_only=True)['profiles'])
    def test_static_default_still_static(self):
        a=grip_fixture();original=grip.build(*a)
        self.assertEqual(original,grip.build(*a,reload_references_only=False))
        self.assertEqual(original['schema'],'fvr.bc2.authored_grip_bindings')
        self.assertIn('AuthoredGrips',grip.cpp_header(original,{'Rifle_sp'}))
    def test_reload_schema_requires_complete_paired_request_before_reads(self):
        ref={'schema':'fvr.bc2.authored_reload_reference_bindings','schema_version':1,'profiles':[]}
        with self.assertRaisesRegex(ValueError,'complete paired'):
            mag.derive(Path('.'),ref,{}, {'Rifle'})

class ContactPrerequisite(unittest.TestCase):
    def pair(self):return mag.paired_reload_grasp(PairedClip(),paired_rig(),'Magazine',mag.identity(),fixture()[0])
    def test_complete_reload_pair_does_not_require_static_support_fingers(self):
        part,attached,body,hand,fingers=fixture();closed={'weapon_relative':{},'bone_evaluation_status':{}}
        with self.assertRaisesRegex(ValueError,'Static complete'):mag.contact_proposal(part,attached,body,closed)
        pair=self.pair();p=mag.contact_proposal(part,attached,body,closed,pair);baseline=mag.design(part,attached,body,hand,fingers)
        for key in ('attached_item','item_from_insertion','weapon_from_entry','measured'):self.assertEqual(p[key],baseline[key])
        self.assertEqual(p['item_from_hand'],pair['item_from_hand']);self.assertEqual(p['wrist_from_fingers'],pair['wrist_from_fingers'])
    def test_partial_reload_pair_never_uses_support_finger_fallback(self):
        part,attached,body,*_=fixture();pair=self.pair();del pair['wrist_from_fingers'][mag.FINGERS[5]]
        with self.assertRaises(KeyError):mag.contact_proposal(part,attached,body,{},pair)
    def test_default_static_proposal_is_predecessor_equivalent(self):
        part,attached,body,hand,fingers=fixture()
        closed={'weapon_relative':{'LeftHand':hand,**fingers},'bone_evaluation_status':{n:'static_authored_pose' for n in ['LeftHand',*mag.FINGERS]}}
        self.assertEqual(mag.contact_proposal(part,attached,body,closed),mag.design(*fixture()))

class BatchBounds(unittest.TestCase):
    def test_filter_uses_exact_definition_not_prefix(self):
        a=grip_fixture();c,m=batch.filter_inputs(a[0],a[2],{'Rifle_sp'});self.assertEqual(len(c['resolved_weapons']),1)
        self.assertEqual(batch.filter_inputs(a[0],a[2],{'Rifle'})[0]['resolved_weapons'],[])
        a[0]['resolved_weapons']*=2
        with self.assertRaisesRegex(ValueError,'Duplicate'):batch.filter_inputs(a[0],a[2],{'Rifle_sp'})
    def test_metadata_coverage_does_not_claim_runtime_or_convert_native_type(self):
        c=grip_fixture()[0];w=c['resolved_weapons'][0]
        w.update(firing_resource='Firing.dbx',firing_sha256='9'*64,fields={'FireLogic.ReloadType':{'value':'rtMagazine'}})
        result=batch.coverage(c,{'profiles':[],'gaps':[]},{'profiles':[],'gaps':[]},{'Rifle_sp','Garand'},{'Garand':'en bloc exception'})
        self.assertEqual(result['summary']['configuration_definitions'],1);self.assertEqual(result['summary']['deferred_assets'],1)
        for r in result['rows']:self.assertFalse(r['runtime_admitted']);self.assertFalse(r['headset_tested'])
        entry=next(r for r in result['rows'] if r['native_asset_name']=='Rifle_sp')['configuration_candidates'][0]
        self.assertIn('insufficient',entry['physical_ammo_family'])
    def test_cache_confines_paths_preserves_selection_bound_and_returns_no_files(self):
        class Fake:
            calls=0
            def __init__(self,path):
                self.index_sha256='a'*64;self.entries=[SimpleNamespace(flags=65536,name='A',size=3)]
            def read_selected(self,names):Fake.calls+=1;return {n:b'xyz' for n in names}
        with contextlib.nullcontext(str(Path(__file__).resolve().parents[1])) as directory:
            game=Path(directory);pool=batch.ArchivePool(game,'Dist/example.fbrb','a'*64,Fake)
            source=pool(game/'Dist/example.fbrb');self.assertEqual(source.read_selected(['A']),{'A':b'xyz'})
            source.read_selected(['A']);self.assertEqual(Fake.calls,1);self.assertEqual(pool.bytes,3)
            with self.assertRaises(ValueError):source.read_selected(['missing'])
            with self.assertRaises(ValueError):source.read_selected(['A','A'])
            with self.assertRaises(ValueError):pool(game.parent/'outside.fbrb')
            self.assertFalse((game/'Dist/example.fbrb').exists())
    def test_cache_validates_exact_common_index(self):
        class Fake:
            def __init__(self,path):self.index_sha256='b'*64
        with contextlib.nullcontext(str(Path(__file__).resolve().parents[1])) as directory:
            game=Path(directory);pool=batch.ArchivePool(game,'Dist/example.fbrb','a'*64,Fake)
            with self.assertRaisesRegex(ValueError,'index differs'):pool(game/'Dist/example.fbrb')
    def test_reader_substitution_restores_after_error(self):
        before=grip.Archive,mag.Archive
        with self.assertRaises(RuntimeError):
            with batch.readers(object()):raise RuntimeError('stop')
        self.assertEqual((grip.Archive,mag.Archive),before)

if __name__=='__main__':unittest.main()
