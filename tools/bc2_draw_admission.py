"""Pure source-side ordinary-draw admission; no transport, clocks or commands.

Call ``begin`` before Prime and carry its returned Session through draw. Every
Prime attempt and additional readiness request consumes the SAME observation
budget; ordinary motion observations do not. ``start_draw`` attaches the existing
absolute draw deadline once.
``assess`` runs only on a complete available Held source at Prime/baseline or a
neutral stow_reach boundary. Empty, pressed transitions and target withdrawal
remain under their existing, different contracts.

The required pure ``full_admission(snapshot, now_ns)`` callback must perform
80's original frame/holster proof and 94's complete mapping/selected proof,
returning 94's resolved binding. Use resolve_slots directly, without changing
auto/block/evaluation or calling require_held first. Request artifacts, process
ownership and command-file ownership remain the transport's mandatory checks.
For EXACT display_missing during unknown eligibility, ``observe_admission``
must instead run original80 full current frame/holster proof and94 selected_matches
against the retained Ready source. The old mapping is HISTORY only. Ready always
requires full_admission and a newly complete unchanged current mapping.

ObserveOnly means UNKNOWN eligibility, never an inferred idle/Busy reason. It
allows no command, replay, lease extension or permission override. The caller
keeps the last acknowledged neutral pose, clears command-source authority,
retains the result record, and requests wholly new evidence within the SAME
bounds. Before issuing that request call check_next_observation. A Ready result
still requires ordinary publication-time validation against original leases.
"""
from dataclasses import dataclass, replace
from enum import Enum
import json
import math


NS = 1_000_000_000
MAX_OBSERVATIONS = 20
ADMISSION_NS = 30 * NS
DRAW_NS = 10 * NS
HOLD_NS = 500_000_000
MAX_REPLACEMENTS = 2
REQUEST_NS = 250_000_000
REFERENCE = ("spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter",
             "floorRelative", "referenceHead", "head")
SELECTION = ("configurationPath", "configurationPathPointer", "inventory",
             "selectedSlot", "soleConfiguredArray", "matchedMeshPath", "meshes")
HOST = ("pid", "runtime", "head", "handL", "handR", "controls", "captureSeq")
UNKNOWN = ("NotEvaluated/auto=false does not identify the suspension cause",
           "source interaction/cancel/reloadBusy are not published",
           "inventory request/visibility/ordinary command/suppression are not fully published")


class Status(str, Enum):
    READY = "Ready"
    OBSERVE_ONLY = "ObserveOnly"
    REFUSE = "Refuse"


class Refusal(ValueError):
    pass


def require(value, message):
    if not value:
        raise Refusal(message)


def integer(value):
    require(isinstance(value, (int, str)) and not isinstance(value, bool), "integer evidence required")
    try:
        return int(value, 16 if value.startswith("0x") else 10) if isinstance(value, str) else value
    except ValueError as error:
        raise Refusal("invalid integer evidence") from error


def nonce_integer(value):
    # Native snapshot JSON serializes the uint64 nonce as a decimal string.
    return integer(value)


