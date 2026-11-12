"""ROOT only: ordinary stow, genuine Empty, strict mode4 knife Draw and Return.

An optional jab follows the current measured blade axis for 120mm in 120ms.
Ordinary current run/install/source/command ownership stays with the shared
transport. No launch, installation, retries, target or damage acceptance.
"""
import argparse
import copy
import ctypes
import datetime
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import stat
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
motion_backend = load('knife_shared_motion', Path(__file__).with_name('drive_bc2_reload_stimulus.py'))
NS, MS = t.NS, 1_000_000
ACTOR, REFERENCE = h80.ACTOR, h80.REFERENCE
KNIFE_RIG = 0xa7f219a1426216ab
JAB_DISTANCE, JAB_MS = .120, 120
DEPENDENCIES = ('tools/drive_bc2_knife_simulation.py', 'tools/drive_bc2_holster_prepared.py',
    'tools/bc2_holster_core.py', 'tools/bc2_holster_snapshot_transport.py',
    'tools/drive_bc2_feed_simulation.py', 'tools/plan_bc2_feed_simulation.py',
    'tools/bc2_draw_readiness_driver.py', 'tools/bc2_draw_admission.py', 'tools/drive_bc2_reload_stimulus.py',
    'include/fvr/interaction/BodyAnchors.h', 'include/fvr/interaction/MeleeStroke.h',
    'src/games/bc2/KnifeGeometry.h', 'src/games/bc2/Bc2KnifeMotionGeometry.h')

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


class ContinuousPins:
    """Hash once under write-denying custody; rejoin pathname identity each read.

    As with MotionInputLease, FILE_SHARE_READ seals the data but Windows POSIX
    rename can replace its name. Check every pathname and unique ancestor on
    every call, without rereading immutable payloads inside a native lease.
    """
    def __init__(self, rows):
        import msvcrt
        require(os.name == 'nt', 'continuous pinned files require Windows custody')
        self.rows, self.entries, self.ancestors = copy.deepcopy(rows), [], set()
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        api, wt = self.api, t.wintypes
        api.CreateFileW.argtypes = (wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p,
                                   wt.DWORD, wt.DWORD, wt.HANDLE)
        api.CreateFileW.restype = wt.HANDLE
        api.CloseHandle.argtypes = (wt.HANDLE,)
        api.CloseHandle.restype = wt.BOOL
        try:
            for row in self.rows:
                path = t.local_path(row['path'])
                require(0 <= integer(row['bytes']) <= 1024*1024, 'pinned file exceeds read bound')
                handle = api.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x00200000, None)
                require(handle not in (None, wt.HANDLE(-1).value), 'cannot seal pinned file: ' + str(path))
                try:
                    fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
                except BaseException:
                    api.CloseHandle(handle)
                    raise
                stream = os.fdopen(fd, 'rb')
                self.entries.append((path, stream, None))
                self.entries[-1] = path, stream, t.owner_file_identity(api, handle)
                raw = stream.read(1024*1024 + 1)
                require(len(raw) == row['bytes'] and t.digest(raw) == row['sha256'],
                        'pinned file changed before custody: ' + str(path))
                self.ancestors.update(path.parents)
            self.check(rows)
        except BaseException:
            self.close()
            raise

    def check(self, rows):
        require(rows == self.rows and len(self.entries) == len(rows) > 0,
                'continuous pinned file custody unavailable or changed')
        # Preserve local_path's redirect checks, visiting shared parents once.
        for path in self.ancestors:
            info = path.lstat()
            require(not stat.S_ISLNK(info.st_mode)
                    and (not getattr(info, 'st_file_attributes', 0) & 0x400
                         or getattr(info, 'st_reparse_tag', 0) == 0x80000017),
                    'pinned file ancestor redirected: ' + str(path))
        for path, stream, identity in self.entries:
            require(not stream.closed, 'pinned file read custody closed: ' + str(path))
            handle = self.api.CreateFileW(str(path), 0, 7, None, 3, 0x00200000, None)
            require(handle not in (None, t.wintypes.HANDLE(-1).value),
                    'pinned file pathname disappeared: ' + str(path))
            try:
                require(t.owner_file_identity(self.api, handle) == identity,
                        'pinned file pathname identity changed: ' + str(path))
            finally:
                self.api.CloseHandle(handle)

    def close(self):
        for _, stream, _ in self.entries:
            stream.close()
        self.entries = []


