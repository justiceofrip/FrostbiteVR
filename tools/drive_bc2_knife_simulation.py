"""ROOT only: ordinary stow, genuine Empty, strict mode4 knife Draw and Return.

An optional reviewed LOCAL unit vector permits exactly one 75mm zero-trigger
jab. Ordinary current run/install/source/command ownership stays with the shared
transport. No launch, installation, retries, target or damage acceptance.
"""
import argparse
import copy
import datetime
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


draw = load('knife_shared_draw', Path(__file__).with_name('drive_bc2_holster_prepared.py'))
h80 = draw.core
t, p = h80.transport, h80.frame
require, integer, Refusal = t.require, t.integer, t.REFUSAL
readiness = load('knife_shared_readiness', Path(__file__).with_name('bc2_draw_readiness_driver.py'))
policy = load('knife_shared_admission', Path(__file__).with_name('bc2_draw_admission.py'))
NS, MS = t.NS, 1_000_000
ACTOR, REFERENCE = h80.ACTOR, h80.REFERENCE
KNIFE_RIG = 0xa7f219a1426216ab
DEPENDENCIES = ('tools/drive_bc2_knife_simulation.py', 'tools/drive_bc2_holster_prepared.py',
    'tools/bc2_holster_core.py', 'tools/bc2_holster_snapshot_transport.py',
    'tools/drive_bc2_feed_simulation.py', 'tools/plan_bc2_feed_simulation.py',
    'tools/bc2_draw_readiness_driver.py', 'tools/bc2_draw_admission.py',
    'include/fvr/interaction/BodyAnchors.h')

def empty(snapshot, now):
    h = h80.holster_proof(snapshot, now)
    require(h["actualEmpty"] is True and h["neutralTrackedInput"] is True
            and h80.neutral(snapshot["input"]), "genuine neutral Empty required")
    p.admit_frame(snapshot, now, integer(snapshot["nonce"]), mechanism_admission=lambda s, n: True)
    require(snapshot["weapon_actions_blocked"] is True, "Empty suppression missing")
    return h80.source_deadline(snapshot)


def preserved(before, after, *, right=False):
    require(all(before["owner"][k] == after["owner"][k] for k in ACTOR), "actor/space changed")
    require(before["selected"]["inventory"] == after["selected"]["inventory"], "inventory owner changed")
    require(all(before["input"][k] == after["input"][k] for k in REFERENCE), "tracking/body basis changed")
    for k in ("grip", "aim", "held", "squeeze", "trigger", "stickX", "stickY"):
        require(before["input"]["hands"][0][k] == after["input"]["hands"][0][k], "left input changed")
    if right:
        require(before["input"]["hands"][1]["grip"] == after["input"]["hands"][1]["grip"],
                "right grip pose changed")


def chest_pose(snapshot):
    # Authored KnifeChest in the exact BodyAnchorHandPose frame, not world/rig
    # space, an old run's pose, or inventory slot 7 interpreted as body slot 5.
    local = p.identity()
    local[3][:3] = [.12, -.25, .15]
    pose = p.to_pose(p.multiply(local, p.supply_body_frame(snapshot["input"])))
    pose["orientation"] = snapshot["input"]["hands"][1]["grip"]["orientation"][:]
    return pose


def at_chest(snapshot):
    pose = snapshot["input"]["hands"][1]["grip"]
    body = p.multiply(p.from_pose(pose), p.inverse(p.supply_body_frame(snapshot["input"])))
    require(math.dist(body[3][:3], [.12, -.25, .15]) <= .002, "current grip missed authored chest handle")


def same_stamps(a, b):
    require(a["owner"] == b["owner"], "input physical owner changed")
    sa, sb = integer(a["sequence"]), integer(b["sequence"])
    if sa == sb:
        require(all(a[k] == b[k] for k in ("observedNs", "deadlineNs")), "same input sequence has conflicting original clocks")
    else:
        require((sa < sb) == (integer(a["observedNs"]) < integer(b["observedNs"])), "input sequence/time ordering conflicts")


class Attempt:
    """Irreversible local cancellation; a later packet never revives authority."""
    def __init__(self):
        self.cancelled = False
        self.used = set()

    def take(self, action):
        require(not self.cancelled and action not in self.used, "cancelled or duplicate " + action)
        self.used.add(action)  # consume before attempting publication, including a refused write

    def check(self):
        require(not self.cancelled, "knife attempt permanently cancelled")


def simulator_controls_neutral(controls):
    required = {'a', 'b', 'x', 'y', 'menu', 'trigL', 'trigR', 'gripL', 'gripR', 'stickL', 'stickR'}
    if not isinstance(controls, dict) or not required <= set(controls) <= required | {'clickL', 'clickR', 'restL', 'restR'}:
        return False
    for key, value in controls.items():
        if key in ('stickL', 'stickR'):
            if not isinstance(value, list) or len(value) != 2:
                return False
            values = value
        else:
            values = [value]
        if not all(isinstance(item, (bool, int, float)) and item == 0 for item in values):
            return False
    return True


