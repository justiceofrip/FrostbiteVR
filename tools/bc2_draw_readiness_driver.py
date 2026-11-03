"""Injectable ordinary-draw readiness adapter; no imports of report helpers.

Root supplies the existing driver class, its full binding module and the pure
policy module. Create AdmissionBudget before Prime, call prime_attempt before
EVERY Prime observation, and carry that same object into make_driver. Only
Prime and extra hold requests charge the shared twenty-observation budget;
ordinary draw observations retain the base driver's existing ten-second bound.
No command, transport, filesystem or clock operation runs on module import.
"""
import copy
from dataclasses import replace
import time


class AdmissionBudget:
    """One admission window and one draw; nested hold reasons share one cap."""

    def __init__(self, policy, started_ns, deadline_ns):
        self.policy = policy
        self.session = policy.begin(started_ns, deadline_ns)
        self.observations = 0
        self.hold_started = 0
        self.replacements = 0
        self.decisions = []
        self.refusal = ""
        self.attached = False

    def check(self, now_ns):
        p, s = self.policy, self.session
        p.require(not self.refusal and not s.refusal, self.refusal or s.refusal)
        p.require(s.admission_started_ns <= now_ns < s.admission_deadline_ns,
                  "original 30s admission deadline expired")
        if s.draw_started_ns:
            p.require(s.draw_started_ns <= now_ns < s.draw_deadline_ns,
                      "original 10s draw deadline expired")
        if self.hold_started:
            p.require(now_ns - self.hold_started < p.HOLD_NS,
                      "shared 500ms observation hold expired")

    def _charge(self, now_ns):
        self.check(now_ns)
        self.policy.check_next_observation(self.session, now_ns, observations_used=self.observations)
        self.policy.require(self.observations < self.policy.MAX_OBSERVATIONS,
                            "shared 20-observation admission budget exhausted")
        self.observations += 1

    def prime_attempt(self, now_ns):
        self.policy.require(not self.attached, "Prime cannot restart after draw attachment")
        self._charge(now_ns)

    def attach_draw(self, started_ns, deadline_ns):
        self.policy.require(not self.attached, "admission budget already attached to a draw")
        self.session = self.policy.start_draw(self.session, started_ns, deadline_ns)
        self.attached = True

    def begin_hold(self, started_ns):
        self.check(started_ns)
        self.hold_started = min(self.hold_started, started_ns) if self.hold_started else started_ns

    def request(self, now_ns):
        self.check(now_ns)
        if self.hold_started:
            self.policy.require(self.replacements < self.policy.MAX_REPLACEMENTS,
                                "shared two replacement observation requests exhausted")
            self._charge(now_ns)
            self.replacements += 1

    def assess(self, snapshot, now_ns, context, full_admission, *, observe_admission=None):
        self.check(now_ns)
        context = replace(context, observations_used=self.observations,
                          hold_started_ns=self.hold_started,
                          hold_replacements=self.replacements)
        result = self.policy.assess(snapshot, now_ns, context, self.session, full_admission,
                                    observe_admission=observe_admission)
        self.session = result.session
        self.decisions.append(dict(status=result.status.value, reason=result.reason,
            nonce=snapshot.get("nonce"), evaluatedNs=now_ns,
            uncertainties=list(result.uncertainties)))
        if result.status == self.policy.Status.OBSERVE_ONLY:
            self.begin_hold(result.session.hold_started_ns)
        elif result.status == self.policy.Status.REFUSE:
            self.refusal = result.reason
        return result

    def admit_prime(self, snapshot, now_ns, context, full_admission):
        """Optional pure Prime admission, after original request/owner checks.

        Context must describe the actual original ACK and request; this helper
        never synthesizes them or extracts a shortened observer stdout record.
        prime_attempt must already have counted the corresponding observation.
        """
        self.policy.require(not self.attached and context.phase == "prime"
                            and self.observations > 0, "original counted Prime context required")
        return self.assess(snapshot, now_ns, context, full_admission)

    def finish_hold(self):
        self.hold_started = self.replacements = 0

    def summary(self):
        s = self.session
        return dict(originalAdmissionStartedNs=s.admission_started_ns,
            originalAdmissionDeadlineNs=s.admission_deadline_ns,
            drawStartedNs=s.draw_started_ns, drawDeadlineNs=s.draw_deadline_ns,
            observationsUsed=self.observations, holdStartedNs=self.hold_started,
            replacementsIssued=self.replacements, decisions=copy.deepcopy(self.decisions),
            refusal=self.refusal or s.refusal)


