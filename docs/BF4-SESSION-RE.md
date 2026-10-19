# BF4 session signals (RE task R5): findings so far

Evidence level: **static analysis only** of the decrypted research image of bf4.exe build
179547 (sha256 `9d9d1a3f...c6c2aa`), RVAs relative to `0x140000000`. The SessionGuard
needs two signals before any gameplay feature (camera, aim, reload) may run: the current
level name and whether the session is multiplayer.

## Source: the game's own status message

The game builds a small status message for its online/presence layer and serialises it
as `online`, `isMultiplayer`, `levelName`, `gameMode` (serialiser `+0x102E560`, message
vtable `+0x1E7E1D8`, constructor `+0x1029A80`). Its only producer, `+0x10438A0`, reads:

| Signal | Where (static) | Notes |
| --- | --- | --- |
| isMultiplayer | `byte [[[[+0x2670D80] + 0x28] + 0x38] + 0xA0]` | `+0x2670D80` is the client game context global (the producer loads it, then `+0x81E0C0`: `[ctx+0x28] - 0xC8`, and `+0x97D5D0`: `[x+0x38] + 0x88`, byte `+0x18`). The game defaults to **1 (multiplayer)** if either link is null. |
| levelName | `char* [[+0x27A1918] + 0x50]` | Second producer call site (`+0x104465A`) passes `[+0x27A1918] + 0x50`; the first passes a level name carried by a message. |
| gameMode | string at `[+0x27A1918] + 0x58` | eastl-style {begin, end, ...}. |
| online | `[[+0x27A19A8] + 0xC] == 2` | not needed by the guard |

The front end runs as its own level: `+0x103B7AA` assigns `"Levels/FrontEnd/FrontEnd"`.
SessionGuard now treats `Levels/FrontEnd/*` as the front end instead of locking on it as a
non-campaign level.

## Live read-only probe (built 2026-10-19, not yet run)

`SessionProbeTick` (Bf4Module.cpp), developer sessions on the analysed build, every ~2 s
for the first minute then ~10 s: logs `session probe: level="..." mode="..."
isMultiplayer=N` with the intermediate pointers (VirtualQuery-checked reads, no writes).
Expected if the notes hold: front end `Levels/FrontEnd/FrontEnd`, campaign
`Levels/SP_*` with isMultiplayer 0.

Nothing consumes these yet: `ObserveGame()` still returns no signals, so the guard stays
Unknown (presentation only) and head tracking stays dormant. After the probe confirms both
in the front end and in a campaign mission, enrol them as bindings (ClientGameContext /
ClientLevel.Name plus a level-info global) and feed `ObserveGame()`; keep the game's own
null-means-multiplayer default.