def native_selection(baseline, evaluated_ns):
    """Retain the admitted native item/slot separately from the body's gun key."""
    empty(baseline, evaluated_ns)
    d, rows = h80.mapping_rows(baseline, evaluated_ns)
    matched = [r for r in rows if r['native']['weapon'] == baseline['owner']['weapon']]
    require(len(matched) == 1, 'one exact selected native inventory row required')
    row = matched[0]
    require(row['assignment']['item'] == d['physicalSelected']
        and row['native']['slot'] == baseline['selected']['selectedSlot'],
        'native inventory key/slot differs from selected configuration')
    return dict(evaluatedNs=evaluated_ns, item=copy.deepcopy(row['assignment']['item']),
        slot=row['native']['slot'], bodySlot=row['assignment']['slot'],
        bodyGun=copy.deepcopy(baseline['bodyHolster']['physicalGun']),
        inventory=d['inventory'], switching=d['switching'], cohort=d['cohort'], revision=d['revision'])


def selection_history(baseline, episode):
    selection = episode.get('nativeSelection')
    require(isinstance(selection, dict), 'missing original admitted native inventory binding')
    require(selection == native_selection(baseline, selection['evaluatedNs']),
                     'original native/body selection binding changed')
    require(selection['evaluatedNs'] <= episode['drawWrittenNs'],
                     'inventory admission was not before owned draw')
    return selection


def owned_epoch(previous, current, fields, label):
    """Exact actor/item binding plus an observed owned equipment transition.

    This primitive grants no Held/Return authority. Its callers also require
    the original draw/return receipt, selected native item and body operation.
    """
    require(isinstance(previous, dict) and isinstance(current, dict)
        and set(previous) == set(current)
        and all(type(current[k]) is int and current[k] > 0 for k in current)
        and all(current[k] == previous[k] for k in fields)
        and type(previous['equipGeneration']) is int
        and current['equipGeneration'] > previous['equipGeneration'] > 0,
        label + ' did not bind a fresh owned equipment epoch')
    return current


def knife_physical(knife, baseline):
    old = baseline['owner']
    require(knife['originalOwner'] == old
        and knife['knife']['id'] == knife['owner']['weapon'] != old['weapon'],
        'knife epoch lacks exact admitted original/native knife binding')
    owned_epoch(old, knife['owner'], ('player','soldier','weak','actorGeneration','space'),
                'drawn native knife')
    return owned_epoch(baseline['input_evidence']['owner'], knife['inputEvidence']['owner'],
        ('actor','actorGeneration','space'), 'drawn physical knife')


def returned_owners(baseline, held, native, physical):
    """The same original native weapon returns under newly observed epochs."""
    knife_owner = knife_physical(held, baseline)
    require(all(native[k] == baseline['owner'][k]
        for k in ('player','soldier','weak','weapon','actorGeneration','space')),
        'returned epoch changed the admitted original actor/weapon')
    owned_epoch(held['owner'], native, ('player','soldier','weak','actorGeneration','space'),
                'returned native original')
    owned_epoch(knife_owner, physical, ('actor','actorGeneration','space'),
                'returned physical original')
    return native, physical


