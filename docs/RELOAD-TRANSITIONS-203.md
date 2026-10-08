# Persistent reload transitions — checkpoint 203

The shared magazine consumer could remain in retirement after switching to a
different weapon profile during an unfinished reload. The old native cycle had
drained and ordinary weapon actions were unblocked, but the consumer still tested
the current attached magazine against the old profile's rig fingerprint. The
current weapon therefore could not start another manual reload.

The regression reproduces that state before the correction: `retiring=1`,
`drained=1`, `blocks_current=0`, with repeated attachment waits. The correction
uses the independently validated current family map for the current attachment's
rig. Retirement identity and cycle must still match the old native request.
It does not complete that old reload, grant ammunition, or admit a new weapon.

`Bc2ReloadTransitionTests` keeps one actual `Bc2MagazinePhysicalReload` and shared
hand arbiter alive through each sequence. Native observations and receipts are
explicitly mocked in this CPU test; they are not actual-game evidence.

- Switch after ejecting, while holding a replacement, and after submitting it.
- Require old-cycle retirement before accepting a new reload.
- Reject a wrong retirement identity/cycle, unverified retirement, the old rig,
  missing attachment, wrong raw owner, and lost focus.
- Recover after valid evidence; complete the next reload and check ammunition.
- Complete twelve alternating-profile reloads, with focus interruption and
  different capacities, without reconstructing consumers or recycling IDs.

The second profile is deliberately synthetic and unregistered. These sequences
prove that the same consumer handles distinct profiles; they do not constitute
AEK, SMG, holster, pickup, vehicle, GPU, or headset acceptance. Full combined
holster/pickup/vehicle input sequences remain follow-up work.

Both public builds passed all 185 CTest suites (x86 68.77 s, x64 24.25 s). The
pre-correction failing log and post-correction passing logs are retained locally.
The exact source-bound operation header/receipt is in `profiles/checkpoint203`;
only the magazine consumer changed within the protected source closure. Native
hook ABI, hide/show bindings, input suppression and cleanup authority did not
change. Calibration data remains in `profiles/checkpoint202`.

## Actual-game regression

The rebuilt x86 module also passed the unchanged strict native sequence audit:
original magazine return followed by removal and replacement, conserved ammo,
zero cancellations and zero magazine presentation fallbacks. The startup-pulse
audit passed separately. The mapped executable sections matched the frozen DLL,
native hooks drained, and the game stayed responsive. BC2 was minimized afterward.

This run repeats the existing XM8 sequence; it is not native cross-profile
switching evidence. No headset session was started. Broader native transitions
and headset feel remain to be checked.
