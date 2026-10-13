# Recovery250: replacement custody across native publication gaps

Actual249 reached replacement carry with zero magazine palette fallbacks, then
cancelled Guided on an expired native ammo read. The shared AmmoSupply now has
an explicit retention operation accepting only current input and the exact
existing supply identity. It renews the same hand claim, accepts real release,
and preserves all tracking, owner, input ordering and claim checks. It accepts
no counts or geometry and cannot acquire, reserve or settle ammunition. Ordinary
supply/seat operations still require a fresh authoritative source.

The BC2 resource adapter uses this path only for typed transient publication
failures with independently current family, native/physical identity, exact gun
claim and tracking epoch. It also retains already submitted original-return or
refill requests without resending or acknowledging them. Original command and
transaction deadlines continue to run. A released replacement returns to an
empty well; missing data cannot create a new object. No new pose, contact or
chest supply evidence is published during the gap.

Eight focused suites pass x86 and x64, including actual-expiry cases with mocked native
Updates, replacement carry and guidance, release and reacquisition, delayed
native completion, and invalid owner/tracking/claim/source conditions. The
shared supply test covers magazines and shells, but only the BC2 magazine
adapter is wired to the new retention operation. Full250 builds pass all240 suites on each architecture. Frozen source digest:
`86d9dd09837bc5069970348a90026d3359294eadee261c59761a086a2d79e7fb`.
Actual250 completed both rifle magazine cycles (original return and chest
replacement), with four completed native ammo operations and 837 paired magazine
presentations, zero fallbacks. Its interrupted pump sequence completed a partial
stroke, 500ms tracking loss, neutral recovery, full stroke, native ready
acknowledgement and support return. Shell insertion then failed: cleanup replayed
an earlier processing timestamp from the cancelled normal pump recognizer after
the shell consumer acquired its claim, clearing shell and gun custody. Shell
geometry/submissions stayed zero. The combined run failed and no final rifle
return/fire was reached. The game remained responsive and detached cleanly;
scene diagnostics flushed 42/42 rows. This is not headset acceptance.

Evidence: local `resource-f2000250-01`, trace SHA-256
`8bf0738bc0185fe42ddc2da9cff2d4117c768e416bfe22a7b15f70016c595f1e`.
The status checker also lost recovery counters when the service yielded to
reload and did not count recovery submissions; those diagnostic gaps must be
fixed without changing this historical failed result.
