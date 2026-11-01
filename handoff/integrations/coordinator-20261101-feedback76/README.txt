Feedback76 — shared runtime observation and snapshot publication

Actual35 lost a snapshot response while the worker continued running; the
publisher had consumed the nonce before publication and discarded failures.
This batch preserves one immutable payload for one bounded sharing/lock retry,
reports terminal errors, and makes Windows readers compatible with replacement.
Freshness, ownership, source leases and native gameplay admission stay unchanged.

The failed SeatFeed first squeeze now has a separate immutable first/current
admission record, enabled only by existing mechanism snapshot requests. It
records current/raw/original packets and the actual evaluated rejection gates.
This is evidence to resolve the remaining grab failure, not a claimed reload fix.

Root Python checks pass17 methods (one Windows sandbox refusal retained).
Both normal native builds pass341/341; source dd09631 is pushed. Private0.1.36
is installed and validated, preserving settings and0.1.35 rollback. It reuses
the306-record asset cache. Installation: reports/coordinator-20261101-feedback76/installation.json.
Actual76 game01 and game03 closed/restored normally. New feed release/acquisition
and underbarrel initialization failures are retained; complete reload and
headset acceptance remain open. Forced native publisher fault paths are untested.
