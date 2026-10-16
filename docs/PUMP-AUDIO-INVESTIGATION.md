# Pump audio investigation — October 16, 2026

The user reports pump audio that does not match manual movement, alongside two
SPAS failures. These are separate observations: audio timing does not establish
the cause of a mechanical failure. This investigation is against checkpoint412,
commit `6faca19`, and its retained410 source. No audio fix or headset acceptance
is claimed.

Candidate413 now supplies an observation-first adapter, verified live map
identities and private staged PCM playback. Its activation and remaining
measurements are documented in [MECHANISM-AUDIO-413.md](MECHANISM-AUDIO-413.md).
This does not change the412 findings below.

## What the checkpoint412 code owned

`Bc2PhysicalPump.cpp` recognizes physical travel and publishes the accepted rear
or forward leg. `BuildPumpPresentation` and `Bc2PumpCompanionParts.h` place the
fore-end and companion bones from that travel; they do not seek a stock animation
or audio timeline. Native firing/cycle execution has its own timing and recovery
rules. There is no pump audio dispatch or selective sound suppression in the
checkpoint412 adapter. Its shared feedback protocol only carries reload-capture and
reload-applied haptics.

The410-to412 native changes affect loading-shell presentation, not pump policy,
native-cycle timing or sound. Therefore412 cannot promise to correct this audio
mismatch. The exact native caller that starts the sound remains unobserved; the
asset findings below do not prove which animation/state transition emits it.

## Verified installed asset evidence

Read-only source: `Dist/win32/levels/mp_common/level-00.fbrb`, archive index SHA256
`6ec10fba796c14d2a05f86d3079e819c38a4aaab3ec9205d7345bd2869554965`.
Resource paths below are relative to `Sound/Weapons/Handheld/UL_shg_SPAS12/`.

| Resource or instance | Observation |
| --- | --- |
| `Spas-12_Pump_1p.dbx` | SHA256 `60bcf023dadd72ab704c2a4e8fb2a1a37d8cb993630cd21389210bb5356174d0` |
| `Audio.SoundTemplate` `9515F482-793E-420D-B107-234F1C7ECE0D` | Public `Start` event `4215727A-F597-4E25-B999-6A1B28E87A13`, event ID `0x0dc0efa5`; sampler named `Pump Action` |
| `Audio.SamplerWaveMapEntry` `594CC781-6378-4166-8DF0-D1FC428C2322` | References the single pump WaveAsset; base pitch1, `LtNone`; sampler delay has an unconnected zero default |
| `Spas-12_Pump_1p_Wave.dbx` | SHA256 `65d558c77842fc87e122453513b5952f4c29cfab7e48a81117029ffdc0a2ab1e` |
| `Audio.WaveAsset` `801F04DE-6A9A-469E-A4F0-098837B5FE3F` | Source name `Spas-12_Pump_1p.wav`, length0.60133332s, nonstreaming, nonseekable, classified `weapon_reloads_1p` |

The inspected archive exposes one named SPAS pump sample, not separate rear and
closure samples. The sound-template reference and event ID are data identities,
not callable function addresses. The inspected weapon/animation-tree DBX has no
direct pump-sound reference. Retained traces do not record audio start times.

## Shared implementation direction and missing evidence

Use presentation-only mechanism events for accepted movement/rear-stop/closure,
with later bolt and magazine stages using the same infrastructure. Deduplicate
by weapon lifetime, shot/cycle and stage across normal handling, recovery,
regrips and reconnects. Map those events to explicitly bound asset cues. Do not
alter ammunition or native firing clocks to synchronize sound.

Before enabling this, verify the native sound-start caller/ABI and local-weapon
ownership; establish selective suppression and a playback/stop backend; and
identify separate rear/closure cues or measured segments of the installed sample.
Playing the whole fixed-duration sample at the first movement cannot follow a
player who pauses halfway through the stroke. Other classes need their own data
bindings, not duplicate interaction code or an assumption that every gun shares
the SPAS event. No game assets were exported, native code changed, or game tests
run for this investigation.