def held(snapshot, baseline, now, after_ns, episode):
    need, number = require, integer
    need(snapshot.get('schema') == 'fvr.bc2.live-mechanism-snapshot.v1'
         and number(snapshot['nonce']) > 0 and 0 < number(snapshot['observed_ns']) <= now,
         'original response schema/nonce/time missing')
    if snapshot.get('valid') is not True:
        need(snapshot.get('valid') is False
             and snapshot.get('invalid_reason') == 'selected_configuration_or_rig_missing_changed_or_expired'
             and snapshot.get('chestKnifeObservationBasis') == 'independent_owned_body',
             'invalid firearm response lacks independent owned knife basis')
    selection = selection_history(baseline, episode)
    need(episode['startedNs'] <= episode['drawWrittenNs'] <= episode['drawAcknowledgedNs']
         <= after_ns <= now < min(episode['deadlineNs'], episode['stageDeadlineNs']),
         'original owned draw/ACK/neutral episode clocks differ')
    k = snapshot.get('chestKnife')
    need(isinstance(k, dict) and k.get('observationOnly') is True and k.get('available') is True
         and k.get('reason') is None and number(k['phase']) == 4,
         'independent current Held proof unavailable')
    for key in ('held', 'stockTriggerReady', 'stockTriggerArmed', 'nativeOwned', 'nativeHealthy',
                'nativeObservationComplete', 'stockTriggerEnabled', 'fireSuppressed', 'mechanicsSettled'):
        need(k[key] is True, 'knife gate lost: '+key)
    need(k['motionEnabled'] is True and k['attackBridgeEnabled'] is False,
         'unexpected knife attack mode')
    owner, old = k['owner'], baseline['owner']
    need(all(number(owner[key]) > 0 for key in old)
         and all(owner[key] == old[key] for key in ('player','soldier','weak','actorGeneration','space'))
         and k['originalOwner'] == old and k['originalGun'] == selection['item']
         and k['knife']['id'] == owner['weapon'] != old['weapon']
         and number(k['knife']['generation']) > 0,
         'native knife/original inventory owner or item mismatch')
    need(number(k['continuity']) > 0 and number(k['bodyOperation']) > 0
         and k['drawReceipts'] == 1 and k['returnReceipts'] == 0,
         'one otherwise-idle owned draw required')
    ev, render, hold = k['inputEvidence'], k['render'], k['hold']
    physical = knife_physical(k, baseline)
    for sample in (ev, render['inputEvidence']):
        p.evidence(sample, owner, physical['equipGeneration'], now)
        need(sample['owner'] == physical and sample['released'][1] is False,
             'current owned knife hand/body generation or held source lost')
    same_stamps(render['inputEvidence'], ev)
    need(number(ev['observedNs']) >= after_ns
         and render['inputEvidence']['sequence'] <= ev['sequence']
         and render['inputEvidence']['observedNs'] <= ev['observedNs'],
         'Held/render source predates owned draw or neutral command')
    need(hold['owner'] == physical and number(hold['id']) > 0
         and hold['hand'] == 1 and hold['kind'] == 1 and hold['item'] == k['knife']
         and number(hold['contact']['id']) > 0 and number(hold['contact']['generation']) > 0
         and hold['prerequisiteClaim'] == 0 and hold['inputSequence'] == ev['sequence']
         and now < number(hold['deadlineNs']) <= number(ev['deadlineNs']),
         'exact current knife GunHold claim lost')
    need(render['owner'] == owner and render['knife'] == k['knife']
         and render['bodyOperation'] == k['bodyOperation'] and render['rigFingerprint'] == KNIFE_RIG,
         'current rendered knife rig/operation differs')
    menu = k['menu']
    need(menu['owner'] == 1 and number(menu['sequence']) > 0
         and menu['epoch'] == baseline['input']['menu']['epoch']
         and now < number(menu['deadlineNs']) <= now + 100_000_000
         and k['trackingBasisGeneration'] == baseline['input']['trackingBasisGeneration'],
         'gameplay menu or original tracking epoch changed')
    need(len(k['controls']) == 2, 'exact two hand controls required')
    for index, controls in enumerate(k['controls']):
        need(controls['held'] == 0 and controls['trigger'] == 0
             and controls['stickX'] == controls['stickY'] == 0
             and controls['squeeze'] == (0 if index == 0 else 1),
             'knife-only observation requires neutral trigger and held right grip')
    need(k['knifeSlot'] == 7 and k['selectedSlots'] == [7,7,7]
         and k['nativeSequence'] == ev['sequence']
         and ev['observedNs'] <= k['nativeObservedNs'] <= ev['nowNs']
         and number(k['nativeTick']) > 0 and number(k['suppressionRequest']) > 0,
         'native three-copy knife selection/input/suppression mismatch')
    until = min(ev['deadlineNs'], menu['deadlineNs'], hold['deadlineNs'], render['inputEvidence']['deadlineNs'])
    need(k['validUntilNs'] == until and snapshot['observed_ns'] <= k['observedNs'] <= now < until,
         'independent Held lease expired or extended')
    # No firearm mapping, aim, native shot authority or synthetic valid frame.
    # Pose actions still require the exact current claim and publication lease.
    return until


