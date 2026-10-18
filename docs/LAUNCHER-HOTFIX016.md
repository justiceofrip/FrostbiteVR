# Launcher 0.1.6 hotfix

This hotfix changes installation and startup scripts. The checkpoint426 native
binaries remain byte-identical to 0.1.5, from native source
`9196965b6f424cbfa33e98650fc230de273fc444de24c720218dbfa5f703b05b`.
The retained native validation is 291/291 C++ suites on each of x86 and x64;
this launcher release does not claim a new native gameplay result.

## Changes

- The game picker and selected path remain visible in a small launcher window.
  Malformed settings have explicit recovery with backup; unrelated settings survive.
- Installation accepts a chosen local fixed drive, including C:, and checks the
  operation's space needs plus 128 MiB, with a 512 MiB minimum. Temporary files stay with the
  installation through process-local settings. Development work still follows
  this repository's G: storage policy.
- Startup validates the executable and reports an unverified build's fingerprint.
  It honors the selected OpenXR runtime and probes it without forcing SteamVR or
  changing runtime registration. This does not enroll unknown EA/store executables.
- The menu reader verifies the supported Direct3D 11 renderer, double-reads its
  dimensions and passes the actual size to the host. It allows up to 90 seconds
  for the menu, with early completion after stable observations. Failure guidance
  identifies `DxVersion=11`; the launcher does not edit game settings.
- A session becomes running only after native VR input telemetry is observed
  without the checked GPU/camera failures. This is not proof of controller tracking,
  focus, headset presentation or successful campaign interaction. Bounded startup
  diagnostics can be included in manually reviewed feedback.

If 0.1.5's old free-space check prevents updating, download the new Setup, run
`Install.cmd` and choose the existing launcher folder. Settings are preserved.

## Validation

| Check | Passed |
| --- | ---: |
| Exact package: install, update, rollback, local feedback, corrupt rejection | 28 |
| Existing desktop launcher suite | 51 |
| Startup / session orchestration / feedback / portable runtime | 20 / 8 / 44 / 42 |
| Settings and UI / storage / installer settings | 68 / 18 / 10 |
| Python cache / portable dependencies / menu reader | 5 / 34 / 8 |

The actual packaged Worker rejected a deliberately unverified executable before
launch, recorded its fingerprint, restored the test settings and left BC2,
XR-host and vrserver processes unchanged.

Cold preparation used the exact staged runtime and its bundled Python. All nine
selected files of an already downloaded decoder were rehashed against the package
recipe. No download occurred. Fresh local outputs contain 48 body parts and audio,
totaling 16,730,338 bytes; the runtime cache integrity check passed. The body cache
hash is `b0825b563c4965dc70fd3922251150ebaaef6a709e18c906c7027465e0039dc4`.
The run had a 128 MiB output budget and made no game, GPU or headset calls. Generated
game-derived data remains private and is not distributed.

## Exact artifacts

| Artifact | SHA-256 |
| --- | --- |
| Frozen packaging inputs | `79da8f158c8cfae9bb4b04b9893739a177eb1eeca42519724ccdc34c16f89685` |
| Asset recipe | `19fb1b06442f49f84f842880630721cfbe5e909718228ade3d2507f65c104e59` |
| Runtime ZIP, 16,471,480 bytes | `150dbf42c8c57eb7a89dc9f303b7a8118473b0c15b33df6ef7df22f7330130a6` |
| Setup ZIP, 16,458,773 bytes | `410be858612cdb4bd4def5a2e30f92f63360e594d67acf5bf73be056a7d081f8` |
| Package validation receipt | `8c016e77fc23d1e9a636c48ee57e8d1229a0678a6183e3908f4da1923edafcff` |
| Cold preparation receipt | `197d0cabf59e4053f5c3706d411b18b0d1ee65011768129bd3b678b7ee1a6bd0` |

Private receipts are retained under `release/validation-0.1.6-01` and
`release/asset-preparation016-01`; they are evidence identifiers, not required
public build inputs. Package tests do not establish Virtual Desktop/VDXR headset
gameplay. Opening-knife damage/repeat attacks and first-mission progression remain
unverified, the death/reload crash remains unresolved, and Nambu downstream launch
and impact alignment remain unverified. See [current state](CURRENT.md).
