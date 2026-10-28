# 0.1.11: dedicated launcher cache below the game folder

The supplied `launcher-last(1).log` failed before game/XR startup because
`prepare_runtime_assets.py` rejected every output under the game directory,
including its own dedicated launcher cache. The Entry caller now supplies the
managed cache root, and the tool verifies that root against its executing
installed package. The original asset generators are unchanged.

Twenty-one targeted tests passed, including actual Entry-to-tool invocation
for nested and separate launcher layouts, stock-file sentinels and rejected
redirected/linked outputs. Sixteen installation checks then passed: real Setup
package selection, fresh install, 0.1.10 update, installed Worker validation,
rollback and byte-for-byte preservation of settings and fixture data.
The Setup fixture suppresses shortcuts at the installer boundary and supplies
the fixture game path; no setup UI, game, headset or downloader was exercised.

This is a narrow overlay on the exact published 0.1.10 payload. Only
`runtime/Entry.ps1`, `runtime/tools/prepare_runtime_assets.py`,
`runtime/runtime.json` and `package.json` differ. All other 84 members and
all nine Setup helpers/bootstrap metadata remain identical. Native source
remains `bc035d08a511df6d0d57cbf7bb78689a8d7842977cbc6bee953ac5940621c133`;
the original normal build receipt remains
`6d6184ebad27f567f40d3051d8b79825e1e04dbf35e316f97980be82dca9b093`.
Gameplay checkpoint 433 and the private 434 diagnostic are not included.

The root-owned diagnostic game PID178424 closed normally after WM_CLOSE;
it took longer than the initial eight-second observation. No forced process
termination or SteamVR change occurred. Local 0.1.11 is installed and validated,
with unchanged settings and verified 0.1.10 rollback. Only managed 0.1.9 was
pruned. No game launch or asset preparation was requested during installation.

Private release evidence is in `bc2vr-recovery/release011`:

- `stage-0.1.11-01/package-receipt.json`:
  `4200a328b71fa02de5a20059c456f08048995af7df88df0c2fb53f4dd230b389`
- `validate-0.1.11-01/validation.json`:
  `28b897a2b937df47180b34c41e167df18c9b38605e0400018dff13b591478343`
- `local-0.1.11-01/result.json`
- `github-draft-verified.json` and `github-published.json`

Published at 2026-10-20T16:08:23Z:
[BC2VR v0.1.11](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.11),
release418599317, public documentation commit
`0dc3d0547a4cd751e312f6a4f93bd32b3e02fb31`. Server-side sizes and SHA-256
digests matched all three local uploads before and after publication:

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| BC2VR-Setup-0.1.11.zip | 16765959 | `2d8895238613a4e299b5eb728262bf3ade34cbc0ae270f42c0b82002d74eeaf9` |
| BC2VR-0.1.11-win64.zip | 16781292 | `ccc513eb36d072070fe129096007ae96288c009042ab8cfdc72493022f0ece9e` |
| bc2vr-update.json | 238 | `a9be06f089bebc7da5160085ac39a6cefc0f7f266100ab0db916512f3ccbcdd2` |

Public notes explicitly retain failed headset knife acceptance and unresolved
opening-encounter/campaign progression and death/reload crashes. This hotfix
is not a claim that those gameplay failures are fixed.
