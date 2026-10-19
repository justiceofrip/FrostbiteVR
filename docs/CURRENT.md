# Current state: validated 0.1.8 startup hotfix; campaign blocked

The latest published package is [0.1.8](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.8).
Its opening-mission knife does not reliably
damage enemies, preventing campaign progression. Death/reload crashes remain
unresolved. No playable-campaign, knife-damage or crash-fix claim is warranted.

This branch starts at published source commit
`b6b89755e2947d953ecc39796a29d722dc74468c` and retains its normal-r4 gameplay.
It excludes the separate unpublished knife caller/ownership changes, diagnostic
hooks, controller fixtures and navigation work.

The 0.1.8 hotfix fixes a confirmed startup failure in the external EA report:
the two native audio readers required G: and rejected the user's X: cache despite
the supported executable and exact cue hashes. They now admit absolute local
drive paths while retaining the exact size, SHA-256 and PCM/WAV checks. This is
a drive-path correction, not enrollment of another executable or blanket EA
compatibility. No files are created on C: or X: by the path regressions.

Startup and feedback use a bounded, complete native failure report when available.
The native exception writer escapes JSON and bounds malformed UTF-8. Session
failures with a reported nonzero native exit code remain failures even when
the game has already exited. NativeTrace can still return zero when the game
closes before it observes the thread result; this is not universal crash
detection. Normal game closure ends cleanly. Feedback remains local and redacted.

This isolated composition passed 293/293 C++ suites on each of x86 and x64,
135 affected launcher/setup checks, and exact-package validation with 44 checks
plus 51 existing launcher checks. Fresh installation, update from the exact
published 0.1.7 package, rollback, same-version repair, corrupted-package
rejection and settings preservation passed. The unchanged asset preparation
recipe and cache were verified with the new staged runtime, without another
cold preparation run. These checks did not start BC2 or a headset session.

The installer, runtime and update manifest were published on 2026-10-19 at
15:18:51 UTC. All three uploaded asset sizes and SHA-256 digests match the
validated files. See
[the exact hotfix scope and validation hashes](LAUNCHER-HOTFIX018.md). The source
closure is `2c285a89ee69cd02effc7ff1f09bf23c56eae8ec105a8b3a765bb94918fec7c1`.
Actual EA/VDXR headset startup remains unverified. The separately saved
`checkpoint/427-knife-investigation` branch is unfinished and is not included.

The published 0.1.7 package and asset evidence remain documented in
[LAUNCHER-HOTFIX017.md](LAUNCHER-HOTFIX017.md). They establish the previous package,
not this candidate. The exact executable gate and authored resource identity
checks are unchanged. All gameplay and headset limitations remain in effect.
