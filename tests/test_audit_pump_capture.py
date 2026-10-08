"""Synthetic schema fixtures, never native acceptance evidence."""
import copy,importlib.util,unittest,json,os,uuid,contextlib
from pathlib import Path
import sys
BASE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(BASE/'tools'))
import audit_pump_capture as mod
spec=importlib.util.spec_from_file_location('fixture_baseline',BASE/'tests/test_audit_pump_hold.py');fixturemod=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixturemod)
@contextlib.contextmanager
def scratch():
    folder=Path(__file__).parent/'test-output'/uuid.uuid4().hex;folder.mkdir(parents=True);yield folder
def fixture():
    args=fixturemod.fixture();trace=args[0];flow=trace['gameplay']['reload_flow'];identity=flow['records'][0]['before'];samples=[]
    def row(time,state):
        native={k:identity[k] for k in mod.OWNER+mod.SERVER};native.update(observed_ns=time,completed_ns=time+50_000,deadline_ns=time+100_000_000,callback_revision=10,hold_phase=2,weapon_data=0xc0000,firing_data=0xd0000)
        native['branches']=[dict(firing=f,**state,timer=.2 if state['current']==7 else 0) for f in flow['diagnostic_hold']['firing']]
        post=copy.deepcopy(native);post.update(observed_ns=time+100_000,completed_ns=time+200_000)
        matrix=[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
        samples.append(dict(before=native,after=post,stable_state_bracket=True,input_sequence=len(samples)+1,input_observed_ns=0,input_deadline_ns=time+100_000_000,selected_observed_ns=time-1_000_000,selected_deadline_ns=time+100_000_000,rig_fingerprint=123,rig_animation=0xe0000,rig_skeleton=0xf0000,rig_pose=0x100000,part_from_weapon_m=matrix,wrist_from_weapon_m=copy.deepcopy(matrix)))
    row(13_300_000_000,dict(current=7,previous=6,next=8,loaded=7,reserve=24))
    row(13_640_000_000,dict(current=7,previous=6,next=8,loaded=7,reserve=24))
    row(13_863_000_000,dict(current=2,previous=1,next=2,loaded=7,reserve=24))
    trace['gameplay']['rig_publication']=dict(pump_part_capture=dict(schema_version=1,runtime_authority=False,submitted_mesh_proven=False,rows=len(samples),rejected=0,dropped=0,samples=samples))
    return args
class Tests(unittest.TestCase):
    def setUp(self):self.args=fixture();self.t=self.args[0];self.c=self.t['gameplay']['rig_publication']['pump_part_capture'];self.row=self.c['samples'][0]
    def result(self):return mod.audit(*self.args,fixturemod.audit)
    def test_positive(self):
        r=self.result();self.assertTrue(r['passed'],r);self.assertEqual(r['details']['completion_count_per_branch'],[1,1,1]);self.assertFalse(r['normal_manual_pump_admitted']);self.assertEqual(r['details']['input_observed_time_missing'],3)
    def test_restore_failure(self):self.t['gameplay']['reload_flow']['diagnostic_hold']['restore_failures']=1;self.assertFalse(self.result()['passed'])
    def test_missing_capture(self):del self.t['gameplay']['rig_publication'];self.assertFalse(self.result()['passed'])
    def test_truncated(self):self.c['dropped']=1;self.assertFalse(self.result()['passed'])
    def test_wrong_owner(self):self.row['before']['equip_generation']+=1;self.assertFalse(self.result()['passed'])
    def test_stale_selected(self):self.row['selected_deadline_ns']=self.row['before']['observed_ns'];self.assertFalse(self.result()['passed'])
    def test_stale_native(self):self.row['after']['deadline_ns']=self.row['after']['completed_ns'];self.assertFalse(self.result()['passed'])
    def test_invented_input_timestamp(self):self.row['input_observed_ns']=self.row['input_deadline_ns'];self.assertFalse(self.result()['passed'])
    def test_reflection(self):self.row['part_from_weapon_m'][0][0]=-1;self.assertFalse(self.result()['passed'])
    def test_nan(self):self.row['wrist_from_weapon_m'][3][0]=float('nan');self.assertFalse(self.result()['passed'])
    def test_wrong_config(self):self.row['after']['weapon_data']+=4;self.assertFalse(self.result()['passed'])
    def test_unjoined_state(self):self.row['before']['branches'][0]['current']=self.row['after']['branches'][0]['current']=5;self.assertFalse(self.result()['passed'])
    def test_false_stability(self):self.row['after']['branches'][0]['next']=1;self.assertFalse(self.result()['passed'])
    def test_capture_cannot_assert_authority(self):self.c['runtime_authority']=True;self.assertFalse(self.result()['passed'])
    def test_order(self):self.c['samples'].reverse();self.assertFalse(self.result()['passed'])
    def test_missing_ready(self):self.c['samples'].pop();self.c['rows']-=1;self.assertFalse(self.result()['passed'])
    def test_malformed_sample(self):self.c['samples'][0]=None;self.assertFalse(self.result()['passed'])
    def test_unknown_input_time_remains_explicit(self):self.assertTrue(self.result()['passed']);self.assertEqual(self.result()['details']['input_observed_time_missing'],3)
    def test_cli_output_collisions_preserve_inputs(self):
        with scratch() as folder:
            root=Path(folder);paths=[root/n for n in ('native-trace.json','result.json','pump-hold-preflight.json','pump-hold-postflight.json','audit.py')]
            for p in paths:p.write_text('original evidence')
            args=['--hold-auditor',str(paths[-1]),'--trace',str(paths[0]),'--receiver',str(root),'--output']
            for target in paths:
                with self.subTest(target=target):
                    with self.assertRaises(SystemExit) as e:mod.main(args+[str(target)])
                    self.assertEqual(e.exception.code,2);self.assertEqual(target.read_text(),'original evidence')
    def test_cli_hardlink_collision_preserves_source(self):
        with scratch() as folder:
            root=Path(folder);source=root/'trace.json';source.write_text('original evidence');alias=root/'alias.json'
            try:os.link(source,alias)
            except OSError as e:self.skipTest(str(e))
            with self.assertRaises(SystemExit) as e:mod.main(['--hold-auditor',str(BASE/'tools/audit_pump_hold.py'),'--trace',str(source),'--receiver',str(root),'--output',str(alias)])
            self.assertEqual(e.exception.code,2);self.assertEqual(source.read_text(),'original evidence')
    def test_reviewed_auditor_hash_required_before_import(self):
        with scratch() as folder:
            source=Path(folder)/'audit.py';source.write_text("raise RuntimeError('This unreviewed code must never execute')")
            with self.assertRaisesRegex(ValueError,'import refused'):mod.load_auditor(source)
        self.assertTrue(callable(mod.load_auditor(BASE/'tools/audit_pump_hold.py').audit))
    def test_duplicate_completion(self):
        flow=self.t['gameplay']['reload_flow'];rows=flow['records'];new=[]
        for branch in range(3):
            for state in (8,1,2):
                source=next(r for r in rows if r['before']['branch']==branch and r['after']['current']==state)
                r=copy.deepcopy(source);r['id']=len(rows)+len(new)+1;r['begin_ns']=14_000_000_000+state*1_000_000;r['end_ns']=r['begin_ns']+200_000
                # Explicit temporal8→1→2 sequence; preserve exact same identity/counts.
                r['begin_ns']=14_000_000_000+(8,1,2).index(state)*1_000_000+branch*100_000;r['end_ns']=r['begin_ns']+200_000
                r['before']=copy.deepcopy(r['after']);new.append(r)
        rows.extend(new);self.assertTrue(fixturemod.audit(*self.args)['passed']);self.assertFalse(self.result()['passed'])
if __name__=='__main__':unittest.main()
