# Committed holster intent across tracking reference changes

The 23:05 headset run accepted SPAS holster request14 at input22197. Roughly
4.7seconds later, tracking space5 became6; the adapter recovered with Show14 and
reacquired GunHold47 without a new draw request. Native weapon/equip/actor stayed
the same. The recording's final seconds show the previously absent SPAS return
across open palms. Earlier raised-hand attempts are a separate unresolved issue;
the old logs did not record their shoulder contact or squeeze edges.

The adapter now retains only an already committed empty-hands intent during a
tracking gap. Fresh recovery, whether or not the space changed, can re-hide the
same item only when all non-space
native/physical ownership fields and the coherently observed native carried-item
identity match. This includes container, weapon/data/persistence/category and
slot/order; same-pointer replacement cannot inherit the intent.

Rebinding creates a new render request. The current native input-cache suppression
receipt and a fresh paired hide receipt must both succeed before empty hands are
adopted. Old hand claims, input deadlines and palette evidence never migrate.
No new body operation is acknowledged. A second tracking gap during rebind starts
another fresh request; it cannot reuse the unfinished pair. Explicit cancellation,
reload ownership, actor/equip/item changes, unsupported profiles and expired
diagnostic capabilities retain the existing visible recovery path.

Shoulder geometry and gesture semantics are unchanged. Currently squeeze must
cross its press threshold inside an assigned shoulder zone. Holding it before
entering does not create another edge. A bounded change-only ring now records
the latest128 observations of shoulder contact, source timestamp, arming/press
state, hand claims, policy reason and request. It reports overwritten-row counts
and does not issue intents. This makes the next recorded failed attempt diagnosable
without claiming that the rebase repair fixes every difficult gesture.

Validation: the new committed-empty rebase regression fails the original code by
observing Show instead of HidePending. The candidate passes33 coordinator,
42 actual inventory-adapter and11 diagnostic groups on each x86/x64 architecture;
the x86 Gameplay translation unit compiles with warnings as errors. Full native
builds/integration and a headset reconnect/holster regression remain necessary.
Production admission remains the existing SPAS profile; scoped XM8 stow remains
a separate unaccepted diagnostic capability. See candidate trace-audit.json for
exact baseline records and the video/QPC alignment limits.
