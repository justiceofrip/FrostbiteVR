"""ROOT-RUN-ONLY single-round fixed-container adapter for an admitted XR session.

--run EXACT_RUN --stage Reload performs the observed opening (if needed), actual
reserve pickup, continuous insertion, receipt, and closure. No launch, firing,
capture, native writes, imported custody or independently owned command channel.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


shared = load("fixed_shared_pump", "drive_bc2_pump_simulation.py")
transport = shared.transport
planner = load("fixed_live_planner", "plan_bc2_fixed_container_simulation.py")
require, integer, NS = transport.require, transport.integer, transport.NS


class FixedContainerDriver(shared.PumpDriver):
    """Reuse pump polling/sending and the ONE feed transport lock/owner/expiry."""
    def __init__(self, args):
        self.requested_stage = args.stage
        self.active_plan, self.demand_reference = None, None
        self.demand_started, self.demand_finished = None, False
        self.gate_receipts, self.release_authorized = [], False
        self.acquired_claim = None
        args.stage, args.profiles, args.capture, args.menu_batches, args.pickup_first = None, None, None, None, False
        transport.Driver.__init__(self, args)
        require(self.link["mechanism_snapshots"]["requested"] is True
                and transport.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                "session did not request mechanism snapshots before native startup")
        self.artifacts.save("fixed-container-invocation.json", dict(stage=self.requested_stage, stepMs=args.step_ms,
            driverSha256=transport.digest(Path(__file__).read_bytes()), plannerSha256=transport.digest(Path(planner.__file__).read_bytes()),
            sharedPumpSha256=transport.digest(Path(shared.__file__).read_bytes()), transportSha256=transport.digest(Path(transport.__file__).read_bytes()),
            nativeAcceptance=False, headsetAcceptance=False))

    def admit_snapshot(self, snapshot, now, nonce):
        x = snapshot.get("fixedContainer")
        if isinstance(x, dict) and x.get("available") is False:
            require(self.active_plan is None and self.commands == 0 and not self.demand_finished
                    and self.demand_started is not None and time.perf_counter_ns()-self.demand_started < NS
                    and x.get("reason") == "capture_pending_or_not_requested" and snapshot.get("feed") is None,
                    "fixed-container unavailable outside initial bounded capture demand")
            def pending(original, evaluated):
                raise shared.CapturePending("initial fixed-container capture pending")
            try:
                planner.frame.admit_frame(snapshot, now, nonce, mechanism_admission=pending)
            except shared.CapturePending:
                pass
            if self.demand_reference is None:
                self.demand_reference = snapshot
            else:
                self.stable_initial_observation(self.demand_reference, snapshot)
            raise shared.CapturePending("initial fixed-container capture pending")
        return planner.admit(snapshot, now, nonce)

    def stable_initial_observation(self, before, after):
        self.stable_snapshot(before, after)

    def stable_snapshot(self, before, after):
        transport.stable_snapshot(before, after)
        planner.require_preserved(planner.preserved(before), planner.preserved(after))
        if before.get("fixedContainer", {}).get("available") is True:
            require(planner.selection(before) == planner.selection(after), "exact fixed-container profile changed")

    def snapshot_source_deadline(self, snapshot):
        deadlines = [super().snapshot_source_deadline(snapshot)]
        x = snapshot.get("fixedContainer")
        if isinstance(x, dict) and x.get("available") is True:
            # The same original leases already bound the fixed-container plan.
            # In particular, a current-at-publication family can expire before
            # delivery while the newer frame/input leases remain current.
            deadlines.extend(integer(value) for value in (
                snapshot["native_deadline_ns"], x["input"]["deadlineNs"],
                x["raw"]["input"]["deadlineNs"], x["view"]["snapshot"]["deadlineNs"],
                x["view"]["binding"]["deadlineNs"], x["tracking"]["gun"]["deadlineNs"]))
            if x.get("physicalFamily") is not None:
                deadlines.append(integer(x["physicalFamily"]["deadlineNs"]))
            if x["tracking"]["hand"] is not None:
                deadlines.append(integer(x["tracking"]["hand"]["deadlineNs"]))
        return min(deadlines)

    def current_source(self):
        if self.latest_snapshot is None:
            self.fresh_available()
        self.monitor = True
        # Hold the committed pose while the shared transport requests and
        # admits a replacement; retain its request/refresh/stage bounds.
        for _ in range(800):
            self.pump()
            if self.latest_snapshot is not None and time.perf_counter_ns() < self.snapshot_source_deadline(self.latest_snapshot):
                return
            time.sleep(.001)
        raise transport.REFUSAL("original fixed-container source expired")

    def accept_snapshot(self, snapshot, raw, now):
        transport.Driver.accept_snapshot(self, snapshot, raw, now)
        if self.demand_reference is not None:
            self.stable_snapshot(self.demand_reference, snapshot)
        if self.active_plan is not None:
            b, x = self.active_plan["baseline"], snapshot["fixedContainer"]
            require(x["tracking"]["lifetime"] == b["lifetime"] and x["supply"] == b["supply"]
                    and planner.binding_key(x["view"]["binding"]) == b["binding"]
                    and x["view"]["identity"] == b["nativeIdentity"], "retained native loader lifetime changed")
            pending = x["pendingRequest"]
            if pending is not None:
                require(self.active_plan["stage"] == "Insert"
                        and pending["preparedCommand"]["before"] == b["counts"]
                        and integer(pending["intent"]["id"]) > integer(b["lastCompletedRequest"]),
                        "unrelated pending transaction during fixed-container motion")

    def validate_publication(self, lines, expected, cleanup):
        shared.ordinary_left(lines)
        if cleanup:
            require(lines == ["grip l 0"], "cleanup permits only owned ordinary left release")
            return
        require(self.demand_finished and self.active_plan is not None and self.latest_snapshot is not None,
                "unadmitted observation cannot authorize input")
        current, now = self.latest_snapshot, time.perf_counter_ns()
        require(now < self.snapshot_source_deadline(current), "original source expired before publication")
        self.admit_snapshot(current, now, integer(current["nonce"]))
        planner.ammo(current, now, idle=True)
        self.stable_snapshot(self.reference, current)
        if self.release_authorized:
            require(lines == ["grip l 0"] and self.gate_receipts
                    and self.gate_receipts[-1]["gate"] == "fixed_step_complete", "ordinary release lacks actual physical completion")
            planner.check_gate(self.active_plan, current, "fixed_step_complete", now, integer(current["nonce"]))
        else:
            require(expected is not None and expected in self.active_plan["schedule"] and lines == expected["commands"],
                    "command differs from admitted fixed-container sample")
            if self.active_plan["stage"] == "Insert":
                planner.held(current, now, 5)
                require(current["fixedContainer"]["tracking"]["hand"]["token"] == self.active_plan["baseline"]["claim"],
                        "insertion lost original acquired custody")
                if expected["grip"] == 0:
                    require(self.gate_receipts and self.gate_receipts[-1]["gate"] == "fixed_aligned"
                            and self.gate_receipts[-1]["heldObservationSpanNs"] >= integer(self.active_plan["policy"]["endpointDwellNs"]),
                            "insertion release lacks actual continuously held endpoint dwell")
                    planner.check_gate(self.active_plan, current, "fixed_aligned", now, integer(current["nonce"]))
            elif expected["phase"] == "move":
                token = planner.held(current, now, 4)
                require(self.acquired_claim is not None and token == self.acquired_claim,
                        "mechanical motion lost the exact observed acquisition claim")

    def wait_gate(self, plan, gate, target):
        require(gate["stop"] is True and gate["requiresFreshSnapshot"] is True
                and gate["countsTowardGestureBudget"] is True and gate["requireContinuousFreshInput"] is True
                and gate["holdGrip"] == target["grip"], "invalid mandatory fixed-container gate")
        until = min(self.deadline, self.stage_deadline, time.perf_counter_ns()+integer(gate["timeoutNs"]))
        commit, first, last_error = self.last_commit, None, "no post-command observation"
        for _ in range(32):
            require(time.perf_counter_ns() < until, "fixed-container gate timed out: "+gate["id"]+": "+last_error)
            state = self.read_state()
            self.ownership(state)
            self.check_target(state, target)
            previous_deadline, self.stage_deadline = self.stage_deadline, until
            try:
                snapshot, _, _ = self.fresh_available()
                now = time.perf_counter_ns()
                require(now < until and integer(snapshot["observed_ns"]) > commit,
                        "gate must observe original native state after endpoint command commit")
                require(now-integer(snapshot["input_evidence"]["observedNs"]) < integer(gate["maxInputGapNs"]),
                        "gate input exceeds native maximum input gap")
                try:
                    receipt = planner.check_gate(plan, snapshot, gate["id"], now, integer(snapshot["nonce"]))
                    planner.require(integer(receipt["inputObservedNs"]) > commit, "gate raw input predates endpoint command commit")
                except planner.Refusal as error:
                    first, last_error = None, str(error)
                    self.artifacts.event(dict(type="fixed_gate_pending", gate=gate["id"], nonce=snapshot["nonce"], error=last_error))
                    continue
                observed = integer(receipt["inputObservedNs"])
                if first is None:
                    first = observed
                span = observed-first
                self.artifacts.event(dict(type="fixed_gate_sample", heldObservationSpanNs=span, **receipt))
                if span < integer(gate["minimumObservationSpanNs"]):
                    continue
                receipt["heldObservationSpanNs"] = span
                if gate["id"] == "fixed_acquired":
                    self.acquired_claim = receipt["claimToken"]
                self.gate_receipts.append(receipt)
                return receipt
            finally:
                self.stage_deadline, self.monitor = previous_deadline, True
        raise transport.REFUSAL("fixed-container gate iteration bound: "+gate["id"]+": "+last_error)

    def stage(self, name, pickup=None, initial_snapshot=None):
        if initial_snapshot is None:
            snapshot, _, _ = self.fresh_available()
        else:
            # Reload already obtained an admitted source to choose its first
            # stage. Plan from that exact source while its original leases are
            # current; requesting again can discard readiness before any input.
            require(self.active_plan is None and self.commands == 0 and self.demand_finished
                    and not self.held_left and not self.pending_command
                    and initial_snapshot is self.latest_snapshot,
                    "initial stage source is not the untouched admitted readiness observation")
            snapshot = initial_snapshot
        plan = planner.plan(snapshot, name, time.perf_counter_ns(), integer(snapshot["nonce"]), self.args.step_ms*1_000_000, pickup)
        require(time.perf_counter_ns() < min(integer(plan["sourceValidUntilNs"]), self.snapshot_source_deadline(snapshot)),
                "planning consumed original source lease")
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        self.reference, self.active_plan, self.release_authorized = snapshot, plan, False
        self.acquired_claim = None
        schedule = plan["schedule"]
        maximum = integer(plan["policy"]["maximumGestureNs"])
        require(0 < integer(plan["durationNs"]) < maximum <= 10*NS
                and self.commands+sum(len(row["commands"])+1 for row in schedule)+2 <= transport.MAX_COMMANDS,
                "fixed-container plan and ordinary release exceed shared budget")
        gates = {integer(g["afterSample"]): g for g in plan["receiptGates"]}
        require(len(gates) == len(plan["receiptGates"]) and max(gates) == len(schedule)-1,
                "fixed-container schedule lacks unique final receipt gate")
        self.artifacts.save(f"fixed-container-plan-{len(self.plans)+1:02}-{name}.json", plan)
        self.plans.append(dict(stage=name, nonce=snapshot["nonce"], samples=len(schedule), nominalDurationNs=plan["durationNs"]))
        start = time.perf_counter_ns()
        self.motion_started, self.stage_deadline, self.monitor = start, min(self.deadline, start+maximum), True
        previous_at = -1
        for index, target in enumerate(schedule):
            at = integer(target["atNs"])
            require(previous_at < at <= integer(plan["durationNs"]), "nonmonotonic fixed-container schedule")
            due = start+at if index == 0 else max(start+at, self.last_commit+at-previous_at)
            self.pause_until(due)
            self.send(target["commands"], expected=target,
                      source_deadline=integer(plan["sourceValidUntilNs"]) if index == 0 else None)
            previous_at = at
            if index in gates:
                self.wait_gate(plan, gates[index], target)
        if name in ("Open", "Close"):
            self.release_authorized = True
            self.send(["grip l 0"])
        self.plans[-1].update(actualDurationNs=time.perf_counter_ns()-start, ordinaryLeftReleaseAcknowledged=name != "Pickup")
        self.motion_started, self.stage_deadline = None, self.deadline
        return plan

    def reload(self):
        snapshot, _, _ = self.fresh_available()
        step = integer(snapshot["fixedContainer"]["tracking"]["step"])
        require(step in (0, 2), "reload starts only from actual unopened/insert-ready empty loader")
        if step == 0:
            self.stage("Open", initial_snapshot=snapshot)
        pickup = self.stage("Pickup", initial_snapshot=snapshot if step == 2 else None)
        self.stage("Insert", pickup)
        if integer(self.latest_snapshot["fixedContainer"]["profile"]["kind"]) == 3:
            self.stage("Close")


class SelectedAmmoDriver(FixedContainerDriver):
    """Observe selected ammunition before the optional single ordinary shot.

    The caller supplies its exact, fresh-run primary/secondary family assertion.
    Full loader tracking is admitted by the caller only after observing empty.
    Attachment-dependent measurements explicitly opt in before the single shot;
    ordinary selected-ammo observers and other fixed launchers do not need sights.
    """
    require_shot_attachment_ready = False

    def assert_selected_family(self, snapshot):
        raise transport.REFUSAL("selected ammunition requires an exact family assertion")

    def admit_selected_frame(self, snapshot, now, nonce):
        planner.frame.admit_frame(snapshot, now, nonce, mechanism_admission=planner.blocked_observation)
        self.assert_selected_family(snapshot)
        require(all(h['trigger'] == h['stickX'] == h['stickY'] == h['held'] == 0
                    for h in snapshot['input']['hands'])
                and snapshot['input']['hands'][0]['squeeze'] <= .35,
                "Selected ammunition decision needs neutral controls/free left hand")

    def admit_selected_observation_identity(self, snapshot):
        x = snapshot['fixedContainer']
        require(isinstance(x, dict) and x.get('schema') == planner.OBSERVATION_SCHEMA
                and x.get('available') is True and x.get('observationOnly') is True,
                "Selected fixed ammunition observation unavailable")
        require(x['owner'] == snapshot['owner']
                and all(x['input'][k] == snapshot['input_evidence'][k] for k in
                        ('owner', 'sequence', 'observedNs', 'deadlineNs', 'focused', 'tracked', 'released')),
                "Selected ammunition observation owner/input changed")
        return x

    @staticmethod
    def selected_ammo_unpublished(snapshot):
        x = snapshot.get('fixedContainer')
        return (isinstance(x, dict) and x.get('schema') == planner.OBSERVATION_SCHEMA
                and ((x.get('available') is False
                      and x.get('reason') == 'capture_pending_or_not_requested'
                      and x.get('view') is None)
                     or (x.get('available') is True and 'view' in x and x['view'] is None)))

    def check_post_shot_observation_owner(self):
        require(self.active_plan is None and self.demand_finished and self.commands == 4
                and self.motion_started is None and not self.pending_command and not self.unsafe_owner
                and not (self.held_left or self.held_right or self.held_trigger or self.held_buttons)
                and self.last_commit is not None and self.last_commit >= self.last_write
                and self.pending_batch == ['trigger r 0', self.sentinel],
                'Selected ammunition observation requires one acknowledged ordinary shot/release')
        self.bounds(time.perf_counter_ns())
        state = self.read_state()
        self.ownership(state)
        require(integer(state['captureSeq']) == self.capture_sequence
                and all(state[k] == self.state[k] for k in ('head', 'handL', 'handR', 'controls')),
                'Selected ammunition observation input/capture changed')
        require(state['controls'] and all(
            all(part == 0 for part in value) if isinstance(value, list) else value == 0
            for value in state['controls'].values()), 'Selected ammunition observation requires neutral input')

    def request_snapshot(self):
        if getattr(self, 'post_shot_observing', False):
            self.check_post_shot_observation_owner()
            require(self.snapshot_index - self.post_shot_request_start < 1 + transport.MAX_SNAPSHOT_REFRESHES,
                    'Selected ammunition observation refresh request budget exhausted')
        return super().request_snapshot()

    def defer_invalid_snapshot(self, snapshot, now):
        if not (getattr(self, 'post_shot_observing', False)
                and snapshot.get('valid') is False and snapshot.get('invalid_reason') == 'shot_expired'):
            return False
        # Transport has retained this exact matching response and checked its
        # writer/failure/response deadline. Its expired native publication has
        # no frame or ammunition authority; only a new full admission can resume.
        self.check_post_shot_observation_owner()
        require(snapshot.get('schema') == 'fvr.bc2.live-mechanism-snapshot.v1'
                and self.pending_nonce is not None and integer(snapshot['nonce']) == self.pending_nonce
                and self.last_request <= integer(snapshot['observed_ns']) <= now
                and time.perf_counter_ns() - self.last_request <= transport.SNAPSHOT_TIMEOUT,
                'Invalid selected ammunition publication identity/time')
        self.artifacts.event(dict(type='selected_ammo_post_shot_expired', nonce=snapshot['nonce'],
            evaluatedNs=now, observationDeadlineNs=min(self.deadline, self.stage_deadline),
            refreshRequests=self.snapshot_index-self.post_shot_request_start,
            lastCommandWrittenNs=self.last_write, commandAuthority=False))
        self.pending_nonce = None
        self.request_snapshot()
        return True

    def check_post_shot_observation_tracking(self, x, now):
        # Tick returns default tracking when its native ammo view is absent.
        # This exact empty shape permits only the existing passive wait; it
        # never becomes latest_snapshot or supplies custody for an action.
        empty_key = dict(id=0, generation=0)
        empty_gun = dict(token=dict(id=0, owner=dict(actor=0, actorGeneration=0,
            equipGeneration=0, space=0), hand=0, kind=0, item=empty_key,
            contact=empty_key, prerequisiteClaim=0), deadlineNs=0, inputSequence=0)
        t = x['tracking']
        if x['view'] is None and t['gun'] == empty_gun:
            require(t == dict(enabled=False, current=False, blocksFire=False,
                    carried=False, pending=False, step=0, targetStep=0, lifetime=0,
                    revision=0, gun=empty_gun, hand=None, target=None, carriedSupply=None,
                    pouchContact=False, grip=False, gripPressed=False)
                    and x['leftClaim'] is None and x['input']['released'] == [True, False],
                    'Missing selected ammunition has nonempty fixed tracking')
            return
        root = planner.claim(t['gun'], x['input'], now, 1, 1)
        retained = self.reference['fixedContainer']['tracking']['gun']
        require(isinstance(retained, dict) and root == retained['token'],
                'Selected ammunition observation GunHold root changed')

    def fresh_available(self):
        if self.commands != 4 or not self.demand_finished:
            return super().fresh_available()
        require(not getattr(self, 'post_shot_observation_started', False),
                'Selected ammunition post-shot observation already attempted')
        self.check_post_shot_observation_owner()
        require(self.reference is not None and self.reference['fixedContainer']['view'] is not None
                and self.reference['fixedContainer']['view']['snapshot']['counts']['loaded'] == 1,
                'Selected ammunition observation lacks its retained loaded chamber')
        self.post_shot_observation_started = self.post_shot_observing = True
        self.post_shot_request_start = self.snapshot_index
        deadline = min(self.deadline, self.stage_deadline)
        # Retain the exact pre-shot binding; an absent publication is not a new
        # identity and never becomes a source for input. No capture clock resets.
        self.latest_snapshot = None
        try:
            for _ in range(1 + transport.MAX_SNAPSHOT_REFRESHES):
                self.check_post_shot_observation_owner()
                snapshot, raw, now = self.fresh_postrelease_observation(deadline)
                self.check_post_shot_observation_owner()
                self.stable_snapshot(self.reference, snapshot)
                require(snapshot['weapon_actions_blocked'] is False and snapshot.get('feed') is None,
                        'Selected ammunition observation has a competing weapon interlock')
                now = time.perf_counter_ns()
                require(now < deadline and now-self.last_request <= transport.SNAPSHOT_TIMEOUT
                        and now < self.snapshot_source_deadline(snapshot),
                        'Selected ammunition observation consumed original response/source deadline')
                self.admit_selected_frame(snapshot, now, integer(snapshot['nonce']))
                require(integer(snapshot['input_evidence']['observedNs']) >= self.last_write
                        and integer(snapshot['shot_input_generation']) > integer(self.reference['shot_input_generation'])
                        and all(h['squeeze'] == h['trigger'] == h['held'] == h['stickX'] == h['stickY'] == 0
                                for h in snapshot['input']['hands']),
                        'Selected ammunition observation contains preceding or active input')
                x = snapshot['fixedContainer']
                if x.get('available') is True:
                    self.admit_selected_observation_identity(snapshot)
                    self.check_post_shot_observation_tracking(x, now)
                if self.selected_ammo_unpublished(snapshot):
                    if x['available'] is True:
                        require(x['viewFresh'] is False and x['tracking']['pending'] is False
                                and x['pendingRequest'] is None and x['tracking']['blocksFire'] is False,
                                'Missing selected ammunition contradicts published state')
                    self.artifacts.event(dict(type='selected_ammo_post_shot_unpublished', nonce=snapshot['nonce'],
                        evaluatedNs=now, observationDeadlineNs=deadline,
                        refreshRequests=self.snapshot_index-self.post_shot_request_start,
                        lastCommandWrittenNs=self.last_write, commandAuthority=False))
                    continue
                # Every original current frame/ammo/native identity gate runs
                # before the replacement is accepted; absence grants no waiver.
                self.accept_snapshot(snapshot, raw, now)
                return snapshot, raw, now
            raise transport.REFUSAL('Selected ammunition observation refresh request budget exhausted')
        finally:
            self.post_shot_observing = False


    def require_initial_observation(self):
        require(self.reference is None and self.latest_snapshot is None
                and self.active_plan is None and not self.demand_finished and self.commands == 0
                and not self.pending_command and not self.unsafe_owner
                and not (self.held_left or self.held_right or self.held_trigger or self.held_buttons)
                and self.motion_started is None and self.demand_started is not None
                and time.perf_counter_ns()-self.demand_started < NS,
                'Selected ammunition initial observation already has action authority or expired')

    def stable_initial_observation(self, before, after):
        self.require_initial_observation()
        require(self.selected_ammo_unpublished(before),
                'Selected ammunition diagnostic reference already contains ammunition')
        transport.stable_snapshot_identity(before, after)
        require(all(before['input']['hands'][0][k] == after['input']['hands'][0][k]
                    for k in ('grip', 'aim')),
                'Selected ammunition passive left-controller input changed')
        self.stable_selected_identity(before, after)
        require(all(e <= cap for e, cap in zip(planner.frame.pose_error(
                    planner.frame.rigid(before['mapping']['anatomicalWristToGrip']),
                    planner.frame.rigid(after['mapping']['anatomicalWristToGrip'])), (.002, .005))),
                'Selected ammunition anatomical calibration changed')
        # No action-space inverse has been activated while ammo is absent.
        # Validate THIS original packet's map; never splice an old W/B/F into it.
        frame, mapping, inp = planner.frame, after['mapping'], after['input']
        world, body, wrist, calibration = [frame.rigid(mapping[k]) for k in
            ('weaponWorldMeters', 'trackingBodyWorldMeters', 'rawLeftWristWorldMeters', 'anatomicalWristToGrip')]
        grip = frame.multiply(frame.from_pose(inp['hands'][0]['grip']),
                              frame.inverse(frame.from_pose(inp['referenceHead'])))
        forward = frame.multiply(frame.multiply(calibration, grip), body)
        require(all(e <= cap for e, cap in zip(frame.pose_error(forward, wrist), (.002, .005))),
                'Selected ammunition original anatomical mapping does not round trip')

    def accept_snapshot(self, snapshot, raw, now):
        require(self.active_plan is None, 'Selected ammunition observer cannot own a loader action plan')
        initial = self.reference is None
        if initial:
            self.require_initial_observation()
        # FixedContainerDriver compares every packet with its initial action
        # geometry. This observer has only diagnostic identity until a complete,
        # current ammunition view passes the shared transport and ammo guards.
        transport.Driver.accept_snapshot(self, snapshot, raw, now)
        if initial:
            self.reference = snapshot
            if self.demand_reference is not None:
                frame = planner.frame
                relative = [frame.multiply(s['mapping']['weaponWorldMeters'],
                            frame.inverse(s['mapping']['trackingBodyWorldMeters']))
                            for s in (self.demand_reference, snapshot)]
                distance, angle = frame.pose_error(*relative)
                if distance > .002 or angle > .005:
                    self.artifacts.event(dict(type='selected_ammo_action_reference_activated',
                        diagnosticNonce=self.demand_reference['nonce'], sourceNonce=snapshot['nonce'],
                        evaluatedNs=now, sourceDeadlineNs=self.snapshot_source_deadline(snapshot),
                        diagnosticDistanceMeters=distance, diagnosticAngleRadians=angle, commands=self.commands))


    def admit_snapshot(self, snapshot, now, nonce):
        self.admit_selected_frame(snapshot, now, nonce)
        x = snapshot.get('fixedContainer')
        if isinstance(x, dict) and x.get('available') is False:
            self.require_initial_observation()
            require(self.selected_ammo_unpublished(snapshot),
                    "Selected ammunition capture absence is not a pending publication")
            return super().admit_snapshot(snapshot, now, nonce)
        x = self.admit_selected_observation_identity(snapshot)
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        elif self.demand_reference is not None:
            self.stable_initial_observation(self.demand_reference, snapshot)
        if x['view'] is None:
            # This is absence, not permission to use another resource or to
            # repair an invalid view. Keep the original capture-demand clock;
            # PumpDriver saves/requests each response with the same transport.
            require(x['viewFresh'] is False and x['tracking']['pending'] is False
                    and x['pendingRequest'] is None and x['tracking']['blocksFire'] is False
                    and snapshot['weapon_actions_blocked'] is False and snapshot.get('feed') is None,
                    "Missing selected ammunition contradicts published state")
            require(self.active_plan is None and self.commands == 0 and not self.demand_finished
                    and not self.pending_command and not self.held_trigger and not self.held_left
                    and self.demand_started is not None
                    and time.perf_counter_ns()-self.demand_started < NS,
                    "selected ammunition unavailable outside initial bounded capture demand")
            if self.demand_reference is None:
                self.demand_reference = snapshot
            raise shared.CapturePending("initial selected ammunition publication pending")
        planner.ammo(snapshot, now, idle=True)
        return x

    def stable_snapshot(self, before, after):
        transport.stable_snapshot(before, after)
        self.stable_selected_identity(before, after)

    def stable_selected_identity(self, before, after):
        a, b = before['sight']['family'], after['sight']['family']
        require({k: v for k, v in a.items() if k != 'observedMs'}
                == {k: v for k, v in b.items() if k != 'observedMs'}
                and b['observedMs'] >= a['observedMs'], "Selected ammunition physical family changed")
        a, b = before.get('fixedContainer', {}).get('view'), after.get('fixedContainer', {}).get('view')
        if a is not None:
            if (getattr(self, 'post_shot_observing', False)
                    and self.selected_ammo_unpublished(after)):
                # Stable frame/family above remains mandatory. This response is
                # diagnostic only; the post-shot reader never accepts absence.
                return
            require(isinstance(b, dict) and planner.binding_key(a['binding']) == planner.binding_key(b['binding'])
                    and a['identity'] == b['identity'], "Selected ammunition owner/binding changed")

    def snapshot_source_deadline(self, snapshot):
        deadline = min(transport.Driver.snapshot_source_deadline(self, snapshot), snapshot['native_deadline_ns'])
        x = snapshot.get('fixedContainer', {})
        if x.get('available') is True:
            deadline = min(deadline, x['input']['deadlineNs'])
        if x.get('view') is not None:
            deadline = min(deadline, x['view']['binding']['deadlineNs'], x['view']['snapshot']['deadlineNs'])
        return deadline

    def selected_shot_ready(self, snapshot, now):
        """Existing current contact proves attachment readiness, not shot capture."""
        self.admit_snapshot(snapshot, now, integer(snapshot['nonce']))
        require(now < self.snapshot_source_deadline(snapshot),
                "Original selected shot source expired")
        counts = planner.ammo(snapshot, now, idle=True)['snapshot']['counts']
        require(counts['loaded'] == 1 and counts['reserve'] > 0 and counts['capacity'] == 1
                and snapshot['weapon_actions_blocked'] is False,
                "One shot requires the fresh selected loaded chamber")
        sight = snapshot['sight']
        require(integer(sight['generation']) == integer(snapshot['input']['generation'])
                == integer(snapshot['shot_input_generation']),
                "Selected shot sight/input generation mismatch")
        require(integer(sight['deadlineQpc']) == integer(snapshot['native_deadline_qpc'])
                and integer(sight['deadlineNs']) == integer(snapshot['native_deadline_ns'])
                and integer(snapshot['valid_until_ns']) <= integer(sight['deadlineNs'])
                and now < integer(sight['deadlineNs']),
                "Selected shot original sight lease expired or mismatched")
        require(sight['status'] in ('available', 'geometry_unavailable'),
                "Selected shot sight family/adapter unavailable")
        native = sight.get('native')
        if native is not None:
            require(native['valid'] is True
                    and integer(native['physicalItem']) == integer(sight['family']['physicalItem']),
                    "Selected shot native sight physical identity mismatch")
        contact = sight['contact']
        if contact is None:
            return False
        require(integer(contact['generation']) == integer(sight['generation'])
                and integer(contact['deadlineQpc']) == integer(sight['deadlineQpc'])
                and integer(contact['deadlineNs']) == integer(sight['deadlineNs']),
                "Selected shot contact generation/original lease mismatch")
        # Native leaf matrices may exist while attachment is still pending.
        # Distance preference and hand acquisition are unrelated to this gate.
        return (sight['status'] == 'available' and contact['valid'] is True
                and contact['previewValid'] is True)

    def wait_pre_shot_ready(self):
        require(self.demand_finished and self.reference is not None and self.latest_snapshot is not None
                and self.active_plan is None and self.commands == 0 and self.motion_started is None
                and not self.pending_command and not self.unsafe_owner
                and not (self.held_left or self.held_right or self.held_trigger or self.held_buttons),
                "Selected shot readiness requires the unused ordinary trigger and admitted ammunition")
        deadline = min(self.deadline, self.stage_deadline)
        for _ in range(800):
            now = time.perf_counter_ns()
            self.bounds(now)
            require(now < deadline, "Original selected shot observation deadline reached")
            state = self.read_state()
            self.ownership(state)
            require(integer(state['captureSeq']) == self.capture_sequence
                    and all(state[k] == self.initial_sim[k] for k in ('head', 'handL', 'handR', 'controls')),
                    "Selected shot readiness input/capture changed")
            snapshot, now = self.latest_snapshot, time.perf_counter_ns()
            source_deadline = self.snapshot_source_deadline(snapshot)
            current = now < source_deadline
            # As in transport expiry handling, validate an expired diagnostic
            # at its publication time. It never supplies command authority.
            ready = self.selected_shot_ready(snapshot, now if current else integer(snapshot['observed_ns']))
            if current and ready:
                self.artifacts.event(dict(type='selected_shot_ready', nonce=snapshot['nonce'],
                    inputGeneration=snapshot['shot_input_generation'], evaluatedNs=now,
                    sourceDeadlineNs=source_deadline, observationDeadlineNs=deadline))
                return
            self.artifacts.event(dict(type='selected_shot_readiness_pending', nonce=snapshot['nonce'],
                reason='attachment_geometry_unavailable' if current else 'original_source_expired',
                evaluatedNs=now, sourceDeadlineNs=source_deadline,
                observationDeadlineNs=deadline, commandAuthority=False))
            # The same owner requests a new immutable publication; no input,
            # capture-demand start, stage bound or original source is rewritten.
            self.fresh_available()
        raise transport.REFUSAL("Selected shot readiness observation iteration bound")

    def validate_publication(self, lines, expected, cleanup):
        if lines == ['trigger r 0']:
            require(self.held_trigger, "Ordinary release requires this owned trigger hold")
            return
        require(not cleanup and lines == ['trigger r 1'] and not self.commands
                and not self.held_trigger and not self.pending_command,
                "Exactly one ordinary trigger press is permitted")
        now = time.perf_counter_ns()
        if self.require_shot_attachment_ready:
            require(self.selected_shot_ready(self.latest_snapshot, now),
                    "Selected shot attachment readiness unavailable at publication")
            return
        self.admit_snapshot(self.latest_snapshot, now, integer(self.latest_snapshot['nonce']))
        counts = planner.ammo(self.latest_snapshot, now, idle=True)['snapshot']['counts']
        require(counts['loaded'] == 1 and counts['reserve'] > 0 and counts['capacity'] == 1
                and self.latest_snapshot['weapon_actions_blocked'] is False,
                "One shot requires the fresh selected loaded chamber")

    def send(self, lines, **kwargs):
        if lines == ['trigger r 1']:
            if self.require_shot_attachment_ready:
                self.wait_pre_shot_ready()
            kwargs['source_deadline'] = self.snapshot_source_deadline(self.latest_snapshot)
        return transport.Driver.send(self, lines, **kwargs)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--stage", choices=("Reload", "Open", "Insert", "Close"), required=True)
    parser.add_argument("--step-ms", type=int, default=40)
    args = parser.parse_args(argv)
    driver, code = None, 2
    result = dict(schema="bc2vr.live-fixed-container-driver.v1", status="refused", nativeAcceptance=False,
                  headsetAcceptance=False, requiresIndependentAllThreePathAudit=True)
    try:
        require(25 <= args.step_ms <= 50, "shared transport command period must be 25-50 ms")
        driver = FixedContainerDriver.__new__(FixedContainerDriver)
        driver.__init__(args)
        if driver.requested_stage == "Reload":
            driver.reload()
        else:
            pickup = driver.stage("Pickup") if driver.requested_stage == "Insert" else None
            driver.stage(driver.requested_stage, pickup)
        result["status"], code = "actual_receipt_gates_observed_and_left_released", 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
    finally:
        if driver is not None:
            # This shared cleanup runs for every exit, including successful runs.
            # It settles only this writer's pending batch and ordinary owned grip.
            if getattr(driver, "artifacts", None):
                try:
                    result["release"] = driver.release_after_failure()
                except Exception as error:
                    result["release"], code = "refused: "+str(error), 2
                if driver.held_left or driver.pending_command:
                    result["status"], code = "refused_unsettled_release", 2
                result.update(durationNs=time.perf_counter_ns()-driver.started, commands=driver.commands,
                    plans=driver.plans, gateReceipts=driver.gate_receipts, leftGripHeld=driver.held_left,
                    pendingCommand=driver.pending_command, artifactDirectory=str(driver.artifacts.directory))
                try:
                    driver.artifacts.save("result.json", result, final=True)
                except Exception as error:
                    result["resultWriteError"], code = str(error), 2
            driver.close()
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
