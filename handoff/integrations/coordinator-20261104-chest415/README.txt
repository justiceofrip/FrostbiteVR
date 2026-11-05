Chest knife: body display, draw and return

Normal Play requests the slot-7 chest holster when the packaged native build supports it. With your right hand empty and controls settled, grip the handle on your chest to draw. Release trigger and grip to return it. The original game's knife is displayed blade-down at that same grab point; selection and temporary custody remove the stowed copy. This adds no empty-hand holster support for ordinary guns.

The new normal stock-trigger mode4 adds an explicit trigger attack while the knife is drawn; release trigger and grip to return. Mode2 remains draw-only, and the older stock-motion path is mode1. Tracked-strike mode3 remains disabled. Native target acquisition and damage are game-owned; a draw or visual swing alone establishes neither. See docs/CURRENT.md for the installed version and the November5 stock-knife handoff for scope.

The strike candidate binds one accepted native Fire input to the knife's acquisition, then to BC2's later attack callback. A health delta must belong to the acquired target. A complete miss, unknown result and confirmed hit are separate. No range, health or timing values are overwritten. A chest draw or a flick of the wrist alone does not establish damage.

Local unit checks: draw-only mode rejects incidental attacks and permits return; the scoped transaction tests pass. The cache-backed x86 test loaded 307 real parts, verified the knife's blade and handle bounds, stow/draw visibility and stale-owner rejection. The 306-part cache from 0.1.61 remains a separate recipe. The 307th part is derived from the user's installed game; no game mesh is included in source or distribution.

Reload session 408 on private 0.1.61: the simulated headset connected, but BC2 did not provide the native Present bootstrap before gameplay. No input or reload was attempted. The coordinator closed the game normally and verified all three original profiles. The new bootstrap error identifies whether the expected swap chain presented, only issued test presents, or failed device/context validation. Startup timing and renderer eligibility are unchanged.
