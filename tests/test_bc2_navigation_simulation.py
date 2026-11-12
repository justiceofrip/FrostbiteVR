"""Assembled synthetic navigation CLI; no game, live session or native evidence.

Real shared construction, publication owner HANDLE, atomic command publication,
ACK parsing, later-frame commitment and release run against temporary files.
Only host liveness/lock and the simulator/native publishers are substituted.
"""
import copy
import gzip
from contextlib import ExitStack, redirect_stdout
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
    spec.loader.exec_module(module)
    return module


fixtures = load("navigation_native_fixture", ROOT / "tests/test_bc2_holster_shared_entry.py")


# Actual87/03 df55fccac9124123a37c7134c48d314d timing projection. Original
# snapshot-002.json.gz SHA256 70752e6dd18ed6851eeb11da8be33f507f4375c3aa8fdab512c56dea3cdbd1cf;
# receive-timing-failure.json SHA256 e29ab266cd7dbb85ba8d39f4148fa6f9eb55c0d896a1c6f2400016fe731ddf6e.
# These are recorded offsets, not new native evidence. The regression translates
# them to the synthetic clock and supplies a deliberately delayed successor.
RECORDED_NAVIGATION_TIMING = dict(
    firstResponseNs=43_000_900, secondResponseNs=142_554_400,
    secondObservedNs=641193151238700, secondInputObservedNs=641193132176600,
    secondValidUntilNs=641193232176600, secondMenuUntilNs=641193230793200,
    thirdRequestNs=641193166731300, failureNs=641193233703300)


# Actual87/04 f35da40c865049ae87c7706f98720232 failed normal zero timing.
# events.jsonl SHA256 22e96245cf968b5cab2c3e44d3a94b808341167032fc8db60ae5bd4b91354fdb;
# receive-timing-failure.json SHA256 43b1a41841e1d237be4e66f6e584fcdd4e47b01f2a42239c8fe183fe923f244d.
# The later zero ACK has no commit timestamp: its regression delay is synthetic.
RECORDED_ZERO_TIMING = dict(
    nonzeroRequestedNs=642353953328600, nonzeroPublishedNs=642353964878500,
    nonzeroCommittedNs=642354026059900, zeroRequestedNs=642354376735900,
    pulseDeadlineNs=642354442933500, zeroPreparationNs=3_750_700,
    lastPumpOvershootNs=13_802_400)


