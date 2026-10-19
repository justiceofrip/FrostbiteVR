# 2026-10-19: BF4 manual reload approach

- Ammunition only ever moves through the game's own transfer (`+0x9BC910`, reached from
  the firing state machine at the ReloadThreshold point). No direct ammo writes, no
  direct calls of the transfer.
- Weapon data assets are shared with AI soldiers (they carry `UnlimitedAmmoForAI`), so
  they are never written. Per-player tuning goes through the player's own runtime firing
  object (for example its reload-time multiplier at `+0x17C`).
- Guns are classified from their own reflected data into the framework's mechanism
  descriptors; authored profiles only for what the data cannot say (belt feed, revolver
  cylinder). Anything unplaced stays on button reload.
- First native approach is input-only (Bf4ReloadDriver): it presses the game's reload and
  holds the trigger, and never writes memory, so it is correct whichever client/server
  copy is authoritative. A declined reload clears its gates; the gun is never stranded.
- Single player keeps a client and a server firing object per gun (client weapon +0x8,
  server weapon +0xA0, told apart by vtables). Reload acknowledgements follow the server
  copy; a write, if ever needed, must cover both copies or none.
- bf4.exe has no RTTI and its vtables sit back to back: a table's start comes from the code
  that stores it, never from gaps between code pointers (one early guess picked an abstract
  base). Live checks compare against the most-derived vtable.
