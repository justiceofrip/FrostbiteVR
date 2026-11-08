"""ROOT-only ordinary preparation of an observed loaded fixed underbarrel.

Uses the shared SelectedAmmoDriver; changes no pose, native state or ammunition.
Preparation is not reload completion. One current stereo corridor review permits
one bounded press/release, then native three-copy Fire proof is audited only
after ordinary session retirement. No weapon-name or installed-version aliases.
"""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True


def load(name):
    path = Path(__file__).with_name(name+'.py')
    spec = importlib.util.spec_from_file_location('fixed_preparation_'+name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fixed_driver = load('drive_bc2_fixed_container_simulation')
clearance_geometry = load('bc2_magazine_bolt_stimulus')
transport, fixed = fixed_driver.transport, fixed_driver.planner
require, integer, NS = transport.require, transport.integer, transport.NS


def selected_family(snapshot):
    """Join the exact current native primary/secondary family, never a name map."""
    sight, selected, eq = snapshot['sight'], snapshot['selected'], snapshot['equipment']
    family = sight['family']
    require(family['selectedSecondary'] is True
            and integer(family['primaryAction']) in (25, 36)
            and integer(family['targetAction']) == integer(family['primaryAction'])
            and family['secondaryAsset'] == eq['asset']
            and family['secondaryConfiguration'] == selected['configurationPath']
            and family['primaryConfiguration'] != family['secondaryConfiguration'],
            'exact selected underbarrel family required')
    for name in ('persistentId', 'primaryAsset', 'primaryConfiguration',
                 'secondaryAsset', 'secondaryConfiguration'):
        require(isinstance(family[name], str) and bool(family[name]), 'incomplete native family')
    require(integer(family['observedMs']) > 0
            and integer(family['physicalItem']) == integer(eq['persistence']) > 0
            and integer(family['inventory']) == integer(selected['inventory']) > 0
            and integer(family['targetWeapon']) > 0
            and integer(family['targetWeapon']) not in (integer(eq['weapon']), integer(eq['persistence']))
            and 0 <= integer(family['targetSlot']) < 9
            and 0 <= integer(selected['selectedSlot']) < 9
            and integer(family['targetSlot']) != integer(selected['selectedSlot']),
            'selected underbarrel physical/inventory target differs')
    return {k:v for k,v in family.items() if k != 'observedMs'}


def baseline(snapshot, now, *, require_reserve=True):
    family = selected_family(snapshot)
    x = snapshot['fixedContainer']
    p, t = x['profile'], x['tracking']
    view = fixed.ammo(snapshot, now, idle=True)
    counts = fixed.counts(view['snapshot']['counts'])
    require(integer(p['kind']) in (3, 4) and integer(p['capacity']) == integer(p['roundsPerInsertion']) == 1
            and p['configurationPath'] == snapshot['selected']['configurationPath']
            and p['meshPath'] == snapshot['selected']['matchedMeshPath']
            and integer(p['rigFingerprint']) == integer(snapshot['rig']['fingerprint']) > 0
            and integer(p['id']) > 0 and integer(p['revision']) > 0,
            'exact registered single-round fixed profile required')
    require(counts['loaded'] == 1 and (counts['reserve'] > 0 or not require_reserve)
            and snapshot['weapon_actions_blocked'] is False and t['blocksFire'] is False
            and t['pending'] is False and t['hand'] is None and x['leftClaim'] is None
            and t['target'] is None and t['carried'] is False and t['carriedSupply'] is None
            and x['pendingRequest'] is None,
            'preparation requires one loaded round, reserve and no held or pending mechanism')
    gun = fixed.claim(t['gun'], x['input'], now, 1, 1)
    require(gun['item'] == dict(id=integer(family['physicalItem']),
                               generation=integer(snapshot['physical_equipment_generation']))
            and integer(gun['prerequisiteClaim']) == 0
            and x['input']['released'] == [True,False],
            'preparation requires current right GunHold and released left hand')
    return copy.deepcopy(dict(owner=snapshot['owner'], nativeIdentity=view['identity'],
        binding=fixed.binding_key(view['binding']), counts=counts, family=family,
        profile=p, gun=gun, lifetime=t['lifetime'], request=view['request'],
        context=view['binding']['context']))


def insertion_context(plan, completed):
    """Kind4 ends at Insert. Never relabel it as the kind3 Close operation."""
    require(completed['schema']=='bc2vr.reload-stimulus-result.v1'
            and completed['status']=='ordinary_stimulus_completed'
            and completed['stimulusCompleted'] is True and completed['boundaryChecksPassed'] is True
            and completed['observerStopped'] is True and completed['observationIncomplete'] is False
            and completed['leftGripHeld'] is False and completed['pendingCommand'] is False
            and all(completed.get(k) is None for k in ('error','observationFailure','diagnosticFailure'))
            and [(p['family'],p['stage']) for p in completed['plans']]==[('fixed','Pickup'),('fixed','Insert')]
            and all(p['stimulusCompleted'] is True and p['boundaryChecksPassed'] is True for p in completed['plans'])
            and completed['plans'][-1]['source']==plan['source']
            and plan['schema']=='bc2vr.reload-stimulus.v1'
            and plan['family']=='fixed' and plan['stage']=='Insert' and plan['continuesHeldCustody'] is True,
            'post-insert pulse requires the actual completed released Insert stimulus')
    geometry=plan['geometryEvidence'];selection=geometry['selection']
    aligned=completed['boundaries']['fixed_aligned']['gate']
    inserted=completed['boundaries']['fixed_inserted']['gate']
    proof=aligned['completionPrerequisite']
    require(geometry['schema']==fixed.SCHEMA and geometry['stage']=='Insert'
            and integer(selection['profile']['kind'])==4
            and plan['mapping']['mechanismSelection']==selection
            and geometry['baseline']['counts']['loaded']==0
            and aligned['gate']=='fixed_aligned' and aligned['observed'] is True
            and inserted['gate']=='fixed_inserted' and inserted['observed'] is True
            and inserted['completionOnly'] is True and proof['stage']=='Insert'
            and proof['planSourceNonce']==geometry['snapshotNonce']
            and proof['planEvaluatedAtNs']==geometry['evaluatedAtNs']
            and geometry['evaluatedAtNs']<aligned['observedNs']<inserted['observedNs']<inserted['sourceValidUntilNs'],
            'kind4 pulse lacks its own aligned and completed insertion proof')
    return dict(geometry=geometry, aligned=aligned, completed=inserted)


def current_insertion(context, snapshot, now):
    require(integer(snapshot['observed_ns'])>integer(context['completed']['observedNs']),
            'post-insert pulse requires a newer current native observation')
    fixed.check_gate(context['geometry'],snapshot,'fixed_inserted',now,integer(snapshot['nonce']),
                     acquired=context['aligned'])


def paired_insertion(plan, pickup, completed):
    context=insertion_context(plan,completed)
    require(pickup['schema']=='bc2vr.reload-stimulus.v1' and pickup['family']=='fixed'
            and pickup['stage']=='Pickup' and pickup['continuesHeldCustody'] is False
            and pickup['finishGrip']==1 and plan['finishGrip']==0
            and completed['plans'][0]['source']==pickup['source']
            and pickup['mapping']['mechanismSelection']==plan['mapping']['mechanismSelection']
            and pickup['source']['evaluatedAtNs']<plan['source']['evaluatedAtNs']
            and pickup['source']['nonce']!=plan['source']['nonce']
            and pickup['geometryEvidence']['baseline']['counts']==context['geometry']['baseline']['counts']
            and pickup['geometryEvidence']['baseline']['nativeIdentity']==context['geometry']['baseline']['nativeIdentity']
            and pickup['geometryEvidence']['baseline']['binding']==context['geometry']['baseline']['binding'],
            'paired Insert lost its original Pickup/source/native lineage')
    return context


def artifact_pin(path, raw):
    return dict(path=str(path),bytes=len(raw),sha256=transport.digest(raw))


def read_pinned(run, path, maximum=transport.MAX_BYTES):
    path=transport.child(run,path)
    value,raw=transport.read_json(path,maximum)
    return value,artifact_pin(path,raw)


def capture_handoff(driver, path, context, owner):
    """Old Insert is provenance; only a newer current receipt authorizes Fire.

    One shared capture may replace the writer, with the original writer/state
    retained before it. No pose, selection, reload or firing command is allowed.
    """
    handoff,handoff_pin=read_pinned(driver.run,path)
    require(handoff['schema']=='bc2vr.fixed-insert-capture-handoff.v1'
            and handoff['insertResult']==context['result'] and handoff['insertPlan']==context['plan']
            and handoff['sourceSha256']==driver.manifest['source_sha256']
            and handoff['host']==driver.host==handoff['owner']['host']==owner['host']
            and handoff['owner']['pending'] is False and owner['pending'] is False
            and transport.same_path(handoff['owner']['artifactDirectory'],Path(context['result']['path']).parent)
            and integer(handoff['deadlineNs'])-integer(handoff['startedNs'])==100*NS
            and 0<integer(handoff['startedNs'])<=integer(handoff['retainedNs'])<integer(handoff['deadlineNs'])
            and time.perf_counter_ns()<integer(handoff['deadlineNs']),
            'capture handoff lost original owner/source or100s reload deadline')
    folder=transport.child(driver.run,owner['artifactDirectory'])
    require(folder!=Path(context['result']['path']).parent,'capture handoff needs one different capture writer')
    records={};pins={}
    for name in ('invocation','initial-state','result','capture'):
        records[name],pins[name]=read_pinned(driver.run,folder/(name+'.json'))
    event_path=folder/'events.jsonl'
    raw=transport.read_bytes(event_path,transport.MAX_BYTES)
    events=[json.loads(line) for line in raw.decode('utf-8').splitlines() if line]
    pins['events']=artifact_pin(event_path,raw)
    requests=[e for e in events if e['type']=='command_request']
    commits=[e for e in events if e['type']=='command_commit']
    require(len(requests)==len(commits)==1,'handoff permits exactly one ordinary capture command')
    request,commit=requests[0],commits[0]
    invocation,initial,result,capture=(records[k] for k in ('invocation','initial-state','result','capture'))
    name=invocation['capture'];old=handoff['owner'];prior=handoff['state'];settled=commit['state']
    require(isinstance(name,str) and name and invocation['stage'] is None and invocation['pickup_first'] is False
            and invocation['menu_batches'] is None and invocation['host']==driver.host
            and invocation['driverSha256']==transport.digest(Path(transport.__file__).read_bytes())
            and invocation['plannerSha256']==transport.digest(Path(transport.planner.__file__).read_bytes())
            and invocation['startedNs']>=handoff['retainedNs']
            and result['schema']=='bc2vr.live-feed-driver.v1' and result['status']=='commands_acknowledged'
            and result['commands']==2 and result['plans']==[] and result['pendingCommand'] is False
            and result['leftGripHeld'] is False and result['triggerHeld'] is False
            and all(result.get(k) is None for k in ('error','release','resultWriteError'))
            and transport.same_path(result['artifactDirectory'],folder),
            'intervening writer was not a successful capture-only invocation')
    require(initial['cmdSeq']==prior['cmdSeq']==old['sequence']==request['beforeSequence']
            and initial['lastCmd']==prior['lastCmd']==old['sentinel']
            and settled['cmdSeq']==owner['sequence']==request['targetSequence']==old['sequence']+2
            and request['commands']==['shot '+name,owner['sentinel']]
            and owner['commandSha256']==transport.digest(('\n'.join(request['commands'])+'\n').encode('ascii'))
            and settled['lastCmd']==owner['sentinel'] and commit['cleanup'] is False
            and integer(handoff['retainedNs'])<=integer(request['atNs'])<=integer(commit['writtenNs'])
            <integer(commit['committedNs'])<integer(handoff['deadlineNs'])
            and initial['captureSeq']==prior['captureSeq']
            and settled['captureSeq']==capture['captureSeq']==result['captureSequence']==initial['captureSeq']+1,
            'capture did not directly succeed the retained Insert writer')
    for key in ('head','handL','handR','controls'):
        require(initial[key]==prior[key]==settled[key]==driver.state[key],
                'capture handoff changed ordinary pose/control: '+key)
    require(all(s['pid']==driver.host['pid'] and s['runtime']=='xr-sim' and s['sessionState']=='FOCUSED'
                and s['lastCmdError']=='' and s['errors']==0 for s in (initial,settled,driver.state)),
            'capture handoff lost current focused host')
    metadata,metadata_pin=read_pinned(driver.run,capture['path'],65536)
    metadata_path=Path(metadata_pin['path'])
    require(metadata_path==driver.run/'sim/capture'/(name+'.json')
            and metadata['name']==name and metadata['sessionState']=='FOCUSED'
            and initial['frame']<metadata['frameIndex']<=driver.state['frame']
            and driver.state['captureSeq']==settled['captureSeq']
            and transport.same_path(driver.state['lastCapture'],metadata_path.with_suffix(''))
            and driver.clearance['reviewedNs']>=commit['committedNs'],
            'current reviewed capture is not the single intervening stereo pair')
    expected={str(metadata_path.with_name(name+'_'+eye+'.png')):eye for eye in ('left','right')}
    require({p['path']:p['eye'] for p in driver.clearance['captures']}==expected,
            'clearance must pin both eyes of the intervening capture')
    # Current exact controller/reference and pin validation still run in prepare
    # and at trigger publication; these records never renew a native source.
    driver.deadline=min(driver.deadline,integer(handoff['deadlineNs']))
    driver.stage_deadline=min(driver.stage_deadline,driver.deadline)
    return dict(handoff=handoff_pin,artifacts=pins,metadata=metadata_pin,owner=owner)


def read_insertion(driver, path, handoff_path=None):
    path=transport.child(driver.run,path)
    owner,_=transport.read_json(driver.run/'live-driver-owner.json',8192)
    result,raw=transport.read_json(path)
    plan_path=path.parent/'stimulus-02.json'
    plan,plan_raw=transport.read_json(plan_path,transport.MAX_BYTES)
    pickup,pickup_pin=read_pinned(driver.run,path.parent/'stimulus-01.json')
    context=paired_insertion(plan,pickup,result)
    context['result']=dict(path=str(path),bytes=len(raw),sha256=transport.digest(raw))
    context['plan']=dict(path=str(plan_path),bytes=len(plan_raw),sha256=transport.digest(plan_raw))
    context['pickup']=pickup_pin
    if handoff_path is None:
        require(transport.same_path(owner['artifactDirectory'],path.parent),
                'post-insert pulse must immediately follow owned Insert or its one verified capture')
    else:
        context['captureHandoff']=capture_handoff(driver,handoff_path,context,owner)
    return context


def after_shot(snapshot, now, before):
    view = fixed.ammo(snapshot, now, idle=True)
    x = snapshot['fixedContainer']
    require(selected_family(snapshot) == before['family']
            and view['identity'] == before['nativeIdentity']
            and fixed.binding_key(view['binding']) == before['binding']
            and view['request'] == before['request']
            and x['profile'] == before['profile']
            and x['tracking']['lifetime'] == before['lifetime']
            and view['snapshot']['counts'] == dict(before['counts'], loaded=0),
            'preparation did not preserve native owner/reserve or consume exactly one round')
    gun = fixed.claim(x['tracking']['gun'], x['input'], now, 1, 1)
    require(gun == before['gun'] and x['input']['released'] == [True,False] and x['leftClaim'] is None
            and x['tracking']['hand'] is None and x['pendingRequest'] is None,
            'preparation changed gun custody or left a transaction')
    return copy.deepcopy(view['snapshot']['counts'])


class PreparationDriver(fixed_driver.SelectedAmmoDriver):
    def assert_selected_family(self, snapshot):
        selected_family(snapshot)

    def validate_publication(self, lines, expected, cleanup):
        if lines==['trigger r 1'] and self.insertion is not None:
            # A verified kind4 insertion can consume the final reserve round.
            # Keep the ordinary one-press/current-frame gates; only the need
            # for ammunition for a *future* reload is inapplicable here.
            require(not cleanup and self.commands==0 and not self.held_trigger and not self.pending_command,
                    'exactly one ordinary post-insert trigger press is permitted')
            self.admit_snapshot(self.latest_snapshot,time.perf_counter_ns(),integer(self.latest_snapshot['nonce']))
        else:
            super().validate_publication(lines, expected, cleanup)
        if lines == ['trigger r 1']:
            now = time.perf_counter_ns()
            require(baseline(self.latest_snapshot, now,require_reserve=self.insertion is None) == self.preparation,
                    'preparation baseline changed before publication')
            if self.insertion is not None:
                current_insertion(self.insertion,self.latest_snapshot,now)
            clearance_geometry.fire_clearance(self.clearance, self.latest_snapshot,
                self.state, self.host, now, self.deadline)

    def prepare(self, clearance_path, insertion_path=None, handoff_path=None, expected_kind=None):
        self.clearance, _ = transport.read_json(transport.child(self.run, clearance_path), 16384)
        require(insertion_path is not None or handoff_path is None,'capture handoff requires actual Insert')
        self.insertion=read_insertion(self,insertion_path,handoff_path) if insertion_path else None
        for pin in self.clearance['captures']:
            require(transport.child(self.run, pin['path']) == transport.local_path(pin['path']),
                    'reviewed capture belongs to another run')
            transport.check_pin(pin)
        snapshot, _, now = self.fresh_available()
        self.preparation = baseline(snapshot, now,require_reserve=self.insertion is None)
        require(expected_kind is None or self.preparation['profile']['kind']==expected_kind,
                'current representative mechanism kind differs')
        if self.insertion is not None:
            current_insertion(self.insertion,snapshot,now)
        require(all(all(part == 0 for part in value) if isinstance(value, list) else value == 0
                    for value in self.state['controls'].values()),
                'preparation starts with neutral ordinary controls')
        pulse = clearance_geometry.fire_clearance(self.clearance, snapshot,
            self.state, self.host, time.perf_counter_ns(), self.deadline)
        # Permanent key survives a refusal. New snapshots do not grant a retry.
        key = dict(host=self.host, owner=self.preparation['owner'],
                   nativeIdentity=self.preparation['nativeIdentity'], counts=self.preparation['counts'],
                   request=self.preparation['request'], lifetime=self.preparation['lifetime'],
                   insertionResultSha256=self.insertion['result']['sha256'] if self.insertion else None)
        attempt = self.run / ('fixed-preparation-attempt-'+transport.digest(transport.json_bytes(key))+'.json')
        with attempt.open('xb') as stream:
            stream.write(transport.json_bytes(dict(schema='bc2vr.fixed-preparation-attempt.v1',
                key=key, artifactDirectory=str(self.artifacts.directory), preparationOnly=True)))
        self.reference = snapshot
        begin = integer(snapshot['input_evidence']['observedNs'])
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+pulse)
        self.send(['trigger r 1'])
        press = self.last_write
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+250_000_000)
        self.send(['trigger r 0'])
        release, committed = self.last_write, self.last_commit
        require(release-press <= pulse, 'preparation pulse exceeded100ms; released; no retry')
        self.stage_deadline = min(self.deadline, committed+600_000_000)
        self.pause_until(committed+100_000_000)
        after, _, now = self.fresh_available()
        counts = after_shot(after, now, self.preparation)
        require(integer(after['input_evidence']['observedNs']) >= committed,
                'post-shot observation predates acknowledged release')
        result = dict(schema='bc2vr.fixed-preparation-pulse.v1', status='native_shot_observation_pending',
            preparationOnly=True, preparation=self.preparation, owner=snapshot['owner'],
            host=self.host, sourceSha256=self.manifest['source_sha256'],
            preparationObservedNs=begin, pressWrittenNs=press, releaseWrittenNs=release,
            releaseCommittedNs=committed, observationEndNs=integer(after['observed_ns']),
            beforeCounts=self.preparation['counts'], afterCounts=counts,
            nativeAcceptance=False, headsetAcceptance=False, nativeShotObserved=False,
            attempt=str(attempt), clearance=self.clearance,
            purpose='post_insert_observation' if self.insertion else 'preparation', insertion=self.insertion)
        self.artifacts.save('fixed-preparation-shot.json', result)
        return result


