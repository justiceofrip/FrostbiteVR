"""Native post-draw firing and effect-pair coverage remain distinct verdicts."""
import copy
import unittest
import test_audit_body_holster_fire as legacy


class NativeFireTests(unittest.TestCase):
    def setUp(self):
        self.base=legacy.FireTests()
        self.base.setUp()
        self.addCleanup(self.base.tearDown)
        self.reset()

    def reset(self):
        self.base.fire()
        self.f=self.base.f
        self.g=self.f.trace['gameplay']
        self.flow=self.g['reload_flow']
        self.flow['owner_lock_drops']=0
        for s in self.f.after['samples']:
            s['state']['loaded']=5
        for r in self.flow['records']:
            for side in ('before','after'):
                if r[side]['loaded']==6:r[side]['loaded']=5
                r[side]['firing']=r[side]['branch']+2
        # Two native rounds, all three distinct native firing copies. The second
        # is deliberately missing its client effect-composition observation.
        self.events=[]
        for branch in range(3):
            for index,ms in enumerate((5010,5060)):
                row=copy.deepcopy(self.flow['records'][branch*2])
                row.update(id=100+branch*10+index,kind=0,begin_ns=ms*1_000_000,
                    end_ns=ms*1_000_000+100_000,begin_tick_ms=ms,end_tick_ms=ms)
                row['before'].update(loaded=7-index,current=2 if index==0 else 9,next=2 if index==0 else 9)
                row['after'].update(loaded=6-index,current=9,next=9)
                row['context_before']=dict(decoded_valid=True,raw_1c=10+index,input_flags=1)
                row['context_after']=dict(row['context_before'])
                self.events.append(row)
        self.flow['records'].extend(self.events)
        self.shots=self.g['fire_origin_observation']['records']
        second=copy.deepcopy(self.shots[-1]);second.update(shot_token=11,ms=5060)
        second['tracked_shot_candidate']['event_generation']=65
        self.shots.append(second)

    def verdict(self):
        return self.f.run()

    def assert_rejected(self):
        result=self.verdict()
        self.assertFalse(result['post_draw_native_firing_verified'],result)
        self.assertFalse(result['sections']['post_draw_native_firing']['passed'],result)

    def test_partial_composition_has_positive_native_proof_and_strict_failure(self):
        r=self.verdict()
        self.assertTrue(r['post_draw_native_firing_verified'],r)
        self.assertFalse(r['mechanical_verified'])
        self.assertFalse(r['sections']['native_ammo_counts']['passed'])
        e=r['sections']['post_draw_native_firing']['evidence']
        self.assertFalse(e['complete_client_server_composition_pairing'])
        self.assertEqual(e['paired_composition_tokens'],[10])
        self.assertEqual(e['unpaired_server_tokens'],[11])
        self.assertEqual(e['native_round_decrease'],2)
        self.assertEqual(len(e['native_events']),2)
        self.assertTrue(all(len(x['count_events'])==3 for x in e['native_events']))
        self.assertFalse(e['native_events'][1]['exact_composition_pair'])
        for key in ('headset_verified','production_input_accepted','production_admission_decision','all_effect_paths_verified'):
            self.assertFalse(e[key])

    def test_fully_paired_remains_strictly_valid(self):
        row=copy.deepcopy(self.shots[-1]);row['client_path']=True;self.shots.append(row)
        r=self.verdict()
        self.assertTrue(r['mechanical_verified'],r)
        self.assertTrue(r['post_draw_native_firing_verified'],r)
        self.assertTrue(r['sections']['post_draw_native_firing']['evidence']['complete_client_server_composition_pairing'])

    def test_count_difference_without_update_events_is_not_positive(self):
        self.flow['records']=self.flow['records'][:6]
        self.assert_rejected()

    def test_unmatched_client_only_or_no_genuine_pair_rejected(self):
        self.shots[0]['shot_token']=99;self.assert_rejected()
        self.reset();self.shots[:]=[r for r in self.shots if not r['client_path']];self.assert_rejected()

    def test_missing_server_receipt_for_second_event_rejected(self):
        self.shots.pop();self.assert_rejected()

    def test_native_event_owner_mismatch_rejected(self):
        for side,key in [('before','weapon'),('after','space'),('after','server_firing')]:
            self.reset()
            if key=='server_firing':key='firing'
            self.events[-1][side][key]+=1
            self.assert_rejected()

    def test_mismatched_event_token_or_unstable_context_rejected(self):
        for side,key,value in [('context_before','raw_1c',99),('context_after','raw_1c',99),
                               ('context_before','decoded_valid',False),('context_before','input_flags',0),
                               ('context_before','input_flags',3),('context_after','input_flags',5)]:
            self.reset();self.events[-1][side][key]=value;self.assert_rejected()

    def test_native_event_or_muzzle_outside_own_window_rejected(self):
        for key,value in [('begin_ns',4_999_000_000),('end_ns',5_081_000_000)]:
            self.reset();self.events[-1][key]=value;self.assert_rejected()
        self.reset();self.shots[-1]['ms']=5061;self.assert_rejected()

    def test_mismatched_matched_pose_or_invalid_unpaired_pose_rejected(self):
        for row in (1,2):
            self.reset()
            self.shots[row]['tracked_shot_candidate']['event_muzzle'][0]=float('nan') if row==2 else .9
            self.assert_rejected()
        self.reset();self.shots[1]['tracked_shot_candidate']['event_generation']+=1;self.assert_rejected()

    def test_changed_count_reserve_kind_or_state_rejected(self):
        for side,key,value in [('after','loaded',4),('after','reserve',9),('after','current',10)]:
            self.reset();self.events[-1][side][key]=value;self.assert_rejected()
        self.reset();self.events[-1]['kind']=3;self.assert_rejected()

    def test_missing_copy_duplicate_token_or_unretained_event_rejected(self):
        self.flow['records'].remove(self.events[-1]);self.assert_rejected()
        self.reset()
        for event in self.events:
            event['context_before']['raw_1c']=event['context_after']['raw_1c']=10
        self.assert_rejected()
        self.reset();self.events[-1]['identity_retained']=False;self.assert_rejected()

    def test_observer_gaps_do_not_prove_complete_native_event_chain(self):
        for key in ('owner_lock_drops','record_lock_drops','dropped'):
            self.reset();self.flow[key]=1;self.assert_rejected()

    def test_zero_native_event_token_valid_when_all_sources_match(self):
        for event in self.events:
            if event['context_before']['raw_1c']==10:
                event['context_before']['raw_1c']=event['context_after']['raw_1c']=0
        for row in self.shots:
            if row['shot_token']==10:row['shot_token']=0
        self.assertTrue(self.verdict()['post_draw_native_firing_verified'])

    def test_positive_run_verdict_also_requires_actual_draw_and_clean_lifetime(self):
        self.f.completion['bootstrap_exit']=12
        r=self.verdict()
        self.assertTrue(r['sections']['post_draw_native_firing']['passed'])
        self.assertFalse(r['post_draw_native_firing_verified'])


if __name__=='__main__':unittest.main()
