Feedback95: current body inventory observation for ordinary diagnostic driving
Baseline: c7c5c97e4eaea10047f43f1963782ee0ebf3994c; installed private0.1.40 unchanged.
Actual92/feed40/xr-game03 refused before input: scoped XM8 was assigned body slot2,
while the saved diagnostic expected slot1. Native inventory index0 was unchanged.
BodyInventory assigns by priority/address and preserves survivors; no fixed slot
or opposite-slot guess can establish the current target weapon.

Bc2RigPublication.cpp now exports optional observation-only bodyInventory from an
existing immutable tracking packet. It checks original owner/input/item leases,
bounded assignments, native inventory relationships and exact configuration data.
Missing or stale joins produce explicit unavailable reasons. No native reads,
gameplay writes, slot policy, capability records or authority extensions are added.

Private driver/resolver: worktree reports/coordinator-20261102-feedback94/slot-binding/.
It admits the full saved observer artifact, resolves exact source/target slots and
rechecks the mapping before source-side commands; original Empty/draw guards remain.
Root checked23 cases including144 assignment/address/order permutations, missing
targets, stale evidence, duplicates, changed identities and prime-to-baseline drift.
Logs: canonical reports/coordinator-20261102-feedback94/slot-binding-tests01.log
and tests02.log (22 passes reused; one expected error-text correction rerun passed).
Normal x86/x64 builds, package/installation and actual reload checks remain pending.
No full LMG reload, headset, performance, destruction or public-release acceptance.