class KnifeTransport(t.Driver):
    def read_state(self):
        check_continuous(self.admission)
        state = super().read_state()
        if self.frozen_sim is not None:
            require(all(state[k] == self.frozen_sim[k] for k in ("head", "handL", "handR")),
                    "concurrent controller/body pose change")
        return state

    def pump(self):
        # No background request/reacquisition. One response per explicit stage;
        # unavailable/expired responses are retained and cancel permanently.
        self.bounds(time.perf_counter_ns())

    def current(self, validator):
        self.attempt.check()
        require(self.pending_nonce is None, "previous observation still pending")
        self.ownership(self.read_state())
        self.request_snapshot()
        until = min(self.deadline, self.stage_deadline, self.last_request + t.SNAPSHOT_TIMEOUT)
        for _ in range(800):
            now = time.perf_counter_ns()
            self.bounds(now)
            require(now < until, "original observation response deadline expired; no retry")
            require(t.read_bytes(self.request_path, 22) == self.request_bytes, "concurrent snapshot requester")
            failure = self.snapshot_failure()
            require(failure is None, "native observation publication failed; no retry")
            self.ownership(self.read_state())
            if self.snapshot_path.exists():
                snapshot, raw = t.read_json(self.snapshot_path, p.SNAPSHOT_LIMIT)
                nonce = integer(snapshot["nonce"])
                require(nonce <= self.pending_nonce, "concurrent snapshot response")
                if nonce == self.pending_nonce:
                    self.artifacts.save(f"snapshot-{self.snapshot_index:03}.json", raw)
                    now = time.perf_counter_ns()
                    require(now < until, "response read consumed original observation deadline")
                    validator(snapshot, now)
                    self.pending_nonce = None
                    self.latest_snapshot = snapshot
                    return snapshot, raw
            time.sleep(.001)
        raise Refusal("bounded observation iteration limit")

    def ownership(self, state, expected=None):
        super().ownership(state, expected)
        if self.gate is not None:
            snapshot, raw, validator = self.gate
            current, current_raw = t.read_json(self.snapshot_path, p.SNAPSHOT_LIMIT)
            require(current_raw == raw and current == snapshot, "admitted snapshot replaced/invalidated; no recovery")
            validator(snapshot, time.perf_counter_ns())

    def validate_publication(self, lines, expected, cleanup):
        if cleanup:
            require(all(x in ("trigger r 0", "grip r 0", "grip l 0") or x.startswith("btn ") and x.endswith(" up")
                        for x in lines), "cleanup may only release owned controls")
            return
        self.attempt.check()
        require(not self.pending_command, "prior command unresolved")
        self.bounds(time.perf_counter_ns())
        if self.phase == "move":
            require(len(lines) == 3 and lines[0] == "hand r follow off"
                    and lines[1].startswith("hand r grip pose ")
                    and lines[2].replace(" aim ", " grip ") == lines[1] and not self.held_right,
                    "only one neutral chest approach permitted")
        elif self.phase == "draw":
            require(lines == ["grip r 1"] and not self.held_right and not self.held_trigger, "one neutral draw only")
        elif self.phase == "capture":
            require(len(lines) == 1 and lines[0].startswith("shot "), "capture phase cannot admit input")
        elif self.phase == "neutral":
            require(lines == ["trigger r 0"] and self.held_right and not self.held_trigger, "held neutral only")
        else:
            raise Refusal("no action admitted in this phase")

    def action(self, action, lines, source=None, validator=None):
        self.attempt.take(action)
        self.phase = action
        self.gate = (*source, validator) if source is not None else None
        try:
            until = validator(source[0], time.perf_counter_ns()) if source else None
            return self.send(lines, source_deadline=until)
        except BaseException:
            self.attempt.cancelled = True
            raise
        finally:
            self.gate = None

    def knife_capture(self, phase):
        self.phase = "capture"
        name = "knife-" + phase + "-" + self.artifacts.directory.name[-8:]
        self.capture(name)
        path = self.run / "sim/capture" / (name + ".json")
        raw = t.read_bytes(path, 65536)
        receipt = dict(phase=phase, path=str(path), sha256=t.digest(raw), bytes=len(raw), captureSequence=self.capture_sequence)
        self.artifacts.save("knife-" + phase + "-capture.json", receipt)
        return receipt


def pin(path):
    path = t.local_path(path)
    raw = t.read_bytes(path, 1024 * 1024)
    return dict(path=str(path), bytes=len(raw), sha256=t.digest(raw))


def dependency_pins():
    return [pin(ROOT / name) for name in DEPENDENCIES]


def dependency_sha256(pins=None):
    # Relocatable content identity; actual paths are checked separately.
    pins = dependency_pins() if pins is None else pins
    return t.digest(t.json_bytes([dict(name=name, bytes=row['bytes'], sha256=row['sha256'])
                                 for name, row in zip(DEPENDENCIES, pins)]))


IMPORTED_PINS = dependency_pins()


def check_continuous(admission):
    for row in admission['continuousPins']:
        t.check_pin(row, 1024 * 1024)


def jab_points(origin, direction):
    require(len(origin) == len(direction) == 3 and all(
        type(v) in (float, int) and math.isfinite(v) for v in list(origin) + list(direction)),
        'Finite LOCAL position/direction required')
    require(abs(math.sqrt(sum(v*v for v in direction)) - 1) < .001,
            'ROOT direction must already be unit length')
    return [[origin[j] + step * .025 * direction[j] for j in range(3)] for step in (1, 2, 3)]


def neutral_trigger_lines(lines):
    for line in lines:
        words = line.split()
        if words and words[0] == 'trigger':
            require(len(words) == 3 and words[1] in ('l', 'r') and float(words[2]) == 0,
                    'Knife diagnostic never permits a pressed trigger')


def review_world(run, name, plan, now):
    require(re.fullmatch(r'[A-Za-z0-9_-]{1,80}', name) is not None, 'Invalid current capture name')
    path = run / 'sim/capture' / (name + '.json')
    meta, _ = t.read_json(path, 65536)
    require(meta['name'] == name and meta['sessionState'] == 'FOCUSED'
            and (meta['width'], meta['height']) == (plan['width'], plan['height'])
            and sorted(v['eye'] for v in meta['views']) == ['left', 'right']
            and 0 <= now - integer(meta['displayTimeNs']) < 50 * NS,
            'Current stereo review expired or does not match this run')
    state, _ = t.read_json(run / 'sim/state.json', 16384)
    require(simulator_controls_neutral(state['controls'])
            and all(state['controls'][key] == value for key, value in meta['controls'].items()),
            'Reviewed neutral controls changed')
    for hand in ('head', 'handL', 'handR'):
        old = meta[hand]
        require(state[hand]['valid'] is True
                and all(len(value) == 3 for value in (old['pos' if hand == 'head' else 'grip'],
                    state[hand]['pos'], old['ypr' if hand == 'head' else 'aimYpr'], state[hand]['ypr']))
                and all(abs(float(x)-float(y)) <= .0002 for x, y in zip(
                    old['pos' if hand == 'head' else 'grip'], state[hand]['pos']))
                and all(abs(float(x)-float(y)) <= .0055 for x, y in zip(
                    old['ypr' if hand == 'head' else 'aimYpr'], state[hand]['ypr'])),
                'Reviewed head/hand pose changed')
    images = []
    for eye in ('left', 'right'):
        image_path = path.with_name(name + '_' + eye + '.png')
        raw = t.read_bytes(image_path, 8 * 1024 * 1024)
        require(raw.startswith(b'\x89PNG\r\n\x1a\n'), 'Reviewed eye image is missing or invalid')
        images.append(dict(path=str(image_path), bytes=len(raw), sha256=t.digest(raw)))
    return dict(metadata=pin(path), images=images, state=state)


