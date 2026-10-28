# Nested launcher cache and crouch mapping

The tester's 0.1.10 preparation traceback came from a launcher installed inside
the BC2 folder. The blanket game-descendant guard rejected the launcher's own
cache. Entry now supplies the managed root; preparation verifies its exact
installed-package relationship and writable output paths. Nested/separate
fixtures, caller integration, link rejection and stock-file preservation pass.

Crouch is now a left-stick click toggle, with Y holding sprint. Crouch custody
uses the stable actor generation rather than BC2's input epoch, which can change
on a brief input gap or weapon swap. Recenter chords cancel unfinished clicks
without clearing same-actor posture. Focused deterministic checks pass.

These are unpublished candidate changes. The knife's actual-game two-miss
check now reaches native acquisition/attack, but that does not replace opening
encounter or headset acceptance.
