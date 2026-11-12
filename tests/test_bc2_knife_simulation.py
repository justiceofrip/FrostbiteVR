"""Assembled synthetic ordinary-run checks; no game, process or headset evidence.

The full CLI/preflight, shared driver constructors, holster sequence, current
snapshot admission, strict Held and Return gates run against temporary files.
Only OS process handles, simulator command execution and native publication are
fakes. Historical report modules and live installed/session files are not used.
"""
import copy
from contextlib import ExitStack, redirect_stdout
import datetime
import importlib.util
import io
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


fixtures = load('knife_holster_test_fixtures', ROOT / 'tests/test_bc2_holster_shared_entry.py')
ENTRY = ROOT / 'tools/drive_bc2_knife_simulation.py'


class Clock:
    def __init__(self):
        self.now = 2_000_000_000

    def tick(self):
        self.now += 10_000
        return self.now

    def sleep(self, seconds):
        self.now += int(seconds * 1_000_000_000)


class FakeWindowsOwner:
    def __init__(self, expected):
        self.expected = expected
        self.api = SimpleNamespace(CloseHandle=lambda handle: None)

    def check(self):
        pass

    def close(self):
        pass


def rebase(value, offset):
    if isinstance(value, dict):
        return {key: (item + offset if type(item) is int and item >= fixtures.NOW - 200_000_000
                      and (key.endswith('Ns') or key.endswith('_ns')) else rebase(item, offset))
                for key, item in value.items()}
    if isinstance(value, list):
        return [rebase(item, offset) for item in value]
    return value


