# Shell reload audio asset evidence — candidate 416

The installed SPAS reload has a looping insertion sound and a separate ending
sound. A complete insertion variation can therefore be prepared separately from
the reload-out and pump resources. The authored evidence below is now backed by
successful same-source native callback and playback diagnostics. Replacement is
enabled only in the prepared private 416 profile; acoustic and headset acceptance
remain open. The existing pump adapter and its private cache are unchanged.

## Exact authored references

`Dist/win32/levels/sp_02/level-00.fbrb` index SHA256:
`274a9e9eefea330e6eca9ae01b11a5e78d470cd03f3b396b6a39698400b0f76b`.
Its `Levels/SP_02/animationsoundmap.dbx` SHA256:
`f9078ce260589598761cb37c2b65db9b15c722118ab2546c9fc6a25563dec73b`.
The existing bounded binary DBX parser resolves these actual reference pairs:

| SPAS first-person animation | Animation GUID | Sound template GUID |
| --- | --- | --- |
| `1P_Reload` | `aa490af3-e76d-42de-81bb-2c3dce520f84` | `4b8fa1d7-5d2f-4caf-9ba9-190410e13b8c` |
| `1P_Reload_out` | `23e2232f-2bbd-48e8-8742-95bc0d15e7b3` | `f3f02982-dde4-49fa-bf8e-f97fe1d341ec` |
| `1P_Reload_in` | `b1324452-c4d7-4db6-9541-41aa42184e36` | **No binding in this SP_02 map** |

The third animation exists in the common archive; absence from this specific map
does not prove that no other runtime route could emit a sound. Do not invent its
template binding. The insertion template is also referenced by TOZ194 and
third-person animations. Template identity alone cannot authorize suppression
for an actor or weapon.

The insertion template `Spas-12_reload_1p.dbx` has SHA256
`1b111469b592f9ad3367463127614ec343f09bc09cc51f19c718bc54fa45da39`.
It selects randomly among three numbered wave entries with `LtForward` looping,
base pitch 1 and an unconnected zero delay. Reload-out references one distinct
wave with `LtNone`. Their public Start/Stop IDs are `0x0dc0efa5`/`0x7c89bb7d`;
these are data event IDs, not native callable addresses. The separate pump
template remains documented in [MECHANISM-AUDIO-413.md](MECHANISM-AUDIO-413.md).

## Locally derived candidate

`tools/bc2_shell_reload_audio_cache.py` reads only the owner's installation and
uses the existing bounded EA XAS decoder. Output must be on G: outside source.
It writes one WAV and a small manifest, under 128 KiB combined. The extractor
does not install or enable replacement. The prepared private 416 profile now
explicitly enables it after the native audits below; the installed 414 profile
remains unchanged. No game audio is committed or redistributed.

- Archive: `Dist/win32/levels/sp_common/level-00.fbrb`, index SHA256
  `57835d6ace174f83eeb46ec8bbfe3acf8a3e7fde5cb8c8e49f084950e975a842`.
- Resource: `Sound/Weapons/Handheld/UL_shg_SPAS12/Spas-12_reload_1_1p_Wave.res`,
  SHA256 `a535681077aa7c82a193fadb10353ff8701b294691ba152fd9c5e35d08947e6e`.
- Decoded PCM SHA256:
  `a516a10a23ac04ebcf76b3d371500bef8ec9a7ced6de5109a9b95fefb8b79484`.
- Candidate: `spas-shell-insert.wav`, mono 48 kHz/16 bit, 29,173 frames,
  0.607770833 s, 58,390 bytes. SHA256:
  `3ec56ca5e0e7fd245d2a7cab3b7140cf45106fc1db4a555b654ffb680f0a7b15`.

The cue uses the complete first authored insertion variation once, with 0.30
gain and 2 ms edge fades. Only codec padding is removed; there is no arbitrary
temporal cut and no graph looping. Exact raw-resource, decoded-PCM and output-WAV
hashes are checked before files are written. The source graph separates this
resource from reload-out/pump, but acoustic content and comfort have not been
accepted by listening or headset testing.

## Integration boundary

The candidate adapter in `Bc2ShellReloadAudio` defaults to observation. Observe
the exact first-person reload callbacks and manual owner before enabling
replacement with `BC2_SHELL_RELOAD_AUDIO_REPLACE=1` and the locally generated cache
directory in `BC2_SHELL_RELOAD_AUDIO_CACHE`.

The implementation emits a cue only from the physical reload adapter's applied
one-shell receipt. A bounded shared ledger deduplicates actor, item and transaction
identities. A separate native-start ledger requires this exact manual cycle's
insertion loop to have been suppressed before replacing its finishing sound or
playing an insertion cue; a finishing callback cannot create that permission.
Unknown callbacks, NPCs, third-person owners and existing pump/bolt behavior pass
through. Reload audio owns a separate bounded playback queue and does not change
pump stages, ammunition authority or hand claims.

Focused source tests cover receipt rejection and deduplication, final-shell
completion, cancellation and native-start ownership. Both normal architecture
builds passed 285/285 C++ suites with source closure
`6abac213efc4070e3e07fb6181074bde123eaf1556320a7fc096203a80923993`.
A route or driver failure disables replacement and records the outcome rather
than blocking the weapon or reporting an undelivered cue as played.

## Actual-game diagnostic acceptance — 2026-10-17

Retained private runs `overnight416/shell-audio-observe01` and
`overnight416/shell-audio-replace01` passed every audit check. Their diagnostic
changes input generation only; it uses the same tested source and normal runtime
consumers. Observe associated one committed shell with exact local insertion
and finish callbacks without suppressing sounds or playing a replacement. Replace
suppressed those two matching native starts and completed exactly one insertion
cue from that shell's applied receipt, without audio errors. Accepted pump audio
completed 2/2 queued cues in each run.

Both bounded sequences also passed magazine handling, physical pumping, shell
insertion, shoulder holstering and rifle firing, with clean hook/driver cleanup
and a responsive game. Audio diagnostics did not alter the frozen normal probe.
These results establish actual callback binding and successful playback completion;
they are not normal-player, acoustic or headset acceptance. Audible content,
volume and comfort still need listening in the headset. The private 416 profile
pins the successful audits before enabling replacement; no installed player
profile was promoted.

The later reticle branch correction changes the runtime source closure to
`f5bc3f788d4c21b39dca9f14c28b0901d9e6c2a2a4bb05ac829885e64d8ffa88`.
Its runtime delta is the reticle source reader/header and `camera_owner`
diagnostic serialization in gameplay. Pump/reload/hand/audio policy is unchanged.
The results above remain tied to initial source `6abac213`; they are not a
native audio rerun of the corrected source. New build and boat validation remain
pending, and no acoustic/headset acceptance is inferred.
