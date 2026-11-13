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
import random
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


# Actual89/01 666a666858e140c4b3cba04916f97625: the first 1500 ms row
# failed while request 3 was still within its unchanged 250 ms response budget.
# snapshot-002.json.gz SHA256 48cdf6416fc731380ae96ef58b4e115b7fe78d3d8a31456b23ae37d7a0e87769.
# Later successors are synthetic: this projects the demonstrated timing gap,
# not a native movement or successful campaign observation.
RECORDED_LONG_TIMING = dict(
    firstResponseNs=197_312_200, secondResponseNs=95_891_400,
    secondObservedNs=650494711475000, secondInputObservedNs=650494690026100,
    secondValidUntilNs=650494790026100, secondMenuUntilNs=650494778720600,
    thirdRequestNs=650494724404300, commandAckNs=40_463_300)


# Actual90/02 d4b9f6c38db247e8a98f0acce5463fc3: speculative request 006
# began during pulse 2, remained pending through zero and timed out before
# pulse 3. The retained matching response still described nonzero input.
RECORDED_DIAGNOSTIC_TIMING = dict(
    requestStartedNs=654246019842800, responseDeadlineNs=654246269842800,
    timeoutEvaluatedNs=654246271531500, observedNs=654246213812000,
    sourceDeadlineNs=654246300738200, menuDeadlineNs=654246293957900,
    neutralCommittedNs=654246267229200, commandAckNs=45_489_100)


# Actual95/02 52e6a7e6e9374590970eddacec0e980b, snapshot-002.json.gz
# SHA256 eab226f1c32fb5d6d5ea5e1aa3bcd1b88d8e9da7a97afb64325af7b4175cec4f.
# The valid weapon response carried this unavailable navigation section after
# a valid same-owner entry observation. No input was published. A successful
# successor below is synthetic; these retained fields do not prove game input.
RECORDED_GATHER_PENDING = dict(
    observedNs=673424943617100, requestStartedNs=673424881619400,
    navigation=dict(observationOnly=True, available=False, reason="gather_pending",
        gatherSequence=24888, sampledNs=673424938650500,
        inputObservedNs=673424923477600, inputDeadlineNs=673425023477600,
        validUntilNs=673425020835600,
        owner=dict(manager=133825344, player=1171441040, soldier=1134923568,
                   weak=120474520, controlled=1134923568, entry=1134928160,
                   router=110282096, cache=117902800,
                   actorGeneration=1, inputEpoch=1, movementEpoch=1),
        onFoot=True, nativePlaying=True, allowedRead=True, allowedMask=435, input=None))


# Actual97/03 346c9a211af449cc9162a98d64590675, snapshot-001.json.gz
# SHA256 6dcf67ffe21343f8081ded101f741a1ac6eaa339c008f56c0e95744fd6aea0d1.
# Current() revoked the worker's available copy after its publication serial
# changed. No input was published. Only the refusal/timing below is recorded;
# the fresh coherent successors in the assembled regression are synthetic.
RECORDED_OBSERVATION_CHANGED = dict(
    observedNs=678667089788300, requestStartedNs=678666959610300,
    navigation=dict(observationOnly=True, available=False, reason="observation_changed",
        gatherSequence=7847, sampledNs=678667085143100,
        inputObservedNs=678667074078600, inputDeadlineNs=678667174078600,
        validUntilNs=678667171703100,
        owner=dict(manager=134480704, player=1168935616, soldier=1174739472,
                   weak=133633384, controlled=1174739472, entry=1174744064,
                   router=111299664, cache=118689232,
                   actorGeneration=1, inputEpoch=1, movementEpoch=1),
        onFoot=True, nativePlaying=True, allowedRead=True, allowedMask=435, input=None))


# Actual94/01 9008cd89094e4eae92c03ab82d0fa956, command seq83; events.jsonl
# SHA256 05e445ef82a15bb6426d49ea0ef626ae855dabf48ed93e55fc79cdfeef08b439.
# The exact requested/acknowledged head projection falsely failed settled
# equality after two completed turns. Seq86 committed exact-zero cleanup.
# Other cyclic representations below are synthetic, not new native evidence.
RECORDED_YAW_WRAP = dict(
    requestedNs=670831005973400, committedNs=670831101933900,
    requestedHead=dict(pos=[0.0, 1.6, 0.0], ypr=[180.0, 0.0, 0.0], valid=True),
    settledHead=dict(pos=[0.0, 1.6, 0.0], ypr=[-180.0, 0.0, 0.0], valid=True))


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