def audit(run, path):
    run, path = transport.local_path(run), transport.child(run, path)
    boundary, _ = transport.read_json(path)
    require(boundary['schema'] == 'bc2vr.fixed-preparation-pulse.v1'
            and boundary['preparationOnly'] is True, 'not a preparation pulse')
    attempt, _ = transport.read_json(transport.child(run, boundary['attempt']))
    link, _ = transport.read_json(run/'session-link.json')
    manifest, _ = transport.read_json(run/'run-manifest.json')
    require(transport.same_path(attempt['artifactDirectory'], path.parent)
            and attempt['preparationOnly'] is True and attempt['key']['host'] == boundary['host'] == link['host']
            and attempt['key']['nativeIdentity'] == boundary['preparation']['nativeIdentity']
            and attempt['key']['counts'] == boundary['beforeCounts']
            and manifest['source_sha256'] == boundary['sourceSha256'], 'preparation belongs to another attempt/run')
    if boundary.get('insertion') is not None:
        context=boundary['insertion']
        for name in ('result','plan','pickup'):
            transport.child(run,context[name]['path']);transport.check_pin(context[name])
        saved_result,_=transport.read_json(Path(context['result']['path']))
        saved_plan,_=transport.read_json(Path(context['plan']['path']),transport.MAX_BYTES)
        saved_pickup,_=transport.read_json(Path(context['pickup']['path']),transport.MAX_BYTES)
        expected=paired_insertion(saved_plan,saved_pickup,saved_result)
        require(all(context[k]==expected[k] for k in expected)
                and attempt['key']['insertionResultSha256']==context['result']['sha256'],
                'post-insert provenance changed')
        if 'captureHandoff' in context:
            capture=context['captureHandoff']
            for item in (capture['handoff'],capture['metadata'],*capture['artifacts'].values()):
                transport.child(run,item['path']);transport.check_pin(item)
            for item in boundary['clearance']['captures']:
                transport.child(run,item['path']);transport.check_pin(item)
    retirement, _ = transport.read_json(run/'native-retirement.json')
    require(retirement['hooks_disabled'] is True and retirement['reload_flow_drained'] is True
            and retirement['pre_activation_refusal'] is False,
            'native shot audit requires ordinary drained retirement')
    report_path = transport.check_pin(retirement['native_report'], 160*1024*1024)
    require(transport.same_path(report_path, transport.local_path(link['folder'])/'native-trace.json'),
            'retired native report belongs to another session')
    native, _ = transport.read_json(report_path, 160*1024*1024)
    return transport.audit_post_reload_shot(native['gameplay']['reload_flow'], boundary)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--fire-clearance', type=Path)
    mode.add_argument('--audit', type=Path)
    parser.add_argument('--after-insert-result',type=Path,
                        help='optional immediately preceding completed kind4 Insert result; never a kind3 Close substitute')
    parser.add_argument('--after-insert-handoff',type=Path,
                        help='original Insert owner/state retained before one verified capture; original100s reload deadline')
    parser.add_argument('--expected-kind',type=int,choices=(3,4),help='optional ROOT-bound registered mechanism representative')
    parser.add_argument('--root-authorized-ordinary-input', action='store_true')
    args = parser.parse_args(argv)
    driver, code = None, 2
    result = dict(schema='bc2vr.fixed-preparation-result.v1', status='refused', preparationOnly=True,
                  reloadCompleted=False, nativeAcceptance=False, headsetAcceptance=False)
    try:
        if args.audit:
            require(args.after_insert_result is None and args.after_insert_handoff is None,'audit uses retained insertion provenance only')
            result.update(audit(args.run, args.audit))
        else:
            require(args.root_authorized_ordinary_input, 'ROOT ordinary-input authorization required')
            args.stage, args.step_ms = 'Reload', 40
            driver = PreparationDriver.__new__(PreparationDriver)
            driver.__init__(args)
            result['pulse'] = driver.prepare(args.fire_clearance,args.after_insert_result,args.after_insert_handoff,args.expected_kind)
            result['status'] = 'ordinary_post_insert_pulse_observed' if args.after_insert_result else 'ordinary_preparation_empty_observed'
        code = 0
    except (Exception, KeyboardInterrupt) as error:
        result['error'] = str(error)
    finally:
        if driver is not None:
            if getattr(driver, 'artifacts', None):
                try:
                    result['release'] = driver.release_after_failure()
                    require(not(driver.held_trigger or driver.held_left or driver.pending_command),
                            'preparation release remains unsettled')
                except Exception as error:
                    result.update(releaseError=str(error), status='refused');code=2
                result.update(commands=driver.commands, artifactDirectory=str(driver.artifacts.directory))
                try: driver.artifacts.save('result.json', result, final=True)
                except Exception as error: result['archiveError']=str(error);code=2
            driver.close()
    print(json.dumps(result, separators=(',', ':'), allow_nan=False))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
