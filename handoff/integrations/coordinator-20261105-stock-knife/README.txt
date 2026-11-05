Chest knife and inventory update

The stock-trigger candidate keeps knife draw/return and physical-stroke qualification separate. With the chest knife drawn, a fresh neutral trigger followed by a press requests its original Fire8 attack. Only the already-selected knife and its genuine held state may supply that request. The engine owns target acquisition, range, animation and damage; no hit or damage is invented from a controller movement. Tracked stabbing remains disabled.

The dedicated mode preserves older draw-only mode2 and forbidden tracked mode3. The ordinary supported-chest option chooses stock-trigger mode4. Holding the trigger at draw, losing the hand/render/input authority, changing the same controller packet or missing the native gate must not later produce an old attack. Native work already accepted is drained without issuing another first command.

Body inventory: fresh native reads still run, but unchanged carried metadata reuses the retained policy lists. Local x86 comparisons of the actual helper observed zero allocations for 10,000 unchanged updates, versus 40,000 for two items, 120,000 for eight and 200,000 for32. Selected-weapon-only changes also avoided allocation; changed slot/category/actor/space cases kept the old rebuild behavior. These counts cover this helper only and do not establish FPS or total frame-time gains.

Focused x86 knife input/configuration and native parsing passed, including same-packet loss/mutation, stale-lock cancellation, restoration non-revival and accepted sibling completion. The first private inventory check omitted the normal optimization flags and exhausted its stack; the unchanged candidate passed with the actual /O2 /Ob1 build settings. No stack limit was raised.

Full normal validation, deployment and in-game stock-attack behavior are tracked in docs/CURRENT.md. The source comparison, private fixture results and any failed checks remain in the local449 evidence folder.
