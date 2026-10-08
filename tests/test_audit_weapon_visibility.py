import copy
import json
from pathlib import Path
import sys
import uuid
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from audit_weapon_visibility import audit,parse

class Fixture:
    def __init__(self,folder):
        self.folder=Path(folder)
        self.manifest=dict(pid=123,weapon_visibility_probe=True,duration_ms=15000,until_host_exit=False,
            game_sha256='a'*64,probe_sha256='b'*64,bootstrap_sha256='c'*64)
        for key in ('physical_reload','physical_reload_probe','reload_request_probe','reload_hold_probe','reload_round_probe',
                    'optic_filter_observe','body_inventory','death_probe','equip_probe','rig_pulse','sight_flip','pass_evidence'):
            self.manifest[key]=False
        self.completion=dict(bootstrap_exit=0,game_exited=False,new_crash_report=False,game_responding=True,stability_observation_ms=15000)
        self.receiver=dict(consumed_pairs=240,native_tracking_transport_verified=True,async_timeouts=0,weapon_visibility_fixture=True,headset_tested=False)
        probe=dict(requested=True,phase=4,failure=0,paired_private_palette_sequence_verified=True,receipts=[20,30,40],
            empty_hands_acknowledged=False,gpu_visibility_verified=False,headset_verified=False,
            player=0x10000,soldier=0x20000,weak=0x30000,weapon=0x40000,actor_generation=5,native_equip_generation=6,physical_equip_generation=7,space=8,rows=[])
        rig=dict(native_animation_written=False,source_changes=0,packing_failures=0,weapon_visibility=dict(
            private_palette_consumer_integrated=True,native_animation_written=False,submitted_visibility_verified=False,headset_verified=False,
            verified_hidden_copies=60,verified_shown_copies=120,paired_pack_receipts=90))
        self.trace=dict(pid=123,hooks_disabled=True,gameplay=dict(weapon_visibility_probe=probe,rig_publication=rig,
            weapon_visibility_action_suppression=dict(verified_commits=200,failures=0)),native_stream=dict(
            published=240,consumed=240,captured_eyes=480,camera_restore_failures=0,gpu_failure_code=0),weapon_visibility_eyes=[],
            world_color_captures=[dict(captured=True,hresult=0,target=dict(width=2,height=2,format=28,samples=1)) for _ in range(3)])
        self.pairs=[dict(pair=i,native_frame=1000+i,tracking=10000+i) for i in range(240)]
        for phase in (1,2,3):
            now=1_000_000_000+phase*2_000_000_000;deadline=now+100_000_000
            probe['rows'].append(dict(phase=phase,now_ns=now,request=phase,input=phase,hidden=phase==2,
                source_observed_ns=now,source_deadline_ns=deadline,receipt_input=phase,draw_serial=phase,
                receipt_observed_ns=now,receipt_deadline_ns=deadline,ordinary_entries_preserved=55,weighted_entries=9))
            for eye in (0,1):
                self.trace['weapon_visibility_eyes'].append(dict(native_frame=999+phase,eye=eye,phase=phase,request=phase,input=phase,
                    hidden_intent=phase==2,paired_pack_receipt=True,draw_serial=phase,receipt_input=phase,receipt_observed_ns=now,
                    receipt_deadline_ns=deadline,now_ns=now+1000000))
                # Identical pixels deliberately cannot prove visual hide.
                (self.folder/f'pair-{phase-1}-eye-{eye}.rgba').write_bytes(bytes(range(16)))
    def run(self):return audit(self.trace,self.manifest,self.completion,self.receiver,self.pairs,self.folder)