def preflight(args):
    require(args.root_ordinary_input and args.root_reviewed_both_world_eyes
            and args.root_reviewed_normal_mode4_and_knife_idle,
            'Explicit ROOT ordinary-input/current stereo/normal mode4/knife-idle review required')
    if args.jab_direction_local is not None:
        jab_points([0., 0., 0.], args.jab_direction_local)
    dependencies = dependency_pins()
    require(dependencies == IMPORTED_PINS
            and dependency_sha256(dependencies) == args.dependencies_sha256,
            'Reviewed shared dependency source changed')
    run = t.local_path(args.run)
    manifest, _ = t.read_json(run / 'run-manifest.json')
    require(manifest['schema'] == 'fvr.xr-game-run.v1'
            and t.same_path(manifest['output_directory'], run)
            and manifest['version'] == args.version and manifest['source_sha256'] == args.source_sha256
            and 'private_campaign' not in manifest, 'Expected ordinary run/source/version differs')
    binding_path = run / 'artifact-binding.json'
    require(t.same_path(manifest['artifact_binding_path'], binding_path), 'Artifact binding is not this run')
    binding, _ = t.read_json(binding_path)
    require(binding['version'] == args.version and binding['source_sha256'] == args.source_sha256
            and 'private_campaign' not in binding, 'Installed artifact source/version differs')
    runtime_path, package_path = (t.check_pin(binding[key]) for key in ('runtime', 'package'))
    require(runtime_path == package_path.parent / 'runtime/runtime.json'
            and package_path.parent.name == args.version
            and package_path.parent.parent.name == 'versions', 'Installed package/runtime layout differs')
    active_path = package_path.parent.parent.parent / 'active.json'
    active, _ = t.read_json(active_path)
    runtime, _ = t.read_json(runtime_path)
    require(active['current'] == args.version and runtime['schema'] == 'bc2vr.runtime.v1'
            and runtime['version'] == args.version and runtime['source_sha256'] == args.source_sha256
            and runtime['diagnostic_only'] is False, 'Active installed normal runtime/source differs')
    package, _ = t.read_json(package_path)
    require(package['version'] == args.version, 'Installed package version differs')
    required = {'BC2NativeProbe.dll', 'BC2XrHost.exe'}
    binaries = [row for row in binding['binaries'] if Path(row['path']).name in required]
    require(len(binaries) == 2 and {Path(row['path']).name for row in binaries} == required,
            'Exact deployed native probe and host binding missing')
    for row in binaries:
        deployed = t.check_pin(row)
        require(deployed == runtime_path.parent / 'bin' / deployed.name,
                'Native binary is outside the admitted installed runtime')
    require(next(row for row in binaries if Path(row['path']).name == 'BC2XrHost.exe')
            == manifest['host_executable'], 'Manifest host differs from installed binding')
    link, _ = t.read_json(run / 'session-link.json')
    require(not (run / 'result.json').exists() and not t.local_path(link['stop_request']).exists(),
            'Run is already stopping or retired')
    storage, _ = t.read_json(t.local_path(args.storage_receipt))
    plan = manifest['capture_plan']
    two_raw = 2 * integer(plan['width']) * integer(plan['height']) * 4 * 2 * 2
    require(storage['schema'] == 'fvr.storage-admission.v1' and storage['operation'] == 'Capture'
            and t.same_path(storage['output_path'], run)
            and integer(storage['planned_bytes']) >= max(18*1024*1024, two_raw + 2*65536)
            and integer(storage['raw_capture_bytes']) >= max(12*1024*1024, two_raw),
            'Fresh StorageBudget receipt must admit both diagnostic captures')
    checked = datetime.datetime.fromisoformat(storage['checked_utc'].replace('Z', '+00:00'))
    age = (datetime.datetime.now(datetime.timezone.utc) - checked).total_seconds()
    require(0 <= age < 60, 'Storage admission expired')
    world = review_world(run, args.world_capture, plan, time.perf_counter_ns())
    require(integer(world['state']['captureSeq']) + 2 <= integer(plan['maximum_compositions']),
            'Two remaining capture slots required')
    paths = [run / name for name in ('run-manifest.json', 'artifact-binding.json',
                                    'session-link.json', 'sim-startup.json')]
    paths += [t.local_path(link['options']), active_path, runtime_path, package_path]
    return dict(continuousPins=dependencies + [pin(path) for path in paths], world=world,
                storage=storage, host=link['host'], version=args.version, sourceSha256=args.source_sha256)


