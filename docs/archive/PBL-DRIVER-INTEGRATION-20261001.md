# PBLB campaign driver candidate

This candidate adds real native input and tracked-camera consumers for the
observed PBLB Patrol boat driver seat. The preceding vehicle work was a pure
planner with only an exit consumer installed; it did not drive this boat.

## Exact scope

- Left stick Y: native Throttle action 0 (authored ConceptMoveFB 0).
- Left stick X: native Yaw action 4 (authored ConceptMoveLR 1).
- Left primary: ChangeVehicle action 16 (concept 36), preserving prior exit.
- HMD orientation and lean: current native seat camera plus a reference captured
  when entering the seat, changing tracking spaces, or recovering tracking.
- No driver native right-stick look, fire, hand IK, infantry aim, snap turn,
  weapon cycle or collision roomscale. Unclassified seats retain native controls
  and the previous exit where their exact router maps 16 to 36.

The driver camera's StaticCameraData refers to Roll 6 and Pitch 5, but its four
limits and zoomed/nonzoomed sensitivities are zero. CameraYaw 10/CameraPitch 9
appear in its router but are not proven consumers for this seat. This candidate
therefore does not bind them. Native seat translation/rotation, including boat
motion, remains authoritative; XR eye poses are unchanged.

## Evidence and guards

`boat-seat-association.json` captured PID 20904 at 18:39:26 UTC read-only, before
the subsequent checkpoint reload replaced the local actor. It identifies
ClientVehicleEntity, slot 0, PlayerEntryComponentData named Entry Driver,
VehicleEntityData named PBLB, and mesh Objects/Vehicles/Sea/Pbl/Pbl_Mesh.
`boat-base-metadata.json` and `boat-camera-input-consumers.json` record actual
reflected offsets, parent hierarchy, authored mappings and first-person camera.
`boat-vehicle-input-config.json` confirms throttle deadzone 0 and yaw deadzone
.15, with yaw inertia in/out .2 seconds.

The adapter revalidates native owner, weak actor, entry array, router/cache
vtables, all hash nodes and the same generation at each gather. The authored
profile separately verifies the reflected layouts, names/mesh, driver order,
class and all 30 mapping rows. No addresses from the capture are in the runtime
profile. Unsupported or changed identity cannot acquire drive writes.

After native gather has run, BuildVehicleInputPlan selects only the verified
actions and preserves that gather's allowed-axis bits. VehicleInputOverride
checks every original word before any write and commits immediately for the
normal native tick. The next original gather rebuilds all actions. No native
object survives a scoped override. Controller recovery requires neutral; left
and right tracking loss is independent. Vehicle state never runs infantry
ControllerActions.

VehicleCameraAnchor counters the reference-relative HMD translation and upright
yaw present upon entering the seat. Previously, the unchanged native seat camera
received the entire displacement accumulated while physically walking on foot.
This is a concrete offset defect; headset validation is still needed to establish
whether it explains all of the reported boat-camera trouble. The same adjusted
prototype goes into BuildTrackedViews for both eyes and the shared culling view.
Camera state is protected by a mutex shared with input publication, and uses the
actual render lease's TrackingFrame rather than a separately sampled pose.

## Integration and validation

`vehicle-consumer.patch` is a minimal diff against current staged source;
`integration-base.json` contains normalized-text SHA256 bases. Review copies are
under `integration/`. They preserve the existing reloadRoundProbe argument and
producer work. Candidate is not deployed or copied into staged automatically.

Copy the new files under include/, src/ and tests/ into the same relative staged
locations. Patch CMake adds Bc2VehicleRoutes.cpp and Bc2BoatProfile.cpp to BC2Camera,
and Bc2VehicleRuntimeTests to the existing test loop. Bc2VehicleCommit.h and
VehicleTracking.h are headers only. NativeProbe has one changed call argument;
Gameplay has input consumption, camera adjustment and diagnostics.

Five regression cases passed x86 and x64: seat anchor and native world motion,
IPD/lean/recenter, continuous axes/neutral recovery/tracking loss, exact native
hash identity rejection, scoped native commit/rollback, and exact boat profile
with seven rejection mutations. The actual changed x86 Gameplay translation
unit compiles; native BC2 Gameplay is intentionally x86 only. Full root builds,
an actual native input pulse, seat camera output and headset behavior remain to
be validated after promotion.

Read-BoatProfile.cpp is an explicit read-only diagnostic for this running PID's
captured manager location; it queries the current player/weak actor on each run.
It opens only QUERY_INFORMATION | VM_READ and verifies the executable path.
It is not a generic profile resolver, hook, default CTest or control sender.
The original run failed closed after checkpoint reload changed the actor; that
failure is saved in boat-profile-live.json. After root reseated the new actor,
boat-profile-live-current.json passed the exact driver guard with all 30 routes
and fingerprint 3637031354888992177. This proves current profile ownership and
metadata recognition, not native input consumption or headset appearance.

For root's native fixture: neutral first, raw leftY .25 for 300 ms, neutral,
raw leftX .40 for 300 ms, then neutral; no fire/exit. The policy deadzone .18
means .40 becomes .2683, exceeding the native yaw deadzone .15. Raw .25 would
become .0854 and would not steer. A subsequent .1 m head lean should move the
render center .1 m from the anchored seat while eye separation remains unchanged.

## Diagnostic delta

Apply vehicle-diagnostics.patch after the original vehicle-consumer.patch;
diagnostics-base.json gives the normalized-text bases for its three files.
Current include/, tests/ and integration/ copies already contain the delta.
It adds nonzero throttle/steer commit counts and maximum observed amplitudes,
plus at most 16 gather records containing owner/seat generations, input sequence,
permissions, original words, requested written values and immediate native cache
readback. Records prefer transitions and sample nonzero holds at 100 ms; neutral
idle does not consume the record budget. A successful readback proves that this
native gather buffer received the pulse, not downstream physics movement.

The delta also clears cached axes/buttons immediately if an otherwise valid
duplicate transport packet loses Stick, Primary or Trigger action-active bits.
Reactivating a component on that same sequence cannot resurrect its prior hold.
Six deterministic cases now pass on both architectures, including this loss and
recovery case; actual changed x86 Gameplay compiles. Camera and route/profile
binding code remain unchanged by the delta.