def make_driver(base_class, binding_module, *, budget, clock=time.perf_counter_ns):
    """Wrap explicitly supplied100/94-compatible classes; never mutate modules.

    The caller may temporarily install the returned class in its PRIVATE100
    module before calling its unchanged main. Empty, stow/cohort, command ACK,
    ownership, target selection and failure grip release remain inherited.
    """
    policy = budget.policy

    class ReadinessDriver(base_class):
        def __init__(self, args):
            self._readiness_ready = None
            self._readiness_record = None
            self._readiness_ack = None
            super().__init__(args)
            self._readiness_ack = self.command_identity()
            budget.attach_draw(self.started, self.deadline)
            self.artifacts.event(dict(type="draw_readiness_admission", **budget.summary()))

        def bounds(self, now):
            super().bounds(now)
            budget.check(now)
            if budget.hold_started:
                current = self.read_state()
                self.ownership(current)
                policy.require(not self.pending_command
                               and self.command_identity() == self._readiness_ack,
                               "acknowledged command changed during shared observation hold")
                policy.require(all(current[key] == self.state[key] for key in policy.HOST),
                               "acknowledged input/owner changed during shared observation hold")

        def _inherited_hold(self):
            for value in (self.holster_hold_started, self.snapshot_refresh_started):
                if value is not None:
                    budget.begin_hold(value)

        def request_snapshot(self):
            self._inherited_hold()
            now = clock()
            self.bounds(now)
            budget.request(now)
            self._readiness_ready = self._readiness_record = None
            if budget.hold_started:
                self.latest_snapshot = self.publication_source = None
            return super().request_snapshot()

        def _full_admission(self, snapshot, now):
            # resolve_slots executes80's entire original frame/holster admission
            # and94's complete observed mapping/selected-configuration proof.
            result = binding_module.resolve_slots(snapshot, now,
                self.args.stow_asset, self.args.stow_config,
                self.args.target_asset, self.args.target_config)
            expected = self.slot_binding or getattr(self.args, "observed_binding", None)
            policy.require(expected is None or result == expected,
                           "body/native binding changed after original admission")
            return result

        def _observe_admission(self, snapshot, now):
            # A typed missing display publication supplies no current mapping.
            # Retained Ready binding is comparison history only. The unchanged
            # original frame/holster and current selected-source proof still run.
            expected = self.slot_binding
            policy.require(expected is not None, "observation hold lacks prior Ready mapping history")
            binding_module.original.HolsterDriver.admit_snapshot(
                self, snapshot, now, binding_module.integer(snapshot["nonce"]))
            binding_module.selected_matches(snapshot, expected["source"])

        def _context(self, snapshot):
            current = self.read_state()
            self.ownership(current)
            identity = self.command_identity()
            if self._readiness_ack is None:
                self._readiness_ack = identity
            pose = self.right_pose
            if pose is None:
                hand = self.state["handR"]
                policy.require(hand["followsHead"] is False,
                               "initial acknowledged right hand must have an absolute pose")
                pose = dict(position=hand["pos"], orientation=
                    binding_module.original.frame.ypr_quaternion(hand["ypr"]))
            return policy.Context(nonce=snapshot["nonce"],
                request_started_ns=self.last_request,
                request_published_ns=self.last_request_published,
                response_deadline_ns=self.last_request + binding_module.transport.SNAPSHOT_TIMEOUT,
                observations_used=budget.observations, phase=self.phase,
                last_write_ns=self.last_write, last_commit_ns=self.last_commit or 0,
                command_identity=self._readiness_ack, current_command_identity=identity,
                acknowledged_state=copy.deepcopy(self.state), current_state=current,
                acknowledged_pose=copy.deepcopy(pose), pending_command=self.pending_command,
                grip_action_started=self.held_right or getattr(self, "stow_ack", None) is not None,
                draw_armed=self.draw_armed)

        def receive_snapshot(self):
            received = super().receive_snapshot()
            if received is None or self.phase not in ("baseline", "stow_reach"):
                return received
            snapshot, raw, _ = received
            now = clock()
            self.bounds(now)
            result = budget.assess(snapshot, now, self._context(snapshot), self._full_admission,
                                   observe_admission=self._observe_admission)
            self.artifacts.event(dict(type="draw_readiness_decision", **budget.decisions[-1]))
            if result.status == policy.Status.READY:
                self._readiness_ready, self._readiness_record = snapshot, result.observation
                return snapshot, raw, now
            # Both unknown and refused evidence lose command-source authority.
            # last_native remains history for the original independent checks.
            self.latest_snapshot = self.publication_source = None
            self._readiness_ready = self._readiness_record = None
            if result.status == policy.Status.REFUSE:
                raise binding_module.REFUSAL(result.reason)
            self.request_snapshot()
            return None

        def accept_snapshot(self, snapshot, raw, now):
            super().accept_snapshot(snapshot, raw, now)
            # Only successful complete inherited admission ends a combined hold.
            budget.check(clock())
            budget.finish_hold()

        def validate_publication(self, lines, expected, cleanup):
            super().validate_publication(lines, expected, cleanup)
            if cleanup:
                return
            budget.check(clock())
            policy.require(not budget.hold_started,
                           "command refused while shared observation hold is pending")
            source = self.publication_source
            source_boundary = self.phase in ("baseline", "stow_reach") or (
                self.phase == "stow_wait" and not self.held_right
                and getattr(self, "stow_ack", None) is None)
            if source_boundary:
                policy.require(source is self._readiness_ready
                               and policy.freeze(source) == self._readiness_record,
                               "command requires the unchanged latest fresh Ready source")
                h = source["bodyHolster"]
                policy.require(h["inventoryEvaluation"] == 1 and h["allowAutomaticGunHold"] is True,
                               "unknown eligibility cannot authorize an ordinary command")
                self._full_admission(source, clock())

        def command(self, snapshot, pose, grip):
            try:
                result = super().command(snapshot, pose, grip)
                self._readiness_ack = self.command_identity()
                return result
            finally:
                # An admitted source authorizes only this original command.
                self._readiness_ready = self._readiness_record = None

    ReadinessDriver.__name__ = "Readiness" + base_class.__name__
    return ReadinessDriver