def with_navigation_observation(snapshot, *, no_shot=True):
    """Small passive native sample using the existing synthetic input geometry."""
    inp = copy.deepcopy(snapshot["input"])
    for hand in inp["hands"]:
        hand.update(active=127, touchActive=1, touched=0)
    observed = snapshot["input_evidence"]["observedNs"]
    deadline = observed + 100_000_000
    nav = dict(observationOnly=True, available=True, reason=None,
        gatherSequence=inp["generation"], sampledNs=snapshot["observed_ns"]-1_000_000,
        inputObservedNs=observed, inputDeadlineNs=deadline,
        validUntilNs=min(deadline, inp["menu"]["deadlineNs"]),
        owner=dict(manager=10, player=11, soldier=22, weak=33, controlled=22,
                   entry=44, router=55, cache=66, actorGeneration=1, inputEpoch=2, movementEpoch=3),
        onFoot=True, nativePlaying=True, allowedRead=True, allowedMask=0x103, input=inp)
    if no_shot:
        snapshot = {key: snapshot[key] for key in ("schema", "nonce", "observed_ns")}
        snapshot.update(valid=False, invalid_reason="no_shot_publication")
    snapshot["navigationObservation"] = nav
    return snapshot


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
        plan = dict(periodic=False, layers=False, maximum_compositions=3, width=640, height=480,
                    eyes=2, image_streams=2, bytes_per_pixel=4, metadata_bytes_per_composition=65536,
                    raw_bytes=3 * 640 * 480 * 16 + 3 * 65536)
        raw_bytes = 512 * 1024 * 1024 + plan["raw_bytes"]
        limits = dict(host_seconds=300, outer_seconds=420, extended_observation=False, tape_max_frames=30000,
                      raw_budget_bytes=raw_bytes, raw_budget_limit_bytes=660 * 1024 * 1024,
                      planned_bytes=raw_bytes + 128 * 1024 * 1024, native_maximum_raw_images=8,
                      sim_compositions=3, sim_capture_width=640, sim_capture_height=480)
        self.manifest = dict(schema="fvr.xr-game-run.v1", output_directory=str(self.run),
            version=self.version, source_sha256=self.source, runtime_manifest=pin("runtime.json"),
            runtime_library=pin("xrsim.dll"), host_executable=pin("host.exe"),
            capture_plan=plan, limits=limits)
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
            mechanism_snapshots=dict(requested=True), capture_plan=plan)
        self.write(self.run / "session-link.json", self.link)
        self.session = dict(host=self.host, supervisor=self.link["supervisor"], source_sha256=self.source,
                            version=self.version, state="running")
        self.write(self.folder / "session.json", self.session)
        self.write(self.folder / "options.json", dict(source_sha256=self.source, version=self.version,
            activate_window=False, xr_manifest=str(self.root / "runtime.json")))
        (self.folder / "mechanism-snapshot.enable").write_bytes(b"1\n")
        self.rows = [dict(left=[0, .5], right=[0, 0], holdMs=400, lookDegrees=[10, 0])]
        self.calls, self.samples, self.owner_records, self.snapshot_requests = [], [], [], []
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
        self.native_navigation = False
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
                held=8 if self.state["controls"]["x" if index == 0 else "a"] else 0,
                squeeze=0, trigger=0, stickX=self.state["controls"]["stickL" if index == 0 else "stickR"][0],
                stickY=self.state["controls"]["stickL" if index == 0 else "stickR"][1])
        if self.replay_empty:
            h = s["bodyHolster"]
            for hand, current in zip(h["input"]["hands"], s["input"]["hands"]):
                hand.update({key: current[key] for key in hand})
            h["neutralTrackedInput"] = self.m.holster.neutral(h["input"])
        if self.native_navigation:
            s = with_navigation_observation(s, no_shot=self.native_navigation == "no_shot")
        if self.snapshot_mutation:
            self.snapshot_mutation(s, driver)
        self.samples.append(copy.deepcopy(s))
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
            if self.driver is not None and (any(any(v) for v in self.driver.sticks) or self.driver.action_button):
                self.held_owner_records.append(json.loads(self.driver.owner_bytes(False)))
            sample = getattr(self.driver, "latest_snapshot", None)
            if sample and self.clock.now >= (sample["navigationObservation"]["validUntilNs"]
                                             if "navigationObservation" in sample else sample["valid_until_ns"]):
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
                    elif words[0] == "btn" and words[1] in ("a", "x") and words[2] in ("down", "up"):
                        self.state["controls"][words[1]] = words[2] == "down"
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
            self.snapshot_requests.append(dict(startedNs=driver.last_request,
                actionDeadlineNs=driver.action_deadline, pendingCommand=driver.pending_command,
                sticks=copy.deepcopy(self.state["controls"]["stickL"] + self.state["controls"]["stickR"])))
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
        recorded = RECORDED_YAW_WRAP
        cases = (("ordinary", 10, [10, 0, 0]),
                 ("recorded180", recorded["requestedHead"]["ypr"][0], recorded["settledHead"]["ypr"]),
                 ("negative180", -180, [180, 0, 0]),
                 ("fullturn", 360, [0, 0, 0]),
                 ("negative_fullturn", -360, [0, 0, 0]),
                 ("changed_yaw", 180, [-179.99, 0, 0]),
                 ("changed_yaw_at_seam", 180, [-179.99999999999997, 0, 0]),
                 ("changed_pitch", 180, [-180, .01, 0]),
                 ("changed_roll", 180, [-180, 0, .01]),
                 ("changed_position", 180, [-180, 0, 0]))
        for scenario, requested_yaw, settled_ypr in cases:
            with self.subTest(scenario=scenario):
                if scenario != "ordinary":
                    self.setUp()  # Each assembled CLI owns a fresh isolated session.
                requested = copy.deepcopy(recorded["requestedHead"])
                requested["ypr"][0] = requested_yaw
                settled = copy.deepcopy(recorded["settledHead"])
                settled["ypr"] = settled_ypr[:]
                if scenario == "changed_position":
                    settled["pos"][0] += .01
                look = 10 if scenario == "ordinary" else (45 if requested_yaw > 0 else -45)
                self.state["head"] = copy.deepcopy(requested)
                self.state["head"]["ypr"][0] -= look
                self.initial_state = copy.deepcopy(self.state)
                self.rows[0].update(holdMs=500, lookDegrees=[look, 0])
                self.rows.append(dict(left=[.25, 0], right=[.1, 0], holdMs=500))
                self.ack_delay = recorded["committedNs"] - recorded["requestedNs"]
                self.state_mutation = lambda state, driver: state.update(head=copy.deepcopy(settled))
                code, result = self.assembled()
                self.assertEqual(self.calls[0][0], "head pose " + " ".join(
                    str(v) for v in requested["pos"] + requested["ypr"]))
                self.assertEqual(self.calls[-1], self.m.ZERO)
                self.assertTrue(all(record["pending"] for record in self.owner_records))
                owner = json.loads((self.run / "live-driver-owner.json").read_text())
                self.assertFalse(owner["pending"])
                self.assertTrue(owner["navigation"]["neutralCommitted"])
                self.assertFalse(result["pendingCommand"])
                self.assertFalse(result["normalStopRequired"])
                self.assertTrue(self.m.neutral_controls(self.state["controls"]))
                self.assertFalse(result["nativeAcceptance"])
                self.assertFalse(result["objectiveEntry"])
                events = [json.loads(line) for line in
                          (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
                commits = [event for event in events if event["type"] == "command_commit"]
                self.assertEqual(commits[0]["state"]["head"], settled)
                if scenario.startswith("changed_"):
                    self.assertEqual(code, 2)
                    self.assertIn("head position/controller poses changed" if scenario == "changed_position"
                                  else "settled navigation input changed", result["error"])
                    self.assertEqual(len(self.calls), 2)
                    self.assertEqual(result["episodes"], [])
                    self.assertTrue(commits[-1]["cleanup"])
                else:
                    self.assertEqual((code, result.get("error")), (0, None))
                    self.assertEqual(len(self.calls), 4)
                    self.assertEqual(self.calls[1], self.m.ZERO)
                    self.assertTrue(self.held_owner_records)
                    self.assertTrue(all(record["pending"] and not record["navigation"]["neutralCommitted"]
                                        for record in self.held_owner_records))
                    self.assertEqual(self.state["head"], settled)
                    for key in ("handL", "handR"):
                        self.assertEqual(self.state[key], self.initial_state[key])
                    self.assertEqual(self.state["head"]["pos"], self.initial_state["head"]["pos"])
                    self.assertEqual(len(result["episodes"]), 2)
                    self.assertTrue(all(not commit["cleanup"] for commit in commits))

    def test_no_shot_navigation_uses_original_native_input_and_acknowledged_zero(self):
        self.native_navigation = "no_shot"
        self.rows += [dict(left=[0, .25], right=[0, 0], holdMs=400, action="jump"),
                      dict(left=[0, 0], right=[0, 0], holdMs=400, action="use")]
        def before_zero(sample, driver):
            if driver.snapshot_index == 3:
                nav = sample["navigationObservation"]
                nav["inputObservedNs"] = driver.last_write - 1_000_000
                nav["inputDeadlineNs"] = nav["inputObservedNs"] + self.m.t.SNAPSHOT_PERIOD
                nav["validUntilNs"] = min(nav["inputDeadlineNs"], nav["input"]["menu"]["deadlineNs"])
        self.snapshot_mutation = before_zero
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(result["episodes"]), 3)
        self.assertEqual(self.calls[1::2], [self.m.ZERO, self.m.ZERO+["btn a up"], self.m.ZERO+["btn x up"]])
        self.assertTrue(self.m.neutral_controls(self.state["controls"]))
        self.assertFalse(result["normalStopRequired"])
        self.assertTrue(all(sample["valid"] is False and sample["invalid_reason"] == "no_shot_publication"
                            and not {"owner", "equipment", "mapping", "input", "physical_equipment_generation"} & sample.keys()
                            for sample in self.samples))
        saved = sorted(Path(result["artifactDirectory"]).glob("snapshot-*.json.gz"))
        self.assertEqual([json.loads(gzip.decompress(path.read_bytes())) for path in saved], self.samples)
        self.assertTrue(all(sample["navigationObservation"]["validUntilNs"]
                            == min(sample["navigationObservation"]["inputDeadlineNs"],
                                   sample["navigationObservation"]["input"]["menu"]["deadlineNs"])
                            for sample in self.samples))
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        refresh = [e for e in events if e.get("reason") == "navigation_input_not_new"]
        self.assertTrue(refresh)
        self.assertEqual(refresh[0]["nonce"], self.samples[2]["nonce"])
        self.assertLess(self.samples[2]["navigationObservation"]["inputObservedNs"],
                        refresh[0]["lastCommandWrittenNs"])
        commits = [e for e in events if e["type"] == "command_commit"]
        for index, episode in enumerate(result["episodes"]):
            source = next(s for s in self.samples if s["nonce"] == episode["admissionNonce"])
            if index:
                self.assertGreaterEqual(source["navigationObservation"]["inputObservedNs"],
                                        commits[index*2-1]["writtenNs"])
            self.assertLess(commits[index*2]["writtenNs"], self.driver.snapshot_source_deadline(source))
        self.assertTrue(all(request["actionDeadlineNs"] is None and not request["pendingCommand"]
                            and request["sticks"] == [0, 0, 0, 0] for request in self.snapshot_requests))
        self.assertEqual(self.driver.hard_deadline-self.driver.started, 10*self.m.t.NS)
        self.assertFalse(result["nativeAcceptance"])

    def test_no_shot_delayed_delivery_refreshes_without_stale_command_authority(self):
        self.native_navigation = "no_shot"
        delayed = []
        def read_delay(path, driver):
            if path == getattr(driver, "snapshot_path", None) and driver.snapshot_index == 1 and not delayed:
                delayed.append(True)
                return 80_000_000
            return 0
        self.read_delay = read_delay
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertTrue(delayed)
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        refresh = [e for e in events if e["type"] == "snapshot_refresh_hold"]
        self.assertEqual(len(refresh), 1)
        self.assertEqual(refresh[0]["nonce"], self.samples[0]["nonce"])
        self.assertGreaterEqual(refresh[0]["evaluatedNs"], refresh[0]["sourceDeadlineNs"])
        self.assertEqual(refresh[0]["lastCommandWrittenNs"], 0)
        self.assertGreater(self.publication_windows[0]["deadlineNs"], refresh[0]["sourceDeadlineNs"])
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_recorded_gather_pending_reacquires_only_at_owned_neutral_boundaries(self):
        self.native_navigation = "valid_weapon"
        self.rows.append(dict(left=[0, .25], right=[0, 0], holdMs=400))
        recorded = RECORDED_GATHER_PENDING
        self.response_delay = lambda driver: (recorded["observedNs"]-recorded["requestStartedNs"]
                                              if driver.snapshot_index == 2 else 0)
        def pending(sample, driver):
            nav = sample["navigationObservation"]
            nav["owner"] = copy.deepcopy(recorded["navigation"]["owner"])
            if driver.snapshot_index == 2:
                offset = sample["observed_ns"] - recorded["observedNs"]
                nav = copy.deepcopy(recorded["navigation"])
                for key in ("sampledNs", "inputObservedNs", "inputDeadlineNs", "validUntilNs"):
                    nav[key] += offset
                sample["navigationObservation"] = nav
            elif driver.snapshot_index == 4:
                nav.update(available=False, reason="gather_pending", input=None)
        self.snapshot_mutation = pending
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(result["episodes"]), 2)
        self.assertEqual([episode["admissionNonce"] for episode in result["episodes"]],
                         [self.samples[index]["nonce"] for index in (2, 4)])
        self.assertEqual(self.driver.navigation_reference, self.samples[0])
        saved = sorted(Path(result["artifactDirectory"]).glob("snapshot-*.json.gz"))
        self.assertEqual([json.loads(gzip.decompress(path.read_bytes())) for path in saved], self.samples)
        self.assertTrue(self.samples[1]["valid"])
        self.assertIsNone(self.samples[1]["navigationObservation"]["input"])
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        refresh = [event for event in events if event["type"] == "snapshot_refresh_hold"]
        self.assertEqual([event["nonce"] for event in refresh],
                         [self.samples[index]["nonce"] for index in (1, 3)])
        self.assertTrue(all(event["reason"] == "navigation_gather_pending"
                            and event["commandAuthority"] is False and event["sourceDeadlineNs"] is None
                            and event["refreshRequest"] == 1 for event in refresh))
        self.assertEqual(refresh[0]["lastCommandWrittenNs"], 0)
        commits = [event for event in events if event["type"] == "command_commit"]
        self.assertEqual(refresh[1]["lastCommandWrittenNs"], commits[1]["writtenNs"])
        for index, episode in enumerate(result["episodes"]):
            source = self.samples[2+index*2]
            self.assertGreaterEqual(commits[index*2]["requestedNs"], source["observed_ns"])
            self.assertLess(commits[index*2]["writtenNs"], self.driver.snapshot_source_deadline(source))
        self.assertTrue(all(request["actionDeadlineNs"] is None and not request["pendingCommand"]
                            and request["sticks"] == [0, 0, 0, 0] for request in self.snapshot_requests))
        self.assertEqual(self.calls[1::2], [self.m.ZERO, self.m.ZERO])
        self.assertTrue(self.m.neutral_controls(self.state["controls"]))
        self.assertEqual(self.driver.hard_deadline-self.driver.started, 10*self.m.t.NS)
        self.assertFalse(result["normalStopRequired"])
        self.assertFalse(result["nativeAcceptance"])

    def test_recorded_observation_changed_reacquires_only_fresh_owned_sources(self):
        self.native_navigation = "valid_weapon"
        self.rows.append(dict(left=[0, .25], right=[0, 0], holdMs=400))
        recorded = RECORDED_OBSERVATION_CHANGED
        self.response_delay = lambda driver: (recorded["observedNs"]-recorded["requestStartedNs"]
                                              if driver.snapshot_index == 1 else 0)
        references = []
        def changed(sample, driver):
            references.append(copy.deepcopy(driver.navigation_reference))
            sample["navigationObservation"]["owner"] = copy.deepcopy(recorded["navigation"]["owner"])
            if driver.snapshot_index in (1, 3, 5):
                nav = copy.deepcopy(recorded["navigation"])
                offset = sample["observed_ns"]-recorded["observedNs"]
                for key in ("sampledNs", "inputObservedNs", "inputDeadlineNs", "validUntilNs"):
                    nav[key] += offset
                sample["navigationObservation"] = nav
        self.snapshot_mutation = changed
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(references[:2], [None, None])
        self.assertEqual(self.driver.navigation_reference, self.samples[1])
        self.assertEqual([episode["admissionNonce"] for episode in result["episodes"]],
                         [self.samples[index]["nonce"] for index in (3, 5)])
        saved = sorted(Path(result["artifactDirectory"]).glob("snapshot-*.json.gz"))
        self.assertEqual([json.loads(gzip.decompress(path.read_bytes())) for path in saved], self.samples)
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        refresh = [event for event in events if event["type"] == "snapshot_refresh_hold"]
        self.assertEqual([event["nonce"] for event in refresh],
                         [self.samples[index]["nonce"] for index in (0, 2, 4)])
        self.assertTrue(all(event["reason"] == "navigation_observation_changed"
                            and event["commandAuthority"] is False and event["sourceDeadlineNs"] is None
                            and event["refreshRequest"] == 1 for event in refresh))
        commits = [event for event in events if event["type"] == "command_commit"]
        self.assertEqual([event["lastCommandWrittenNs"] for event in refresh],
                         [0, 0, commits[1]["writtenNs"]])
        for index, source_index in enumerate((3, 5)):
            source = self.samples[source_index]
            self.assertGreaterEqual(commits[index*2]["requestedNs"], source["observed_ns"])
            self.assertLess(commits[index*2]["writtenNs"], self.driver.snapshot_source_deadline(source))
        self.assertTrue(all(request["actionDeadlineNs"] is None and not request["pendingCommand"]
                            and request["sticks"] == [0, 0, 0, 0] for request in self.snapshot_requests))
        self.assertEqual(self.calls[1::2], [self.m.ZERO, self.m.ZERO])
        self.assertEqual(self.driver.hard_deadline-self.driver.started, 10*self.m.t.NS)
        self.assertFalse(result["normalStopRequired"])
        self.assertFalse(result["nativeAcceptance"])

    def test_observation_changed_preserves_owner_and_successor_refusals(self):
        changes = (*self.m.NAVIGATION_OWNER, "successor_owner", "menu", "tracking",
                   "owner_unavailable", "route_changed", "stale_input", "observation_busy", "nonnull_input")
        for index, change in enumerate(changes):
            with self.subTest(change=change):
                if index:
                    self.setUp()
                self.native_navigation = "valid_weapon"
                def changed(sample, driver):
                    nav = sample["navigationObservation"]
                    if driver.snapshot_index == 2:
                        nav.update(available=False, reason="observation_changed")
                        if change != "nonnull_input":
                            nav["input"] = None
                        if change in self.m.NAVIGATION_OWNER:
                            nav["owner"][change] += 1
                    elif driver.snapshot_index == 3:
                        if change == "successor_owner":
                            nav["owner"]["actorGeneration"] += 1
                        elif change == "menu":
                            nav["input"]["menu"]["epoch"] += 1
                        elif change == "tracking":
                            nav["input"]["trackingBasisGeneration"] += 1
                        else:
                            nav.update(available=False, reason=change, input=None)
                self.snapshot_mutation = changed
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertEqual(self.calls, [])
                self.assertEqual(self.driver.navigation_reference, self.samples[0])
                self.assertEqual(len(self.snapshot_requests),
                                 2 if change in self.m.NAVIGATION_OWNER or change == "nonnull_input" else 3)
                self.assertEqual(result["release"], "no navigation input published")
                self.assertFalse(result["normalStopRequired"])

    def test_observation_changed_repeated_or_stale_bytes_cannot_supply_authority(self):
        for index, change in enumerate(("repeated_changed", "stale_source", "old_nonce", "empty_identity")):
            with self.subTest(change=change):
                if index:
                    self.setUp()
                self.native_navigation = "no_shot"
                def changed(sample, driver):
                    nav = sample["navigationObservation"]
                    if change == "empty_identity":
                        nav.update(available=False, reason="observation_changed", input=None,
                                   owner=dict.fromkeys(self.m.NAVIGATION_OWNER, 0))
                    elif driver.snapshot_index == 2 or (driver.snapshot_index > 2 and change == "repeated_changed"):
                        nav.update(available=False, reason="observation_changed", input=None)
                    elif driver.snapshot_index > 2:
                        if change == "old_nonce":
                            sample.clear()
                            sample.update(copy.deepcopy(self.samples[0]))
                        else:
                            sample["navigationObservation"] = copy.deepcopy(self.samples[0]["navigationObservation"])
                self.snapshot_mutation = changed
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertEqual(self.calls, [])
                self.assertEqual(result["release"], "no navigation input published")
                self.assertEqual(self.driver.hard_deadline-self.driver.started, 10*self.m.t.NS)
                self.assertLessEqual(self.driver.snapshot_refresh_requests, self.m.t.MAX_SNAPSHOT_REFRESHES)
                self.assertEqual(self.driver.navigation_reference,
                                 None if change == "empty_identity" else self.samples[0])
                if change == "repeated_changed":
                    self.assertIn("refresh budget exhausted", result["error"])
                    self.assertEqual(len(self.snapshot_requests), 2+self.m.t.MAX_SNAPSHOT_REFRESHES)
                elif change == "empty_identity":
                    self.assertEqual(len(self.snapshot_requests), 1)
                else:
                    self.assertEqual(len(self.snapshot_requests), 3)

    def test_initial_gather_pending_without_identity_cannot_establish_reference(self):
        self.native_navigation = "no_shot"
        references = []
        def pending(sample, driver):
            references.append(copy.deepcopy(driver.navigation_reference))
            if driver.snapshot_index == 1:
                nav = sample["navigationObservation"]
                nav.update(available=False, reason="gather_pending", input=None,
                           owner=dict.fromkeys(self.m.NAVIGATION_OWNER, 0), gatherSequence=0,
                           sampledNs=0, inputObservedNs=0, inputDeadlineNs=0, validUntilNs=0,
                           onFoot=False, nativePlaying=False, allowedRead=False, allowedMask=0)
        self.snapshot_mutation = pending
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(references[:2], [None, None])
        self.assertEqual(self.driver.navigation_reference, self.samples[1])
        self.assertEqual(result["episodes"][0]["admissionNonce"], self.samples[2]["nonce"])
        self.assertEqual(len(self.snapshot_requests), 3)
        self.assertTrue(all(request["sticks"] == [0, 0, 0, 0] for request in self.snapshot_requests))

    def test_gather_pending_refresh_keeps_shared_count_time_and_invocation_bounds(self):
        for index, limit in enumerate(("requests", "500ms", "invocation")):
            with self.subTest(limit=limit):
                if index:
                    self.setUp()
                self.native_navigation = "no_shot"
                def pending(sample, driver):
                    if driver.snapshot_index >= 2:
                        sample["navigationObservation"].update(available=False, reason="gather_pending", input=None)
                self.snapshot_mutation = pending
                if limit == "500ms":
                    self.response_delay = lambda driver: 240_000_000 if driver.snapshot_index >= 3 else 0
                delayed = []
                if limit == "invocation":
                    def read_delay(path, driver):
                        if path.name == "state.json" and driver.navigation_reference is not None and not delayed:
                            delayed.append(True)
                            return driver.deadline-self.clock.now-50_000_000
                        return 0
                    self.read_delay = read_delay
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertEqual(self.calls, [])
                self.assertEqual(result["release"], "no navigation input published")
                self.assertEqual(self.driver.hard_deadline-self.driver.started, 10*self.m.t.NS)
                self.assertLessEqual(self.driver.snapshot_refresh_requests, self.m.t.MAX_SNAPSHOT_REFRESHES)
                self.assertLessEqual(len(self.snapshot_requests), 2+self.m.t.MAX_SNAPSHOT_REFRESHES)
                self.assertTrue(self.m.neutral_controls(self.state["controls"]))
                if limit == "requests":
                    self.assertIn("refresh budget exhausted", result["error"])
                    self.assertEqual(self.driver.snapshot_refresh_requests, self.m.t.MAX_SNAPSHOT_REFRESHES)
                elif limit == "500ms":
                    self.assertIn("expired snapshot refresh budget exhausted", result["error"])
                    self.assertGreaterEqual(self.clock.now-self.driver.snapshot_refresh_started,
                                            self.m.t.SNAPSHOT_REFRESH_BUDGET)
                else:
                    self.assertTrue(delayed)
                    self.assertRegex(result["error"], "deadline|maximumGestureNs")
                    self.assertEqual(len(self.snapshot_requests), 2)

    def test_gather_pending_rechecks_neutral_live_owner_and_tracking_before_refresh(self):
        for index, change in enumerate(("active_pulse", "pending_sticks", "focus", "tracking", "command_owner")):
            with self.subTest(change=change):
                if index:
                    self.setUp()
                self.native_navigation = "no_shot"
                def pending(sample, driver):
                    if driver.snapshot_index == 2:
                        sample["navigationObservation"].update(available=False, reason="gather_pending", input=None)
                        if change == "active_pulse":
                            driver.action_deadline = self.clock.now+400_000_000
                        elif change == "pending_sticks":
                            driver.pending_sticks = [[0, .5], [0, 0]]
                        elif change == "focus":
                            self.state["sessionState"] = "VISIBLE"
                        elif change == "tracking":
                            self.state["handL"]["valid"] = False
                        else:
                            (self.run / "live-driver-owner.json").write_bytes(b"{}")
                self.snapshot_mutation = pending
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertEqual(self.calls, [])
                self.assertEqual(len(self.snapshot_requests), 2)
                self.assertIsNone(self.driver.snapshot_refresh_started)
                self.assertEqual(self.driver.snapshot_refresh_requests, 0)

    def test_gather_pending_and_expired_delivery_share_the_same_refresh_budget(self):
        self.native_navigation = "no_shot"
        def pending(sample, driver):
            if driver.snapshot_index in (2, 4):
                sample["navigationObservation"].update(available=False, reason="gather_pending", input=None)
        self.snapshot_mutation = pending
        delayed = []
        def read_delay(path, driver):
            if path == getattr(driver, "snapshot_path", None) and driver.snapshot_index == 3 and not delayed:
                delayed.append(True)
                return 80_000_000
            return 0
        self.read_delay = read_delay
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("refresh budget exhausted", result["error"])
        self.assertTrue(delayed)
        self.assertEqual(self.calls, [])
        self.assertEqual(len(self.snapshot_requests), 2+self.m.t.MAX_SNAPSHOT_REFRESHES)
        self.assertEqual(self.driver.snapshot_refresh_requests, self.m.t.MAX_SNAPSHOT_REFRESHES)
        self.assertEqual(self.driver.navigation_reference, self.samples[0])
        self.assertEqual(result["release"], "no navigation input published")

    def test_gather_pending_never_replaces_established_native_identity(self):
        for index, change in enumerate((*self.m.NAVIGATION_OWNER, "successor_owner", "menu", "tracking", "missing")):
            with self.subTest(change=change):
                if index:
                    self.setUp()
                self.native_navigation = "no_shot"
                def pending(sample, driver):
                    nav = sample["navigationObservation"]
                    if driver.snapshot_index == 2:
                        nav.update(available=False, reason="gather_pending", input=None)
                        if change in self.m.NAVIGATION_OWNER:
                            nav["owner"][change] += 1
                    elif driver.snapshot_index == 3:
                        if change == "successor_owner":
                            nav["owner"]["actorGeneration"] += 1
                        elif change == "menu":
                            nav["input"]["menu"]["epoch"] += 1
                        elif change == "tracking":
                            nav["input"]["trackingBasisGeneration"] += 1
                        else:
                            del sample["navigationObservation"]
                self.snapshot_mutation = pending
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertEqual(self.calls, [])
                self.assertEqual(self.driver.navigation_reference, self.samples[0])
                self.assertEqual(len(self.snapshot_requests), 2 if change in self.m.NAVIGATION_OWNER else 3)
                self.assertEqual(result["release"], "no navigation input published")

    def test_gather_pending_successor_with_definitive_refusal_is_not_retried(self):
        for index, reason in enumerate(("owner_unavailable", "menu_unavailable", "hands_untracked", "stale_input")):
            with self.subTest(reason=reason):
                if index:
                    self.setUp()
                self.native_navigation = "valid_weapon"
                def refusal(sample, driver):
                    if driver.snapshot_index >= 2:
                        sample["navigationObservation"].update(available=False, input=None,
                            reason="gather_pending" if driver.snapshot_index == 2 else reason)
                self.snapshot_mutation = refusal
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertIn(reason, result["error"])
                self.assertEqual(self.calls, [])
                self.assertEqual(len(self.snapshot_requests), 3)
                self.assertEqual(self.driver.snapshot_refresh_requests, 1)

    def test_passive_native_proof_rejects_stale_menu_vehicle_and_invalid_input_before_commands(self):
        mutations = {
            "stale": lambda n: n.update(inputObservedNs=self.clock.now-200_000_000,
                                         inputDeadlineNs=self.clock.now-100_000_000,
                                         validUntilNs=self.clock.now-100_000_000),
            "renewed_source": lambda n: n.update(inputObservedNs=self.clock.now-200_000_000),
            "extended_validity": lambda n: n.update(validUntilNs=n["validUntilNs"]+1),
            "future_sample": lambda n: n.update(sampledNs=self.clock.now+10_000_000),
            "menu": lambda n: n["input"]["menu"].update(owner=2),
            "expired_menu": lambda n: (n["input"]["menu"].update(deadlineNs=self.clock.now-1),
                                       n.update(validUntilNs=self.clock.now-1)),
            "vehicle": lambda n: n["owner"].update(controlled=999),
            "on_foot": lambda n: n.update(onFoot=False),
            "playing": lambda n: n.update(nativePlaying=False),
            "allowed_read": lambda n: n.update(allowedRead=False),
            "allowed_mask": lambda n: n.update(allowedMask=0x102),
            "focus": lambda n: n["input"].update(focused=False),
            "tracking": lambda n: n["input"]["hands"][0].update(aimTracked=False),
            "trigger": lambda n: n["input"]["hands"][1].update(trigger=.1),
            "buttons": lambda n: n["input"]["hands"][0].update(held=8),
        }
        for index, (name, mutate) in enumerate(mutations.items()):
            with self.subTest(name=name):
                if index:
                    self.setUp()
                self.native_navigation = "no_shot"
                self.snapshot_mutation = lambda sample, driver: mutate(sample["navigationObservation"])
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertEqual(self.calls, [])
                self.assertEqual(result["release"], "no navigation input published")
                self.assertFalse(result["normalStopRequired"])

    def test_explicit_native_lock_drop_stop_and_pause_cannot_fall_back_to_valid_weapon(self):
        for index, reason in enumerate(("observation_busy", "stopped", "input_pause", "route_changed",
                                        "disabled", "observation_changed", "owner_unavailable", "stale_input",
                                        "invalid_input", "focus_loss", "head_untracked", "hands_untracked",
                                        "not_on_foot", "native_inactive", "menu_unavailable",
                                        "source_clock_changed", "menu_clock_changed")):
            with self.subTest(reason=reason):
                if index:
                    self.setUp()
                self.native_navigation = "valid_weapon"
                self.snapshot_mutation = lambda sample, driver: sample.update(
                    navigationObservation=dict(observationOnly=True, available=False, reason=reason))
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertIn(reason, result["error"])
                self.assertEqual(self.calls, [])
                self.assertTrue(self.samples[0]["valid"])
                self.assertEqual(len(self.snapshot_requests), 1)
                self.assertIsNone(self.driver.snapshot_refresh_started)

    def test_passive_actor_route_tracking_and_menu_continuity_refuse_next_pulse(self):
        for index, change in enumerate(("actorGeneration", "inputEpoch", "movementEpoch", "router", "cache", "entry",
                                        "menu", "tracking", "missing")):
            with self.subTest(change=change):
                if index:
                    self.setUp()
                self.native_navigation = "no_shot"
                self.rows = [dict(left=[0, 0], right=[0, 0], holdMs=1000, action="use")]
                def mutation(sample, driver):
                    if driver.snapshot_index >= 3:
                        nav = sample["navigationObservation"]
                        if change == "missing":
                            del sample["navigationObservation"]
                        elif change == "menu":
                            nav["input"]["menu"]["epoch"] += 1
                        elif change == "tracking":
                            nav["input"]["trackingBasisGeneration"] += 1
                        else:
                            nav["owner"][change] += 1
                self.snapshot_mutation = mutation
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertEqual(len(result["episodes"]), 1)
                self.assertEqual(self.calls, [self.m.ZERO+["btn x down"], self.m.ZERO+["btn x up"], self.m.ZERO])
                self.assertFalse(result["normalStopRequired"])
                self.assertTrue(self.m.neutral_controls(self.state["controls"]))

    def test_passive_repeated_input_cannot_authorize_a_new_pulse(self):
        self.native_navigation = "no_shot"
        def repeat(sample, driver):
            if self.samples:
                previous = self.samples[-1]["navigationObservation"]
                nav = sample["navigationObservation"]
                nav["gatherSequence"] = previous["gatherSequence"]
                nav["input"]["generation"] = previous["input"]["generation"]
        self.snapshot_mutation = repeat
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("new original native input sample", result["error"])
        self.assertEqual(self.calls, [])
        self.assertEqual(len(self.snapshot_requests), 2+self.m.t.MAX_SNAPSHOT_REFRESHES)
        self.assertEqual(self.driver.snapshot_refresh_requests, self.m.t.MAX_SNAPSHOT_REFRESHES)
        self.assertLess(self.clock.now, self.driver.hard_deadline)
        self.assertTrue(self.m.neutral_controls(self.state["controls"]))

    def test_passive_original_source_deadline_still_caps_atomic_publication(self):
        self.native_navigation = "no_shot"
        self.publication_delay = 80_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [])
        self.assertEqual(result["release"], "no navigation input published")
        self.assertLessEqual(self.publication_windows[0]["deadlineNs"],
                             self.samples[-1]["navigationObservation"]["validUntilNs"])

    def test_passive_stop_request_preserves_pending_custody_and_forbids_more_input(self):
        self.native_navigation = "no_shot"
        self.state_mutation = lambda state, driver: (self.folder / "stop.request").write_bytes(b"1\n")
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("Stop already requested", result["error"])
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])
        self.assertFalse(self.m.neutral_controls(self.state["controls"]))

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

    def test_actions_press_once_per_row_and_release_with_neutral_ack(self):
        self.rows = [dict(left=[0, .5], right=[0, 0], holdMs=1500, action="jump"),
                     dict(left=[0, 0], right=[0, 0], holdMs=1500, action="use")]
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(result["episodes"]), 6)
        self.assertEqual(self.calls[0], ["stick l 0 0.5", "stick r 0 0", "btn a down"])
        self.assertEqual(self.calls[1], self.m.ZERO + ["btn a up"])
        self.assertEqual(self.calls[6], self.m.ZERO + ["btn x down"])
        self.assertEqual(self.calls[7], self.m.ZERO + ["btn x up"])
        self.assertEqual([line for batch in self.calls for line in batch if line.startswith("btn ")],
                         ["btn a down", "btn a up", "btn x down", "btn x up"])
        self.assertEqual([episode["action"] for episode in result["episodes"]],
                         ["jump", None, None, "use", None, None])
        self.assertTrue(any(record["navigation"]["actionButton"] == "x"
                            and record["navigation"]["sticks"] == [[0, 0], [0, 0]]
                            for record in self.held_owner_records))
        self.assertTrue(all(record["pending"] and not record["navigation"]["neutralCommitted"]
                            for record in self.held_owner_records))
        self.assertTrue(self.m.neutral_controls(self.state["controls"]))
        self.assertIsNone(result["actionButton"])
        self.assertIsNone(result["pendingActionButton"])
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["vehicleEntry"])
        self.assertTrue(all(not episode["nativeAction"] and not episode["vehicleEntry"]
                            for episode in result["episodes"]))
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        commits = [event for event in events if event["type"] == "command_commit"]
        for window, commit in zip(self.publication_windows, commits):
            if window["release"]:
                self.assertEqual(window["deadlineNs"] - commit["requestedNs"], self.m.COMMAND_WINDOW)
                self.assertLessEqual(window["deadlineNs"], window["pulseDeadlineNs"])
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertFalse(owner["pending"])
        self.assertTrue(owner["navigation"]["neutralCommitted"])

    def test_only_reviewed_actions_and_stationary_use_are_admitted(self):
        for action in ("fire", "reload", "a", "x", "", None, True, [], {}):
            with self.subTest(action=action), self.assertRaises(self.m.Refusal):
                self.m.plan([dict(left=[0, 0], right=[0, 0], holdMs=400, action=action)])
        for left, right in (([0, .1], [0, 0]), ([0, 0], [.1, 0]), ([0, 0], [0, .1])):
            with self.subTest(left=left, right=right), self.assertRaisesRegex(self.m.Refusal, "both sticks neutral"):
                self.m.plan([dict(left=left, right=right, holdMs=400, action="use")])
        self.rows[0]["action"] = "use"
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("both sticks neutral", result["error"])
        self.assertEqual(self.calls, [])

    def test_action_readiness_loss_releases_only_the_exact_owned_button(self):
        scenarios = [(action, loss) for action in ("jump", "use") for loss in ("focus", "handL", "handR")]
        for index, (action, loss) in enumerate(scenarios):
            with self.subTest(action=action, loss=loss):
                if index:
                    self.setUp()
                self.rows = [dict(left=[0, .5] if action == "jump" else [0, 0],
                                  right=[0, 0], holdMs=1000, action=action)]
                self.state_mutation = (lambda state, driver: state.update(sessionState="VISIBLE")) if loss == "focus" else (
                    lambda state, driver: state[loss].update(valid=False))
                code, result = self.assembled()
                button = self.m.ACTION_BUTTONS[action]
                self.assertEqual(code, 2)
                self.assertIn("focus/tracking lost", result["error"])
                self.assertEqual(len(self.calls), 2)
                self.assertEqual(self.calls[-1], self.m.ZERO + ["btn " + button + " up"])
                self.assertEqual([line for batch in self.calls for line in batch if line.startswith("btn ")],
                                 ["btn " + button + " down", "btn " + button + " up"])
                self.assertTrue(self.publication_windows[-1]["cleanup"])
                self.assertTrue(self.m.neutral_controls(self.state["controls"]))
                self.assertIsNone(result["actionButton"])
                self.assertFalse(result["pendingCommand"])
                self.assertFalse(result["normalStopRequired"])
                self.assertEqual(result["episodes"], [])

    def test_pending_action_without_ack_keeps_custody_and_refuses_release(self):
        self.rows = [dict(left=[0, 0], right=[0, 0], holdMs=400, action="use")]
        self.suppress_ack = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [self.m.ZERO + ["btn x down"]])
        self.assertTrue(result["pendingCommand"])
        self.assertTrue(result["normalStopRequired"])
        self.assertEqual(result["pendingActionButton"], "x")
        self.assertTrue(self.state["controls"]["x"])
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertTrue(owner["pending"])
        self.assertEqual(owner["navigation"]["pendingActionButton"], "x")

    def test_foreign_owner_after_action_forbids_all_further_input(self):
        self.rows = [dict(left=[0, 0], right=[0, 0], holdMs=400, action="use")]
        self.state_mutation = lambda state, driver: self.write(
            self.run / "live-driver-owner.json", dict(host=self.host, pending=False, foreign=True))
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [self.m.ZERO + ["btn x down"]])
        self.assertTrue(result["normalStopRequired"])
        self.assertTrue(self.state["controls"]["x"])

    def test_pending_or_foreign_entry_owner_cannot_publish_action(self):
        for index, scenario in enumerate(("pending", "foreign")):
            with self.subTest(scenario=scenario):
                if index:
                    self.setUp()
                self.rows = [dict(left=[0, 0], right=[0, 0], holdMs=400, action="use")]
                self.write(self.run / "live-driver-owner.json", dict(host=self.host,
                    pending=scenario == "pending", sequence=999, sentinel="note foreign",
                    commandSha256="b" * 64))
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertIn("unsettled command writer", result["error"])
                self.assertEqual(self.calls, [])

    def test_action_requires_neutral_buttons_at_entry(self):
        self.rows = [dict(left=[0, 0], right=[0, 0], holdMs=400, action="use")]
        self.state["controls"]["a"] = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("neutral entry", result["error"])
        self.assertEqual(self.calls, [])
        self.assertTrue(self.state["controls"]["a"])

    def test_use_cannot_continue_after_actor_equipment_or_on_foot_readiness_changes(self):
        for index, transition in enumerate(("actor", "equipment", "on_foot")):
            with self.subTest(transition=transition):
                if index:
                    self.setUp()
                self.rows = [dict(left=[0, 0], right=[0, 0], holdMs=1500, action="use")]
                def change_owner(sample, driver):
                    if driver.snapshot_index >= 3:
                        if transition == "actor":
                            sample["owner"]["actorGeneration"] += 1
                            sample["input_evidence"]["owner"]["actorGeneration"] += 1
                        elif transition == "equipment":
                            sample["physical_equipment_generation"] += 1
                            sample["input_evidence"]["owner"]["equipGeneration"] += 1
                        else:
                            # Native on-foot publication is revoked on seat entry;
                            # a button ACK supplies no replacement authority.
                            sample["valid"] = False
                self.snapshot_mutation = change_owner
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertIn("native snapshot refused" if transition == "on_foot" else "lifetime changed",
                              result["error"])
                self.assertEqual(self.calls, [self.m.ZERO + ["btn x down"],
                                             self.m.ZERO + ["btn x up"], self.m.ZERO])
                self.assertEqual(len(result["episodes"]), 1)
                self.assertFalse(result["normalStopRequired"])
                self.assertFalse(result["vehicleEntry"])

    def test_subpulses_require_new_original_admission_neutral_and_original_total_budget(self):
        for scenario in ("current", "missing", "native_focus_loss", "total_budget"):
            with self.subTest(scenario=scenario):
                if scenario != "current":
                    self.setUp()  # Each assembled CLI owns a fresh isolated session.
                self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=2000) for _ in range(4)]
                self.rows[0]["lookDegrees"] = [10, 0]
                self.m.plan(self.rows)
                with self.assertRaises(self.m.Refusal):
                    self.m.plan(self.rows + [self.rows[0]])
                self.response_delay = lambda driver: 100_000_000
                # Slightly larger than actual89/01's 28,520 decoded / 7,729
                # compressed source. Exercise the unchanged aggregate caps at
                # a realistic round trip, retaining every original byte.
                padding = random.Random(1057).randbytes(6500).hex() + "x"*16_000
                self.snapshot_mutation = lambda sample, driver: sample.update(observationPadding=padding)
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
                    self.assertEqual(len(result["episodes"]), 16)
                    self.assertEqual(result["plannedSubpulses"], 16)
                    self.assertEqual(result["plannedDurationMs"], 8000)
                    self.assertEqual(len(self.calls), 32)
                    self.assertEqual(self.state["head"]["ypr"], [10, 0, 0])
                    self.assertTrue(all(not event["cleanup"] for event in commits))
                    previous_commit = 0
                    nonces = set()
                    for index, episode in enumerate(result["episodes"]):
                        elapsed = episode["neutralCommittedNs"] - episode["startedNs"]
                        self.assertEqual((episode["rowIndex"], episode["subpulseIndex"]), divmod(index, 4))
                        self.assertEqual(episode["plannedDurationMs"], 500)
                        self.assertEqual(episode["completedElapsedNs"], elapsed)
                        self.assertLess(elapsed, 500_000_000)
                        self.assertEqual(self.calls[index*2+1], self.m.ZERO)
                        source = next(s for s in self.samples if s["nonce"] == episode["admissionNonce"])
                        self.assertNotIn(source["nonce"], nonces)
                        nonces.add(source["nonce"])
                        self.assertGreater(source["observed_ns"], previous_commit)
                        self.assertTrue(all(h["stickX"] == h["stickY"] == 0 for h in source["input"]["hands"]))
                        self.assertLess(commits[index*2]["writtenNs"], driver.snapshot_source_deadline(source))
                        previous_commit = episode["neutralCommittedNs"]
                    for window, commit in zip(self.publication_windows, commits):
                        if window["release"]:
                            self.assertEqual(window["deadlineNs"] - commit["requestedNs"], self.m.COMMAND_WINDOW)
                            self.assertLessEqual(window["deadlineNs"], window["pulseDeadlineNs"])
                    self.assertEqual(result["completedElapsedNs"],
                                     sum(e["completedElapsedNs"] for e in result["episodes"]))
                    artifacts = Path(result["artifactDirectory"])
                    restored = [gzip.decompress(path.read_bytes()) for path in sorted(artifacts.glob("snapshot-*.json.gz"))]
                    self.assertEqual([json.loads(raw) for raw in restored], self.samples)
                    self.assertLess(sum(map(len, restored)), 8*1024*1024)
                    self.assertLess(sum(path.stat().st_size for path in artifacts.iterdir()), self.m.t.MAX_BYTES)
                else:
                    self.assertEqual(code, 2)
                    self.assertTrue(commits[-1]["cleanup"])
                    if scenario == "total_budget":
                        self.assertRegex(result["error"], "no active budget|deadline|maximumGestureNs")
                        self.assertLess(len(result["episodes"]), 16)
                        self.assertGreater(self.clock.now - driver.started, 9_000_000_000)
                    else:
                        if scenario == "missing":
                            self.assertIn("snapshot response not received within request budget", result["error"])
                            self.assertEqual(len(result["episodes"]), 1)
                            self.assertEqual(sum(lines != self.m.ZERO for lines in self.calls), 1)
                        else:
                            self.assertEqual(len(result["episodes"]), 1)
                            self.assertEqual(len(self.calls), 3)
                            self.assertIn("invalid input focus/head/time", result["error"])
                            self.assertLess(commits[-1]["requestedNs"] - self.samples[-1]["observed_ns"], self.m.PERIOD)

    def test_recorded_long_row_round_trip_is_split_before_head_turn(self):
        recorded = RECORDED_LONG_TIMING
        self.rows = [dict(left=[0, -.5], right=[0, 0], holdMs=1500),
                     dict(left=[0, 0], right=[0, 0], holdMs=500, lookDegrees=[45, 0])]
        self.ack_delay = recorded["commandAckNs"]
        self.snapshot_read_delay = recorded["thirdRequestNs"] - recorded["secondObservedNs"]
        self.response_delay = lambda driver: (
            recorded["firstResponseNs"] if driver.snapshot_index == 1 else
            recorded["secondResponseNs"] if driver.snapshot_index == 2 else 100_000_000)

        def recorded_second_source(sample, driver):
            if driver.snapshot_index != 2:
                return
            offset = sample["observed_ns"] - recorded["secondObservedNs"]
            sample["valid_until_ns"] = sample["native_deadline_ns"] = recorded["secondValidUntilNs"] + offset
            sample["input_evidence"].update(observedNs=recorded["secondInputObservedNs"] + offset,
                                            deadlineNs=sample["valid_until_ns"])
            sample["selected"]["deadlineNs"] = sample["valid_until_ns"]
            sample["input"]["menu"]["deadlineNs"] = recorded["secondMenuUntilNs"] + offset
        self.snapshot_mutation = recorded_second_source
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(result["episodes"]), 4)
        self.assertEqual(result["plannedDurationMs"], 2000)
        self.assertEqual([e["rowIndex"] for e in result["episodes"]], [0, 0, 0, 1])
        self.assertEqual(self.calls[:6], [["stick l 0 -0.5", "stick r 0 0"], self.m.ZERO]*3)
        self.assertEqual(self.calls[7], self.m.ZERO)
        self.assertEqual(self.state["head"]["ypr"], [45, 0, 0])
        self.assertIn("hold", self.cached_expiry_phases)
        self.assertFalse(result["nativeAcceptance"])

    def test_recorded_diagnostic_timeout_cannot_be_created_during_a_pulse(self):
        recorded = RECORDED_DIAGNOSTIC_TIMING
        self.rows = [dict(left=[0, -.5], right=[0, 0], holdMs=1500),
                     dict(left=[0, 0], right=[0, 0], holdMs=500, lookDegrees=[45, 0])]
        self.ack_delay = recorded["commandAckNs"]
        elapsed = recorded["timeoutEvaluatedNs"] - recorded["requestStartedNs"]
        self.assertEqual(elapsed, 251_688_700)
        self.assertEqual(recorded["responseDeadlineNs"] - recorded["requestStartedNs"], 250_000_000)
        self.assertLess(recorded["neutralCommittedNs"], recorded["responseDeadlineNs"])
        self.assertLess(recorded["timeoutEvaluatedNs"], recorded["menuDeadlineNs"])
        # Replay the failing delay if a speculative request is attempted. Fresh
        # neutral requests have the observed ordinary ~100 ms round trip.
        self.response_delay = lambda driver: elapsed if driver.action_deadline is not None else 100_000_000
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(result["episodes"]), 4)
        self.assertEqual(len(self.calls), 8)
        self.assertEqual(len(self.snapshot_requests), 5)  # entry plus four admissions
        self.assertTrue(all(request["actionDeadlineNs"] is None and not request["pendingCommand"]
                            and request["sticks"] == [0, 0, 0, 0] for request in self.snapshot_requests))
        for request, prior in zip(self.snapshot_requests[2:], result["episodes"]):
            self.assertGreater(request["startedNs"], prior["neutralCommittedNs"])
        self.assertIsNone(self.driver.pending_nonce)
        self.assertFalse(result["normalStopRequired"])
        self.assertFalse(result["nativeAcceptance"])

    def test_recorded_timeout_at_neutral_boundary_still_refuses_without_retry(self):
        recorded = RECORDED_DIAGNOSTIC_TIMING
        self.rows = [dict(left=[0, -.5], right=[0, 0], holdMs=1500)]
        self.response_delay = lambda driver: (
            recorded["timeoutEvaluatedNs"] - recorded["requestStartedNs"] if driver.snapshot_index == 4 else 0)
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("snapshot response not received within request budget", result["error"])
        self.assertEqual(len(result["episodes"]), 2)
        self.assertEqual(sum(lines != self.m.ZERO for lines in self.calls), 2)
        self.assertEqual(len(self.snapshot_requests), 4)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertEqual(result["sticks"], [[0, 0], [0, 0]])
        self.assertFalse(result["normalStopRequired"])

    def test_subpulse_partition_preserves_budget_and_refuses_unusable_tail_before_input(self):
        for duration in (602, 750, 999, 1001, 1501, 1999, 2000):
            rows = [dict(left=[0, .3], right=[0, 0], holdMs=duration, lookDegrees=[10, 0])]
            schedule = self.m.pulse_schedule(self.m.plan(rows))
            self.assertEqual(sum(row["holdMs"] for row in schedule), duration)
            self.assertTrue(all(300 < row["holdMs"] <= 500 for row in schedule))
            self.assertEqual(sum("lookDegrees" in row for row in schedule), 1)
        self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=500),
                     dict(left=[0, .3], right=[0, 0], holdMs=501)]
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("no active budget", result["error"])
        self.assertEqual(self.calls, [])

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
        self.assertTrue(all(s["bodyHolster"]["neutralTrackedInput"] for s in self.samples))
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
        self.rows.append(dict(left=[.3, 0], right=[0, 0], holdMs=400))
        def mutation(s, driver):
            if self.calls:
                s["bodyHolster"]["freeRightCurrent"] = False
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("genuine Empty proof", result["error"])
        self.assertEqual(len(self.calls), 3)
        self.assertEqual(len(result["episodes"]), 1)
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
        self.assertIsNone(self.driver.pending_nonce)
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

    def test_post_admission_stalls_are_attributed_without_extra_preflight_reads(self):
        cases = (("state.json", 1, "command_pause"),
                 ("state.json", 2, "command_state_read"),
                 ("command.txt", 2, "command_ownership"),
                 ("state.json", 3, "navigation_publication_state_read"))
        for index, (target, ordinal, phase) in enumerate(cases):
            with self.subTest(phase=phase):
                if index:
                    self.setUp()
                self.native_navigation = "no_shot"
                self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=500),
                             dict(left=[.3, 0], right=[0, 0], holdMs=500)]
                reads = []

                def delayed_read(path, driver):
                    if (len(self.calls) == 2 and driver.authorized_lines is not None
                            and not driver.cleanup_mode and not driver.releasing_sticks
                            and not driver.pending_command):
                        reads.append(path.name)
                        if path.name == target and reads.count(target) == ordinal:
                            return 110_000_000
                    return 0

                self.read_delay = delayed_read
                code, result = self.assembled()
                self.assertEqual(code, 2)
                self.assertIn("original navigation source/menu lease", result["error"])
                self.assertEqual(reads, ["session.json", "state.json", "live-driver-owner.json", "command.txt",
                                         "session.json", "state.json", "command.txt", "session.json", "state.json"])
                self.assertEqual(self.calls, [["stick l 0 0.3", "stick r 0 0"], self.m.ZERO, self.m.ZERO])
                self.assertEqual(len(result["episodes"]), 1)
                self.assertFalse(result["pendingCommand"])
                self.assertFalse(result["normalStopRequired"])
                self.assertEqual([window["cleanup"] for window in self.publication_windows], [False, False, True])
                artifacts = Path(result["artifactDirectory"])
                timing = json.loads((artifacts / "receive-timing-failure.json").read_text())
                self.assertFalse(timing["pendingCommand"])
                self.assertEqual(timing["transientReadRetries"], {})
                self.assertGreaterEqual(timing["phaseNs"][phase]["maxNs"], 110_000_000)
                for _, _, other in cases:
                    if other != phase:
                        self.assertLess(timing["phaseNs"][other]["maxNs"], 110_000_000)
                events = [json.loads(line) for line in (artifacts / "events.jsonl").read_text().splitlines()]
                refresh = [event for event in events if event["type"] == "snapshot_refresh_hold"]
                self.assertEqual(len(refresh), 1)
                self.assertEqual(refresh[0]["reason"], "navigation_input_not_new")
                # The fixture's input sample is 2 ms older than its response.
                # The immediate successor after zero therefore predates that
                # publication and must refresh while neutral, before admission.
                # Account for each request instead of treating this required
                # native-input refresh as an extra command-preparation read.
                self.assertEqual(len(self.snapshot_requests), 4)
                self.assertEqual([sample["nonce"] for sample in self.samples], [
                    self.driver.navigation_reference["nonce"],  # entry
                    result["episodes"][0]["admissionNonce"],  # first pulse
                    refresh[0]["nonce"],  # rejected neutral successor
                    self.driver.latest_snapshot["nonce"]])  # second pulse admission
                self.assertEqual(timing["nonce"], self.samples[-1]["nonce"])
                zero_commit = next(event for event in events if event["type"] == "command_commit"
                                   and event["committedNs"] == result["episodes"][0]["neutralCommittedNs"])
                self.assertFalse(zero_commit["cleanup"])
                self.assertEqual(refresh[0]["lastCommandWrittenNs"], zero_commit["writtenNs"])
                self.assertLess(self.samples[2]["navigationObservation"]["inputObservedNs"],
                                zero_commit["writtenNs"])
                self.assertGreaterEqual(self.samples[3]["navigationObservation"]["inputObservedNs"],
                                        zero_commit["writtenNs"])
                self.assertTrue(all(request["actionDeadlineNs"] is None and not request["pendingCommand"]
                                    and request["sticks"] == [0, 0, 0, 0] for request in self.snapshot_requests))
                self.assertTrue(all(request["startedNs"] > zero_commit["committedNs"]
                                    for request in self.snapshot_requests[2:]))
                self.assertEqual(sum(event["type"] == "command_request" for event in events), 3)

    def test_recorded_source_can_expire_after_publication_without_new_diagnostic_authority(self):
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
        self.assertEqual(len(self.snapshot_requests), 2)
        self.assertIsNone(self.driver.pending_nonce)
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

    def test_owned_normal_zero_never_starts_a_diagnostic_refresh(self):
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
        self.assertIsNone(self.driver.snapshot_refresh_started)
        self.assertIsNone(self.driver.pending_nonce)
        self.assertEqual(len(self.snapshot_requests), 2)
        self.assertLess(result["episodes"][0]["neutralCommittedNs"] - result["episodes"][0]["startedNs"],
                        400_000_000)
        events = [json.loads(line) for line in
                  (Path(result["artifactDirectory"]) / "events.jsonl").read_text().splitlines()]
        self.assertFalse(any(event["type"] == "snapshot_refresh_hold" for event in events))
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
        self.rows = [dict(left=[0, .3], right=[0, 0], holdMs=1000)]
        self.response_delay = lambda driver: 120_000_000 if driver.snapshot_index >= 3 else 0
        self.snapshot_mutation = lambda sample, driver: mutation(sample) if driver.snapshot_index >= 3 else None
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn(message, result["error"])
        self.assertEqual(len(self.calls), 3)
        self.assertEqual(len(result["episodes"]), 1)
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
        self.assertEqual(len(self.calls), 3)
        self.assertEqual(len(result["episodes"]), 1)
        self.assertFalse(result["normalStopRequired"])

    def test_source_expiry_fails_closed_without_restamping(self):
        self.rows.append(dict(left=[.3, 0], right=[0, 0], holdMs=400))
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
        self.rows.append(dict(left=[.3, 0], right=[0, 0], holdMs=400))
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
        def mutation(state, driver):
            if self.calls:
                self.write(self.run / "live-driver-owner.json", dict(host=self.host, pending=False, foreign=True))
        self.state_mutation = mutation
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
        # Neutral-boundary requests retain every original response at the same
        # per-response, decoded aggregate and shared disk limits.
        self.rows = [dict(left=[0, .25], right=[0, 0], holdMs=500) for _ in range(4)]
        self.snapshot_mutation = lambda s, driver: s.update(observationPadding="x"*48_000)
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        paths = sorted(Path(result["artifactDirectory"]).glob("snapshot-*.json.gz"))
        restored = [gzip.decompress(path.read_bytes()) for path in paths]
        self.assertEqual(len(restored), 5)
        self.assertGreater(sum(map(len, restored)), 4*sum(path.stat().st_size for path in paths))
        self.assertLess(sum(path.stat().st_size for path in Path(result["artifactDirectory"]).iterdir()), self.m.t.MAX_BYTES)
        self.assertEqual([json.loads(raw) for raw in restored], self.samples)
        self.assertEqual(len(self.calls), 8)


if __name__ == "__main__":
    unittest.main()
