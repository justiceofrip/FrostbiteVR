# PBLB configured launch sight line: native branch correction

The first normal416 monitor run found a verified native source but no admitted
reticle: its actual PBLB GMG had `ShotConfig` camera bytes `[0,1,0]` and inherited
speed coefficient `1`. The older reader rejected both unconditionally. This was
a concrete source-admission failure, not evidence that a glyph had rendered.

A subsequent read-only bracket retained the same exact process, driver seat,
mounted gun and firing configuration. It observed `effects+0x128 == 0`, inherited
velocity at `effects+0x20` equal to `(0,0,0)`, and no additional transform. The
installed executable SHA-256 was
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
All 15 existing native code proof spans were independently reverified.

The unchanged native code establishes these branches:

| Native address | Meaning used by the read-only adapter |
| --- | --- |
| `0x57A11C–0x57A132` | A null `effects+0x128` skips the camera override before testing authored `ShotConfig+0x49`. |
| `0x57A7A7–0x57A8B6` | The later `+0x49` branch retains the earlier matrix. With the camera owner null and the other adjustment branches excluded, the two matrices are equal. |
| `0x57A73E–0x57A7CE` | Native inherited velocity is added componentwise after rotation: `configured world velocity + effects velocity × inherit coefficient`. |
| `0x57AB29–0x57AB47` | The mounted dispatch supplies multiplier `1.0` and context `effects+0x160`; the optional context transform is `effects+0x174`, already excluded. |

The corrected reader includes the camera-owner pointer and inherited velocity
in its existing double snapshot. It admits authored camera-spawn value `1` only
with the verified null owner, and supports inherited-speed coefficients `0` and
`1`. It sums velocity in native coordinates before the established Z reflection.
Active camera ownership, other camera flags, unknown coefficients, missing or
changing fields, nonfinite velocity and optional transforms still reject.

This displays the configured, unspread initial launch sight line. It does not
predict collision, gravity, spread or an impact point. The native shot witness,
source lifetime, input freshness and immutable stereo-pair rules are unchanged;
no desired HMD ray substitutes for observed native data.

Regression coverage includes the actual authored flag/coefficient combination,
nonzero inherited velocity changing normalized direction without moving the
origin, active camera ownership, unknown and nonfinite inputs, missing fields,
and changes between the two source reads. Test execution and actual-game
acceptance are recorded separately from this implementation.

The first fixture also requested 240 pairs inside an eight-second native window
but received only 116 while BC2 was in the background. Its failed evidence is
retained. The corrected private fixture requests 64 pairs, paced across the same
6.5-second control period, with a ten-second native deadline. It preserves the
single 2000–2080 ms trigger window and performs no weapon switching or movement.
