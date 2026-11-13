"""Deterministic shared preparation tests; synthetic evidence grants no acceptance."""
import copy
from contextlib import ExitStack
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec)
    # Resolve shared fixture dependencies from this checkout under embedded Python.
    with patch.object(sys,'path',[str(ROOT/'tests'),*sys.path]):
        spec.loader.exec_module(module)
    return module

d=load('prepare_fixed_tests',ROOT/'tools/prepare_bc2_fixed_chamber.py')
fixture=load('prepare_fixed_fixtures',ROOT/'tests/test_bc2_selected_shot_readiness.py')
audit_fixture=load('prepare_fixed_audit',ROOT/'tests/test_bc2_fixed_postreload_observation.py')
insert_fixture=load('prepare_fixed_insert',ROOT/'tests/test_bc2_reload_stimulus_geometry.py')
NOW=1_120_000_000

def snapshot():
    _,s=fixture.SelectedShotReadinessTests().response(NOW)
    return s

def preparation_native():
    flow,b=audit_fixture.FixedPostreloadTests().native()
    baseline=copy.deepcopy(b['closure']['fixedPlan']['baseline'])
    b.update(schema='bc2vr.fixed-preparation-pulse.v1',preparationOnly=True,preparation=baseline,
             preparationObservedNs=b.pop('neutralWrittenNs'))
    del b['closure']
    b.pop('closeResultSha256',None)
    return flow,b

def inserted(with_pickup=False):
    g,xf=insert_fixture.g,insert_fixture.xf
    start=xf.NOW
    source=insert_fixture.complete(xf.snapshot(4))
    pickup=g.build_stimulus('fixed',None,source,start,'Pickup')
    held=xf.held_after_pickup(source,pickup['geometryEvidence'])
    plan=g.build_stimulus('fixed',None,held,start+200_000_000,'Insert',pickup=pickup)
    aligned=xf.aligned_for_insert(held)
    prior={'fixed_aligned':g.check_boundary(plan,aligned,'fixed_aligned',start+300_000_000)}
    done=xf.completed(aligned,plan['geometryEvidence']);done['fixedContainer']['raw']['admittedAtTick']=False
    complete=g.check_boundary(plan,done,'fixed_inserted',start+500_000_000,prior=prior)
    result=dict(schema='bc2vr.reload-stimulus-result.v1',status='ordinary_stimulus_completed',
        stimulusCompleted=True,boundaryChecksPassed=True,observerStopped=True,observationIncomplete=False,
        leftGripHeld=False,pendingCommand=False,plans=[dict(family='fixed',stage=p['stage'],source=copy.deepcopy(p['source']),
            stimulusCompleted=True,boundaryChecksPassed=True) for p in (pickup,plan)],
        boundaries=dict(prior,fixed_inserted=complete))
    current=xf.refresh(done,start+520_000_000,130)
    current['fixedContainer']['lastCompletion']=copy.deepcopy(done['fixedContainer']['lastCompletion'])
    values=plan,result,current,start+520_000_000
    return (*values,pickup) if with_pickup else values