def check_reviewed_start(driver, admission):
    state = admission['world']['state']
    require(driver.host == admission['host']
            and driver.manifest['version'] == admission['version']
            and driver.manifest['source_sha256'] == admission['sourceSha256'],
            'Admitted ordinary source/host changed')
    require(all(driver.state[key] == state[key] for key in
                ('cmdSeq', 'lastCmd', 'captureSeq', 'head', 'handL', 'handR', 'controls')),
            'ROOT reviewed command/input state changed before stow')


def make_stow(admission):
    now = time.perf_counter_ns()

    class DirectDrawBudget(readiness.AdmissionBudget):
        def request(self, now_ns):
            # No Prime preceded this command: charge the actual first request.
            # Original replacement and ordinary-motion accounting is unchanged.
            if self.attached and self.observations == 0 and not self.hold_started:
                self._charge(now_ns)
            return super().request(now_ns)

    budget = DirectDrawBudget(policy, now, now + 30*NS)
    base = readiness.make_driver(draw.HolsterDriver, draw.core, budget=budget)

    class Stow(base):
        def read_state(self):
            check_continuous(admission)
            return super().read_state()

        def validate_publication(self, lines, expected, cleanup):
            neutral_trigger_lines(lines)
            return super().validate_publication(lines, expected, cleanup)

        def run_holster(self):
            # The shared ordinary sequence stops at its genuine neutral Empty
            # boundary. Target mapping still participates in full admission.
            check_reviewed_start(self, admission)
            initial, now = self.current()
            self.require_held(initial)
            require(h80.neutral(initial['input']) and initial['bodyHolster']['neutralTrackedInput'] is True,
                    'Initial source requires neutral tracked inputs')
            require(initial['equipment']['asset'] == self.args.stow_asset
                    and initial['selected']['configurationPath'] == self.args.stow_config
                    and integer(initial['bodyHolster']['selectedSlot']['slot']) == self.args.stow_slot
                    and integer(initial['bodyHolster']['inventoryEvaluation']) == 1,
                    'Selected source differs from exact intended stow identity')
            require(self.args.stow_slot != self.args.target_slot
                    and (self.args.stow_asset, self.args.stow_config) !=
                    (self.args.target_asset, self.args.target_config), 'Distinct mapping target required')
            self.baseline = initial
            self.phase = 'stow_reach'
            at_stow = self.reach(slot=self.args.stow_slot)
            require(h80.neutral(at_stow['input']) and at_stow['bodyHolster']['neutralTrackedInput'] is True,
                    'Stow press lacks fresh neutral rearm')
            self.phase = 'stow_wait'
            self.command(at_stow, self.right_pose, 1)
            observed_empty = self.wait_state('empty')
            self.save_observation('observed-empty-held.json', observed_empty)
            self.command(observed_empty, self.right_pose, 0)
            self.phase = 'empty'
            neutral_empty, now = self.current()
            self.require_empty(neutral_empty)
            self.save_observation('observed-empty-neutral.json', neutral_empty)
            return neutral_empty

    return Stow