class Admission(dict):
    """Serializable evidence with one invocation-owned, non-serialized seal."""
    lease = None

    def seal(self):
        require(self.lease is None, 'continuous pinned file custody already acquired')
        self.lease = ContinuousPins(self['continuousPins'])

    def close(self):
        if self.lease is not None:
            self.lease.close()
            self.lease = None


def check_continuous(admission):
    require(isinstance(admission, Admission) and admission.lease is not None,
            'continuous pinned file custody unavailable')
    admission.lease.check(admission['continuousPins'])


def unit_direction(direction):
    require(len(direction) == 3 and all(type(v) in (float, int) and math.isfinite(v) for v in direction),
            'Finite LOCAL direction required')
    require(abs(math.sqrt(sum(v*v for v in direction)) - 1) < .001,
            'ROOT direction must already be unit length')
    return direction


def measured_jab(snapshot, now):
    """Current rendered knife rotation, joined to the strict Held input.

    Authored RightHandInKnife * placedKnife recovers the tracked right wrist;
    its rotation is unchanged by arm reach solving. Transforming the measured
    blade rotation back through body/reference therefore matches native
    KnifeMotionFromTrackedGrip, without substituting the left wrist or camera.
    """
    k, inp = snapshot['chestKnife'], snapshot['input']
    p.admit_frame(snapshot, now, integer(snapshot['nonce']), mechanism_admission=lambda s, n: True)
    same_stamps(snapshot['input_evidence'], k['inputEvidence'])
    require(snapshot['owner'] == k['owner']
            and all(snapshot['input_evidence'][key] == k['inputEvidence'][key]
                    for key in ('owner', 'sequence', 'observedNs', 'deadlineNs', 'focused', 'tracked', 'released'))
            and inp['generation'] == k['render']['inputEvidence']['sequence']
            and snapshot['rig']['fingerprint'] == k['render']['rigFingerprint'] == KNIFE_RIG
            and snapshot['selected']['configurationPath'] == 'Objects/Weapons/Handheld/Knife/Knife'
            and snapshot['selected']['matchedMeshPath'] == 'Objects/Weapons/Handheld/Knife/Knife_Mesh'
            and snapshot['equipment']['asset'] == 'KNV-1'
            and inp['trackingBasisGeneration'] == k['trackingBasisGeneration'],
            'jab geometry is not the current rendered Held knife')
    for hand, controls in zip(inp['hands'], k['controls']):
        require(all(hand[key] == controls[key] for key in controls), 'jab geometry/input controls differ')
    weapon = p.rigid(snapshot['mapping']['weaponWorldMeters'])
    body = p.rigid(snapshot['mapping']['trackingBodyWorldMeters'])
    reference = p.from_pose(inp['referenceHead'])
    # Remove translations before composition, including large world positions.
    for matrix in (weapon, body, reference):
        matrix[3] = [0., 0., 0., 1.]
    knife = p.multiply(p.multiply(weapon, p.inverse(body)), reference)
    axis = [-knife[2][0], -knife[2][1], knife[2][2]]  # authored -Z; LH -> OpenXR LOCAL
    length = math.sqrt(sum(v*v for v in axis))
    require(.999 <= length <= 1.001, 'measured blade axis is not rigid')
    direction = unit_direction([v/length for v in axis])
    grip = inp['hands'][1]['grip']
    endpoint = [p.finite(v) + JAB_DISTANCE*d for v, d in zip(grip['position'], direction)]
    require(all(math.dist(point, inp['head']['position']) <= 1.5 for point in (grip['position'], endpoint)),
            'jab leaves native head-relative reach')
    return dict(backend='xr-sim.hand-to-grip.smoothstep.v1', durationMs=JAB_MS,
        distanceMeters=JAB_DISTANCE, directionLocal=direction, directionIsMeasuredBladeAxis=True,
        origin=copy.deepcopy(grip['position']), endpoint=endpoint,
        yawPitchRollDegrees=p.quaternion_ypr(grip['orientation']),
        sourceNonce=snapshot['nonce'], sourceInput=inp['generation'],
        hold=copy.deepcopy(k['hold']), knife=copy.deepcopy(k['knife']), bodyOperation=k['bodyOperation'],
        nativeMeleeStroke=dict(minimumSpeed=.65, minimumTravel=.06, maximumSpeed=8.,
            maximumStep=.30, minimumAlignment=.65, settleNs=120*MS, maximumSampleGapNs=75*MS,
            maximumStrokeNs=350*MS, rearmNs=180*MS), peakSpeedMetersPerSecond=1.5)


