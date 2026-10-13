# Local294: checkpoint restart during a removed rifle magazine verified

An actual-game controller run removed the original 22-round XM8 magazine,
opened the VR pause menu with its pointer and restarted the checkpoint. The
replacement player recovered stereo presentation, exited the boat, drew the
rifle, removed and returned its original magazine (22/191 to 0/191 to 22/191),
then fired and physically pumped the SPAS and inserted a shell (7/24 to 8/23).
Support returned in 134.72 ms. All three native branches independently confirmed
each magazine operation. All 240 published stereo pairs were consumed, including
166 after replacement, with no fallback, menu error, exception or ammunition loss.

This adds a private diagnostic mode for a real removed-magazine interruption;
normal player behavior is unchanged. The test retains the actual detached-item
witness and original bounded deadline rather than completing a reload on behalf
of the player. The new mode is disabled in both normal builds. Both architectures
pass all 243 suites; four relevant focused suites also pass on each architecture.

Prior293 verifies five native M95 bolt cycles through empty (magazine refill
remains native fallback); prior290 verifies eight SPAS pumps, shell insertion,
holsters and F2000 magazine return. Prior279 verifies an unfinished-pump menu
restart. These are separate preserved proofs, not fleet or headset acceptance.

Still open: sustained campaign/death recovery, retiring unfinished pump debt with
the rifle selected first, native pistol/slide and belt-fed loading, broader weapon
enrollment, M95 manual magazine loading, and visual headset acceptance. Seventeen
compiled magazine prerequisites do not mean seventeen accepted guns. No new
headset test was launched and nothing was published.

Evidence: `scene294-recovery-01/recovery-audit.json`.
Source: `e319710e5c7d60da8e9d0927f113788d729b54b378c2bda600289f4be91f5a4d`.

Trace: `c1cf1c25b98066531fb1489a59120747218c856fe6a28da95e3c2fd898cb984e`.