# Admission projection of actual86/02 da9146dcd773411eb7e108a7675f1e4d
# snapshot-001.json.gz (SHA256 43f8dcdaa90526ef0151c9bcc2c6d341690b88cf2d44a3ffe712c4300814b5d4).
# Replay below changes only the synthetic clock, nonce and simulated input;
# the original independent holster/outer sequence and lease relationships stay.
RECORDED_EMPTY = json.loads(r'''
{
  "schema": "fvr.bc2.live-mechanism-snapshot.v1",
  "valid": true,
  "nonce": "1794513515487785900",
  "observed_ns": 638831968158400,
  "valid_until_ns": 638832033593000,
  "native_deadline_ns": 638832033593000,
  "shot_input_generation": 17678,
  "physical_equipment_generation": 6,
  "weapon_actions_blocked": true,
  "owner": {
    "player": 1192238816,
    "soldier": 1192226816,
    "weak": 116786824,
    "weapon": 126919920,
    "actorGeneration": 1,
    "equipGeneration": 5,
    "space": 2
  },
  "equipment": {
    "weapon": 126919920,
    "data": 475548192,
    "persistence": 134985184,
    "asset": "XM8_sp_s"
  },
  "rig": {
    "soldier": 1192226816,
    "weak": 116786824,
    "animation": 1157341152,
    "skeleton": 109900976,
    "pose": 1183117984,
    "worldHeader": 1157280512,
    "worldMatrices": 1157280528,
    "skinMatrices": 1157289936,
    "evaluatedMatrices": 1157299360,
    "count": 147,
    "nativeIk": true,
    "fingerprint": 12101763329092097707
  },
  "selected": {
    "sequence": 17675,
    "observedNs": 638831873021200,
    "deadlineNs": 638832073021200,
    "configurationPath": "Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped",
    "configurationPathPointer": 122280704,
    "configurationPathVerified": true,
    "inventory": 1192188704,
    "selectedSlot": 0,
    "soleConfiguredArray": 126265520,
    "matchedMeshPath": "Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh",
    "meshes": [
      {
        "address": 121637376,
        "typeInfo": 29324564,
        "namePointer": 130042880,
        "path": "Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh"
      },
      {
        "address": 123622336,
        "typeInfo": 29324564,
        "namePointer": 123678672,
        "path": "Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh"
      }
    ]
  },
  "input": {
    "generation": 17678,
    "spaceGeneration": 2,
    "predictedNs": 638831935441638,
    "focused": true,
    "headValid": true,
    "floorRelative": true,
    "worldUnitsPerMeter": 1,
    "trackingBasisGeneration": 2,
    "referenceHead": {
      "position": [
        0,
        1.60000002,
        0
      ],
      "orientation": [
        0,
        0,
        0,
        1
      ]
    },
    "head": {
      "position": [
        0,
        1.60000002,
        0
      ],
      "orientation": [
        0,
        0,
        0,
        1
      ]
    },
    "hands": [
      {
        "grip": {
          "position": [
            -0.200000003,
            1.29999995,
            -0.349999994
          ],
          "orientation": [
            0,
            0,
            0,
            1
          ]
        },
        "aim": {
          "position": [
            -0.200000003,
            1.29999995,
            -0.349999994
          ],
          "orientation": [
            0,
            0,
            0,
            1
          ]
        },
        "gripTracked": true,
        "aimTracked": true,
        "active": 127,
        "held": 0,
        "touchActive": 1,
        "touched": 0,
        "stickX": 0,
        "stickY": 0,
        "trigger": 0,
        "squeeze": 0
      },
      {
        "grip": {
          "position": [
            0.119999997,
            1.35000002,
            -0.150000006
          ],
          "orientation": [
            0,
            0,
            0,
            1
          ]
        },
        "aim": {
          "position": [
            0.119999997,
            1.35000002,
            -0.150000006
          ],
          "orientation": [
            0,
            0,
            0,
            1
          ]
        },
        "gripTracked": true,
        "aimTracked": true,
        "active": 63,
        "held": 0,
        "touchActive": 1,
        "touched": 0,
        "stickX": 0,
        "stickY": 0,
        "trigger": 0,
        "squeeze": 0
      }
    ],
    "menu": {
      "owner": 1,
      "sequence": 39256,
      "epoch": 6,
      "deadlineNs": 638832031523400
    }
  },
  "input_evidence": {
    "owner": {
      "actor": 501595590875934720,
      "actorGeneration": 1,
      "equipGeneration": 6,
      "space": 2
    },
    "sequence": 17678,
    "observedNs": 638831933593000,
    "deadlineNs": 638832033593000,
    "nowNs": 638831947230500,
    "focused": true,
    "tracked": [
      true,
      true
    ],
    "released": [
      true,
      true
    ]
  },
  "bodyHolster": {
    "available": true,
    "observedNs": 638831967962400,
    "probeRead": {
      "enabled": true,
      "samplePresent": true,
      "reason": null,
      "sampledNs": 638831966664600,
      "inputObservedNs": 638831950085000,
      "inputDeadlineNs": 638832050085000,
      "inputNowNs": 638831961117000,
      "inputSequence": 17679,
      "trialStartNs": 0,
      "trialDeadlineNs": 0
    },
    "sampledNs": 638831966664600,
    "validUntilNs": 638832033593000,
    "phase": 3,
    "actualEmpty": true,
    "nativeTick": 25170,
    "request": 4,
    "nativeOwner": {
      "player": 1192238816,
      "soldier": 1192226816,
      "weak": 116786824,
      "weapon": 126919920,
      "actorGeneration": 1,
      "equipGeneration": 5,
      "space": 2
    },
    "inputEvidence": {
      "owner": {
        "actor": 501595590875934720,
        "actorGeneration": 1,
        "equipGeneration": 6,
        "space": 2
      },
      "sequence": 17679,
      "observedNs": 638831950085000,
      "deadlineNs": 638832050085000,
      "nowNs": 638831961117000,
      "focused": true,
      "tracked": [
        true,
        true
      ],
      "released": [
        true,
        true
      ]
    },
    "physicalGun": {
      "id": 134985184,
      "generation": 6
    },
    "inputMatchesSnapshot": false,
    "inputSequenceRelation": "newer",
    "neutralTrackedInput": true,
    "input": {
      "generation": 17679,
      "spaceGeneration": 2,
      "focused": true,
      "headValid": true,
      "hands": [
        {
          "gripTracked": true,
          "aimTracked": true,
          "held": 0,
          "squeeze": 0,
          "trigger": 0,
          "stickX": 0,
          "stickY": 0
        },
        {
          "gripTracked": true,
          "aimTracked": true,
          "held": 0,
          "squeeze": 0,
          "trigger": 0,
          "stickX": 0,
          "stickY": 0
        }
      ]
    },
    "selectedSlot": {
      "slot": 2,
      "item": {
        "id": 126919920,
        "generation": 1
      }
    },
    "leftClaim": null,
    "rightClaim": null,
    "inventoryEvaluation": 1,
    "emptyHands": true,
    "blockWeaponActions": true,
    "allowAutomaticGunHold": false,
    "freeRightCurrent": true,
    "freeRight": {
      "authorizationDeadlineNs": 0,
      "inputEvidence": {
        "owner": {
          "actor": 501595590875934720,
          "actorGeneration": 1,
          "equipGeneration": 6,
          "space": 2
        },
        "sequence": 17679,
        "observedNs": 638831950085000,
        "deadlineNs": 638832050085000,
        "nowNs": 638831961117000,
        "focused": true,
        "tracked": [
          true,
          true
        ],
        "released": [
          true,
          true
        ]
      },
      "visibility": {
        "nativeOwner": {
          "player": 1192238816,
          "soldier": 1192226816,
          "weak": 116786824,
          "weapon": 126919920,
          "actorGeneration": 1,
          "equipGeneration": 5,
          "space": 2
        },
        "request": 4,
        "inputSequence": 17678,
        "physicalEquipGeneration": 6,
        "drawSerial": 25167,
        "observedNs": 638831959507400,
        "deadlineNs": 638832033593000,
        "verifiedCopyMask": 3,
        "hidden": true,
        "submittedVisibilityVerified": false
      },
      "suppression": {
        "owner": {
          "player": 1192238816,
          "soldier": 1192226816,
          "weak": 116786824,
          "weapon": 126919920,
          "actorGeneration": 1,
          "equipGeneration": 5,
          "space": 2
        },
        "inputEvidence": {
          "owner": {
            "actor": 501595590875934720,
            "actorGeneration": 1,
            "equipGeneration": 6,
            "space": 2
          },
          "sequence": 17679,
          "observedNs": 638831950085000,
          "deadlineNs": 638832050085000,
          "nowNs": 638831960954300,
          "focused": true,
          "tracked": [
            true,
            true
          ],
          "released": [
            true,
            true
          ]
        },
        "request": 4,
        "nativeTick": 25170,
        "cache": 124669440,
        "equipmentDispatched": false
      }
    }
  },
  "feed": null
}
''')


def recorded_empty_snapshot(now, nonce):
    offset = now - RECORDED_EMPTY["observed_ns"]

    def replay(value, key=""):
        if isinstance(value, dict):
            return {k: replay(v, k) for k, v in value.items()}
        if isinstance(value, list):
            return [replay(v) for v in value]
        if type(value) is int and value > 0 and (key.endswith("Ns") or key.endswith("_ns")):
            return value + offset
        return value

    result = replay(RECORDED_EMPTY)
    result["nonce"] = str(nonce)
    return result


class Clock:
    def __init__(self):
        self.now = 2_000_000_000

    def tick(self):
        self.now += 10_000
        return self.now

    def sleep(self, seconds):
        self.now += int(seconds * 1_000_000_000)