class KnifeDriver(KnifeTransport):
    def __init__(self, args, admission, stow):
        self.admission, self.stow = admission, stow
        self.attempt, self.gate, self.phase = Attempt(), None, 'baseline'
        self.baseline = self.frozen_sim = None
        self.release_attempted = False
        self.allowed_jab_lines = None
        args.stage = args.profiles = args.capture = args.menu_batches = None
        args.pickup_first = False
        super().__init__(args)
        require(self.host == admission['host'], 'Host changed between stow and knife')
        require(self.link['mechanism_snapshots']['requested'] is True
                and t.read_bytes(self.folder / 'mechanism-snapshot.enable', 2) == b'1\n',
                'Native-startup snapshot opt-in missing')
        require(simulator_controls_neutral(self.state['controls']), 'Neutral Empty controls required')
        require((self.sequence, self.sentinel, self.command_hash) == stow['command'],
                'Stow command lineage changed')
        require(self.capture_sequence + 2 <= self.capture_plan['maximum_compositions'],
                'Two capture slots required')
        self.artifacts.save('knife-admission.json', admission)

    def read_state(self):
        state = super().read_state()
        require(state['controls']['trigL'] == state['controls']['trigR'] == 0,
                'Trigger changed during zero-trigger episode')
        initial = getattr(self, 'initial_sim', None)
        if initial is not None:
            require(all(state[key] == initial[key] for key in ('head', 'handL')),
                    'Head/left hand changed during knife episode')
        return state

    def validate_publication(self, lines, expected, cleanup):
        neutral_trigger_lines(lines)
        if self.phase.startswith('jab-') and not cleanup:
            self.attempt.check()
            self.bounds(time.perf_counter_ns())
            require(lines == self.allowed_jab_lines and self.gate is not None and self.held_right
                    and not self.held_trigger and not self.pending_command, 'Exact admitted jab pose required')
            return
        return super().validate_publication(lines, expected, cleanup)

    def action(self, action, lines, source=None, validator=None):
        self.prepare_next_command(lines)
        try:
            return super().action(action, lines, source, validator)
        finally:
            self.discard_prepared_command()

    def release_after_failure(self):
        if self.release_attempted:
            return 'already attempted; no second release'
        self.release_attempted = True
        return t.Driver.release_after_failure(self)

    def held_now(self, after):
        return self.current(lambda s, now: held(s, self.baseline, now, after, self.episode))

    def initial_held(self, after):
        cutoff = min(after + 3*NS, self.deadline - 3*NS)
        while time.perf_counter_ns() + t.SNAPSHOT_TIMEOUT < cutoff:
            self.pause_until(self.last_request + t.SNAPSHOT_PERIOD)
            admitted = []

            def observe(s, now):
                require(s.get('schema') == 'fvr.bc2.live-mechanism-snapshot.v1'
                        and 0 < s['observed_ns'] <= now and now - s['observed_ns'] < 100*MS,
                        'Initial Held diagnostic lacks current response identity')
                k = s.get('chestKnife')
                if isinstance(k, dict) and k.get('available') is True:
                    held(s, self.baseline, now, after, self.episode)
                    admitted.append(True)
                    return
                require(isinstance(k, dict) and k.get('observationOnly') is True
                        and k.get('available') is False and k.get('reason') == 'observation_unavailable',
                        'Explicit initial Held rejection is terminal')
                require(s.get('valid') is True or (s.get('valid') is False
                    and s.get('invalid_reason') == 'selected_configuration_or_rig_missing_changed_or_expired'
                    and s.get('chestKnifeObservationBasis') == 'independent_owned_body'),
                    'Unexpected native observation failure')
                # Explicit absence grants no input and never renews the window.

            source = self.current(observe)
            if admitted:
                return source
        raise Refusal('Strict mode4 Held unavailable in original bounded observation window')

    def execute(self):
        start = self.current(empty)
        h80.same_outer(self.stow['snapshot'], start[0])
        h80.source_identity(self.stow['snapshot'], start[0])
        require(start[0]['input_evidence']['observedNs'] >= self.stow['acknowledgedNs'],
                'Knife Empty predates acknowledged stow release')
        pose = chest_pose(start[0])
        ypr = p.quaternion_ypr(pose['orientation'])

        def lines_at(position):
            line = 'hand r grip pose ' + ' '.join(f'{v:.9f}' for v in position + ypr)
            return [line, line.replace(' grip ', ' aim ')]

        self.action('move', ['hand r follow off'] + lines_at(pose['position']), start, empty)
        self.frozen_sim = copy.deepcopy(self.state)

        def draw_ready(s, now):
            until = empty(s, now)
            preserved(start[0], s)
            h80.source_identity(start[0], s)
            at_chest(s)
            require(s['input_evidence']['observedNs'] >= self.last_write, 'Empty predates chest approach')
            return until

        drawn = self.current(draw_ready)
        self.baseline = drawn[0]
        selection = native_selection(self.baseline, time.perf_counter_ns())
        self.action('draw', ['grip r 1'], drawn, draw_ready)
        self.episode = dict(startedNs=self.started, deadlineNs=self.deadline, stageDeadlineNs=self.stage_deadline,
            drawWrittenNs=self.last_write, drawAcknowledgedNs=self.last_commit, nativeSelection=selection)
        self.action('neutral', ['trigger r 0'])
        after = self.last_commit
        self.pause_until(after + 180*MS)
        observed = self.initial_held(after)
        self.artifacts.save('strict-held-mode4.json', observed[1])
        held_capture = self.knife_capture('held')
        direction = self.args.jab_direction_local
        steps, jab_start = [], None
        if direction is not None:
            for i, position in enumerate(jab_points(pose['position'], direction), 1):
                if jab_start is not None:
                    self.pause_until(self.last_commit + 40*MS)
                current = self.held_now(after)
                now = time.perf_counter_ns()
                if jab_start is None:
                    jab_start = now
                require(now - jab_start < 300*MS and now + NS < self.deadline,
                        'One jab window or Return reserve exhausted')
                self.frozen_sim = None
                self.allowed_jab_lines = lines_at(position)
                self.action('jab-' + str(i), self.allowed_jab_lines, current,
                    lambda s, n: held(s, self.baseline, n, after, self.episode))
                self.frozen_sim = copy.deepcopy(self.state)
                steps.append(dict(step=i, position=position, writtenNs=self.last_write,
                                  acknowledgedNs=self.last_commit))
                after = self.last_commit
            self.artifacts.save('zero-trigger-jab.json', dict(directionLocal=direction,
                directionIsMeasuredBladeAxis=False, triggerPressed=False, steps=steps,
                nativeRecognition='unresolved', damage='unknown'))
            self.pause_until(self.last_commit + 180*MS)
        self.gate = None
        release = self.release_after_failure()
        require(not self.held_right and not self.pending_command, 'Owned release was not acknowledged')
        release_ack = self.last_commit

        def returned(s, now):
            require(s.get('schema') == 'fvr.bc2.live-mechanism-snapshot.v1'
                    and 0 < s['observed_ns'] <= now and now - s['observed_ns'] < 100*MS,
                    'Return diagnostic lacks current response identity')
            if not (s.get('valid') is True and s.get('bodyHolster', {}).get('actualEmpty') is True):
                require(s.get('valid') is True or (s.get('valid') is False
                    and s.get('invalid_reason') == 'selected_configuration_or_rig_missing_changed_or_expired'
                    and s.get('chestKnifeObservationBasis') == 'independent_owned_body'),
                    'Unexpected Return observation failure')
                return False
            empty(s, now)
            returned_owners(self.baseline, observed[0]['chestKnife'], s['owner'], s['input_evidence']['owner'])
            require(s['input_evidence']['observedNs'] >= release_ack, 'Empty predates owned release')
            return True

        found_empty = False
        while time.perf_counter_ns() + t.SNAPSHOT_TIMEOUT < self.deadline:
            self.pause_until(self.last_request + t.SNAPSHOT_PERIOD)
            result = []
            self.current(lambda s, now: result.append(returned(s, now)))
            if result[0]:
                found_empty = True
                break
        require(found_empty, 'Owned release lacks fresh original-owner Empty within original deadline')
        return_capture = self.knife_capture('returned')
        return dict(status='strict_held_and_fresh_return_empty', jabSteps=len(steps), triggerPressed=False,
            nativeRecognition='requires retired attack masks/receipts' if steps else 'jab not requested',
            nativeReturn='fresh Empty only; exact Return history audit pending', damage='unknown',
            rootVisualReview='pending', release=release, captures=[held_capture, return_capture],
            nativeAcceptance=False, headsetAcceptance=False)