class FixedCaptureCompositionTests(unittest.TestCase):
    """Producer-shaped artifacts only; no game/input/native acceptance."""
    def test_paired_production_order_and_final_plan_source_are_required(self):
        plan,result,_,_,pickup=inserted(True)
        self.assertEqual(d.paired_insertion(plan,pickup,result)['geometry']['stage'],'Insert')
        for mutate in (
            lambda p,r,u:r.update(plans=r['plans'][1:]),
            lambda p,r,u:r['plans'][0].update(stimulusCompleted=False),
            lambda p,r,u:r['plans'][1]['source'].update(nonce=999),
            lambda p,r,u:p.update(continuesHeldCustody=False),
            lambda p,r,u:u['geometryEvidence']['baseline'].update(nativeIdentity={}),
            lambda p,r,u:r.update(observerStopped=False)):
            p,r,u=copy.deepcopy((plan,result,pickup));mutate(p,r,u)
            with self.subTest(mutate=mutate),self.assertRaises(d.transport.REFUSAL):d.paired_insertion(p,u,r)

    def capture_fixture(self,run):
        def save(path,value):
            path.parent.mkdir(parents=True,exist_ok=True)
            raw=d.transport.json_bytes(value);path.write_bytes(raw)
            return d.artifact_pin(path,raw)
        plan,result,current,now,pickup=inserted(True)
        insert=run/'insert';capture=run/'capture-writer'
        result['artifactDirectory']=str(insert)
        result_pin=save(insert/'result.json',result)
        plan_pin=save(insert/'stimulus-02.json',plan)
        save(insert/'stimulus-01.json',pickup)
        host=dict(pid=123,path='test-host',startTicks=1)
        head=dict(valid=True,pos=[0,0,0],ypr=[0,0,0])
        state=dict(pid=123,runtime='xr-sim',cmdSeq=100,lastCmd='note insert',lastCmdError='',errors=0,
            frame=600,endFrames=600,captureSeq=1,sessionState='FOCUSED',head=head,handL=copy.deepcopy(head),
            handR=copy.deepcopy(head),controls=dict(gripL=0,gripR=0,triggerR=0))
        old=dict(host=host,pending=False,sequence=100,sentinel='note insert',commandSha256='old',artifactDirectory=str(insert))
        handoff=dict(schema='bc2vr.fixed-insert-capture-handoff.v1',insertResult=result_pin,insertPlan=plan_pin,
            owner=old,state=state,host=host,sourceSha256='source',startedNs=1_000_000_000,
            deadlineNs=101_000_000_000,retainedNs=2_000_000_000,observationOnly=True,commandAuthority=False)
        handoff_path=run/'handoff.json';save(handoff_path,handoff)
        save(capture/'initial-state.json',state)
        save(capture/'invocation.json',dict(stage=None,pickup_first=False,capture='review',menu_batches=None,
            host=host,startedNs=2_100_000_000,driverSha256=d.transport.digest(Path(d.transport.__file__).read_bytes()),
            plannerSha256=d.transport.digest(Path(d.transport.planner.__file__).read_bytes())))
        metadata=run/'sim/capture/review.json'
        settled=copy.deepcopy(state);settled.update(cmdSeq=102,lastCmd='note root-feed-test',captureSeq=2,frame=610,endFrames=610,
            lastCapture=str(metadata.with_suffix('')))
        commands=['shot review','note root-feed-test']
        owner=dict(host=host,pending=False,sequence=102,sentinel=commands[-1],
            commandSha256=d.transport.digest(('\n'.join(commands)+'\n').encode('ascii')),artifactDirectory=str(capture))
        save(run/'live-driver-owner.json',owner)
        save(capture/'result.json',dict(schema='bc2vr.live-feed-driver.v1',status='commands_acknowledged',commands=2,
            plans=[],pendingCommand=False,leftGripHeld=False,triggerHeld=False,captureSequence=2,artifactDirectory=str(capture)))
        save(capture/'capture.json',dict(path=str(metadata),captureSeq=2,width=10,height=10))
        save(metadata,dict(name='review',frameIndex=605,sessionState='FOCUSED'))
        events=[dict(type='command_request',atNs=2_200_000_000,beforeSequence=100,targetSequence=102,commands=commands),
                dict(type='command_commit',writtenNs=2_210_000_000,committedNs=2_220_000_000,cleanup=False,state=settled)]
        (capture/'events.jsonl').write_bytes(b''.join(d.transport.json_bytes(e) for e in events))
        captures=[]
        for eye in ('left','right'):
            path=metadata.with_name('review_'+eye+'.png');path.write_bytes(b'deterministic fixture; not rendered evidence')
            captures.append(dict(d.artifact_pin(path,path.read_bytes()),eye=eye))
        driver=SimpleNamespace(run=run,host=host,manifest=dict(source_sha256='source'),state=copy.deepcopy(settled),
            deadline=200_000_000_000,stage_deadline=200_000_000_000,
            clearance=dict(reviewedNs=2_300_000_000,captures=captures))
        return driver,handoff_path,capture,save,current,now

    def test_actual_paired_artifact02_and_single_capture_keep_original_deadline(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(d.time,'perf_counter_ns',return_value=3_000_000_000):
            driver,handoff,_,_,current,now=self.capture_fixture(Path(temp))
            context=d.read_insertion(driver,Path(temp)/'insert/result.json',handoff)
            self.assertTrue(context['plan']['path'].endswith('stimulus-02.json'))
            self.assertTrue(context['pickup']['path'].endswith('stimulus-01.json'))
            self.assertEqual(driver.deadline,101_000_000_000)
            d.current_insertion(context,current,now)
            current['fixedContainer']['lastCompletion']['outcome']['receipt']['copiesVerified']=False
            with self.assertRaises(d.fixed.Refusal):d.current_insertion(context,current,now)

    def test_capture_operation_pose_source_lineage_and_stale_pair_are_refused(self):
        mutations=(
            ('invocation.json',lambda x:x.update(menu_batches='movement')),
            ('result.json',lambda x:x.update(commands=4)),
            ('result.json',lambda x:x.update(status='refused')),
            ('initial-state.json',lambda x:x.update(cmdSeq=101)),
            ('initial-state.json',lambda x:x['handL']['pos'].__setitem__(0,1)),
            ('invocation.json',lambda x:x.update(driverSha256='stale')),
            ('capture.json',lambda x:x.update(captureSeq=1)))
        for filename,mutate in mutations:
            with self.subTest(filename=filename),tempfile.TemporaryDirectory() as temp,patch.object(d.time,'perf_counter_ns',return_value=3_000_000_000):
                driver,handoff,folder,save,_,_=self.capture_fixture(Path(temp))
                path=folder/filename;value=d.transport.read_json(path)[0];mutate(value);save(path,value)
                with self.assertRaises(d.transport.REFUSAL):d.read_insertion(driver,Path(temp)/'insert/result.json',handoff)

    def test_expiry_changed_original_source_or_noncurrent_image_refuses(self):
        for target in ('expiry','source','image','review','partial','extra-command'):
            with self.subTest(target=target),tempfile.TemporaryDirectory() as temp,patch.object(d.time,'perf_counter_ns',return_value=3_000_000_000):
                driver,handoff,folder,save,_,_=self.capture_fixture(Path(temp))
                if target in ('expiry','source'):
                    value=d.transport.read_json(handoff)[0]
                    if target=='expiry':value['deadlineNs']=3_000_000_000
                    else:value['sourceSha256']='foreign'
                    save(handoff,value)
                elif target=='image':driver.clearance['captures'][0]['path']=str(Path(temp)/'stale.png')
                elif target=='review':driver.clearance['reviewedNs']=1
                elif target=='partial':
                    path=Path(temp)/'insert/result.json';value=d.transport.read_json(path)[0];value['stimulusCompleted']=False;save(path,value)
                else:
                    path=folder/'events.jsonl';raw=path.read_bytes();path.write_bytes(raw+raw)
                with self.assertRaises(d.transport.REFUSAL):d.read_insertion(driver,Path(temp)/'insert/result.json',handoff)

    def test_immediate_kind4_still_joins_the_real_paired_result_without_fake_close(self):
        with tempfile.TemporaryDirectory() as temp:
            driver,handoff,_,save,_,_=self.capture_fixture(Path(temp))
            original=d.transport.read_json(handoff)[0]
            save(Path(temp)/'live-driver-owner.json',original['owner'])
            context=d.read_insertion(driver,Path(temp)/'insert/result.json')
            self.assertNotIn('captureHandoff',context)
            self.assertNotIn('Close',str(context))

class FixedPreparationTests(unittest.TestCase):
    def test_native_family_selected_without_weapon_name_or_slot_alias(self):
        for slot,kind,label in ((2,3,'first'),(5,4,'other')):
            s=snapshot();x=s['fixedContainer'];family=s['sight']['family']
            s['selected']['selectedSlot']=slot;x['profile']['kind']=kind
            s['selected']['configurationPath']=x['profile']['configurationPath']=family['secondaryConfiguration']='Objects/Weapons/'+label
            s['equipment']['asset']=family['secondaryAsset']=label
            before=copy.deepcopy(s)
            result=d.baseline(s,NOW)
            self.assertEqual(result['profile']['kind'],kind)
            self.assertEqual(s,before)

    def test_nonselected_wrong_physical_primary_action_or_configuration_refuses(self):
        mutations=(lambda s:s['sight']['family'].update(selectedSecondary=False),
            lambda s:s['sight']['family'].update(physicalItem=999),
            lambda s:s['sight']['family'].update(targetAction=33),
            lambda s:s['sight']['family'].update(secondaryConfiguration='wrong'),
            lambda s:s['sight']['family'].update(inventory=999),
            lambda s:s['fixedContainer']['profile'].update(rigFingerprint=999))
        for mutation in mutations:
            s=snapshot();mutation(s)
            with self.subTest(mutation=mutation),self.assertRaises((d.transport.REFUSAL,d.fixed.Refusal)):
                d.baseline(s,NOW)

    def test_loaded_native_copies_pending_hands_and_original_expiry_refuse(self):
        mutations=(lambda s:s['fixedContainer']['view']['snapshot']['counts'].update(loaded=0),
            lambda s:s['fixedContainer']['view']['snapshot']['counts'].update(capacity=2),
            lambda s:s['fixedContainer']['view']['snapshot']['counts'].update(reserve=0),
            lambda s:s['fixedContainer']['view']['snapshot'].update(coherentIdleVerified=False),
            lambda s:s['fixedContainer']['view']['identity'].update(firing=[51,51,53]),
            lambda s:s['fixedContainer']['view']['binding'].update(deadlineNs=NOW),
            lambda s:s['fixedContainer']['tracking'].update(hand={'unexpected':True}),
            lambda s:s['fixedContainer']['tracking'].update(carried=True),
            lambda s:s['fixedContainer']['input'].update(released=[True,True]),
            lambda s:s['fixedContainer'].update(pendingRequest={'unexpected':True}),
            lambda s:s.update(weapon_actions_blocked=True))
        for mutation in mutations:
            s=snapshot();mutation(s)
            with self.subTest(mutation=mutation),self.assertRaises((d.transport.REFUSAL,d.fixed.Refusal)):
                d.baseline(s,NOW)

    def test_after_shot_requires_exact_one_round_and_same_custody(self):
        s=snapshot();b=d.baseline(s,NOW)
        s['fixedContainer']['view']['snapshot']['counts']['loaded']=0
        self.assertEqual(d.after_shot(s,NOW,b),dict(loaded=0,reserve=7,capacity=1))
        for mutation in (lambda s:s['fixedContainer']['view']['snapshot']['counts'].update(reserve=6),
                         lambda s:s['fixedContainer']['view'].update(request=2),
                         lambda s:s['fixedContainer']['tracking']['gun']['token'].update(id=999),
                         lambda s:s['fixedContainer']['view']['identity'].update(serverItem=999)):
            altered=copy.deepcopy(s);mutation(altered)
            with self.subTest(mutation=mutation),self.assertRaises((d.transport.REFUSAL,d.fixed.Refusal)):
                d.after_shot(altered,NOW,b)

    def test_new_audit_joins_all_three_copies_without_a_close_receipt(self):
        flow,b=preparation_native();result=d.transport.audit_post_reload_shot(flow,b)
        self.assertEqual(result['status'],'native_fixed_preparation_shot_observed')
        self.assertEqual((result['loadedBefore'],result['loadedAfter'],result['roundsObserved']),(1,0,1))
        self.assertTrue(result['preparationOnly']);self.assertFalse(result['reloadCompleted'])
        self.assertFalse(result['nativeAcceptance']);self.assertFalse(result['headsetAcceptance'])
        self.assertEqual({e['branch'] for e in result['events']},{0,1,2})

    def test_kind4_post_insert_uses_actual_insert_gate_without_close(self):
        plan,result,current,now=inserted()
        context=d.insertion_context(plan,result)
        d.current_insertion(context,current,now)
        self.assertEqual(context['geometry']['stage'],'Insert')
        self.assertEqual(context['geometry']['selection']['profile']['kind'],4)
        self.assertNotIn('Close',str(context))

    def test_kind3_or_unfinished_wrong_alignment_cannot_bypass_close_gate(self):
        mutations=(lambda p,r:p['geometryEvidence']['selection']['profile'].update(kind=3),
            lambda p,r:r.update(status='refused'),
            lambda p,r:r.update(pendingCommand=True),
            lambda p,r:r['boundaries']['fixed_aligned']['gate']['completionPrerequisite'].update(stage='Close'),
            lambda p,r:r['boundaries']['fixed_inserted']['gate'].update(observed=False))
        for mutation in mutations:
            plan,result,_,_=inserted();mutation(plan,result)
            with self.subTest(mutation=mutation),self.assertRaises(d.transport.REFUSAL):
                d.insertion_context(plan,result)

    def test_kind4_current_completion_requires_exact_verified_native_receipt(self):
        plan,result,current,now=inserted();context=d.insertion_context(plan,result)
        for mutation in (lambda s:s['fixedContainer']['lastCompletion']['outcome']['receipt'].update(copiesVerified=False),
                         lambda s:s['fixedContainer']['view']['snapshot']['counts'].update(reserve=5),
                         lambda s:s['fixedContainer']['tracking'].update(lifetime=999),
                         lambda s:s.update(observed_ns=context['completed']['observedNs'])):
            s=copy.deepcopy(current);mutation(s)
            with self.subTest(mutation=mutation),self.assertRaises((d.transport.REFUSAL,d.fixed.Refusal)):
                d.current_insertion(context,s,now)

    def test_only_post_insert_mode_can_consume_last_reserved_round(self):
        s=snapshot();s['fixedContainer']['view']['snapshot']['counts']['reserve']=0
        with self.assertRaises(d.transport.REFUSAL):d.baseline(s,NOW)
        self.assertEqual(d.baseline(s,NOW,require_reserve=False)['counts']['reserve'],0)

    def test_post_insert_publication_still_consumes_current_receipt_and_review(self):
        with tempfile.TemporaryDirectory() as temp:
            v,p,_,_=self.pulse_driver(Path(temp));s=snapshot()
            s['fixedContainer']['view']['snapshot']['counts']['reserve']=0
            v.latest_snapshot=s;v.preparation=d.baseline(s,NOW,require_reserve=False)
            v.clearance,_=d.transport.read_json(p);v.insertion={'synthetic':'test dispatch only'}
            v.commands=0;v.held_trigger=False;v.pending_command=False
            observed=[];v.admit_snapshot=lambda *args:observed.append(args)
            with patch.object(d,'current_insertion') as receipt:
                v.validate_publication(['trigger r 1'],None,False)
                receipt.assert_called_once_with(v.insertion,s,NOW)
            self.assertEqual(len(observed),1)
            with patch.object(d,'current_insertion',side_effect=d.transport.REFUSAL('invalid actual receipt')):
                with self.assertRaisesRegex(d.transport.REFUSAL,'invalid actual receipt'):
                    v.validate_publication(['trigger r 1'],None,False)
            v.commands=2
            with self.assertRaises(d.transport.REFUSAL):v.validate_publication(['trigger r 1'],None,False)

    def test_preparation_never_accepts_fabricated_close_or_reload_identity(self):
        for field,value in (('preparationOnly',False),('closure',{}),('closeResultSha256','fake')):
            flow,b=preparation_native();b[field]=value
            with self.subTest(field=field),self.assertRaises(d.transport.REFUSAL):
                d.transport.audit_post_reload_shot(flow,b)

    def test_audit_missing_copy_wrong_firing_reserve_timing_or_override_refuses(self):
        mutations=(lambda f,b:f.update(records=[r for r in f['records'] if r['before']['branch']!=2]),
            lambda f,b:b['preparation']['nativeIdentity']['firing'].__setitem__(1,999),
            lambda f,b:b['afterCounts'].update(reserve=999),
            lambda f,b:b.update(releaseWrittenNs=b['pressWrittenNs']+100_000_001),
            lambda f,b:f.update(drained=False),
            lambda f,b:f['records'][0]['fixed_input'].update(applied=1))
        for mutation in mutations:
            flow,b=preparation_native();mutation(flow,b)
            with self.subTest(mutation=mutation),self.assertRaises(d.transport.REFUSAL):
                d.transport.audit_post_reload_shot(flow,b)

    def pulse_driver(self,folder,*,late=False,wrong_after=False):
        def mutate(s,v):
            if wrong_after:
                s['fixedContainer']['view']['snapshot']['counts']['reserve']=6
        # Reuse the assembled prepare/send/ACK/passive observation fixture.
        # Only the synthetic runtime timer or native count observation changes.
        runtime,path,_=TimedPreparationObservationTests.timed_driver(self,folder,mutate)
        if late:
            runtime.timer_ms=400
        return runtime.driver,path,runtime,runtime.driver.artifacts.saved

    def test_one_bounded_pulse_and_permanent_same_owner_attempt(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);v,p,runtime,saved=self.pulse_driver(folder)
            result=v.prepare(p)
            self.assertEqual(runtime.batches(),[[d.transport.TIMED_TRIGGER_COMMAND]])
            self.assertIsNone(result['releaseWrittenNs'])
            self.assertIsNone(v.timed_trigger)
            self.assertFalse(v.held_trigger or v.pending_command)
            self.assertTrue(result['preparationOnly']);self.assertNotIn('closure',result)
            second,p,runtime2,_=self.pulse_driver(folder)
            with self.assertRaises(FileExistsError):second.prepare(p)
            self.assertEqual(runtime2.batches(),[])

    def test_late_release_or_uncertain_after_never_repeats_press(self):
        for late,wrong in ((True,False),(False,True)):
            with self.subTest(late=late),tempfile.TemporaryDirectory() as temp:
                v,p,runtime,_=self.pulse_driver(Path(temp),late=late,wrong_after=wrong)
                with self.assertRaises(d.transport.REFUSAL):v.prepare(p)
                self.assertEqual(runtime.batches(),[[d.transport.TIMED_TRIGGER_COMMAND]])
                self.assertEqual(v.held_trigger,late)
                self.assertEqual(v.timed_trigger is not None,late)
                with self.assertRaisesRegex(d.transport.REFUSAL,'already used'):
                    v.single_trigger_pulse()
                self.assertEqual(runtime.batches(),[[d.transport.TIMED_TRIGGER_COMMAND]])

    def test_current_review_capture_hash_and_aim_are_not_optional(self):
        for change in ('hash','aim'):
            with self.subTest(change=change),tempfile.TemporaryDirectory() as temp:
                v,p,runtime,_=self.pulse_driver(Path(temp))
                if change=='hash':(Path(temp)/'right.png').write_bytes(b'changed')
                else:
                    review,_=d.transport.read_json(p)
                    review['handR']={'changed':True}
                    runtime.json_files[p]=review
                    runtime.files[p]=d.transport.json_bytes(review)
                with self.assertRaises((d.transport.REFUSAL,d.clearance_geometry.Refusal)):v.prepare(p)
                self.assertEqual(runtime.batches(),[])

class TimedPreparationObservationTests(unittest.TestCase):
    """Assembled prepare/send/ACK/passive read path, with simulated runtime IO."""
    def timed_driver(self, folder, mutate=None, missing=None):
        timed=load('fixed_preparation_timed_fixture',ROOT/'tests/test_bc2_timed_trigger.py')
        self.enterContext(patch.object(timed, 'd', d.transport))
        self.enterContext(patch.object(timed.transport_tests, 'd', d.transport))
        h = timed.RuntimeHarness(self, ack_ms=88)
        v = h.driver
        v.__class__ = d.PreparationDriver
        del v.validate_publication
        v.run, v.manifest = folder, dict(source_sha256='source')
        v.active_plan = v.demand_reference = v.demand_started = None
        v.demand_finished = False
        before = snapshot()
        before['input']['hands'][1]['squeeze'] = 0.
        h.publish(before)
        captures = []
        for eye in ('left', 'right'):
            path = folder/(eye+'.png')
            raw = eye.encode(); path.write_bytes(raw); h.files[path] = raw
            captures.append(dict(eye=eye, path=str(path), bytes=len(raw), sha256=d.transport.digest(raw)))
        clearance = dict(schema='bc2vr.root-fire-clearance.v1', rootReviewedVisibleCorridor=True,
            host=v.host, owner=before['owner'], configurationPath=before['selected']['configurationPath'],
            rig=before['rig'], reviewedNs=h.now-1, referenceHead=before['input']['referenceHead'],
            rightAim=before['input']['hands'][1]['aim'], captures=captures,
            **{key: copy.deepcopy(h.state[key]) for key in ('head', 'handL', 'handR', 'controls')})
        path = folder/'clearance.json'
        h.json_files[path], h.files[path] = clearance, d.transport.json_bytes(clearance)
        observed = []
        def respond():
            if h.response_due is not None and h.now >= h.response_due:
                s = fixture.fixed.refresh(before, h.now, int(h.files[v.request_path]))
                if v.commands:
                    if not observed:
                        s['fixedContainer'].update(view=None, viewFresh=False)
                        if missing is not None:
                            missing(s)
                    else:
                        s['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
                        if mutate is not None:
                            mutate(s, v)
                    observed.append(copy.deepcopy(s))
                h.publish(s); h.response_due = None
        h.on_sleep = respond
        return h, path, observed

    def scoped_harness(self):
        stack = ExitStack()
        original = self.enterContext
        self.enterContext = stack.enter_context
        stack.callback(setattr, self, 'enterContext', original)
        return stack

    def test_timed_preparation_missing_then_empty_uses_real_shared_observer(self):
        with tempfile.TemporaryDirectory() as temp:
            h, path, observed = self.timed_driver(Path(temp))
            v = h.driver
            result = v.prepare(path)
            self.assertEqual(h.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])
            self.assertEqual(v.commands, 2)
            self.assertEqual(len(observed), 2)
            self.assertIsNone(observed[0]['fixedContainer']['view'])
            self.assertEqual(result['afterCounts'], dict(loaded=0, reserve=7, capacity=1))
            self.assertEqual(v.reference['fixedContainer']['view']['snapshot']['counts']['loaded'], 1)
            self.assertEqual(v.stage_deadline, result['releaseCommittedNs']+d.NS)
            self.assertEqual(v.selected_release_observed_ns(), result['releaseCommittedNs'])
            held = [e for e in v.artifacts.events if e['type']=='selected_ammo_post_shot_unpublished']
            self.assertEqual(len(held), 1); self.assertFalse(held[0]['commandAuthority'])
            self.assertFalse(result['nativeShotObserved'] or result['nativeAcceptance'])
            with self.assertRaisesRegex(d.transport.REFUSAL, 'already attempted'):
                v.fresh_available()

    def test_timed_replacement_still_rejects_binding_identity_lifetime_and_pre_release_input(self):
        mutations = (
            lambda s,v:s['fixedContainer']['view']['binding'].update(switching=999),
            lambda s,v:s['fixedContainer']['view']['identity'].update(serverItem=999),
            lambda s,v:s['fixedContainer']['tracking'].update(lifetime=999),
            lambda s,v:s['fixedContainer']['view']['snapshot']['counts'].update(reserve=6),
            lambda s,v:s['fixedContainer']['view']['binding'].update(deadlineNs=s['observed_ns']),
            lambda s,v:s['input_evidence'].update(observedNs=v.selected_release_observed_ns()-1),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as temp, self.scoped_harness():
                h, path, _ = self.timed_driver(Path(temp), mutate)
                with self.assertRaises((d.transport.REFUSAL, d.fixed.Refusal)):
                    h.driver.prepare(path)
                self.assertEqual(h.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])
                self.assertEqual(h.driver.commands, 2)

    def test_timed_observer_requires_completed_pulse_and_exact_settled_command_owner(self):
        for field, value in (('selected_timed_release', None), ('timed_trigger', {'pending':True}),
                ('commands',4), ('sequence',999), ('last_write',0), ('last_commit',0),
                ('pending_batch',['trigger r 0']), ('timed_trigger_supported',False),
                ('_single_trigger_used',False), ('held_trigger',True), ('pending_command',True)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temp, self.scoped_harness():
                h, path, _ = self.timed_driver(Path(temp))
                v=h.driver; v.prepare(path)
                setattr(v, field, value)
                with self.assertRaises(d.transport.REFUSAL):
                    v.check_post_shot_observation_owner()
                self.assertEqual(h.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])

    def test_timed_pulse_failure_retains_timer_without_observer_authority(self):
        with tempfile.TemporaryDirectory() as temp:
            h, path, observed = self.timed_driver(Path(temp))
            h.timer_ms = 400
            with self.assertRaises(d.transport.REFUSAL):
                h.driver.prepare(path)
            self.assertIsNone(getattr(h.driver,'selected_timed_release',None))
            self.assertIsNotNone(h.driver.timed_trigger)
            with self.assertRaises(d.transport.REFUSAL):
                h.driver.check_post_shot_observation_owner()
            self.assertEqual(observed,[])
            self.assertEqual(h.batches(),[[d.transport.TIMED_TRIGGER_COMMAND]])

    @staticmethod
    def default_missing_tracking(s):
        # Native84 serializes these defaults and its constant spent-case policy
        # even when Tick could not read an ammunition view or publish custody.
        s['fixedContainer']['tracking'] = dict(enabled=False, current=False, blocksFire=False,
            carried=False, pending=False, step=0, targetStep=0, lifetime=0, revision=0,
            gun=dict(token=dict(id=0, owner=dict(actor=0, actorGeneration=0, equipGeneration=0, space=0),
                hand=0, kind=0, item=dict(id=0,generation=0), contact=dict(id=0,generation=0), prerequisiteClaim=0),
                deadlineNs=0, inputSequence=0), hand=None, target=None, carriedSupply=None,
            spentCasePresent=False, spentCaseEpoch=0, spentCaseEjectedNs=0,
            spentCaseEjectionWorldMeters=None, spentCasePoseWeaponMeters=None,
            spentCasePolicy=dict(modOwned=True, outwardAxis=[0,0,1], maximumWorldUpDot=-.5,
                                 dwellNs=100_000_000, maxInputGapNs=100_000_000),
            pouchContact=False, grip=False, gripPressed=False)

    def test_timed_preparation_default_spent_case_fields_only_allow_passive_wait(self):
        with tempfile.TemporaryDirectory() as temp:
            def missing(s):
                self.default_missing_tracking(s)
                s['fixedContainer']['tracking']['presentationMetadata'] = {'diagnostic': True}
                s['fixedContainer']['tracking']['spentCasePolicy']['presentationMetadata'] = None
            h, path, observed = self.timed_driver(Path(temp), missing=missing)
            v = h.driver
            result = v.prepare(path)
            self.assertEqual(h.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])
            self.assertEqual(v.commands, 2)
            self.assertEqual(len(observed), 2)
            self.assertIsNone(observed[0]['fixedContainer']['view'])
            self.assertEqual(result['afterCounts'], dict(loaded=0, reserve=7, capacity=1))
            self.assertEqual(v.stage_deadline, result['releaseCommittedNs']+d.NS)
            self.assertEqual(v.reference['fixedContainer']['view']['snapshot']['counts']['loaded'], 1)
            held, = [e for e in v.artifacts.events if e['type']=='selected_ammo_post_shot_unpublished']
            self.assertFalse(held['commandAuthority'])
            self.assertEqual(held['observationDeadlineNs'], v.stage_deadline)
            self.assertFalse(result['nativeShotObserved'] or result['nativeAcceptance'])

    def test_missing_tracking_malformed_spent_case_or_foreign_owner_refuses_without_replay(self):
        mutations = (
            lambda x:x['tracking'].update(spentCasePresent=True),
            lambda x:x['tracking'].update(spentCaseEpoch=1),
            lambda x:x['tracking'].update(spentCaseEjectedNs=1),
            lambda x:x['tracking'].update(spentCaseEjectionWorldMeters=[]),
            lambda x:x['tracking'].update(spentCasePoseWeaponMeters=[]),
            lambda x:x['tracking']['spentCasePolicy'].update(modOwned=False),
            lambda x:x['tracking']['spentCasePolicy'].update(outwardAxis=[1,0,0]),
            lambda x:x['tracking']['spentCasePolicy'].update(dwellNs='100000000'),
            lambda x:x['tracking'].pop('spentCaseEpoch'),
            lambda x:x['tracking'].update(current=True),
            lambda x:x['tracking'].update(lifetime=1),
            lambda x:x['tracking'].update(hand={}),
            lambda x:x['tracking'].update(pending=True),
            lambda x:x.update(leftClaim={}),
            lambda x:x.update(pendingRequest={}),
            lambda x:x['owner'].update(soldier=999),
            lambda x:x['input']['owner'].update(actor=999),
            lambda x:x['tracking']['gun']['token']['owner'].update(actor=999),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as temp, self.scoped_harness():
                def missing(s):
                    self.default_missing_tracking(s)
                    mutate(s['fixedContainer'])
                h, path, observed = self.timed_driver(Path(temp), missing=missing)
                with self.assertRaises((d.transport.REFUSAL, d.fixed.Refusal)):
                    h.driver.prepare(path)
                self.assertEqual(h.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])
                self.assertEqual(len(observed), 1)
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertFalse(any(e['type']=='selected_ammo_post_shot_unpublished'
                                     for e in h.driver.artifacts.events))

    @unittest.skipUnless(os.environ.get('BC2_FIXED_PREPARATION_REPLAY'),
                         'optional retained normal83/07 timed preparation replay')
    def test_actual_normal83_07_absence_is_diagnostic_then_original_observation_expires(self):
        folder = Path(os.environ['BC2_FIXED_PREPARATION_REPLAY'])
        pins = {
            'snapshot-002.json':'1ff1c759818d1fb35a82604f8c4cc82dc8d6a63862fd1479689a37e4a85899c7',
            'snapshot-003.json':'58cf164fdca643ab21d93528754d8ce91e7601f0bc1ecc89dd3dfc442d19a1f9',
            'events.jsonl':'ff8552198b6405c331011907af2074a4a3cf38c378a344a6523db078e30a1223',
            'request-003-timing.json':'c268976bbec61536fb28bc0e389a558344b0c2293ba98464f4f8e95c9a51306e',
            'receive-timing-failure.json':'16e09460bd0c4be6764ed1cb1d81bfdd6797d6fbf9df376acb7993506b6bc4e5',
        }
        blobs = {name:(folder/name).read_bytes() for name in pins}
        self.assertEqual({name:hashlib.sha256(raw).hexdigest() for name,raw in blobs.items()},pins)
        self.assert_retained_absence_expires(blobs)

    @unittest.skipUnless(os.environ.get('BC2_FIXED_PREPARATION_GAP_REPLAY'),
                         'optional retained normal84/01 default spent-case tracking replay')
    def test_actual_normal84_01_default_spent_case_tracking_waits_without_ammunition_success(self):
        folder = Path(os.environ['BC2_FIXED_PREPARATION_GAP_REPLAY'])
        pins = {
            'snapshot-002.json':'dd289aba406a71eb205cfcf3c58652a0300c839586ee6f8a877c147051bdf090',
            'snapshot-003.json':'5c8b74d45aeca405103e108844970b71373bf8eab7736904e913006e2c94337d',
            'events.jsonl':'1807cae6062fae28d2455cd76c3c66e0a62d023efb51c47cdf717dd22a37f918',
            'request-003-timing.json':'b41c9678eb47eea0353a908e597e6eb7802909cbf271317462b4d572c9447560',
            'receive-timing-failure.json':'37a7bd222864230aca48bb70074f2625f7d08facffddc7a7d3e5652b3cc120a1',
        }
        blobs = {name:(folder/name).read_bytes() for name in pins}
        self.assertEqual({name:hashlib.sha256(raw).hexdigest() for name,raw in blobs.items()},pins)
        actual = json.loads(blobs['snapshot-003.json'])
        expected = copy.deepcopy(actual)
        self.default_missing_tracking(expected)
        self.assertEqual(actual['fixedContainer']['tracking'], expected['fixedContainer']['tracking'])
        # This episode never produced a replacement ammo view. Its replay
        # reaches the existing polling bound before the unchanged time budget.
        self.assert_retained_absence_expires(blobs, 'iteration bound')

    def assert_retained_absence_expires(self, blobs, expected_error='timed out'):
        before, absent = [json.loads(blobs[name]) for name in ('snapshot-002.json','snapshot-003.json')]
        events = [json.loads(line) for line in blobs['events.jsonl'].splitlines()]
        request, = [e for e in events if e['type']=='command_request']
        commit, = [e for e in events if e['type']=='command_commit']
        neutral, = [e for e in events if e['type']=='timed_trigger_neutral_observed']
        self.assertEqual(request['commands'][0],d.transport.TIMED_TRIGGER_COMMAND)
        self.assertEqual(neutral['runtimeSha256'],d.transport.TIMED_TRIGGER_RUNTIME_SHA256)
        self.assertIsNone(absent['fixedContainer']['view'])
        with tempfile.TemporaryDirectory() as temp:
            h, _, _ = self.timed_driver(Path(temp)); v=h.driver
            h.on_sleep = None
            failure = json.loads(blobs['receive-timing-failure.json'])
            h.now = failure['firstPhaseError']['atNs'] if failure['firstPhaseError'] else failure['frozenNs']
            self.enterContext(patch.object(d.time,'sleep',lambda seconds:setattr(h,'now',h.now+round(seconds*d.NS))))
            h.state = copy.deepcopy(neutral['neutralState']); v.state=copy.deepcopy(h.state)
            v.reference = v.latest_snapshot = before
            v.demand_finished, v.demand_started, v.commands = True, before['observed_ns'], 2
            v.deadline = neutral['driverDeadlineNs']; v.hard_deadline = v.deadline
            v.stage_deadline = neutral['neutralObservedNs']+d.NS
            v.last_write, v.last_commit = commit['writtenNs'],commit['committedNs']
            v.sequence,v.sentinel,v.capture_sequence = h.state['cmdSeq'],h.state['lastCmd'],h.state['captureSeq']
            v.pending_batch = request['commands']; v._single_trigger_used = True
            # Reconstitute only the successful transport completion from this
            # retained ACK/natural-neutral event; never invent an ammo result.
            v.selected_timed_release = (v.sequence,v.last_write,v.last_commit,neutral['neutralObservedNs'])
            h.files[v.command_path] = ('\n'.join(v.pending_batch)+'\n').encode()
            v.command_hash = d.transport.digest(h.files[v.command_path])
            timing = json.loads(blobs['request-003-timing.json'])
            v.last_request,v.pending_nonce,v.snapshot_index = timing['requestStartedNs'],int(absent['nonce']),3
            v.request_bytes = (str(v.pending_nonce)+'\n').encode(); h.files[v.request_path]=v.request_bytes
            h.publish(absent); h.files[v.snapshot_path]=blobs['snapshot-003.json']; h.response_after=None
            clocks = v.deadline,v.hard_deadline,v.stage_deadline,v.demand_started
            with self.assertRaisesRegex(d.transport.REFUSAL,'post-stage receipt observation '+expected_error):
                v.fresh_available()
            self.assertEqual(v.artifacts.saved['snapshot-003.json'],blobs['snapshot-003.json'])
            self.assertEqual((v.deadline,v.hard_deadline,v.stage_deadline,v.demand_started),clocks)
            # Timeout is checked immediately after the existing 1 ms poll.
            self.assertLess(h.now, v.stage_deadline+1_000_000)
            self.assertIs(v.reference,before); self.assertIsNone(v.latest_snapshot)
            self.assertEqual(h.command_writes,[])
            held = [e for e in v.artifacts.events if e['type']=='selected_ammo_post_shot_unpublished']
            self.assertEqual(len(held),1); self.assertFalse(held[0]['commandAuthority'])


if __name__=='__main__':unittest.main()
