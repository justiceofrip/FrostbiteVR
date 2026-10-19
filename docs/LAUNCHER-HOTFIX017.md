# Launcher 0.1.7: recovery and asset preparation

Published as [BC2VR 0.1.7](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.7)
on 2026-10-19 at 11:39:58 UTC. All three uploaded asset sizes and SHA-256 digests
match the exact tested package. The local G: launcher was updated with settings
preserved, the verified cache installed and 0.1.6 retained for rollback. This
hotfix retains the same native VR binaries, with no new headset acceptance.

Package validation now reports expected and observed identity fields. When
identity or a listed file fails verification, feedback and recovery remain
available through the fixed official repository; runtime execution remains
blocked. Setup/update verifies the full replacement before repairing a damaged
same-version folder. A bounded journal restores interrupted uncommitted repairs,
and committed cleanup verifies the replacement before removing the backup.
Settings and a distinct verified previous version are preserved. A healthy
same-version package remains protected from replacement. A launcher whose own
scripts cannot run still requires the latest trusted Setup. Choose the existing
launcher folder containing `active.json`, preserving its settings.

Asset preparation records the failing stage, a bounded error and completion
status. Feedback includes this record and the package check. Public cache
preparation requires the exact reviewed resource contents before parsing; only
explicit archive-index provenance may differ. Configuration, geometry, skeleton,
animation and authored-profile checks remain intact. The authoring workflow keeps
its default index checks, and resource mode cannot generate enrollment metadata.

The external user's precise identity-field mismatch and asset failure remain
unconfirmed. Matching assets does not establish support for another executable,
EA/store build or Virtual Desktop/VDXR headset session. The native executable gate
is unchanged.

| Focused validation | Passed |
| --- | ---: |
| Package recovery | 37 |
| Settings/UI / desktop launcher | 70 / 51 |
| Startup / feedback | 26 / 45 |
| Asset diagnostics and cache storage | 9 |
| Package-staging Python checks | 5 |
| Resource identity, geometry and audio | 66 |

The exact staged package passed **44 package checks plus 51 launcher checks**,
including an upgrade from published 0.1.6, the real installed Worker, damaged
metadata/file recovery, same-version repair and verified rollback. Fresh cold
preparation with packaged Python passed runtime cache integrity and the exact
recipe/game completion diagnostics. All six generated files, including the
48-part body cache, match the verified 0.1.6 outputs byte for byte: 16,730,338 bytes
total. The existing decoder's nine selected files were rehashed; no download,
game launch or headset session occurred. Derived game data remains private.

Frozen packaging inputs: 122 files, manifest SHA-256
`f3b14c96efc45cb334f6ea31b6a48a691b93be67ca8b6181e86c831717c17911`.
Native source remains
`9196965b6f424cbfa33e98650fc230de273fc444de24c720218dbfa5f703b05b`;
the existing x86/x64 results remain 291/291 suites each.

| Exact artifact | SHA-256 |
| --- | --- |
| Runtime ZIP, 16,477,345 bytes | `a4704e47aa880607cc899262ee3bfe0e378fdac61a6bb32fb5456deccba592d2` |
| Setup ZIP, 16,465,796 bytes | `00d293e817e8eb41c878a9a8b0e71f55cce9f38e63263bb4a11b612af1fb283f` |
| Asset recipe | `e197fd19371bb73686cf804a882940e3b11374193be0956ba87588d4960da251` |
| Package validation receipt | `b2b00d3eb98f3df1bce369486582a73d73e6ca27564396aca9974cd4121e5568` |
| Cold preparation receipt | `97dcdfc774a2082b2cc2808ba45383e7a5a6f41d9e78dc98a0dbb49bb3849d96` |

Opening-knife damage/repeat attacks and first-mission progression remain
unverified; death/reload crashes remain unresolved; Nambu downstream launch and
impact alignment remain unverified. No full-campaign, all-weapons or new headset
acceptance is claimed. See [current state](CURRENT.md).
