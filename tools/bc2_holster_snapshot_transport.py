"""Canonical snapshot receiving for the existing bounded holster adapters.

Wrap the final holster driver class after loading its existing module chain:
    HolsterDriver = canonical_holster_snapshot_driver(HolsterDriver, transport)
No live work, imports of frozen reports, or monkey-patching occurs here.
"""


def canonical_holster_snapshot_driver(base, transport):
    """Retain holster policy; use one canonical receive/late-response loop.

    The supplied transport must be the same module used by the base's MRO.
    Existing pending-body, draw-gap, command and observation bounds stay owned
    by the base. A timed-out response only follows canonical neutral readiness.
    """
    transport.require(issubclass(base, transport.Driver),
                      "holster base and canonical snapshot transport differ")

    class HolsterDriver(base):
        def receive_native_snapshot(self):
            return transport.Driver.receive_snapshot(self)

        def defer_invalid_snapshot(self, snapshot, now):
            if self.unavailable_draw_publication(snapshot):
                # Canonical receive retained the original raw bytes, checked
                # this exact time against the request deadline, and matched
                # the nonce. Legacy raw is used only in its timeout branch;
                # this guard makes that branch unreachable without inventing
                # bytes or re-reading a possibly replaced response.
                transport.require(now - self.last_request <= transport.SNAPSHOT_TIMEOUT,
                                  "draw publication gap exceeded original response deadline")
                self.hold_draw_publication_gap(snapshot, None, now)
                return True
            return super().defer_invalid_snapshot(snapshot, now)

    return HolsterDriver