class AuditTests(unittest.TestCase):
    def setUp(self):
        self.scratch=(Path(__file__).parents[1]/'test-temp').resolve();self.scratch.mkdir(exist_ok=True)
        self.directory=self.scratch/uuid.uuid4().hex;self.directory.mkdir();self.f=Fixture(self.directory)
    def tearDown(self):
        self.assertEqual(self.directory.resolve().parent,self.scratch)
        for file in self.directory.iterdir():
            self.assertTrue(file.is_file());file.unlink()
        self.directory.rmdir()
    def test_mechanical_pass_still_requires_visual_review(self):
        r=self.f.run();self.assertTrue(r['mechanical_verified']);self.assertTrue(r['visual_review_ready'])
        self.assertEqual(r['status'],'mechanical_pass_pending_visual');self.assertFalse(r['gpu_visibility_verified']);self.assertFalse(r['empty_hands_acknowledged'])
        self.assertEqual({x['phase'] for x in r['sampleable_eye_pairs']},{'baseline','hidden','restored'})
        self.assertTrue(all(p['width']==2 and p['height']==2 and len(p['sha256'])==64 for s in r['sampleable_eye_pairs'] for p in s['pixels']))
    def test_no_baseline_hide_or_restore_is_distinguished(self):
        probe=self.f.trace['gameplay']['weapon_visibility_probe']
        for i,status in enumerate(('no_baseline_receipts','no_hide_receipts','no_restore_receipts')):
            probe['receipts']=[10,10,10];probe['receipts'][i]=0
            r=self.f.run();self.assertFalse(r['mechanical_verified']);self.assertEqual(r['status'],status)
    def test_original_source_failure_and_suppression_failure(self):
        self.f.trace['gameplay']['rig_publication']['source_changes']=1
        self.assertFalse(self.f.run()['sections']['private_palette_sequence']['passed'])
        self.f.trace['gameplay']['rig_publication']['source_changes']=0
        self.f.trace['gameplay']['weapon_visibility_action_suppression']['failures']=1
        self.assertFalse(self.f.run()['mechanical_verified'])
    def test_duplicate_or_missing_eye_never_yields_stable_pair(self):
        self.f.trace['weapon_visibility_eyes'][3]['eye']=0
        r=self.f.run();self.assertTrue(r['mechanical_verified']);self.assertFalse(r['visual_review_ready'])
        self.assertNotIn('hidden',{s['phase'] for s in r['sampleable_eye_pairs']})
    def test_mixed_phase_or_expired_eye_receipt_rejected(self):
        self.f.trace['weapon_visibility_eyes'][3]['phase']=3
        self.assertFalse(self.f.run()['visual_review_ready'])
        self.f.trace['weapon_visibility_eyes'][3]['phase']=2
        row=self.f.trace['weapon_visibility_eyes'][2];row['now_ns']=row['receipt_deadline_ns']
        self.assertFalse(self.f.run()['visual_review_ready'])
    def test_dimensions_must_be_measured_and_byte_count_exact(self):
        self.f.trace['world_color_captures'][2]['target']['width']=4
        self.assertFalse(self.f.run()['visual_review_ready'])
        self.f.trace['world_color_captures'][2]['target']['width']=2
        (self.f.folder/'pair-1-eye-0.rgba').write_bytes(b'bad')
        self.assertFalse(self.f.run()['visual_review_ready'])
    def test_same_input_cannot_be_restamped(self):
        rows=self.f.trace['gameplay']['weapon_visibility_probe']['rows'];duplicate=copy.deepcopy(rows[0]);duplicate['source_observed_ns']-=1;rows.insert(1,duplicate)
        self.assertFalse(self.f.run()['sections']['private_palette_sequence']['passed'])
    def test_other_fixture_and_missing_cleanup_not_accepted(self):
        self.f.manifest['physical_reload_probe']=True
        self.assertFalse(self.f.run()['mechanical_verified'])
        self.f.manifest['physical_reload_probe']=False;self.f.trace['hooks_disabled']=False
        self.assertFalse(self.f.run()['mechanical_verified'])
    def test_duplicate_receiver_frame_and_hidden_source_family_change(self):
        self.f.pairs[1]['native_frame']=self.f.pairs[0]['native_frame']
        self.assertFalse(self.f.run()['sections']['stereo_delivery']['passed'])
        self.f.pairs[1]['native_frame']=1001
        self.f.trace['gameplay']['weapon_visibility_probe']['rows'][1]['weighted_entries']=16
        self.assertFalse(self.f.run()['sections']['private_palette_sequence']['passed'])
    def test_duplicate_json_and_nonfinite_rejected(self):
        for text in ('{"phase":1,"phase":2}','{"value":NaN}'):
            with self.assertRaises(ValueError):parse(text)

if __name__=='__main__':unittest.main()
