# Consistent body-display input — local367

Normal363 established the correct chest contact while weapons are held and
holstered. Its stationary check still omitted ammunition in two of92 stable
pairs. The source audit identified avoidable mixed-publication reads; the exact
cause of each saved omission was not recorded.

Selected-holster and carried-equipment reads now validate tracking from the
same immutable input publication that supplies their pose. They no longer
require a separately published firing guard to have exactly the same input
generation. Stowed ammunition and its hidden weapon also share that original
input snapshot instead of independently fetching it twice.

Current native equipment, actor/space identities, actual inventory assignment,
explicit hide and free-hand evidence, enabled state and original deadlines still
govern visibility. Advancing free-hand evidence must pass the existing typed
continuity check. Withdrawal, different ownership, untracking, expiry and a
changed publication during the bounded read still reject the display. No older
frame is restamped and no display creates ammunition or a hand claim.

The common admission check has deterministic coverage for focus/head/hand
tracking loss, malformed input, expiration, ordinary packet advancement and the
carried-weapon path's existing left-hand tracking exemption. Display admission
alone remains insufficient to create a holstered prop.

Both full builds pass245 suites (x86:173.57s; x64:101.21s). Its first actual-game stationary run published ammunition in all92 stable pairs, but all191 eye depth captures were rejected, so no props were composed. This is a failed visual check. A fresh run with private368 read-only rejection counters passed95 depth/composition pairs and all93 stable both-eye ammunition samples, with20 visibly changed saved eye images. The sampled clears match the existing world/first-person sequence (1920x1080, D24S8, 8 samples); only the first uninitialized eye was rejected. The previous failure did not recur, so its cause remains unresolved. The observer changed no admission and its source was restored exactly. Draw-transition acceptance is still pending. The preserved player package is361. Existing reload, pump,
bolt and weapon enrollment are unchanged; this is not new whole-roster or
headset acceptance. See [363 display evidence](STOWED-AMMO-363.md) and
[startup/menu comparison and limitations](GRAPHICS-AND-MENU-366.md).