def body_snapshot(m, now, nonce, state, *, is_empty=False, returned=False):
    s = rebase(fixtures.snapshot(), now - fixtures.NOW)
    s['nonce'] = nonce
    generation = nonce % 1_000_000 + 1
    # Request nonces increase in the fixture, and independently emitted original
    # inputs have their own monotonic sequence.
    generation = int(now // 10_000)
    s['input']['generation'] = s['shot_input_generation'] = generation
    s['input']['menu'] = dict(epoch=7)
    for index, hand in enumerate(('handL', 'handR')):
        pose = dict(position=state[hand]['pos'][:], orientation=m.p.ypr_quaternion(state[hand]['ypr']))
        s['input']['hands'][index].update(grip=copy.deepcopy(pose), aim=copy.deepcopy(pose), held=0,
            squeeze=state['controls']['gripL' if index == 0 else 'gripR'])
    s['input_evidence']['sequence'] = generation
    s['input_evidence']['released'] = [True, state['controls']['gripR'] == 0]
    s['selected']['sequence'] = generation
    if returned:
        s['owner']['equipGeneration'] = 3
        s['physical_equipment_generation'] = 4
        s['input_evidence']['owner']['equipGeneration'] = 4
    h, ev = s['bodyHolster'], s['input_evidence']
    h.update(input=copy.deepcopy(s['input']), nativeOwner=copy.deepcopy(s['owner']),
             inputEvidence=copy.deepcopy(ev), nativeTick=generation,
             neutralTrackedInput=state['controls']['gripR'] == 0)
    h['physicalGun']['generation'] = s['physical_equipment_generation']
    h['rightClaim']['inputSequence'] = generation
    h['rightClaim']['token'].update(owner=copy.deepcopy(ev['owner']), item=copy.deepcopy(h['physicalGun']))
    d = s['bodyInventory']
    d.update(selectedOwner=copy.deepcopy(s['owner']), physicalOwner=copy.deepcopy(ev['owner']),
             inputEvidence=copy.deepcopy(ev), sequence=generation)
    for row in d['slots']:
        row['configuration']['sequence'] = generation
        row['configuration']['owner'] = copy.deepcopy(s['owner'])
    if is_empty:
        h.update(phase=3, actualEmpty=True, emptyHands=True, rightClaim=None,
                 blockWeaponActions=True, allowAutomaticGunHold=False, freeRightCurrent=True)
        s['weapon_actions_blocked'] = True
        deadline = ev['deadlineNs']
        h['freeRight'] = dict(inputEvidence=copy.deepcopy(ev), authorizationDeadlineNs=0,
            visibility=dict(nativeOwner=copy.deepcopy(s['owner']), request=h['request'], hidden=True,
                verifiedCopyMask=3, drawSerial=1, physicalEquipGeneration=s['physical_equipment_generation'],
                inputSequence=generation, observedNs=ev['observedNs'], deadlineNs=deadline),
            suppression=dict(owner=copy.deepcopy(s['owner']), inputEvidence=copy.deepcopy(ev),
                request=h['request'], nativeTick=h['nativeTick'], cache=0x10000, equipmentDispatched=False))
    return s


def knife_snapshot(m, baseline, now, nonce, state=None):
    owner = dict(baseline['owner'], weapon=0x90000, equipGeneration=2)
    physical = dict(baseline['input_evidence']['owner'], equipGeneration=3)
    ev = dict(owner=physical, sequence=now // 10_000, observedNs=now-1_000_000,
              deadlineNs=now+80_000_000, nowNs=now, focused=True, tracked=[True, True], released=[True, False])
    knife = dict(id=owner['weapon'], generation=1)
    k = dict(observationOnly=True, available=True, reason=None, phase=4,
        motionEnabled=True, attackBridgeEnabled=False, owner=owner, originalOwner=copy.deepcopy(baseline['owner']),
        originalGun=copy.deepcopy(baseline['bodyHolster']['selectedSlot']['item']), knife=knife,
        continuity=1, bodyOperation=1, drawReceipts=1, returnReceipts=0, inputEvidence=ev,
        hold=dict(owner=copy.deepcopy(physical), id=1, hand=1, kind=1, item=knife,
            contact=dict(id=1, generation=1), prerequisiteClaim=0, inputSequence=ev['sequence'],
            deadlineNs=ev['deadlineNs']),
        render=dict(owner=copy.deepcopy(owner), knife=knife, bodyOperation=1, rigFingerprint=m.KNIFE_RIG,
                    inputEvidence=copy.deepcopy(ev)),
        menu=dict(owner=1, sequence=1, epoch=7, deadlineNs=ev['deadlineNs']),
        trackingBasisGeneration=baseline['input']['trackingBasisGeneration'],
        controls=[dict(held=0, trigger=0, stickX=0, stickY=0, squeeze=i) for i in (0, 1)],
        knifeSlot=7, selectedSlots=[7, 7, 7], nativeSequence=ev['sequence'],
        nativeObservedNs=now, nativeTick=1, suppressionRequest=1,
        validUntilNs=ev['deadlineNs'], observedNs=now)
    for key in ('held', 'stockTriggerReady', 'stockTriggerArmed', 'nativeOwned', 'nativeHealthy',
                'nativeObservationComplete', 'stockTriggerEnabled', 'fireSuppressed', 'mechanicsSettled'):
        k[key] = True
    result = dict(schema='fvr.bc2.live-mechanism-snapshot.v1', valid=False,
        invalid_reason='selected_configuration_or_rig_missing_changed_or_expired',
        chestKnifeObservationBasis='independent_owned_body', nonce=nonce, observed_ns=now, chestKnife=k)
    if state is not None:
        result = body_snapshot(m, now, nonce, state)
        result.update(valid=True, invalid_reason=None, chestKnife=k, owner=copy.deepcopy(owner),
            input_evidence=copy.deepcopy(ev), physical_equipment_generation=physical['equipGeneration'],
            valid_until_ns=ev['deadlineNs'], native_deadline_ns=ev['deadlineNs'],
            shot_input_generation=ev['sequence'], weapon_actions_blocked=True)
        result['input']['generation'] = ev['sequence']
        result['equipment'].update(weapon=owner['weapon'], asset='KNV-1')
        result['rig']['fingerprint'] = m.KNIFE_RIG
        mesh = 'Objects/Weapons/Handheld/Knife/Knife_Mesh'
        result['selected'].update(configurationPath='Objects/Weapons/Handheld/Knife/Knife',
            matchedMeshPath=mesh, meshes=[dict(path=mesh)], sequence=ev['sequence'],
            observedNs=ev['observedNs'], deadlineNs=ev['deadlineNs'])
        # Saved87/03 input15082 rotations. No historical live ownership is used.
        result['mapping'] = dict(weaponWorldMeters=[
            [.764220357, -.245604619, .596359909, 0], [.630696595, .477942348, -.611385584, 0],
            [-.134866670, .843355596, .520155549, 0], [164.747543, 170.810394, 378.535217, 1]],
            trackingBodyWorldMeters=[[.896846652, 0, .442341536, 0], [0, 1, 0, 0],
                [-.442341536, 0, .896846652, 0], [164.771576, 171.059097, 378.295654, 1]])
    return result


class KnifeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knife-source-only-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        names = ('drive_bc2_knife_simulation.py', 'drive_bc2_holster_prepared.py', 'bc2_holster_core.py',
            'bc2_holster_snapshot_transport.py', 'drive_bc2_feed_simulation.py', 'plan_bc2_feed_simulation.py',
            'bc2_draw_readiness_driver.py', 'bc2_draw_admission.py', 'drive_bc2_reload_stimulus.py')
        for name in names:
            target = self.root / 'tools' / name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(ROOT / 'tools' / name, target)
        anchors = self.root / 'include/fvr/interaction/BodyAnchors.h'
        anchors.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / 'include/fvr/interaction/BodyAnchors.h', anchors)
        for name in ('include/fvr/interaction/MeleeStroke.h', 'src/games/bc2/KnifeGeometry.h',
                     'src/games/bc2/Bc2KnifeMotionGeometry.h'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, path)
        self.m = load('knife_source_only_' + self.root.name.replace('-', '_'), self.root / 'tools' / ENTRY.name)
        self.clock = Clock()
        self.run, self.folder = self.root / 'run', self.root / 'session'
        (self.run / 'sim/capture').mkdir(parents=True)
        self.folder.mkdir()
        self.version, self.source = '0.1.999', 'a' * 64
        self.package = self.root / 'launcher/versions' / self.version
        self.runtime = self.package / 'runtime'
        (self.runtime / 'bin').mkdir(parents=True)
        self.write(self.package / 'package.json', dict(version=self.version))
        self.write(self.runtime / 'runtime.json', dict(schema='bc2vr.runtime.v1', version=self.version,
                   source_sha256=self.source, diagnostic_only=False))
        self.active = self.package.parent.parent / 'active.json'
        self.write(self.active, dict(current=self.version))
        for name in ('BC2NativeProbe.dll', 'BC2XrHost.exe'):
            (self.runtime / 'bin' / name).write_bytes(('synthetic-' + name).encode())
        self.host = dict(pid=999, path=str(self.runtime / 'bin/BC2XrHost.exe'), start_utc='2026-01-01T00:00:00Z')
        (self.root / 'xrsim.dll').write_bytes(b'fake-runtime')
        self.write(self.root / 'runtime-x64.json', dict(runtime='synthetic'))
        pins = self.m.pin
        self.plan = dict(periodic=False, layers=False, maximum_compositions=3, width=640, height=480)
        self.manifest = dict(schema='fvr.xr-game-run.v1', output_directory=str(self.run),
            version=self.version, source_sha256=self.source, artifact_binding_path=str(self.run / 'artifact-binding.json'),
            runtime_manifest=pins(self.root / 'runtime-x64.json'), runtime_library=pins(self.root / 'xrsim.dll'),
            host_executable=pins(self.runtime / 'bin/BC2XrHost.exe'), capture_plan=self.plan)
        self.write(self.run / 'run-manifest.json', self.manifest)
        self.binding = dict(version=self.version, source_sha256=self.source,
            runtime=pins(self.runtime / 'runtime.json'), package=pins(self.package / 'package.json'),
            binaries=[pins(self.runtime / 'bin' / name) for name in ('BC2NativeProbe.dll', 'BC2XrHost.exe')])
        self.write(self.run / 'artifact-binding.json', self.binding)
        controls = {key: False for key in ('a', 'b', 'x', 'y', 'menu', 'clickL', 'clickR', 'restL', 'restR')}
        controls.update(trigL=0, trigR=0, gripL=0, gripR=0, stickL=[0, 0], stickR=[0, 0])
        self.state = dict(pid=999, runtime='xr-sim', errors=0, lastCmdError='', sessionRunning=True,
            actionsAttached=True, sessionState='FOCUSED', paceMode='free', captureSeq=1, cmdSeq=1, frame=1, endFrames=1,
            lastCmd='note startup', controls=controls,
            head=dict(valid=True, pos=[0, 1.6, 0], ypr=[0, 0, 0]),
            handL=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]),
            handR=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]))
        self.write(self.run / 'sim/state.json', self.state)
        (self.run / 'sim/command.txt').write_bytes(b'note startup\n')
        self.write(self.run / 'sim-startup.json', dict(root_commands_ready=True, status='ready', host=self.host,
            target_sequence=1, command_file=pins(self.run / 'sim/command.txt'), settled_state=self.state))
        self.link = dict(root_commands_ready=True, host=self.host, supervisor=dict(pid=111), folder=str(self.folder),
            sim_state=str(self.run / 'sim/state.json'), command=str(self.run / 'sim/command.txt'),
            session=str(self.folder / 'session.json'), options=str(self.folder / 'options.json'),
            stop_request=str(self.folder / 'stop.request'), startup_receipt=str(self.run / 'sim-startup.json'),
            mechanism_snapshots=dict(requested=True))
        self.write(self.run / 'session-link.json', self.link)
        self.write(self.folder / 'session.json', dict(host=self.host, supervisor=self.link['supervisor'],
            source_sha256=self.source, version=self.version, state='running'))
        self.write(self.folder / 'options.json', dict(source_sha256=self.source, version=self.version,
            activate_window=False, xr_manifest=str(self.root / 'runtime-x64.json')))
        (self.folder / 'mechanism-snapshot.enable').write_bytes(b'1\n')
        meta = dict(name='world', sessionState='FOCUSED', width=640, height=480,
            displayTimeNs=self.clock.now, views=[dict(eye='left'), dict(eye='right')],
            controls=copy.deepcopy(controls), head=dict(pos=self.state['head']['pos'], ypr=[0, 0, 0]))
        for hand in ('handL', 'handR'):
            meta[hand] = dict(grip=self.state[hand]['pos'], aimYpr=[0, 0, 0])
        self.write(self.run / 'sim/capture/world.json', meta)
        for eye in ('left', 'right'):
            (self.run / 'sim/capture' / ('world_' + eye + '.png')).write_bytes(b'\x89PNG\r\n\x1a\nsynthetic')
        self.storage = self.root / 'storage.json'
        self.write(self.storage, dict(schema='fvr.storage-admission.v1', operation='Capture', output_path=str(self.run),
            planned_bytes=18*1024*1024, raw_capture_bytes=12*1024*1024,
            checked_utc=datetime.datetime.now(datetime.timezone.utc).isoformat()))
        self.calls, self.prepared, self.snapshots = [], [], []
        self.held_mutation = None
        self.motion = None
        self.motion_fault = None
        self.owner_during_motion = []
        self.ack_delay_ns = 0
        self.expired_publication_checks = 0

    def write(self, path, value):
        path.write_text(json.dumps(value), encoding='utf-8')

    def argv(self, jab=False):
        args = ['--run', str(self.run), '--version', self.version, '--source-sha256', self.source,
            '--dependencies-sha256', self.m.dependency_sha256(), '--world-capture', 'world',
            '--storage-receipt', str(self.storage), '--root-ordinary-input', '--root-reviewed-both-world-eyes',
            '--root-reviewed-normal-mode4-and-knife-idle', '--stow-asset', fixtures.SOURCE[0],
            '--stow-config', fixtures.SOURCE[1], '--target-asset', fixtures.TARGET[0],
            '--target-config', fixtures.TARGET[1]]
        return args + (['--jab'] if jab else [])

    def arguments(self):
        values = dict(run=str(self.run), version=self.version, source_sha256=self.source,
            dependencies_sha256=self.m.dependency_sha256(), world_capture='world', storage_receipt=str(self.storage),
            root_ordinary_input=True, root_reviewed_both_world_eyes=True,
            root_reviewed_normal_mode4_and_knife_idle=True, jab=False, jab_direction_local=None)
        return SimpleNamespace(**values)

    def start_motion(self, words):
        values = list(map(float, words[4:10]))
        self.motion = dict(origin=self.state['handR']['pos'][:], endpoint=values[:3], ypr=values[3:],
            started=self.clock.now, duration=int(words[10])*1_000_000)
        if not self.motion['duration']:
            self.state['handR'].update(pos=values[:3], ypr=values[3:])
            self.motion = None

    def advance_motion(self):
        if self.motion is not None:
            motion = self.motion
            u = min(1., max(0., (self.clock.now-motion['started'])/motion['duration']))
            f = u*u*(3-2*u)
            self.state['handR'].update(pos=[a+(b-a)*f for a, b in zip(motion['origin'], motion['endpoint'])],
                                      ypr=motion['ypr'])
            owner = self.run/'live-driver-owner.json'
            if owner.exists():
                try:
                    self.owner_during_motion.append(json.loads(owner.read_text()))
                except PermissionError:
                    pass  # Shared real publication temporarily seals this file.
            if u == 1:
                self.motion = None
        if self.motion_fault != 'stalled_frames':
            self.state['frame'] += 1
            self.state['endFrames'] += 1
        self.write(self.run/'sim/state.json', self.state)

    def fake_send(self, driver, lines, expected=None, capture=None, cleanup=False, source_deadline=None, **kwargs):
        # Fake simulator execution only. Actual admission and command ownership
        # run before every mock ACK; this is not native evidence.
        driver.ownership(driver.read_state())
        driver.validate_publication(lines, expected, cleanup)
        now = self.clock.tick()
        if source_deadline is not None:
            self.m.require(now < source_deadline, 'Synthetic publication expired')
        self.calls.append((driver.__class__.__name__, lines[:], cleanup))
        driver.last_write = now
        sentinel = 'note root-feed-synthetic-' + str(len(self.calls))
        raw = ('\n'.join(lines + [sentinel]) + '\n').encode()
        driver.sequence += len(lines) + 1
        driver.commands += len(lines) + 1
        driver.sentinel, driver.command_hash = sentinel, self.m.t.digest(raw)
        driver.command_path.write_bytes(raw)
        for line in lines:
            words = line.split()
            if words[:3] == ['hand', 'r', 'follow']:
                self.state['handR']['followsHead'] = False
            elif words[:4] == ['hand', 'r', 'grip', 'pose']:
                numbers = list(map(float, words[4:]))
                self.state['handR'].update(pos=numbers[:3], ypr=numbers[3:])
            elif words[:4] == ['hand', 'r', 'to', 'grip']:
                self.start_motion(words)
            elif words[:2] == ['grip', 'r']:
                self.state['controls']['gripR'] = float(words[2])
            elif words[:2] == ['trigger', 'r']:
                self.state['controls']['trigR'] = float(words[2])
            elif words[0] == 'shot':
                driver.capture_sequence += 1
                self.state['captureSeq'] = driver.capture_sequence
                self.write(self.run / 'sim/capture' / (words[1] + '.json'), dict(width=640, height=480))
        self.state.update(cmdSeq=driver.sequence, lastCmd=sentinel)
        self.write(driver.state_path, self.state)
        driver.state = copy.deepcopy(self.state)
        driver.remember_controls(lines)
        driver.pending_command = False
        driver.pending_batch = lines + [sentinel]
        driver.last_commit = self.clock.tick()
        driver.save_owner(False)

    def run_assembled(self, jab=False, replace_at_publication=False, real_transport=False,
                      stow_publication_expiry=False, pin_io_cost_ns=0):
        m = self.m
        original_request = m.t.Driver.request_snapshot
        original_replace = m.t.replace_command
        original_read = m.t.Driver.read_state
        original_pump = m.KnifeTransport.pump
        original_pin = m.t.check_pin
        simulated_command = self.run.joinpath('sim/command.txt').read_bytes()
        current_driver = None
        delayed_ack = None

        def simulate():
            nonlocal simulated_command, delayed_ack
            if delayed_ack is not None and self.clock.now >= delayed_ack[0]:
                delayed_ack[1].write_text(delayed_ack[2], encoding='utf-8')
                delayed_ack = None
            raw = self.run.joinpath('sim/command.txt').read_bytes()
            if raw == simulated_command:
                return
            simulated_command = raw
            batch = raw.decode('ascii').splitlines()
            lines, sentinel = batch[:-1], batch[-1]
            driver = current_driver
            self.calls.append((driver.__class__.__name__, lines[:], driver.release_attempted
                               if isinstance(driver, m.KnifeDriver) else False))
            for line in lines:
                words = line.split()
                if words[:3] == ['hand', 'r', 'follow']:
                    self.state['handR']['followsHead'] = False
                elif words[:4] == ['hand', 'r', 'grip', 'pose']:
                    numbers = list(map(float, words[4:]))
                    self.state['handR'].update(pos=numbers[:3], ypr=numbers[3:])
                elif words[:4] == ['hand', 'r', 'to', 'grip']:
                    self.start_motion(words)
                elif words[:2] == ['grip', 'r']:
                    self.state['controls']['gripR'] = float(words[2])
                elif words[:2] == ['trigger', 'r']:
                    self.state['controls']['trigR'] = float(words[2])
                elif words[0] == 'shot':
                    self.state['captureSeq'] += 1
                    self.write(self.run / 'sim/capture' / (words[1] + '.json'), dict(width=640, height=480))
            self.state.update(cmdSeq=self.state['cmdSeq'] + len(batch), lastCmd=sentinel,
                              frame=self.state['frame'] + 2, endFrames=self.state['endFrames'] + 2)
            ack_text = (
                f"seq={self.state['cmdSeq']} frame={self.state['frame'] - 1} error=-\n"
                f"last={' | '.join(batch)}\n")
            moving = any(line.startswith('hand r to grip ') and line.endswith(' 120') for line in lines)
            if moving and self.ack_delay_ns:
                delayed_ack = self.clock.now+self.ack_delay_ns, driver.ack_path, ack_text
            else:
                driver.ack_path.write_text(ack_text, encoding='utf-8')
            self.write(driver.state_path, self.state)
            if moving and self.motion_fault == 'pending_owner_replaced':
                self.write(self.run/'live-driver-owner.json', {'foreign': True})
                raise m.Refusal('synthetic receive interruption with pending jab')

        class SimulatorOwner(FakeWindowsOwner):
            def check(self):
                simulate()

        def replace(temporary, path, deadline_ns, validate, rename=None):
            driver = current_driver
            if stow_publication_expiry and not isinstance(driver, m.KnifeDriver):
                self.original_stow_deadline = deadline_ns
                self.clock.now = max(self.clock.now, deadline_ns)
            if replace_at_publication and isinstance(driver, m.KnifeDriver) and driver.phase == 'jab':
                self.assertTrue(driver.pending_command)
                self.assertIsNotNone(driver.gate)
                self.assertEqual(self.publication_refusals, [])
                before = driver.pending_before
                self.publication_refusals.append(dict(driver=driver, atNs=self.clock.now,
                    sequence=before['cmdSeq'], sentinel=before['lastCmd'],
                    raw=path.read_bytes(), state=copy.deepcopy(self.state)))
                if replace_at_publication == 'expire':
                    self.clock.now += 100_000_000
                else:
                    snapshot, _ = m.t.read_json(driver.snapshot_path)
                    snapshot['nonce'] += 1
                    self.write(driver.snapshot_path, snapshot)
            return original_replace(temporary, path, deadline_ns, validate, rename)

        def slow_pin(*args, **kwargs):
            result = original_pin(*args, **kwargs)
            self.clock.now += pin_io_cost_ns
            return result

        def request(driver):
            nonlocal current_driver
            current_driver = driver
            original_request(driver)
            self.clock.now += 3_000_000
            now = self.clock.tick()
            if isinstance(driver, m.KnifeDriver) and driver.held_right:
                s = knife_snapshot(m, driver.baseline, now, driver.pending_nonce, self.state if jab else None)
                if self.held_mutation:
                    self.held_mutation(s)
            else:
                is_knife = isinstance(driver, m.KnifeDriver)
                s = body_snapshot(m, now, driver.pending_nonce, self.state,
                    is_empty=is_knife or driver.phase in ('stow_wait', 'empty'),
                    returned=is_knife and driver.release_attempted)
            self.snapshots.append(copy.deepcopy(s))
            self.write(driver.snapshot_path, s)

        def prepare(driver, lines):
            self.prepared.append(lines[:])

        def pause(driver, deadline):
            self.clock.now = max(self.clock.now, deadline)
            driver.bounds(self.clock.tick())

        def read(driver):
            self.advance_motion()
            return original_read(driver)

        def pump(driver):
            original_pump(driver)
            if self.ack_delay_ns and isinstance(driver, m.KnifeDriver) and driver.phase == 'jab' and driver.gate:
                state = driver.read_state()
                if state['cmdSeq'] == driver.sequence:
                    driver.ownership(state)
                    if self.clock.now >= driver.gate[0]['chestKnife']['validUntilNs']:
                        self.expired_publication_checks += 1

        case = self
        class MotionLease:
            def __init__(self, driver, until):
                self.driver = driver
                self.raw = driver.owner_bytes(False)
                self.check(until)
            def check(self, until):
                if case.motion_fault == 'owner_replaced':
                    case.write(case.run/'live-driver-owner.json', {'foreign': True})
                if (case.run/'live-driver-owner.json').read_bytes() != self.raw:
                    self.driver.unsafe_owner = True
                    raise m.Refusal('motion input ownership changed')
                if case.motion_fault == 'cancel':
                    raise m.Refusal('synthetic owned motion interruption')
            def close(self):
                pass

        with ExitStack() as stack:
            stack.enter_context(patch.object(m.time, 'perf_counter_ns', self.clock.tick))
            stack.enter_context(patch.object(m.time, 'sleep', self.clock.sleep))
            stack.enter_context(patch.object(m.motion_backend, 'NEUTRAL_MOTION_RUNTIME_SHA256',
                self.m.pin(self.root/'xrsim.dll')['sha256']))
            stack.enter_context(patch.object(m.motion_backend, 'MotionInputLease', MotionLease))
            stack.enter_context(patch.object(m.t.Driver, 'read_state', read))
            stack.enter_context(patch.object(m.KnifeTransport, 'pump', pump))
            # readiness.make_driver captures its default clock at import.
            stack.enter_context(patch.object(m.readiness.make_driver, '__kwdefaults__',
                dict(m.readiness.make_driver.__kwdefaults__, clock=self.clock.tick)))
            stack.enter_context(patch.object(m.t, 'WindowsOwner',
                SimulatorOwner if replace_at_publication or real_transport else FakeWindowsOwner))
            stack.enter_context(patch.object(m.t.Driver, 'acquire_lock', lambda driver: None))
            stack.enter_context(patch.object(m.t.Driver, 'request_snapshot', request))
            stack.enter_context(patch.object(m.t, 'check_pin', slow_pin))
            if replace_at_publication or real_transport or stow_publication_expiry:
                # Keep real preparation, send, publication revalidation, ACK and
                # rollback. Only the native source and simulator are synthetic;
                # Windows file handles below belong to this temporary fixture.
                if replace_at_publication or stow_publication_expiry:
                    self.publication_refusals = []
                    stack.enter_context(patch.object(m.t, 'replace_command', replace))
            else:
                stack.enter_context(patch.object(m.t.Driver, 'send', lambda driver, *a, **kw: self.fake_send(driver, *a, **kw)))
                stack.enter_context(patch.object(m.t.Driver, 'prepare_next_command', prepare))
            stack.enter_context(patch.object(m.t.Driver, 'pause_until', pause))
            stack.enter_context(patch.object(m.h80.ObservationHolsterDriver, 'pause_until', pause))
            output = stack.enter_context(redirect_stdout(io.StringIO()))
            code = m.main(self.argv(jab))
        return code, json.loads(output.getvalue().strip().splitlines()[-1])

    def test_source_only_import_and_dependency_change_refusal(self):
        self.assertFalse((self.root / 'reports').exists())
        self.assertIs(self.m.t, self.m.draw.core.transport)
        self.assertTrue(issubclass(self.m.KnifeDriver, self.m.t.Driver))
        args = self.arguments()
        (self.root / 'tools/bc2_draw_admission.py').write_text('# changed', encoding='utf-8')
        with self.assertRaisesRegex(Exception, 'dependency source changed'):
            self.m.preflight(args)

    def test_sealed_source_runtime_and_metadata_deny_writes_until_close(self):
        m = self.m
        paths = [self.root/'tools/bc2_draw_admission.py', self.root/'xrsim.dll', self.active]
        admission = m.Admission(continuousPins=[m.pin(path) for path in paths])
        self.addCleanup(admission.close)
        admission.seal()
        for path in paths:
            raw = path.read_bytes()
            replacement = path.with_name(path.name + '.replacement')
            replacement.write_bytes(raw)
            with self.assertRaises(PermissionError):
                path.write_bytes(raw)
            with self.assertRaises(PermissionError):
                replacement.replace(path)
            with self.assertRaises(PermissionError):
                path.unlink()
        m.check_continuous(admission)
        streams = [entry[1] for entry in admission.lease.entries]
        admission.close()
        self.assertTrue(all(stream.closed for stream in streams))
        for path in paths:
            path.write_bytes(b'after custody')
        with self.assertRaisesRegex(Exception, 'custody unavailable'):
            m.check_continuous(admission)

    def test_partial_pin_acquisition_failure_releases_prior_and_failing_file(self):
        m = self.m
        paths = [self.root/'xrsim.dll', self.active]
        rows = [m.pin(path) for path in paths]
        rows[-1]['sha256'] = '0'*64
        with self.assertRaisesRegex(Exception, 'pinned file changed before custody'):
            m.ContinuousPins(rows)
        for path in paths:
            path.write_bytes(b'failure retired every handle')

    def test_same_bytes_alternate_path_identity_refuses_retained_pin(self):
        m = self.m
        path = self.root/'xrsim.dll'
        raw = path.read_bytes()
        replacement = path.with_suffix('.replacement')
        replacement.write_bytes(raw)
        admission = m.Admission(continuousPins=[m.pin(path)])
        self.addCleanup(admission.close)
        admission.seal()
        original_open = admission.lease.api.CreateFileW
        def alternate(name, access, share, *args):
            self.assertEqual((name, access, share), (str(path), 0, 7))
            # Inject a replaced pathname view while retaining the original
            # real data HANDLE; both OS files contain exactly the same bytes.
            return original_open(str(replacement), access, share, *args)
        self.assertEqual(path.read_bytes(), raw)
        with patch.object(admission.lease.api, 'CreateFileW', alternate):
            with self.assertRaisesRegex(Exception, 'pinned file pathname identity changed'):
                m.check_continuous(admission)

    def test_pin_ancestor_redirect_and_changed_manifest_rows_are_refused(self):
        m = self.m
        admission = m.Admission(continuousPins=[m.pin(self.root/'xrsim.dll')])
        self.addCleanup(admission.close)
        admission.seal()
        original = Path.lstat
        def redirected(path):
            if path == self.root:
                return SimpleNamespace(st_mode=0, st_file_attributes=0x400, st_reparse_tag=0xa0000003)
            return original(path)
        with patch.object(Path, 'lstat', redirected):
            with self.assertRaisesRegex(Exception, 'ancestor redirected'):
                m.check_continuous(admission)
        admission['continuousPins'][0]['sha256'] = '0'*64
        with self.assertRaisesRegex(Exception, 'custody unavailable or changed'):
            m.check_continuous(admission)

    def test_repeated_pin_checks_keep_full_assembled_native_budget_and_close(self):
        m = self.m
        original = m.check_continuous
        leases = []
        def checked(admission):
            leases.append(admission.lease)
            # The slow disk model must not reenter payload hashing inside a
            # source lease. Real pathname/ancestor checks still execute.
            with patch.object(m.t, 'check_pin', side_effect=AssertionError('pin payload reread')):
                return original(admission)
        with patch.object(m, 'check_continuous', checked):
            code, result = self.run_assembled(jab=True, real_transport=True, pin_io_cost_ns=1_000_000)
        self.assertEqual(code, 0, result)
        self.assertGreater(len(leases), 40)
        self.assertTrue(all(lease is leases[0] for lease in leases))
        self.assertEqual(leases[0].entries, [])
        self.assertEqual(result['jabSteps'], 1)
        self.assertFalse(result['pendingCommand'] or result['rightGripHeld'])

    def test_expired_original_stow_publication_keeps_zero_commands_and_closes_pins(self):
        code, result = self.run_assembled(real_transport=True, stow_publication_expiry=True)
        self.assertEqual(code, 2, result)
        self.assertEqual(result['error'], 'original command publication deadline exceeded')
        self.assertEqual(result['commands'], 0)
        self.assertFalse(result['pendingCommand'] or result['rightGripHeld'] or result['triggerHeld'])
        self.assertIsNone(result['outstandingMotion'])
        timing = json.loads((Path(result['artifactDirectory'])/'receive-timing-failure.json').read_text())
        self.assertEqual(timing['originalPublicationDeadlineNs'], self.original_stow_deadline)
        self.assertGreaterEqual(timing['firstPhaseError']['atNs'], self.original_stow_deadline)
        for path in (self.root/'tools/bc2_draw_admission.py', self.root/'xrsim.dll', self.active):
            path.write_bytes(b'failure retired invocation pins')

    def test_ordinary_preflight_and_installed_source_refusals(self):
        with patch.object(self.m.time, 'perf_counter_ns', self.clock.tick):
            admitted = self.m.preflight(self.arguments())
            self.assertEqual(admitted['version'], '0.1.999')
            self.write(self.active, dict(current='0.1.998'))
            with self.assertRaisesRegex(Exception, 'Active installed'):
                self.m.preflight(self.arguments())
            self.write(self.active, dict(current=self.version))
            self.binding['source_sha256'] = 'b' * 64
            self.write(self.run / 'artifact-binding.json', self.binding)
            with self.assertRaisesRegex(Exception, 'artifact source'):
                self.m.preflight(self.arguments())

    def test_explicit_root_review_and_old_world_refused(self):
        args = self.arguments()
        args.root_ordinary_input = False
        with self.assertRaisesRegex(Exception, 'Explicit ROOT'):
            self.m.preflight(args)
        args.root_ordinary_input = True
        with patch.object(self.m.time, 'perf_counter_ns', return_value=self.clock.now+51*self.m.NS):
            with self.assertRaisesRegex(Exception, 'stereo review expired'):
                self.m.preflight(args)

    def test_full_ordinary_stow_draw_strict_held_return_without_jab(self):
        code, result = self.run_assembled()
        self.assertEqual((code, result['status']), (0, 'strict_held_and_fresh_return_empty'), result)
        self.assertEqual(result['stow']['status'], 'genuine_neutral_empty')
        self.assertEqual(result['jabSteps'], 0)
        self.assertEqual(len(result['captures']), 2)
        self.assertEqual(result['damage'], 'unknown')
        self.assertFalse(result['nativeAcceptance'] or result['headsetAcceptance'])
        commands = [lines for name, lines, _ in self.calls if name == 'KnifeDriver']
        self.assertEqual(commands.count(['grip r 1']), 1)
        self.assertEqual(commands.count(['grip r 0']), 1)
        self.assertFalse(any('trigger r 1' in lines for lines in commands))
        self.assertTrue(any(s.get('chestKnifeObservationBasis') == 'independent_owned_body' for s in self.snapshots))

    def test_single_measured_120mm_jab_retains_motion_custody_until_endpoint(self):
        code, result = self.run_assembled(jab=True, real_transport=True)
        self.assertEqual(code, 0, result)
        self.assertEqual(result['jabSteps'], 1)
        strokes = [lines for name, lines, _ in self.calls if name == 'KnifeDriver'
                   and lines[0].startswith('hand r to grip ')]
        self.assertEqual(len(strokes), 1)
        self.assertEqual(len(strokes[0]), 1)
        self.assertTrue(strokes[0][0].endswith(' 120'))
        jab = result['jab']
        self.assertAlmostEqual(math.dist(jab['origin'], jab['endpoint']), .120)
        for actual, expected in zip(jab['directionLocal'], [-.109131712, -.843355823, .526157034]):
            self.assertAlmostEqual(actual, expected, places=8)
        self.assertTrue(jab['directionIsMeasuredBladeAxis'])
        self.assertGreater(jab['endpointNs'], jab['acknowledgedNs']+120_000_000)
        self.assertTrue(self.owner_during_motion)
        self.assertTrue(all(row['pending'] and row['outstandingMotion'] for row in self.owner_during_motion))
        self.assertIsNone(result['outstandingMotion'])
        self.assertFalse(json.loads((self.run/'live-driver-owner.json').read_text())['pending'])
        self.assertEqual(result['damage'], 'unknown')

    def test_strict_held_first_refusal_returns_once_without_capture_or_jab(self):
        self.held_mutation = lambda s: s['chestKnife']['render'].update(rigFingerprint=0)
        code, result = self.run_assembled(jab=True)
        self.assertEqual(code, 2, result)
        self.assertIn('rendered knife rig', result['error'])
        commands = [lines for name, lines, _ in self.calls if name == 'KnifeDriver']
        self.assertEqual(commands.count(['grip r 1']), 1)
        self.assertEqual(commands.count(['grip r 0']), 1)
        self.assertFalse(any(lines[0].startswith('shot ') or len(lines) == 2 for lines in commands))

    def test_replaced_jab_source_at_real_publication_rolls_back_and_refuses_cleanup(self):
        code, result = self.run_assembled(jab=True, replace_at_publication=True)
        self.assertEqual((code, result['status']), (2, 'refused'), result)
        self.assertIn('admitted snapshot replaced/invalidated', result['error'])
        self.assertEqual(result['stow']['status'], 'genuine_neutral_empty')
        self.assertEqual(len(self.publication_refusals), 1)
        refusal = self.publication_refusals[0]
        driver = refusal['driver']
        self.assertTrue(driver.attempt.cancelled and driver.unsafe_owner and driver.release_attempted)
        self.assertFalse(result['pendingCommand'] or result['triggerHeld'])
        self.assertTrue(result['rightGripHeld'])
        self.assertEqual(result['release'], 'not attempted: no settled owned held controller')
        self.assertEqual((driver.sequence, driver.sentinel, driver.command_hash),
            (refusal['sequence'], refusal['sentinel'], self.m.t.digest(refusal['raw'])))
        self.assertEqual(driver.command_path.read_bytes(), refusal['raw'])
        self.assertEqual({key: self.state[key] for key in ('head','handL','handR','controls')},
                         {key: refusal['state'][key] for key in ('head','handL','handR','controls')})
        commands = [lines for name, lines, _ in self.calls if name == 'KnifeDriver']
        self.assertEqual(commands.count(['grip r 1']), 1)
        self.assertEqual(sum(lines[0].startswith('shot ') for lines in commands), 1)
        self.assertFalse(any(len(lines) == 2 or lines == ['grip r 0'] for lines in commands))
        self.assertIsNone(driver._prepared_command)
        self.assertIsNone(driver._publication_owner)
        self.assertFalse(getattr(driver, '_prepared_cleanup_pending', []))
        self.assertLess(self.clock.now - refusal['atNs'], 250 * self.m.MS)
        self.assertEqual(driver.release_after_failure(), 'already attempted; no second release')
        self.assertEqual(driver.command_path.read_bytes(), refusal['raw'])

    def test_constructor_refuses_options_mismatch_before_any_command(self):
        self.write(self.folder / 'options.json', dict(source_sha256='b'*64, version=self.version,
            activate_window=False, xr_manifest=str(self.root / 'runtime-x64.json')))
        code, result = self.run_assembled()
        self.assertEqual(code, 2)
        self.assertIn('options do not match', result['error'])
        self.assertEqual(self.calls, [])

    def test_source_and_binary_refusals_precede_driver_construction(self):
        self.manifest['source_sha256'] = 'b'*64
        self.write(self.run / 'run-manifest.json', self.manifest)
        code, result = self.run_assembled()
        self.assertEqual(code, 2)
        self.assertIn('ordinary run/source/version', result['error'])
        self.assertEqual(self.calls, [])
        self.manifest['source_sha256'] = self.source
        self.write(self.run / 'run-manifest.json', self.manifest)
        (self.runtime / 'bin/BC2NativeProbe.dll').write_bytes(b'wrong')
        code, result = self.run_assembled()
        self.assertEqual(code, 2)
        self.assertIn('pinned file', result['error'])
        self.assertEqual(self.calls, [])

    def test_direction_trigger_and_irreversible_attempt_guards(self):
        for direction in ([0., 0., -2.], [0., 0., float('nan')]):
            with self.assertRaises(Exception):
                self.m.unit_direction(direction)
        for hand in ('l', 'r'):
            with self.assertRaises(Exception):
                self.m.neutral_trigger_lines(['trigger ' + hand + ' .01'])
        attempt = self.m.Attempt()
        attempt.take('draw')
        with self.assertRaises(Exception):
            attempt.take('draw')
        attempt.cancelled = True
        with self.assertRaises(Exception):
            attempt.take('jab')

    def test_owned_interruption_cancels_runtime_motion_and_releases_grip_once(self):
        self.motion_fault = 'cancel'
        code, result = self.run_assembled(jab=True, real_transport=True)
        self.assertEqual(code, 2, result)
        self.assertIn('owned motion interruption', result['error'])
        self.assertEqual(result['release'], 'owned interpolation cancellation and grip release acknowledged')
        cancels = [lines for name, lines, cleanup in self.calls if name == 'KnifeDriver'
                   and cleanup and lines[0].startswith('hand r to grip ')]
        self.assertEqual(len(cancels), 1)
        self.assertTrue(cancels[0][0].endswith(' 0'))
        self.assertEqual(cancels[0][1:], ['grip r 0'])
        self.assertFalse(result['rightGripHeld'])
        self.assertIsNone(result['outstandingMotion'])

    def test_endpoint_timeout_preserves_failure_and_cancels_owned_motion(self):
        self.motion_fault = 'stalled_frames'
        code, result = self.run_assembled(jab=True, real_transport=True)
        self.assertEqual(code, 2, result)
        self.assertIn('deadline', result['error'].lower())
        self.assertEqual(result['release'], 'owned interpolation cancellation and grip release acknowledged')
        self.assertIsNone(result['outstandingMotion'])

    def test_foreign_motion_owner_refuses_cancellation_and_retains_pending_receipt(self):
        self.motion_fault = 'owner_replaced'
        code, result = self.run_assembled(jab=True, real_transport=True)
        self.assertEqual(code, 2, result)
        self.assertIn('ownership changed', result['error'])
        self.assertIsNotNone(result['outstandingMotion'])
        self.assertTrue(result['rightGripHeld'])
        self.assertEqual(result['release'], 'not attempted: no settled owned held controller')
        self.assertFalse(any(cleanup for name, lines, cleanup in self.calls if name == 'KnifeDriver'))

    def test_pending_ack_cleanup_cannot_overwrite_replaced_motion_owner(self):
        self.motion_fault = 'pending_owner_replaced'
        code, result = self.run_assembled(jab=True, real_transport=True)
        self.assertEqual(code, 2, result)
        self.assertIn('pending jab', result['error'])
        self.assertIn('owner record replaced', result['release'])
        self.assertEqual(json.loads((self.run/'live-driver-owner.json').read_text()), {'foreign': True})
        self.assertIsNotNone(result['outstandingMotion'])
        self.assertTrue(result['rightGripHeld'])
        self.assertFalse(any(cleanup for name, lines, cleanup in self.calls if name == 'KnifeDriver'))

    def test_late_parse_ack_is_not_endpoint_or_renewed_native_authority(self):
        self.ack_delay_ns = 140_000_000
        code, result = self.run_assembled(jab=True, real_transport=True)
        self.assertEqual(code, 0, result)
        jab = result['jab']
        self.assertGreater(jab['acknowledgedNs']-jab['writtenNs'], 140_000_000)
        self.assertGreater(jab['endpointNs']-jab['acknowledgedNs'], 120_000_000)
        self.assertEqual(result['jabSteps'], 1)
        self.assertIsNone(result['outstandingMotion'])
        self.assertGreater(self.expired_publication_checks, 0)

    def test_expired_held_before_atomic_publication_never_starts_jab(self):
        code, result = self.run_assembled(jab=True, replace_at_publication='expire')
        self.assertEqual(code, 2, result)
        self.assertIn('deadline', result['error'].lower())
        self.assertFalse(any(lines[0].startswith('hand r to grip ') for _, lines, _ in self.calls))
        self.assertIsNone(result['outstandingMotion'])

    def test_unpinned_continuous_backend_refuses_before_stow(self):
        args = self.arguments()
        args.jab = True
        with patch.object(self.m.time, 'perf_counter_ns', self.clock.tick):
            with self.assertRaisesRegex(Exception, 'pinned xr-sim'):
                self.m.preflight(args)

    def test_current_geometry_mismatch_refuses_motion_and_releases_once(self):
        self.held_mutation = lambda s: s['input'].update(generation=s['input']['generation']+1)
        code, result = self.run_assembled(jab=True)
        self.assertEqual(code, 2, result)
        self.assertIn('generation mismatch', result['error'])
        self.assertFalse(any(lines[0].startswith('hand r to grip ') for _, lines, _ in self.calls))
        self.assertEqual(sum(lines == ['grip r 0'] for name, lines, _ in self.calls if name == 'KnifeDriver'), 1)

    def test_measured_direction_is_invariant_to_body_motion_and_tracks_reference_rotation(self):
        m = self.m
        baseline = body_snapshot(m, self.clock.now, 1, self.state, is_empty=True)
        self.state['controls']['gripR'] = 1
        s = knife_snapshot(m, baseline, self.clock.now, 2, self.state)
        original = m.measured_jab(s, self.clock.now)['directionLocal']
        rotation = m.p.from_pose(dict(position=[2,3,4], orientation=m.p.ypr_quaternion([60,20,10])))
        for key in ('weaponWorldMeters','trackingBodyWorldMeters'):
            s['mapping'][key] = m.p.multiply(s['mapping'][key], rotation)
        transformed = m.measured_jab(s, self.clock.now)['directionLocal']
        self.assertLess(math.dist(original, transformed), .000001)
        q = m.p.ypr_quaternion([35,0,0])
        s['input']['referenceHead']['orientation'] = q
        measured = m.measured_jab(s, self.clock.now)['directionLocal']
        self.assertLess(math.dist(measured, m.p.quat_rotate(q, original)), .000001)

    def test_strict_held_rejects_owner_claim_epoch_mode_and_expiry_changes(self):
        m = self.m
        baseline = body_snapshot(m, self.clock.now, 1, self.state, is_empty=True)
        selection = m.native_selection(baseline, self.clock.now)
        now = self.clock.now + 200_000_000
        episode = dict(startedNs=self.clock.now-10_000_000, deadlineNs=now+m.NS,
            stageDeadlineNs=now+m.NS, drawWrittenNs=self.clock.now+10_000_000,
            drawAcknowledgedNs=self.clock.now+20_000_000, nativeSelection=selection)
        after = self.clock.now+30_000_000
        original = knife_snapshot(m, baseline, now, 2)
        self.assertGreater(m.held(original, baseline, now, after, episode), now)
        changes = (
            ('owner', lambda k: k['owner'].update(space=99)),
            ('original item', lambda k: k['originalGun'].update(generation=99)),
            ('physical epoch', lambda k: k['inputEvidence']['owner'].update(equipGeneration=2)),
            ('claim', lambda k: k['hold'].update(id=0)),
            ('claim expiry', lambda k: k['hold'].update(deadlineNs=now)),
            ('menu epoch', lambda k: k['menu'].update(epoch=99)),
            ('mode', lambda k: k.update(motionEnabled=False)),
            ('bridge', lambda k: k.update(attackBridgeEnabled=True)),
            ('prior draw', lambda k: k.update(drawReceipts=2)),
            ('prior return', lambda k: k.update(returnReceipts=1)),
            ('trigger', lambda k: k['controls'][1].update(trigger=.1)),
            ('extended lease', lambda k: k.update(validUntilNs=k['validUntilNs']+1)),
            ('selected native copy', lambda k: k.update(selectedSlots=[7, 7, 0])))
        for label, change in changes:
            with self.subTest(label=label):
                current = copy.deepcopy(original)
                change(current['chestKnife'])
                with self.assertRaises(Exception):
                    m.held(current, baseline, now, after, episode)
        with self.assertRaises(Exception):
            m.held(original, baseline, now+100_000_000, after, episode)


if __name__ == '__main__':
    unittest.main()
