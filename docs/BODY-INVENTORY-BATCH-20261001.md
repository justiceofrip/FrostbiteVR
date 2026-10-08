# Body inventory reader batching — October 1, 2026

This delta changes only the read-only inventory resolver and its regressions.
Native owner, class/enum, pointer uniqueness, route bounds and both independent
Snapshot passes are preserved. There is no persistent metadata or dynamic cache
in the Gameplay consumer. Each pass reads fresh bounded owner/item/map/descriptor
ranges and one bounded string per field. Unreadable, non-ASCII or unterminated
metadata strings reject; the tool does not fall back to byte-by-byte probing.

The read-only current BC2 observation confirms XM8 slot1/category wcAssault0,
SPAS slot0/category wcShotgun1 and attached launcher slot3/category wcUgl5.
Current action7 targets [0,3,2,5,1], so the adapter resolves SPAS as first populated
candidate. This observation does not select it or prove the native eligibility
callback will accept it. The native selector and setter both perform eligibility
checks; exact selection plus the existing ordinary rig publication remains the
adapter acknowledgement. Negative cycle input reverses targets, while the body
adapter always emits the verified positive pulse. Static proof addresses and
limitations are in reports/body-inventory-batch-20261001.json.

29 focused adapter groups pass in x86 and x64, including changed selection,
changed item category and changed enum metadata between passes; map read failure,
overlarge descriptors/routes, unterminated strings and maximum span size. These
are additions to the original hand ownership, route, input and body-anchor tests.

With the same minimal getter/name Type callback, actual reader calls fell from
1,374 to 244. Including Type callback reads, direct external RPM calls fell from
1,482 to 352. Two direct samples per version measured mean 5.747 / max 5.796 ms before
and mean 1.335 / max 1.505 ms after. Eight samples per version using the helper's page
snapshots measured mean 1.866 / max 2.275 ms before and mean 0.616 / max 0.690 ms after.
All 20 observations passed. The page helper discards dynamic pages at each of the
actual reader's two owner passes; it is not shipped in Gameplay.

These small bounded measurements include Python callback/RPM overhead. They do
not measure injected game-frame cost. The earlier exploratory measurement used
Inspector's full recursive reflection Type helper and overstated Gameplay's
callback work (9,491 RPM calls); it is superseded by the minimum-Type comparison.
No hooks, native game calls, restart, input, remote allocation or game writes were
performed. The observer DLL is loaded into the external Python helper only.

Root can rerun the bounded reader with:

    python recovery/body-inventory-observer/observe_body_inventory.py --pid PID --batch --output NEW_REPORT

The helper requires the exact installed process path and repeats current owner
coherence. If the player is not on foot or changes owner, it rejects rather than
substituting an old native identity. Actor/equip/space labels used by this external
diagnostic are observer-local tokens, never XR/runtime input or render authority.