def freeze(value):
    """Immutable exact JSON record; caller mutation cannot rewrite history."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


@dataclass(frozen=True)
class Session:
    admission_started_ns: int
    admission_deadline_ns: int
    draw_started_ns: int = 0
    draw_deadline_ns: int = 0
    observations_used: int = 0
    last_now_ns: int = 0
    last_nonce: int = 0
    last_observed_ns: int = 0
    identity: str = ""
    mapping_identity: str = ""
    previous_evidence: str = ""
    hold_started_ns: int = 0
    hold_replacements: int = 0
    hold_ack: str = ""
    uncertain_records: tuple = ()
    refusal: str = ""


@dataclass(frozen=True)
class Context:
    # Transport supplies the original matching request, not a new clock origin.
    nonce: int
    request_started_ns: int
    request_published_ns: int
    response_deadline_ns: int
    observations_used: int
    phase: str
    last_write_ns: int
    last_commit_ns: int
    command_identity: tuple
    current_command_identity: tuple
    acknowledged_state: dict
    current_state: dict
    acknowledged_pose: dict
    pending_command: bool = False
    grip_action_started: bool = False
    draw_armed: bool = False
    # Only explicit observed blockers belong here. Missing native causes must
    # stay UNKNOWN, never be converted to a fabricated False/idle assertion.
    explicit_blockers: tuple = ()
    # The adapter shares these with pre-existing92 missing/stale holds. A
    # change of publication reason must never create a fresh hold budget.
    hold_started_ns: int = 0
    hold_replacements: int = 0


@dataclass(frozen=True)
class Result:
    status: Status
    reason: str
    session: Session
    observation: str
    binding: str = ""
    uncertainties: tuple = ()
    commands: tuple = ()


def begin(started_ns, deadline_ns):
    require(0 < integer(started_ns) < integer(deadline_ns)
            <= started_ns + ADMISSION_NS, "invalid original admission bounds")
    return Session(started_ns, deadline_ns)


def _bounds(session, now_ns):
    require(not session.refusal, "session already refused: " + session.refusal)
    require(session.admission_started_ns <= integer(now_ns)
            < session.admission_deadline_ns, "original 30s admission deadline expired")
    require(now_ns >= session.last_now_ns, "evaluation clock regressed")
    if session.draw_started_ns:
        require(session.draw_started_ns <= now_ns < session.draw_deadline_ns,
                "original 10s draw deadline expired")
    if session.hold_started_ns:
        require(now_ns - session.hold_started_ns < HOLD_NS,
                "original 500ms observation hold expired")


def start_draw(session, started_ns, deadline_ns):
    _bounds(session, started_ns)
    require(not session.draw_started_ns, "draw clock cannot restart")
    require(integer(started_ns) < integer(deadline_ns) <= started_ns + DRAW_NS,
            "invalid original draw bounds")
    return replace(session, draw_started_ns=started_ns, draw_deadline_ns=deadline_ns)


def check_next_observation(session, now_ns, observations_used=None):
    """Preflight BEFORE requesting a replacement; does not reserve or reset it."""
    _bounds(session, now_ns)
    used = session.observations_used if observations_used is None else integer(observations_used)
    require(session.observations_used <= used < MAX_OBSERVATIONS,
            "shared 20-observation admission budget exhausted")
    require(not session.hold_started_ns or session.hold_replacements + used - session.observations_used < MAX_REPLACEMENTS,
            "two replacement observation requests exhausted")


def _neutral(inp):
    require(inp["focused"] is True and inp["headValid"] is True,
            "input focus/head lost")
    require(len(inp["hands"]) == 2, "two tracked hands required")
    for hand in inp["hands"]:
        require(hand["gripTracked"] is True and hand["aimTracked"] is True,
                "hand tracking lost")
        require(integer(hand["held"]) == 0 and type(hand["squeeze"]) in (int, float)
                and math.isfinite(hand["squeeze"]) and 0 <= hand["squeeze"] <= .01
                and hand["trigger"] == 0 and hand["stickX"] == hand["stickY"] == 0,
                "consumed controls are not neutral")


def _ack(context, snapshot, now_ns):
    require(context.phase in ("prime", "baseline", "stow_reach"),
            "not a neutral source-side admission boundary")
    require(context.pending_command is False and context.grip_action_started is False
            and context.draw_armed is False, "pending command or grip action already started")
    require(not context.explicit_blockers, "explicit blocker: " + repr(context.explicit_blockers))
    require(context.command_identity and context.command_identity == context.current_command_identity,
            "acknowledged command ownership changed")
    require(0 <= integer(context.last_write_ns) <= integer(context.last_commit_ns) <= now_ns,
            "command acknowledgement clocks invalid")
    # run_holster enters stow_reach before its first command. At that boundary
    # the already owned initial host ACK is evidence; 0/0 stays 0/0.
    host = context.current_state
    require(all(context.acknowledged_state[key] == host[key] for key in HOST),
            "last acknowledged pose/controls/owner changed")
    require(host["sessionRunning"] is True and host["actionsAttached"] is True
            and host["sessionState"] == "FOCUSED" and host["paceMode"] == "free"
            and host["runtime"] == "xr-sim" and integer(host["errors"]) == 0
            and host["lastCmdError"] == ""
            and all(host[key]["valid"] is True for key in ("head", "handL", "handR")),
            "current simulator focus/tracking/session lost")
    require(all(host["controls"][key] == 0 for key in ("gripL", "gripR", "trigL", "trigR"))
            and all(host["controls"][key] == [0, 0] for key in ("stickL", "stickR"))
            and all(host["controls"][key] is False for key in (
                "a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR")),
            "acknowledged simulator controls are not neutral")
    expected = context.acknowledged_pose
    for key in ("grip", "aim"):
        actual = snapshot["input"]["hands"][1][key]
        require(math.dist(actual["position"], expected["position"]) < .002,
                "consumed right pose differs from acknowledged pose")
        a, b = actual["orientation"], expected["orientation"]
        require(len(a) == len(b) == 4 and all(math.isfinite(v) for v in (*a, *b)),
                "invalid acknowledged orientation")
        na, nb = math.sqrt(sum(v*v for v in a)), math.sqrt(sum(v*v for v in b))
        require(abs(na-1) < .001 and abs(nb-1) < .001, "invalid orientation norm")
        angle = 2 * math.acos(min(1., abs(sum(x*y for x, y in zip(a, b))) / (na*nb)))
        require(angle < .005, "consumed right orientation differs from acknowledged pose")
    return freeze(dict(command=context.command_identity, written=context.last_write_ns,
                       committed=context.last_commit_ns,
                       host={key: host[key] for key in HOST}, pose=expected))


def _evidence(snapshot, now_ns, last_write_ns):
    h, outer = snapshot["bodyHolster"], snapshot["input_evidence"]
    body = h["inputEvidence"]
    samples = dict(outer=outer, body=body)
    if snapshot["bodyInventory"]["available"] is True:
        samples["mapping"] = snapshot["bodyInventory"]["inputEvidence"]
    for name, sample in samples.items():
        maximum = 100_000_000 if name == "outer" else 150_000_000
        require(0 < integer(sample["observedNs"]) <= integer(sample["nowNs"]) <= now_ns
                < integer(sample["deadlineNs"])
                and 0 < sample["deadlineNs"] - sample["observedNs"] <= maximum,
                "original input evidence stale or extended")
        require(sample["observedNs"] >= last_write_ns and integer(sample["sequence"]) > 0
                and sample["focused"] is True and sample["tracked"] == [True, True],
                "original input predates command or lost focus/tracking")
    claim = h["rightClaim"]
    require(0 < integer(claim["inputSequence"]) <= integer(body["sequence"])
            and now_ns < integer(claim["deadlineNs"]) <= integer(body["deadlineNs"]),
            "current GunHold claim expired or extended")
    return dict(samples, claim=dict(sequence=claim["inputSequence"], deadlineNs=claim["deadlineNs"]))


def _mapping_identity(snapshot, binding):
    d = snapshot["bodyInventory"]
    rows = []
    for row in d["slots"]:
        c = row["configuration"]
        rows.append(dict(assignment=row["assignment"], native=row["native"],
                         configuration={key: c[key] for key in (
                             "asset", "configurationPath", "configurationPathPointer", "soleConfiguredArray")}))
    rows.sort(key=lambda row: row["assignment"]["slot"])
    return freeze(dict(binding=binding, count=d["count"], nativeCount=d["nativeCount"], rows=rows))


def _identity(snapshot):
    h = snapshot["bodyHolster"]
    return freeze(dict(outer={key: snapshot[key] for key in (
        "owner", "equipment", "physical_equipment_generation", "rig")},
        selected={key: snapshot["selected"][key] for key in SELECTION},
        reference={key: snapshot["input"][key] for key in REFERENCE},
        left={key: snapshot["input"]["hands"][0][key] for key in (
            "grip", "aim", "held", "squeeze", "trigger", "stickX", "stickY")},
        physicalGun=h["physicalGun"], selectedSlot=h["selectedSlot"], token=h["rightClaim"]["token"],
        request=h["request"]))


def _continuity(previous, current):
    for name in ("outer", "body", "mapping", "claim"):
        if name not in previous or name not in current:
            continue
        a, b = previous[name], current[name]
        require(b["sequence"] >= a["sequence"], "original evidence sequence regressed: " + name)
        if name != "claim":
            require(b["observedNs"] >= a["observedNs"], "original evidence time regressed: " + name)
        if b["sequence"] == a["sequence"]:
            keys = ("deadlineNs",) if name == "claim" else (
                "owner", "observedNs", "deadlineNs", "focused", "tracked")
            require(all(a[key] == b[key] for key in keys),
                    "same-sequence evidence changed or restamped: " + name)


def assess(snapshot, now_ns, context, session, full_admission, observe_admission=None):
    """Return a decision and successor Session; never mutate input or issue I/O.

    observations_used is the total original Prime + extra readiness request count,
    including rejected/missing observations handled by existing Prime logic.
    Ordinary motion may leave this count unchanged. Every additional request in
    an unknown/missing/stale hold counts, even if handled by92. Refuse is terminal.
    """
    record = ""
    try:
        record = freeze(snapshot)
        _bounds(session, context.request_started_ns)
        _bounds(session, now_ns)
        used = integer(context.observations_used)
        require(0 < used <= MAX_OBSERVATIONS and session.observations_used <= used,
                "shared observation ordinal regressed or exceeded 20")
        nonce = nonce_integer(context.nonce)
        require(0 < nonce == nonce_integer(snapshot["nonce"])
                and nonce > session.last_nonce, "matching wholly new request required")
        require(session.last_observed_ns < integer(context.request_started_ns)
                <= integer(context.request_published_ns) <= integer(snapshot["observed_ns"]) <= now_ns
                < integer(context.response_deadline_ns)
                and context.response_deadline_ns == context.request_started_ns + REQUEST_NS,
                "original request/response clocks invalid or expired")
        hold_started = session.hold_started_ns
        if context.hold_started_ns:
            require(0 < integer(context.hold_started_ns) <= now_ns,
                    "invalid inherited observation hold clock")
            hold_started = min(hold_started or context.hold_started_ns, context.hold_started_ns)
        require(not hold_started or now_ns - hold_started < HOLD_NS, "original 500ms observation hold expired")
        replacements = session.hold_replacements
        if session.hold_started_ns:
            require(used > session.observations_used, "replacement observation not charged")
            replacements += used - session.observations_used
        replacements = max(replacements, integer(context.hold_replacements))
        require(0 <= replacements <= MAX_REPLACEMENTS, "two replacement observation requests exhausted")
        require(context.phase == "prime" or session.draw_started_ns > 0,
                "source draw boundary lacks original draw clock")
        h = snapshot["bodyHolster"]
        require(h["available"] is True, "complete current Held body evidence unavailable")
        require(integer(h["phase"]) == 1 and h["actualEmpty"] is False
                and h["emptyHands"] is False and h["leftClaim"] is None
                and h["rightClaim"] is not None, "exact Held/GunHold state unavailable")
        require(snapshot["weapon_actions_blocked"] is False and h["blockWeaponActions"] is False,
                "explicit weapon-action blocker")
        require(h["freeRightCurrent"] is False and h["freeRight"] is None,
                "free/visibility/suppression evidence present")
        # Current JSON omits these optional native facts. Absence stays UNKNOWN;
        # any explicitly supplied positive fact refuses both Ready and waiting.
        for name in ("pendingOperation", "pendingRequest", "visibility", "suppression", "ordinaryDrawCommand",
                     "cancel", "reloadBusy", "interactionSuspended"):
            require(not h.get(name), "explicit body blocker: " + name)
        _neutral(snapshot["input"])
        _neutral(h["input"])
        require(h["neutralTrackedInput"] is True, "body neutral evidence disagrees")
        ack = _ack(context, snapshot, now_ns)
        require(not session.hold_ack or session.hold_ack == ack,
                "acknowledged pose or command changed during observation hold")
        evaluation, automatic = integer(h["inventoryEvaluation"]), h["allowAutomaticGunHold"]
        ready = evaluation == 1 and automatic is True
        unknown = evaluation == 0 and automatic is False
        require(ready or unknown, "explicit unsupported/disabled or inconsistent inventory evaluation")
        d = snapshot["bodyInventory"]
        mapping_identity, binding = session.mapping_identity, None
        if d["available"] is True:
            require(callable(full_admission), "complete original admission callback required")
            binding = full_admission(snapshot, now_ns)
            require(isinstance(binding, dict) and binding, "complete exact mapping binding required")
            mapping_identity = _mapping_identity(snapshot, binding)
            require(not session.mapping_identity or mapping_identity == session.mapping_identity,
                    "source complete mapping changed")
        else:
            require(unknown and session.mapping_identity and d["available"] is False
                    and d["observationOnly"] is True and d["reason"] == "display_missing"
                    and snapshot["observed_ns"] <= integer(d["sampledNs"]) <= now_ns,
                    "current complete mapping unavailable outside exact unknown display_missing")
            require(callable(observe_admission), "current frame/holster/selected proof required for missing display")
            require(observe_admission(snapshot, now_ns) is None,
                    "observation callback must return no binding or authority")
        require(freeze(snapshot) == record, "admission callback mutated original evidence")
        current_evidence = _evidence(snapshot, now_ns, context.last_write_ns)
        identity = _identity(snapshot)
        require(not session.identity or session.identity == identity,
                "source owner/item/mapping/current GunHold token changed")
        if session.previous_evidence:
            _continuity(json.loads(session.previous_evidence), current_evidence)
        if session.previous_evidence and "mapping" not in current_evidence:
            current_evidence["mapping"] = json.loads(session.previous_evidence)["mapping"]
        current = replace(session, observations_used=used, last_now_ns=now_ns,
                          last_nonce=nonce, last_observed_ns=snapshot["observed_ns"],
                          identity=identity, mapping_identity=mapping_identity,
                          previous_evidence=freeze(current_evidence))
        if ready:
            return Result(Status.READY, "fresh complete unchanged Held source", replace(
                current, hold_started_ns=0, hold_replacements=0, hold_ack=""), record, freeze(binding))
        require(context.phase == "stow_reach" and session.identity,
                "unknown eligibility requires prior Ready neutral stow_reach source")
        require(used < MAX_OBSERVATIONS and replacements < MAX_REPLACEMENTS,
                "unknown eligibility exhausted original observation budget")
        current = replace(current, hold_started_ns=hold_started or now_ns,
                          hold_replacements=replacements, hold_ack=ack,
                          uncertain_records=session.uncertain_records + (record,))
        return Result(Status.OBSERVE_ONLY, "eligibility unknown; zero commands", current, record,
                      uncertainties=UNKNOWN + (("current complete mapping UNKNOWN: display_missing",)
                                               if d["available"] is False else ()))
    except Exception as error:
        reason = str(error) or type(error).__name__
        return Result(Status.REFUSE, reason, replace(session, refusal=reason), record,
                      uncertainties=UNKNOWN if snapshot.get("bodyHolster", {}).get("inventoryEvaluation") == 0 else ())
