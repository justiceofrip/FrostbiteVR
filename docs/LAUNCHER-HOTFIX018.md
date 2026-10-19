# 0.1.8 startup hotfix

Status: isolated builds and exact-package validation passed on 2026-10-19;
the three release assets are frozen for publication.
This hotfix starts from published 0.1.7 source commit
`b6b89755e2947d953ecc39796a29d722dc74468c`. It retains the published gameplay
implementation and explicitly excludes the ongoing knife investigation.

## Scope

The supplied EA startup report identifies `Mechanism audio callback binding or
private cache unverified`. Its executable and requested cue hashes match the
supported inputs. The native readers nevertheless rejected its X: cache because
their path checks required G:. `Bc2AudioCachePath.h` replaces that workstation
restriction with absolute local-drive path validation. Both readers retain the
exact byte count, SHA-256, WAV and PCM gates. Relative, network, device, traversal,
alternate-stream and malformed paths remain rejected.

The native exception writer now emits bounded, escaped JSON. Startup displays a
complete small failure object's reason; invalid, partial or oversized reports use
the existing log fallback. Feedback includes only bounded sanitized failure fields.
The session coordinator records a nonzero native result as failure even when the
game has already exited. NativeTrace can still return zero when the process
exits before its thread result is observed; this is not universal crash
detection. Normal close remains distinct.

There are no changes to native hooks, executable signatures, knife admission,
firing, movement, controller fixtures, cache content or resource enrollment.
The documented G: rule applies to this development workspace, not public users.

## Completed validation

- The exact native dependency closure, including both new helper headers, was
  frozen. Capability records remain identical to the published build; only their
  source binding changes. Full normal builds passed **293/293 suites on x86 and
  x64**, including `Bc2AudioCachePath` and `Bc2ProbeFailure`.
- **135 focused checks passed:** startup 26, feedback 45, portable session 13,
  native failure diagnostics 25 and user setups 26. The setup matrix invokes
  real installer/launcher functions while replacing external boundaries. It
  does not emulate another GPU, headset connection or store executable.
- Exact binary hashes, normal build flags, generated headers and full build
  reports were verified before packaging. All 1,784 packaging inputs were pinned.
- The exact ZIPs passed **44 package checks plus 51 existing launcher checks**:
  clean installation, update from the exact published 0.1.7 package, rollback,
  same-version repair, corruption rejection, settings preservation, feedback
  and the installed Worker-to-Entry validation route. No game was started.
- Reuse of the previous cold-preparation evidence was established by rechecking
  65 preparation/Python files, nine decoder files and six cache files. The
  unchanged recipe was reconstructed, and the new staged runtime accepted the
  existing cache. No download, cache copy or repeat cold preparation was needed.

The native-failure test's copied fixture initially omitted its final CRLF; this
made its exact-length assertion fail. Only that test fixture was corrected
between the full architecture builds, then the 25-check suite passed. Native
source and generated capability bindings were unchanged. The original and
amended source manifests are retained with the private build evidence.

## Frozen identities

| Item | SHA-256 |
| --- | --- |
| Native source closure | `2c285a89ee69cd02effc7ff1f09bf23c56eae8ec105a8b3a765bb94918fec7c1` |
| Paired build receipt | `3667f2d76030f65dfb15e4f462835b374882b6d7093abd2f4c501b3bb7fdc39a` |
| Packaging input manifest | `9a5a08d33d5133f9bdd8636aec782b8dcc8017dc65acecc3732159094caf21bf` |
| Focused validation | `499f99bdf589dbc9c1c0770f9a2b580f0287f78058893e46d36076f069962574` |
| Exact-package validation | `1acb583c260838127992b3949a7d1c776bd15742d0ec5cfe4dcc40cf35800bc2` |
| Asset-reuse validation | `fb424f8658c0260cfc3eaef5b7b4dafc35b5fe5207adc243e28cbc490092edef` |
| `BC2VR-Setup-0.1.8.zip` | `dc59ea4a0dafe83ae09f59987286f5bf459bea4992465ac82735431b018a740f` |
| `BC2VR-0.1.8-win64.zip` | `ea7e469083fa5d3f629c4ed4fc15b0ab3ed2b3217dd6ee0506d09dc47ef768e9` |
| `bc2vr-update.json` | `d2547b26fa80e248065741e73c721162359b5e5bd3b60aead730ead3fa69ce9b` |

The setup ZIP is 16,718,283 bytes; the runtime ZIP is 16,732,466 bytes and expands
to 39,881,397 bytes. Final documentation was updated after the packaging freeze;
compiled code, runtime payloads and packaging inputs were not changed.

The release consists of `BC2VR-Setup-0.1.8.zip`, `BC2VR-0.1.8-win64.zip` and
`bc2vr-update.json`. The setup includes the runtime ZIP, installer scripts and
pinned bootstrap manifest. Do not publish build receipts with workstation paths,
symbols, native input fixtures, game-derived caches, game data or captures.

## Public limitations

The campaign remains blocked by the published knife behavior. This release does
not contain or claim the unpublished knife candidate, verified enemy damage,
opening progression, a death/reload crash fix, or broad EA/Steam/VDXR acceptance.
Only the confirmed drive rejection and bounded startup reporting are addressed.
These build and packaging checks do not establish actual X: native startup or
headset rendering. The release notes retain that limitation.