def motion_line(position, ypr, duration_ms):
    require(duration_ms in (0, JAB_MS) and len(position) == len(ypr) == 3
            and all(math.isfinite(v) for v in position+ypr), 'invalid bounded knife motion')
    return 'hand r to grip ' + ' '.join(f'{v:.9f}' for v in position+ypr) + ' ' + str(duration_ms)


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
        unit_direction(args.jab_direction_local)
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
    jab_pins = []
    if getattr(args, 'jab', False) or args.jab_direction_local is not None:
        runtime_pin = manifest['runtime_library']
        require(runtime_pin['sha256'] == motion_backend.NEUTRAL_MOTION_RUNTIME_SHA256,
                'continuous jab requires the pinned xr-sim runtime')
        t.check_pin(runtime_pin)
        jab_pins = [runtime_pin]
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
    return Admission(continuousPins=dependencies + jab_pins + [pin(path) for path in paths], world=world,
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
        self.outstanding_motion = None
        self._motion_owner_bytes = None
        self._jab_before_publication = None
        self.allowed_cancel_lines = None
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
        motion = self.outstanding_motion
        if motion is not None:
            require(all(state[key] == motion['basis'][key] for key in ('head', 'handL')),
                    'jab changed head/left hand')
            controls = dict(state['controls'])
            if self.allowed_cancel_lines is not None and controls['gripR'] == 0:
                controls['gripR'] = 1
            require(controls == motion['controls'], 'jab controls changed')
            hand = state['handR']
            current = p.from_pose(dict(position=hand['pos'], orientation=p.ypr_quaternion(hand['ypr'])))
            origin = p.from_pose(dict(position=motion['origin'],
                orientation=p.ypr_quaternion(motion['yawPitchRollDegrees'])))
            require(hand['valid'] is True and hand['followsHead'] is False
                    and p.pose_error(current, origin)[1] <= .001, 'jab hand tracking/orientation changed')
            delta = [a-b for a, b in zip(hand['pos'], motion['origin'])]
            travel = sum(a*b for a, b in zip(delta, motion['directionLocal']))
            perpendicular = math.sqrt(sum((v-travel*d)**2 for v, d in zip(delta, motion['directionLocal'])))
            require(-.0002 <= travel <= JAB_DISTANCE+.0002 and perpendicular <= .0002,
                    'jab hand left the admitted straight segment')
        return state

    def owner_bytes(self, pending):
        raw = super().owner_bytes(pending)
        if self.outstanding_motion is None:
            return raw
        value = json.loads(raw)
        value.update(pending=True, outstandingMotion=self.outstanding_motion)
        return t.json_bytes(value)

    def ownership(self, state, expected=None):
        before = self._jab_before_publication
        published_jab = (self.phase == 'jab' and self.gate is not None and self.outstanding_motion is not None
            and before is not None and self.last_write != before[0]
            and (self.sequence, self.sentinel, self.command_hash) != before[1:])
        if not published_jab:
            return super().ownership(state, expected)
        # Shared send updates last_write only after the admitted atomic rename.
        # The one bounded stroke is now published: keep exact source bytes and
        # command custody, but an aged initiating lease is not an ACK failure.
        # The pre-rename callback still takes the ordinary fresh Held path.
        t.Driver.ownership(self, state, expected)
        snapshot, raw, _ = self.gate
        current, current_raw = t.read_json(self.snapshot_path, p.SNAPSHOT_LIMIT)
        require(current_raw == raw and current == snapshot, 'admitted jab snapshot replaced after publication')

    def save_owner(self, pending, *, settled_writer=None):
        # Shared pending-ACK cleanup also calls this method. Rejoin the exact
        # retained motion owner before it can overwrite/settle that record.
        if self._motion_owner_bytes is not None:
            self.dispose_publication_owner()
            if t.read_bytes(self.run/'live-driver-owner.json', 8192) != self._motion_owner_bytes:
                self.unsafe_owner = True
                raise Refusal('outstanding jab owner record replaced before settlement')
        super().save_owner(pending, settled_writer=settled_writer)
        self._motion_owner_bytes = self.owner_bytes(pending) if self.outstanding_motion is not None else None

    def validate_publication(self, lines, expected, cleanup):
        neutral_trigger_lines(lines)
        if cleanup and self.outstanding_motion is not None:
            require(lines == self.allowed_cancel_lines and self.held_right and not self.held_trigger
                    and not self.pending_command and not self.unsafe_owner,
                    'outstanding jab requires exact owned cancellation and release')
            return
        if self.phase == 'jab' and not cleanup:
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
        if self.outstanding_motion is not None:
            self.monitor = False
            self.gate = self.frozen_sim = None
            if self.pending_command and not self.unsafe_owner:
                self.settle_pending_for_release()
            if self.pending_command or self.unsafe_owner or not self.held_right:
                return 'not attempted: no settled owned held controller'
            require(time.perf_counter_ns()+250*MS < self.hard_deadline, 'jab cancellation reserve exhausted')
            state = self.motion_state(self.hard_deadline)
            self.allowed_cancel_lines = [motion_line(state['handR']['pos'],
                self.outstanding_motion['yawPitchRollDegrees'], 0), 'grip r 0']
            self.prepare_next_command(self.allowed_cancel_lines)
            try:
                self.send(self.allowed_cancel_lines, cleanup=True)
            finally:
                self.discard_prepared_command()
            require(not self.pending_command and not self.held_right, 'jab cancellation release was not acknowledged')
            motion_backend.ReloadStimulus.check_target(self.state, dict(hand='r',
                positionMeters=state['handR']['pos'],
                yawPitchRollDegrees=self.outstanding_motion['yawPitchRollDegrees']))
            self.clear_motion()
            self.artifacts.event(dict(type='knife_motion_cancelled', acknowledgedNs=self.last_commit,
                                      originalActionRemainsRefused=True), cleanup=True)
            return 'owned interpolation cancellation and grip release acknowledged'
        return t.Driver.release_after_failure(self)

    def motion_state(self, until):
        state = t.read_until(until, self.read_state)
        t.read_until(until, self.ownership, state)
        if t.read_until(until, t.read_bytes, self.run/'live-driver-owner.json', 8192) != self.owner_bytes(False):
            self.unsafe_owner = True
            raise Refusal('outstanding jab owner record replaced')
        require(state['captureSeq'] == self.capture_sequence, 'unexpected capture during jab')
        return state

    def clear_motion(self):
        old = self.outstanding_motion
        self.outstanding_motion = None
        try:
            self.save_owner(False)
        except BaseException:
            self.outstanding_motion = old
            raise

    def execute_jab(self, after):
        current = self.held_now(after)
        preserved(self.baseline, current[0])
        plan = measured_jab(current[0], time.perf_counter_ns())
        reviewed = self.args.jab_direction_local
        require(reviewed is None or math.dist(reviewed, plan['directionLocal']) <= .001,
                'reviewed direction differs from the currently measured blade axis')
        state = self.read_state()
        self.ownership(state)
        motion_backend.ReloadStimulus.check_target(state, dict(hand='r', positionMeters=plan['origin'],
            yawPitchRollDegrees=plan['yawPitchRollDegrees']))
        require(time.perf_counter_ns()+JAB_MS*MS+motion_backend.RECEIVE_NS+NS < self.deadline,
                'single jab and Return reserve exhausted')
        self.outstanding_motion = dict(plan, runtimeSha256=self.manifest['runtime_library']['sha256'],
            basis={key: copy.deepcopy(state[key]) for key in ('head', 'handL')},
            controls=copy.deepcopy(state['controls']))
        self.frozen_sim = None
        self.allowed_jab_lines = [motion_line(plan['endpoint'], plan['yawPitchRollDegrees'], JAB_MS)]
        before = self.sequence, self.sentinel, self.command_hash
        self._jab_before_publication = (self.last_write, *before)
        try:
            self.action('jab', self.allowed_jab_lines, current,
                lambda s, n: held(s, self.baseline, n, after, self.episode))
        except BaseException:
            if (self.sequence, self.sentinel, self.command_hash) == before:
                self.outstanding_motion = None  # No published runtime motion to cancel.
                if not self.unsafe_owner:
                    self.save_owner(False)
            raise
        # Reuse the shared motion custody primitive. ACK is parsing, not arrival;
        # wait from ACK conservatively, then require a later completed frame.
        finish = self.last_commit + JAB_MS*MS
        until = min(self.deadline, self.stage_deadline, finish+motion_backend.RECEIVE_NS)
        require(finish < until, 'jab exceeds original deadline')
        lease = motion_backend.MotionInputLease(self, until)
        def check_lease():
            try:
                lease.check(until)
            except motion_backend.transport.ReadDeadlineExceeded:
                raise
            except BaseException:
                self.unsafe_owner = True
                raise
        try:
            while time.perf_counter_ns() < finish:
                self.bounds(time.perf_counter_ns())
                self.owner.check()
                require(not (self.folder/'stop.request').exists(), 'normal stop during jab')
                check_lease()
                time.sleep(.005)
            first = self.motion_state(until)
            while True:
                self.bounds(time.perf_counter_ns())
                check_lease()
                time.sleep(.005)
                state = self.motion_state(until)
                if integer(state['frame']) > integer(first['frame']) and integer(state['endFrames']) > integer(first['endFrames']):
                    break
            motion_backend.ReloadStimulus.check_target(state, dict(hand='r', positionMeters=plan['endpoint'],
                yawPitchRollDegrees=plan['yawPitchRollDegrees']))
            require(time.perf_counter_ns() < until, 'original jab endpoint deadline')
        except BaseException:
            # A replaced sealed pathname never grants cancellation authority.
            try:
                self.motion_state(until)
            except BaseException:
                if time.perf_counter_ns() < until:
                    self.unsafe_owner = True
            raise
        finally:
            lease.close()
        self.state = state
        proof = dict(plan, writtenNs=self.last_write, acknowledgedNs=self.last_commit,
            endpointNs=time.perf_counter_ns(), endpointFrame=state['frame'], endpointEndFrames=state['endFrames'],
            triggerPressed=False, nativeRecognition='requires retired attack receipts/masks', damage='unknown')
        self.clear_motion()
        self.frozen_sim = copy.deepcopy(state)
        self.artifacts.save('zero-trigger-jab.json', proof)
        return proof

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
        jab = None
        if self.args.jab or self.args.jab_direction_local is not None:
            jab = self.execute_jab(after)
            self.pause_until(time.perf_counter_ns() + 180*MS)
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
        return dict(status='strict_held_and_fresh_return_empty', jabSteps=int(jab is not None), jab=jab,
            triggerPressed=False,
            nativeRecognition='requires retired attack masks/receipts' if jab else 'jab not requested',
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
                triggerHeld=driver.held_trigger, outstandingMotion=getattr(driver, 'outstanding_motion', None),
                artifactDirectory=str(driver.artifacts.directory))
            driver.artifacts.save('knife-result.json', result, final=True)
    finally:
        driver.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--print-dependency-sha256', action='store_true', help='Read-only dependency identity')
    for name in ('run', 'version', 'source-sha256', 'dependencies-sha256', 'world-capture', 'storage-receipt',
                 'stow-asset', 'stow-config', 'target-asset', 'target-config'):
        parser.add_argument('--' + name)
    parser.add_argument('--jab', action='store_true', help='One current measured-axis120mm/120ms zero-trigger jab')
    parser.add_argument('--jab-direction-local', type=float, nargs=3,
        help='Optional reviewed-axis assertion; actual direction always comes from current Held geometry')
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
    driver, admission, code = None, None, 2
    result = dict(status='refused', damage='unknown', nativeAcceptance=False, headsetAcceptance=False)
    try:
        admission = preflight(args)
        admission.seal()
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
        finally:
            if admission is not None:
                try:
                    admission.close()
                except Exception as error:
                    result['pinCloseError'], code = str(error), 2
    print(json.dumps(result, separators=(',', ':'), allow_nan=False))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
