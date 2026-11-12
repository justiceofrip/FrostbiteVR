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


def knife_snapshot(m, baseline, now, nonce):
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
    return dict(schema='fvr.bc2.live-mechanism-snapshot.v1', valid=False,
        invalid_reason='selected_configuration_or_rig_missing_changed_or_expired',
        chestKnifeObservationBasis='independent_owned_body', nonce=nonce, observed_ns=now, chestKnife=k)


class KnifeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knife-source-only-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        names = ('drive_bc2_knife_simulation.py', 'drive_bc2_holster_prepared.py', 'bc2_holster_core.py',
            'bc2_holster_snapshot_transport.py', 'drive_bc2_feed_simulation.py', 'plan_bc2_feed_simulation.py',
            'bc2_draw_readiness_driver.py', 'bc2_draw_admission.py')
        for name in names:
            target = self.root / 'tools' / name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(ROOT / 'tools' / name, target)
        anchors = self.root / 'include/fvr/interaction/BodyAnchors.h'
        anchors.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / 'include/fvr/interaction/BodyAnchors.h', anchors)
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
            actionsAttached=True, sessionState='FOCUSED', paceMode='free', captureSeq=1, cmdSeq=1,
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

    def write(self, path, value):
        path.write_text(json.dumps(value), encoding='utf-8')

    def argv(self, jab=False):
        args = ['--run', str(self.run), '--version', self.version, '--source-sha256', self.source,
            '--dependencies-sha256', self.m.dependency_sha256(), '--world-capture', 'world',
            '--storage-receipt', str(self.storage), '--root-ordinary-input', '--root-reviewed-both-world-eyes',
            '--root-reviewed-normal-mode4-and-knife-idle', '--stow-asset', fixtures.SOURCE[0],
            '--stow-config', fixtures.SOURCE[1], '--target-asset', fixtures.TARGET[0],
            '--target-config', fixtures.TARGET[1]]
        return args + (['--jab-direction-local', '0', '0', '-1'] if jab else [])

    def arguments(self):
        values = dict(run=str(self.run), version=self.version, source_sha256=self.source,
            dependencies_sha256=self.m.dependency_sha256(), world_capture='world', storage_receipt=str(self.storage),
            root_ordinary_input=True, root_reviewed_both_world_eyes=True,
            root_reviewed_normal_mode4_and_knife_idle=True, jab_direction_local=None)
        return SimpleNamespace(**values)

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
        self.write(self.run / 'live-driver-owner.json', dict(host=self.host, pending=False,
            commandSha256=driver.command_hash, sequence=driver.sequence, sentinel=driver.sentinel))

    def run_assembled(self, jab=False, replace_at_publication=False):
        m = self.m
        original_request = m.t.Driver.request_snapshot
        original_replace = m.t.replace_command
        simulated_command = self.run.joinpath('sim/command.txt').read_bytes()
        current_driver = None

        def simulate():
            nonlocal simulated_command
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
                elif words[:2] == ['grip', 'r']:
                    self.state['controls']['gripR'] = float(words[2])
                elif words[:2] == ['trigger', 'r']:
                    self.state['controls']['trigR'] = float(words[2])
                elif words[0] == 'shot':
                    self.state['captureSeq'] += 1
                    self.write(self.run / 'sim/capture' / (words[1] + '.json'), dict(width=640, height=480))
            self.state.update(cmdSeq=self.state['cmdSeq'] + len(batch), lastCmd=sentinel,
                              frame=self.state['frame'] + 2, endFrames=self.state['endFrames'] + 2)
            driver.ack_path.write_text(
                f"seq={self.state['cmdSeq']} frame={self.state['frame'] - 1} error=-\n"
                f"last={' | '.join(batch)}\n", encoding='utf-8')
            self.write(driver.state_path, self.state)

        class SimulatorOwner(FakeWindowsOwner):
            def check(self):
                simulate()

        def replace(temporary, path, deadline_ns, validate, rename=None):
            driver = current_driver
            if isinstance(driver, m.KnifeDriver) and driver.phase == 'jab-1':
                self.assertTrue(driver.pending_command)
                self.assertIsNotNone(driver.gate)
                self.assertEqual(self.publication_refusals, [])
                before = driver.pending_before
                self.publication_refusals.append(dict(driver=driver, atNs=self.clock.now,
                    sequence=before['cmdSeq'], sentinel=before['lastCmd'],
                    raw=path.read_bytes(), state=copy.deepcopy(self.state)))
                snapshot, _ = m.t.read_json(driver.snapshot_path)
                snapshot['nonce'] += 1
                self.write(driver.snapshot_path, snapshot)
            return original_replace(temporary, path, deadline_ns, validate, rename)

        def request(driver):
            nonlocal current_driver
            current_driver = driver
            original_request(driver)
            self.clock.now += 3_000_000
            now = self.clock.tick()
            if isinstance(driver, m.KnifeDriver) and driver.held_right:
                s = knife_snapshot(m, driver.baseline, now, driver.pending_nonce)
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

        with ExitStack() as stack:
            stack.enter_context(patch.object(m.time, 'perf_counter_ns', self.clock.tick))
            stack.enter_context(patch.object(m.time, 'sleep', self.clock.sleep))
            # readiness.make_driver captures its default clock at import.
            stack.enter_context(patch.object(m.readiness.make_driver, '__kwdefaults__',
                dict(m.readiness.make_driver.__kwdefaults__, clock=self.clock.tick)))
            stack.enter_context(patch.object(m.t, 'WindowsOwner',
                SimulatorOwner if replace_at_publication else FakeWindowsOwner))
            stack.enter_context(patch.object(m.t.Driver, 'acquire_lock', lambda driver: None))
            stack.enter_context(patch.object(m.t.Driver, 'request_snapshot', request))
            if replace_at_publication:
                # Keep real preparation, send, publication revalidation, ACK and
                # rollback. Only the native source and simulator are synthetic;
                # Windows file handles below belong to this temporary fixture.
                self.publication_refusals = []
                self.state.update(frame=1, endFrames=1)
                self.write(self.run / 'sim/state.json', self.state)
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

    def test_optional_single_75mm_jab_uses_three_fresh_held_sources(self):
        code, result = self.run_assembled(jab=True)
        self.assertEqual(code, 0, result)
        self.assertEqual(result['jabSteps'], 3)
        pairs = [lines for name, lines, _ in self.calls if name == 'KnifeDriver' and len(lines) == 2]
        self.assertEqual(len(pairs), 3)
        z = [float(lines[0].split()[6]) for lines in pairs]
        self.assertAlmostEqual(z[0] - z[-1], .05)
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
        self.assertEqual(self.state, refusal['state'])
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
        self.assertAlmostEqual(self.m.jab_points([0., 0., 0.], [0., 0., -1.])[-1][2], -.075)
        for direction in ([0., 0., -2.], [0., 0., float('nan')]):
            with self.assertRaises(Exception):
                self.m.jab_points([0., 0., 0.], direction)
        for hand in ('l', 'r'):
            with self.assertRaises(Exception):
                self.m.neutral_trigger_lines(['trigger ' + hand + ' .01'])
        attempt = self.m.Attempt()
        attempt.take('draw')
        with self.assertRaises(Exception):
            attempt.take('draw')
        attempt.cancelled = True
        with self.assertRaises(Exception):
            attempt.take('jab-1')

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