@unittest.skipUnless(sys.platform == "win32", "shared publication HANDLE path requires Windows")
class NavigationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="navigation-offline-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("drive_bc2_navigation_simulation.py", "drive_bc2_feed_simulation.py",
                     "plan_bc2_feed_simulation.py", "bc2_holster_core.py"):
            target = self.root / "tools" / name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(ROOT / "tools" / name, target)
        self.m = load("navigation_" + self.root.name, self.root / "tools/drive_bc2_navigation_simulation.py")
        self.clock = Clock()
        self.run, self.folder = self.root / "run", self.root / "session"
        (self.run / "sim").mkdir(parents=True)
        self.folder.mkdir()
        self.version, self.source = "0.1.999", "a" * 64
        for name in ("host.exe", "xrsim.dll", "runtime.json"):
            (self.root / name).write_bytes(b"synthetic-only")
        pin = lambda name: dict(path=str(self.root / name), bytes=(self.root / name).stat().st_size,
                                sha256=self.m.t.digest((self.root / name).read_bytes()))
        self.host = dict(pid=999, path=str(self.root / "host.exe"), start_utc="2026-01-01T00:00:00Z")
        self.manifest = dict(schema="fvr.xr-game-run.v1", output_directory=str(self.run),
            version=self.version, source_sha256=self.source, runtime_manifest=pin("runtime.json"),
            runtime_library=pin("xrsim.dll"), host_executable=pin("host.exe"),
            capture_plan=dict(periodic=False, layers=False, maximum_compositions=3, width=640, height=480))
        self.write(self.run / "run-manifest.json", self.manifest)
        controls = {key: False for key in ("a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR")}
        controls.update(trigL=0, trigR=0, gripL=0, gripR=0, stickL=[0, 0], stickR=[0, 0])
        self.state = dict(pid=999, runtime="xr-sim", errors=0, lastCmdError="", sessionRunning=True,
            actionsAttached=True, sessionState="FOCUSED", paceMode="free", captureSeq=0, cmdSeq=1,
            lastCmd="note startup", frame=1, endFrames=1, controls=controls,
            head=dict(valid=True, pos=[0, 1.6, 0], ypr=[0, 0, 0]),
            handL=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]),
            handR=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]))
        self.initial_state = copy.deepcopy(self.state)
        self.write(self.run / "sim/state.json", self.state)
        (self.run / "sim/command.txt").write_bytes(b"note startup\n")
        self.write(self.run / "sim-startup.json", dict(root_commands_ready=True, status="ready", host=self.host,
            target_sequence=1, command_file=dict(sha256=self.m.t.digest(b"note startup\n")), settled_state=self.state))
        self.link = dict(root_commands_ready=True, host=self.host, supervisor=dict(pid=111), folder=str(self.folder),
            sim_state=str(self.run / "sim/state.json"), command=str(self.run / "sim/command.txt"),
            session=str(self.folder / "session.json"), options=str(self.folder / "options.json"),
            stop_request=str(self.folder / "stop.request"), startup_receipt=str(self.run / "sim-startup.json"),
            mechanism_snapshots=dict(requested=True))
        self.write(self.run / "session-link.json", self.link)
        self.session = dict(host=self.host, supervisor=self.link["supervisor"], source_sha256=self.source,
                            version=self.version, state="running")
        self.write(self.folder / "session.json", self.session)
        self.write(self.folder / "options.json", dict(source_sha256=self.source, version=self.version,
            activate_window=False, xr_manifest=str(self.root / "runtime.json")))
        (self.folder / "mechanism-snapshot.enable").write_bytes(b"1\n")
        self.rows = [dict(left=[0, .5], right=[0, 0], holdMs=400, lookDegrees=[10, 0])]
        self.calls, self.samples, self.owner_records = [], [], []
        self.driver = None
        self.snapshot_mutation = None
        self.state_mutation = None
        self.ack_delay = 0
        self.suppress_ack = False
        self.cleanup_failure = False
        self.publication_delay = 0
        self.competing_writer = False
        self.omit_later_frame = False
        self.held_owner_records = []
        self.bad_ack_once = False
        self.replay_empty = False
        self.response_delay = lambda driver: 0
        self.omit_response_from = None
        self.snapshot_read_delay = 0
        self.read_delay = lambda path, driver: 0
        self.publication_windows = []
        self.cached_expiry_phases = set()

    def write(self, path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def argv(self):
        self.write(self.run / "navigation.json", self.rows)
        return ["--run", str(self.run), "--navigation", str(self.run / "navigation.json"),
                "--version", self.version, "--source-sha256", self.source,
                "--root-authorized-ordinary-navigation"]

    def snapshot(self, driver):
        now = self.clock.tick()
        s = (recorded_empty_snapshot(now, driver.pending_nonce) if self.replay_empty
             else fixtures.outer_snapshot(now, driver.pending_nonce, now // 10_000, "held"))
        if not self.replay_empty:
            s["input"]["menu"] = dict(owner=1, sequence=now // 10_000, epoch=7, deadlineNs=now+70_000_000)
        s["input"]["head"] = dict(position=self.state["head"]["pos"][:],
            orientation=self.m.p.ypr_quaternion(self.state["head"]["ypr"]))
        for index, hand in enumerate(("handL", "handR")):
            pose = dict(position=self.state[hand]["pos"][:],
                        orientation=self.m.p.ypr_quaternion(self.state[hand]["ypr"]))
            s["input"]["hands"][index].update(grip=copy.deepcopy(pose), aim=copy.deepcopy(pose),
                held=0, squeeze=0, trigger=0, stickX=self.state["controls"]["stickL" if index == 0 else "stickR"][0],
                stickY=self.state["controls"]["stickL" if index == 0 else "stickR"][1])
        if self.replay_empty:
            h = s["bodyHolster"]
            for hand, current in zip(h["input"]["hands"], s["input"]["hands"]):
                hand.update({key: current[key] for key in hand})
            h["neutralTrackedInput"] = self.m.holster.neutral(h["input"])
        if self.snapshot_mutation:
            self.snapshot_mutation(s, driver)
        self.samples.append(copy.deepcopy(s))
        if any(any(v) for v in driver.sticks):
            self.held_owner_records.append(json.loads(driver.owner_bytes(False)))
        self.write(driver.snapshot_path, s)

    def assembled(self, args=None):
        m, current, pending_ack, pending_snapshot = self.m, b"note startup\n", None, None
        original_request = m.t.Driver.request_snapshot
        original_init = m.NavigationDriver.__init__
        original_publish = m.t.publish_command
        original_read = m.t.read_bytes
        test = self

        def simulate():
            nonlocal current, pending_ack
            sample = getattr(self.driver, "latest_snapshot", None)
            if sample and self.clock.now >= sample["valid_until_ns"]:
                self.cached_expiry_phases.add(
                    "release" if self.driver.releasing_sticks else
                    "ack" if self.driver.pending_command else
                    "hold" if any(any(v) for v in self.driver.sticks) else "idle")
            path = self.run / "sim/command.txt"
            raw = path.read_bytes()
            if raw != current:
                current = raw
                batch = raw.decode("ascii").splitlines()
                if batch[:-1]:
                    self.calls.append(batch[:-1])
                    self.owner_records.append(json.loads((self.run / "live-driver-owner.json").read_text()))
                for line in batch[:-1]:
                    words = line.split()
                    if words[0] == "stick":
                        self.state["controls"]["stickL" if words[1] == "l" else "stickR"] = list(map(float, words[2:]))
                    elif words[:2] == ["head", "pose"]:
                        numbers = list(map(float, words[2:]))
                        self.state["head"].update(pos=numbers[:3], ypr=numbers[3:])
                    else:
                        raise AssertionError("unexpected simulator input: " + line)
                self.state.update(cmdSeq=self.state["cmdSeq"]+len(batch), lastCmd=batch[-1],
                                  frame=self.state["frame"]+1)
                ack_delay = self.ack_delay(self.driver) if callable(self.ack_delay) else self.ack_delay
                pending_ack = (self.clock.now+ack_delay, batch, self.state["frame"])
            if pending_ack and self.clock.now >= pending_ack[0] and not self.suppress_ack:
                _, batch, frame = pending_ack
                (self.run / "sim/ack.txt").write_text(
                    f"seq={self.state['cmdSeq']} frame={frame} error=-\nlast={' | '.join(batch)}\n", encoding="utf-8")
                if not self.omit_later_frame:
                    self.state.update(frame=frame+1, endFrames=self.state["endFrames"]+1)
                pending_ack = None
            if self.state_mutation and self.calls:
                self.state_mutation(self.state, self.driver)
            self.write(self.run / "sim/state.json", self.state)

        class Owner:
            def __init__(self, expected):
                self.api = SimpleNamespace(CloseHandle=lambda handle: None)
            def check(self):
                simulate()
            def close(self):
                pass

        def initialize(driver, args):
            self.driver = driver
            original_init(driver, args)

        def request(driver):
            nonlocal pending_snapshot
            original_request(driver)
            if self.omit_response_from is None or driver.snapshot_index < self.omit_response_from:
                pending_snapshot = (driver.last_request + self.response_delay(driver), driver)
                deliver_snapshot()

        def deliver_snapshot():
            nonlocal pending_snapshot
            if pending_snapshot and self.clock.now >= pending_snapshot[0]:
                _, driver = pending_snapshot
                pending_snapshot = None
                self.snapshot(driver)

        def publish(path, raw, deadline_ns, validate):
            self.publication_windows.append(dict(deadlineNs=deadline_ns,
                pulseDeadlineNs=self.driver.action_deadline, release=self.driver.releasing_sticks,
                cleanup=self.driver.cleanup_mode))
            self.clock.now += (self.publication_delay(self.driver) if callable(self.publication_delay)
                               else self.publication_delay)
            if self.cleanup_failure and self.driver.cleanup_mode:
                raise OSError("synthetic zero publication failure")
            if self.competing_writer and self.calls:
                path.write_bytes(b"note competing-writer\n")
            return original_publish(path, raw, deadline_ns, validate)

        def read(path, maximum):
            if self.driver is not None:
                self.clock.now += self.read_delay(path, self.driver)
            deliver_snapshot()
            raw = original_read(path, maximum)
            if (self.snapshot_read_delay and self.driver is not None and self.driver.snapshot_index == 2
                    and path == self.driver.snapshot_path
                    and int(json.loads(raw)["nonce"]) == self.driver.pending_nonce):
                self.clock.now += self.snapshot_read_delay
                self.snapshot_read_delay = 0
            if self.bad_ack_once and path.name == "ack.txt":
                self.bad_ack_once = False
                raw = raw.split(b"last=")[0] + b"last=foreign batch\n"
            return raw

        with ExitStack() as stack:
            stack.enter_context(patch.object(m.time, "perf_counter_ns", self.clock.tick))
            stack.enter_context(patch.object(m.time, "sleep", self.clock.sleep))
            stack.enter_context(patch.object(m.t, "WindowsOwner", Owner))
            stack.enter_context(patch.object(m.t.Driver, "acquire_lock", lambda driver: None))
            stack.enter_context(patch.object(m.NavigationDriver, "__init__", initialize))
            stack.enter_context(patch.object(m.t.Driver, "request_snapshot", request))
            stack.enter_context(patch.object(m.t, "publish_command", publish))
            stack.enter_context(patch.object(m.t, "read_bytes", read))
            output = stack.enter_context(redirect_stdout(io.StringIO()))
            code = m.main(self.argv() if args is None else args)
        return code, json.loads(output.getvalue().strip().splitlines()[-1])

    def test_real_assembled_head_stick_zero_and_second_episode(self):
        self.rows.append(dict(left=[.25, 0], right=[.1, 0], holdMs=400))
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(self.calls), 4)
        self.assertEqual(self.calls[1], self.m.ZERO)
        self.assertEqual(self.calls[3], self.m.ZERO)
        self.assertTrue(all(record["pending"] for record in self.owner_records))
        self.assertTrue(self.held_owner_records)
        self.assertTrue(all(record["pending"] and not record["navigation"]["neutralCommitted"]
                            for record in self.held_owner_records))
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertFalse(owner["pending"])
        self.assertTrue(owner["navigation"]["neutralCommitted"])
        self.assertEqual(self.state["head"]["ypr"], [10, 0, 0])
        for key in ("handL", "handR"):
            self.assertEqual(self.state[key], self.initial_state[key])
        self.assertEqual(self.state["head"]["pos"], self.initial_state["head"]["pos"])
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["objectiveEntry"])
        self.assertEqual(len(result["episodes"]), 2)

    def test_explicit_root_and_reviewed_batch_required(self):
        args = self.argv()[:-1]
        code, result = self.assembled(args)
        self.assertEqual(code, 2)
        self.assertIn("explicit ROOT", result["error"])
        self.assertEqual(self.calls, [])
        with self.assertRaises(self.m.Refusal):
            self.m.plan([dict(left=[0, 1], right=[0, 0], holdMs=2001)])
        with self.assertRaises(self.m.Refusal):
            self.m.plan([dict(left=[0, 1], right=[0, 0], holdMs=150, commands=["trigger r 1"])])

    def test_sustained_pulses_require_current_native_observation_and_original_total_budget(self):
        for scenario in ("current", "missing", "native_focus_loss", "total_budget"):
            with self.subTest(scenario=scenario):
                if scenario != "current":
                    self.setUp()  # Each assembled CLI owns a fresh isolated session.
                self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=2000) for _ in range(4)]
                self.m.plan(self.rows)
                with self.assertRaises(self.m.Refusal):
                    self.m.plan(self.rows + [self.rows[0]])
                if scenario == "missing":
                    self.omit_response_from = 3
                elif scenario == "native_focus_loss":
                    self.snapshot_mutation = lambda sample, driver: (
                        sample["input"].update(focused=False) if driver.snapshot_index >= 3 else None)
                elif scenario == "total_budget":
                    # Time already spent constructing this invocation belongs
                    # to its original ten seconds, not a fresh per-pulse clock.
                    self.read_delay = lambda path, driver: (
                        7_000_000_000 if path.name == "run-manifest.json" else 0)
                code, result = self.assembled()
                driver = self.driver
                self.assertEqual(driver.hard_deadline - driver.started, 10 * self.m.t.NS)
                self.assertLess(self.clock.now, driver.hard_deadline)
                self.assertFalse(result["normalStopRequired"])
                self.assertFalse(result["pendingCommand"])
                self.assertEqual(result["sticks"], [[0, 0], [0, 0]])
                self.assertEqual(self.calls[-1], self.m.ZERO)
                self.assertFalse(result["nativeAcceptance"])
                events = [json.loads(line) for line in
                          (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
                commits = [event for event in events if event["type"] == "command_commit"]
                if scenario == "current":
                    self.assertEqual((code, result.get("error")), (0, None))
                    self.assertEqual(len(result["episodes"]), 4)
                    self.assertEqual(len(self.calls), 8)
                    self.assertGreater(len(self.samples), 200)
                    self.assertTrue(all(not event["cleanup"] for event in commits))
                    for episode in result["episodes"]:
                        elapsed = episode["neutralCommittedNs"] - episode["startedNs"]
                        self.assertGreater(elapsed, 1_600_000_000)
                        self.assertLess(elapsed, 2_000_000_000)
                        held = [sample for sample in self.samples
                                if episode["startedNs"] <= sample["observed_ns"] < episode["neutralCommittedNs"]]
                        self.assertGreater(len(held), 50)
                        self.assertTrue(all(next_sample["observed_ns"] < driver.snapshot_source_deadline(sample)
                                            for sample, next_sample in zip(held, held[1:])))
                    for window, commit in zip(self.publication_windows, commits):
                        if window["release"]:
                            self.assertEqual(window["deadlineNs"] - commit["requestedNs"], self.m.COMMAND_WINDOW)
                            self.assertLessEqual(window["deadlineNs"], window["pulseDeadlineNs"])
                else:
                    self.assertEqual(code, 2)
                    self.assertTrue(commits[-1]["cleanup"])
                    if scenario == "total_budget":
                        self.assertIn("no active budget", result["error"])
                        self.assertLess(len(result["episodes"]), 4)
                        self.assertGreater(self.clock.now - driver.started, 9_000_000_000)
                    else:
                        self.assertEqual(result["episodes"], [])
                        self.assertEqual(len(self.calls), 2)
                        if scenario == "missing":
                            self.assertIn("lost current native observation", result["error"])
                            lost_at = driver.snapshot_source_deadline(driver.latest_snapshot)
                        else:
                            self.assertIn("invalid input focus/head/time", result["error"])
                            lost_at = self.samples[-1]["observed_ns"]
                        self.assertLess(commits[-1]["requestedNs"] - lost_at, self.m.PERIOD)
                        self.assertLess(commits[-1]["committedNs"] - commits[0]["requestedNs"], 150_000_000)

    def test_recorded_genuine_empty_assembled_movement_preserves_fire_block(self):
        self.replay_empty = True
        original = copy.deepcopy(RECORDED_EMPTY)
        # Exercise the real callback against the untouched native projection at
        # its original observation time before replaying the assembled command.
        self.m.p.admit_frame(RECORDED_EMPTY, RECORDED_EMPTY["observed_ns"],
            int(RECORDED_EMPTY["nonce"]), mechanism_admission=self.m.empty_movement_admission)
        self.assertEqual(RECORDED_EMPTY, original)
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertTrue(all(s["weapon_actions_blocked"] is True for s in self.samples))
        self.assertTrue(all(s["bodyHolster"]["inputSequenceRelation"] == "newer" for s in self.samples))
        self.assertTrue(any(not s["bodyHolster"]["neutralTrackedInput"] for s in self.samples))
        self.assertTrue(all(s["bodyHolster"]["leftClaim"] is None
                            and s["bodyHolster"]["rightClaim"] is None for s in self.samples))
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["normalStopRequired"])

    def test_blocked_without_current_empty_refuses_before_input(self):
        self.replay_empty = True
        def mutation(s, driver):
            s["bodyHolster"].update(actualEmpty=False, phase=1)
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("without an admitted retained mechanism", result["error"])
        self.assertEqual(self.calls, [])

    def test_stale_recorded_empty_refuses_before_input(self):
        self.replay_empty = True
        self.snapshot_mutation = lambda s, driver: s["bodyHolster"].update(validUntilNs=self.clock.now-1)
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("holster observation lease", result["error"])
        self.assertEqual(self.calls, [])

    def test_lost_empty_proof_releases_owned_sticks(self):
        self.replay_empty = True
        def mutation(s, driver):
            if self.calls:
                s["bodyHolster"]["freeRightCurrent"] = False
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("genuine Empty proof", result["error"])
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_empty_original_lease_caps_publication(self):
        self.replay_empty = True
        self.snapshot_mutation = lambda s, driver: s["bodyHolster"].update(validUntilNs=self.clock.now+5_000_000)
        self.publication_delay = 8_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [])
        self.assertRegex(result["error"], "deadline|lease")

    def test_empty_proof_rejects_unjoined_stale_or_nonempty_observations(self):
        now = RECORDED_EMPTY["observed_ns"]
        mutations = (
            lambda s: s["bodyHolster"].update(available=False),
            lambda s: s["bodyHolster"].update(actualEmpty=False),
            lambda s: s["bodyHolster"].update(phase=1),
            lambda s: s["bodyHolster"].update(emptyHands=False),
            lambda s: s["bodyHolster"].update(rightClaim={}),
            lambda s: s["bodyHolster"].update(blockWeaponActions=False),
            lambda s: s["bodyHolster"].update(allowAutomaticGunHold=True),
            lambda s: s["bodyHolster"]["nativeOwner"].update(actorGeneration=2),
            lambda s: s["bodyHolster"]["inputEvidence"]["owner"].update(equipGeneration=7),
            lambda s: s["bodyHolster"]["inputEvidence"].update(deadlineNs=now-1),
            lambda s: s["bodyHolster"]["input"].update(generation=17678),
            lambda s: s["bodyHolster"].update(inputSequenceRelation="same"),
            lambda s: s["bodyHolster"].update(neutralTrackedInput=False),
            lambda s: s["bodyHolster"]["freeRight"]["visibility"].update(deadlineNs=now-1),
            lambda s: s["bodyHolster"]["freeRight"]["suppression"].update(nativeTick=25169),
            lambda s: s["bodyHolster"]["freeRight"]["suppression"].update(equipmentDispatched=True),
            lambda s: s["bodyHolster"]["freeRight"].update(authorizationDeadlineNs=now-1),
            lambda s: s["input"]["menu"].update(owner=3),
            lambda s: s["input"]["menu"].update(deadlineNs=now-1),
            lambda s: s["input"].update(focused=False),
            lambda s: s["input"]["hands"][1].update(gripTracked=False),
            lambda s: s["input"]["hands"][1].update(trigger=.1),
        )
        driver = self.m.NavigationDriver.__new__(self.m.NavigationDriver)
        driver.navigation_reference = None
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                sample = copy.deepcopy(RECORDED_EMPTY)
                mutate(sample)
                with self.assertRaises(Exception):
                    driver.admit_snapshot(sample, now, int(sample["nonce"]))
        for key, value in (("trigger", .1), ("squeeze", .1), ("held", 1), ("gripTracked", False)):
            with self.subTest(holster_input=key):
                sample = copy.deepcopy(RECORDED_EMPTY)
                sample["bodyHolster"]["input"]["hands"][1][key] = value
                sample["bodyHolster"]["neutralTrackedInput"] = False
                with self.assertRaises(self.m.ObservationLost):
                    driver.admit_snapshot(sample, now, int(sample["nonce"]))

    def test_delayed_ack_consumes_original_pulse_and_explicit_zero(self):
        self.ack_delay = 35_000_000
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertLess(result["episodes"][0]["neutralCommittedNs"]-result["episodes"][0]["startedNs"],
                        self.rows[0]["holdMs"]*1_000_000)

    def test_saved_zero_timing_keeps_full_shared_window_after_pump_and_preparation(self):
        recorded = RECORDED_ZERO_TIMING
        self.rows = [dict(left=[0, -.6], right=[0, 0], holdMs=500)]
        self.publication_delay = recorded["nonzeroPublishedNs"] - recorded["nonzeroRequestedNs"]
        self.ack_delay = lambda driver: (90_000_000 if driver.releasing_sticks else
            recorded["nonzeroCommittedNs"] - recorded["nonzeroPublishedNs"])
        self.response_delay = lambda driver: 240_000_000 if driver.snapshot_index >= 3 else 0
        delays = set()

        def delayed_read(path, driver):
            if path.name != "state.json" or driver.action_deadline is None or driver.pending_command:
                return 0
            if driver.releasing_sticks and "preparation" not in delays:
                delays.add("preparation")
                return recorded["zeroPreparationNs"]
            if (not driver.releasing_sticks and driver.stage_deadline == driver.action_deadline
                    and self.clock.now >= driver.action_deadline - self.m.RELEASE_RESERVE - 2_000_000
                    and "hold" not in delays):
                delays.add("hold")
                return recorded["lastPumpOvershootNs"]
            return 0
        self.read_delay = delayed_read
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(delays, {"hold", "preparation"})
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        commits = [event for event in events if event["type"] == "command_commit"]
        self.assertEqual(len(commits), 2)
        self.assertTrue(all(not event["cleanup"] for event in commits))
        zero = commits[-1]
        window = self.publication_windows[-1]
        self.assertEqual(window["deadlineNs"] - zero["requestedNs"], 250_000_000)
        self.assertLessEqual(window["deadlineNs"], window["pulseDeadlineNs"])
        self.assertGreater(zero["committedNs"] - zero["requestedNs"],
                           recorded["pulseDeadlineNs"] - recorded["zeroRequestedNs"])
        self.assertLess(zero["committedNs"], window["pulseDeadlineNs"])
        self.assertIsNotNone(self.driver.pending_nonce)
        self.assertEqual(self.calls, [["stick l 0 -0.6", "stick r 0 0"], self.m.ZERO])
        self.assertFalse(result["normalStopRequired"])

    def test_short_pulse_refuses_before_nonzero_when_full_zero_reserve_cannot_fit(self):
        self.rows = [dict(left=[0, .5], right=[0, 0], holdMs=100)]
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("no active budget", result["error"])
        self.assertEqual(self.calls, [])
        self.assertEqual(result["release"], "no navigation input published")
        self.assertEqual(result["episodes"], [])
        self.assertFalse(result["normalStopRequired"])

    def test_hold_pump_cannot_spend_zero_reserve_and_cleanup_stays_owned(self):
        delayed = []
        def delayed_read(path, driver):
            if (path.name == "state.json" and driver.action_deadline is not None
                    and driver.stage_deadline == driver.action_deadline
                    and not driver.releasing_sticks and not driver.pending_command and not delayed):
                delayed.append(True)
                return self.m.PERIOD + 1_000_000
            return 0
        self.read_delay = delayed_read
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertTrue(delayed)
        self.assertIn("original acknowledgement/read deadline exceeded", result["error"])
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(result["episodes"], [])
        self.assertFalse(result["normalStopRequired"])
        self.assertTrue(self.publication_windows[-1]["cleanup"])

    def test_slow_zero_preparation_refuses_clipped_window_then_cleans_up(self):
        delayed = []
        def delayed_read(path, driver):
            if (path.name == "state.json" and driver.releasing_sticks and not driver.cleanup_mode
                    and not driver.pending_command and not delayed):
                delayed.append(True)
                return 60_000_000
            return 0
        self.read_delay = delayed_read
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertTrue(delayed)
        self.assertRegex(result["error"], "deadline|full zero publication/ACK window")
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertEqual(len(self.calls), 2)
        self.assertTrue(self.publication_windows[-1]["cleanup"])
        self.assertEqual(result["episodes"], [])
        self.assertFalse(result["normalStopRequired"])

    def test_stall_after_release_pause_refuses_before_command_request(self):
        reads = []
        def delayed_read(path, driver):
            if (path.name == "state.json" and driver.releasing_sticks and not driver.cleanup_mode
                    and not driver.pending_command):
                reads.append(True)
                # First read is the bounded pause pump. The second is shared
                # _send's ownership read before its exact request timestamp.
                if len(reads) == 2:
                    return 60_000_000
            return 0
        self.read_delay = delayed_read
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(reads), 2)
        self.assertIn("full zero publication/ACK window", result["error"])
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertTrue(self.publication_windows[-1]["cleanup"])
        self.assertEqual(result["episodes"], [])
        self.assertFalse(result["normalStopRequired"])

    def test_recorded_pending_response_crosses_cached_expiry_in_ack_hold_and_normal_zero(self):
        recorded = RECORDED_NAVIGATION_TIMING
        self.rows = [dict(left=[0, -.3], right=[0, 0], holdMs=400)]
        self.ack_delay = 65_000_000
        self.snapshot_read_delay = recorded["thirdRequestNs"] - recorded["secondObservedNs"]
        self.response_delay = lambda driver: (
            recorded["firstResponseNs"] if driver.snapshot_index == 1 else
            recorded["secondResponseNs"] if driver.snapshot_index == 2 else 150_000_000)

        def recorded_second_source(s, driver):
            if driver.snapshot_index != 2:
                return
            offset = s["observed_ns"] - recorded["secondObservedNs"]
            s["valid_until_ns"] = s["native_deadline_ns"] = recorded["secondValidUntilNs"] + offset
            s["input_evidence"].update(observedNs=recorded["secondInputObservedNs"] + offset,
                                       deadlineNs=s["valid_until_ns"])
            s["selected"]["deadlineNs"] = s["valid_until_ns"]
            s["input"]["menu"]["deadlineNs"] = recorded["secondMenuUntilNs"] + offset
        self.snapshot_mutation = recorded_second_source
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertTrue({"ack", "hold", "release"} <= self.cached_expiry_phases)
        self.assertEqual(self.calls, [["stick l 0 -0.3", "stick r 0 0"], self.m.ZERO])
        episode = result["episodes"][0]
        self.assertLess(episode["neutralCommittedNs"] - episode["startedNs"], 400_000_000)
        artifacts = Path(result["artifactDirectory"])
        events = [json.loads(line) for line in (artifacts / "events.jsonl").read_text().splitlines()]
        self.assertTrue(all(not event["cleanup"] for event in events if event["type"] == "command_commit"))
        original = json.loads(gzip.decompress((artifacts / "snapshot-002.json.gz").read_bytes()))
        self.assertEqual(original, self.samples[1])
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["normalStopRequired"])

    def test_missing_response_retains_request_budget_then_releases_without_new_input(self):
        self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=400),
                     dict(left=[.3, 0], right=[0, 0], holdMs=400)]
        self.omit_response_from = 3
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("snapshot response not received within request budget", result["error"])
        self.assertEqual(len(self.calls), 3)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertEqual(len(result["episodes"]), 1)
        self.assertFalse(result["normalStopRequired"])
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        timeout = next(event for event in events if event["type"] == "snapshot_response_timeout")
        self.assertEqual(timeout["responseDeadlineNs"] - timeout["requestStartedNs"], 250_000_000)
        self.assertGreaterEqual(timeout["elapsedNs"], 250_000_000)

    def test_expired_response_refresh_cannot_delay_owned_normal_zero(self):
        self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=400)]
        self.response_delay = lambda driver: 60_000_000 if driver.snapshot_index >= 3 else 0

        def expired_original(sample, driver):
            if driver.snapshot_index != 3:
                return
            def shift(value):
                if isinstance(value, dict):
                    for key, child in value.items():
                        if type(child) is int and child > 0 and (key.endswith("Ns") or key.endswith("_ns")):
                            value[key] -= 110_000_000
                        else:
                            shift(child)
                elif isinstance(value, list):
                    for child in value:
                        shift(child)
            shift(sample)
        self.snapshot_mutation = expired_original
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertIsNotNone(self.driver.snapshot_refresh_started)
        self.assertIsNotNone(self.driver.pending_nonce)
        self.assertLess(result["episodes"][0]["neutralCommittedNs"] - result["episodes"][0]["startedNs"],
                        400_000_000)
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        self.assertTrue(any(event["type"] == "snapshot_refresh_hold" for event in events))
        self.assertTrue(all(not event["cleanup"] for event in events if event["type"] == "command_commit"))

    def test_normal_zero_ack_cannot_extend_original_pulse_deadline(self):
        self.ack_delay = lambda driver: (
            290_000_000 if driver.releasing_sticks and not driver.cleanup_mode else 0)
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertRegex(result["error"], "deadline|maximumGestureNs")
        self.assertEqual(result["episodes"], [])
        self.assertEqual(sum(lines != self.m.ZERO for lines in self.calls), 1)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_missing_response_after_normal_zero_cannot_admit_next_pulse(self):
        self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=400),
                     dict(left=[.3, 0], right=[0, 0], holdMs=400)]
        self.omit_response_from = 3
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("snapshot response not received within request budget", result["error"])
        self.assertEqual(len(result["episodes"]), 1)
        self.assertEqual(sum(lines != self.m.ZERO for lines in self.calls), 1)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_next_nonzero_publication_still_requires_current_source(self):
        self.rows.append(dict(left=[.3, 0], right=[0, 0], holdMs=400))
        self.publication_delay = lambda driver: (
            90_000_000 if len(self.calls) >= 2 and not driver.releasing_sticks else 0)
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertRegex(result["error"], "deadline|lease|stale snapshot")
        self.assertEqual(len(result["episodes"]), 1)
        self.assertEqual(sum(lines != self.m.ZERO for lines in self.calls), 1)
        self.assertEqual(self.calls[-1], self.m.ZERO)

    def test_head_only_publication_still_requires_current_source(self):
        self.rows = [dict(left=[0, 0], right=[0, 0], holdMs=400, lookDegrees=[10, 0])]
        self.publication_delay = 90_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertRegex(result["error"], "deadline|lease|stale snapshot")
        self.assertEqual(self.calls, [])

    def assert_delayed_native_loss_releases(self, mutation, message):
        self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=500)]
        self.response_delay = lambda driver: 120_000_000 if driver.snapshot_index >= 3 else 0
        self.snapshot_mutation = lambda sample, driver: mutation(sample) if driver.snapshot_index >= 3 else None
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn(message, result["error"])
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_delayed_native_focus_loss_still_refuses(self):
        self.assert_delayed_native_loss_releases(
            lambda s: s["input"].update(focused=False), "invalid input focus/head/time")

    def test_delayed_native_tracking_loss_still_refuses(self):
        self.assert_delayed_native_loss_releases(
            lambda s: s["input"]["hands"][1].update(aimTracked=False), "required controller tracking missing")

    def test_delayed_native_epoch_loss_still_refuses(self):
        self.assert_delayed_native_loss_releases(
            lambda s: s["input"]["menu"].update(epoch=8), "tracking/head/menu epoch changed")

    def test_actor_loss_releases_owned_sticks_and_refuses_next_episode(self):
        self.rows.append(dict(left=[1, 0], right=[0, 0], holdMs=400))
        def mutation(s, driver):
            if self.calls:
                s["owner"]["actorGeneration"] += 1
                s["input_evidence"]["owner"]["actorGeneration"] += 1
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("lifetime changed", result["error"])
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertEqual(len(self.calls), 2)
        self.assertFalse(result["normalStopRequired"])

    def test_source_expiry_fails_closed_without_restamping(self):
        def mutation(s, driver):
            if self.calls:
                s["valid_until_ns"] = s["input_evidence"]["deadlineNs"] = self.clock.now - 1
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertTrue(any(s["valid_until_ns"] < s["observed_ns"] for s in self.samples))
        self.assertFalse(result["nativeAcceptance"])

    def test_focus_loss_preserves_release_ownership(self):
        self.state_mutation = lambda state, driver: state.update(sessionState="VISIBLE")
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("focus/tracking lost", result["error"])
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_tracking_loss_preserves_release_ownership(self):
        self.state_mutation = lambda state, driver: state["handR"].update(valid=False)
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_original_publication_lease_expires_before_write(self):
        self.publication_delay = 120_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [])
        self.assertIn("deadline", result["error"])

    def test_missing_ack_preserves_pending_custody_and_requests_stop(self):
        self.suppress_ack = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["pendingCommand"])
        self.assertTrue(result["normalStopRequired"])
        self.assertIn("normal Stop", result["nextAction"])
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertTrue(owner["pending"])

    def test_cleanup_failure_never_claims_neutral_or_retries(self):
        self.state_mutation = lambda state, driver: state.update(sessionState="VISIBLE")
        self.cleanup_failure = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])
        self.assertIn("synthetic zero publication failure", result["releaseError"])

    def test_competing_writer_means_no_further_input(self):
        self.competing_writer = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])
        self.assertIn("ownership", result["releaseError"])

    def test_source_version_mismatch_refuses_before_input(self):
        args = self.argv()
        args[args.index("--version")+1] = "0.1.998"
        code, result = self.assembled(args)
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [])
        self.assertIn("source/version", result["error"])

    def test_non_neutral_entry_cannot_release_someone_elses_sticks(self):
        self.state["controls"]["stickL"] = [0, 1]
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("neutral entry", result["error"])
        self.assertEqual(self.calls, [])
        self.assertEqual(self.state["controls"]["stickL"], [0, 1])

    def test_menu_loss_releases_without_claiming_playing(self):
        def mutation(s, driver):
            if self.calls:
                s["input"]["menu"]["owner"] = 3
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("Playing", result["error"])
        self.assertEqual(self.calls[-1], self.m.ZERO)

    def test_session_source_loss_forbids_further_input_and_requires_stop(self):
        def mutation(state, driver):
            self.session["source_sha256"] = "b"*64
            self.write(self.folder / "session.json", self.session)
        self.state_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])

    def test_owner_record_replacement_forbids_zero(self):
        def mutation(s, driver):
            if self.calls:
                self.write(self.run / "live-driver-owner.json", dict(host=self.host, pending=False, foreign=True))
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])

    def test_ack_requires_later_committed_frame(self):
        self.omit_later_frame = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["pendingCommand"])
        self.assertTrue(result["normalStopRequired"])

    def test_late_ack_does_not_renew_pulse_deadline(self):
        self.ack_delay = 190_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertRegex(result["error"], "deadline|maximumGestureNs")
        self.assertEqual(result["episodes"], [])
        self.assertEqual(self.calls[-1], self.m.ZERO)

    def test_observed_ack_ambiguity_stays_revoked_after_file_recovers(self):
        self.bad_ack_once = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("acknowledgement batch text mismatch", result["error"])
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])

    def test_four_pulses_retain_original_large_responses_within_shared_disk_bound(self):
        # Earlier zero scheduling produces fewer diagnostics. Keep the original
        # aggregate compression/cap assertion with still-legal larger samples.
        self.rows = [dict(left=[0, .25], right=[0, 0], holdMs=500) for _ in range(4)]
        self.snapshot_mutation = lambda s, driver: s.update(observationPadding="x"*48_000)
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        paths = sorted(Path(result["artifactDirectory"]).glob("snapshot-*.json.gz"))
        restored = [gzip.decompress(path.read_bytes()) for path in paths]
        self.assertGreater(sum(map(len, restored)), self.m.t.MAX_BYTES)
        self.assertLess(sum(path.stat().st_size for path in Path(result["artifactDirectory"]).iterdir()), self.m.t.MAX_BYTES)
        self.assertEqual([json.loads(raw) for raw in restored], self.samples)
        self.assertEqual(len(self.calls), 8)


if __name__ == "__main__":
    unittest.main()