def finish(driver, result):
    if driver is None:
        return
    try:
        if getattr(driver, 'artifacts', None):
            result.update(durationNs=time.perf_counter_ns()-driver.started, commands=driver.commands,
                pendingCommand=driver.pending_command, rightGripHeld=driver.held_right,
                triggerHeld=driver.held_trigger, artifactDirectory=str(driver.artifacts.directory))
            driver.artifacts.save('knife-result.json', result, final=True)
    finally:
        driver.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--print-dependency-sha256', action='store_true', help='Read-only dependency identity')
    for name in ('run', 'version', 'source-sha256', 'dependencies-sha256', 'world-capture', 'storage-receipt',
                 'stow-asset', 'stow-config', 'target-asset', 'target-config'):
        parser.add_argument('--' + name)
    parser.add_argument('--jab-direction-local', type=float, nargs=3)
    for name in ('root-ordinary-input', 'root-reviewed-both-world-eyes',
                 'root-reviewed-normal-mode4-and-knife-idle'):
        parser.add_argument('--' + name, action='store_true')
    args = parser.parse_args(argv)
    if args.print_dependency_sha256:
        print(dependency_sha256())
        return 0
    for name in ('run', 'version', 'source_sha256', 'dependencies_sha256', 'world_capture', 'storage_receipt',
                 'stow_asset', 'stow_config', 'target_asset', 'target_config'):
        if not getattr(args, name):
            parser.error('--' + name.replace('_', '-') + ' is required')
    args.observed_binding = None
    driver, code = None, 2
    result = dict(status='refused', damage='unknown', nativeAcceptance=False, headsetAcceptance=False)
    try:
        admission = preflight(args)
        Stow = make_stow(admission)
        driver = Stow.__new__(Stow)
        driver.__init__(args)
        snapshot = driver.run_holster()
        require(not driver.pending_command and not driver.held_right and not driver.held_trigger,
                'Stow did not finish released')
        stow = dict(snapshot=snapshot, command=driver.command_identity()[:3], acknowledgedNs=driver.last_commit)
        stow_result = dict(status='genuine_neutral_empty', nativeAcceptance=False, headsetAcceptance=False)
        finish(driver, stow_result)
        driver = None
        result['stow'] = stow_result
        driver = KnifeDriver.__new__(KnifeDriver)
        driver.__init__(args, admission, stow)
        result.update(driver.execute())
        code = 0
    except (Exception, KeyboardInterrupt) as error:
        result['error'] = str(error)
        if driver is not None and getattr(driver, 'artifacts', None):
            if hasattr(driver, 'attempt'):
                driver.attempt.cancelled = True
            try:
                result['release'] = driver.release_after_failure()
            except Exception as cleanup:
                result['release'] = 'refused: ' + str(cleanup)
    finally:
        try:
            finish(driver, result)
        except Exception as error:
            result['resultWriteError'], code = str(error), 2
    print(json.dumps(result, separators=(',', ':'), allow_nan=False))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
