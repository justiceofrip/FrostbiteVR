# Checkpoint354: visible pump inspection and material table reader

Both full normal354 builds pass245 suites. The353 normal player stage is preserved.354 adds an isolated input-driver option
to hold the SPAS sideways, at a reachable position in front of the emulated view.
It does not change player tracking, gun calibration, native ammunition ownership
or manual mechanism rules. Normal builds explicitly disable this option. It
cannot combine with scene replacement, magazine-only, input pause or interrupted
pump fixtures. A connected regression verifies the same real consumers complete
and that missing native evidence still prevents progression.

## Native result and captured assembly

`bc2vr-recovery/inspection354-native01` passes the complete actual-game sequence:
three magazine cycles, rifle support, shoulder swaps, firing and manual pumping,
shell insertion, support return and subsequent rifle fire. All six magazine
commands have independent server and both-client receipts. Final rifle counts
are29/180; support returns110.7143ms after shell completion.240 stereo pairs are
received. Pump presentation records374 moving and1131 closed/resting paired
palettes with zero reach, pose or source rejects and zero fallbacks. Twelve
record-lock drops mean the log is not full callback coverage.

The gallery now includes the receiver, folded stock, rear locking ring and right
grip before the shot, during muzzle flash, and at sampled points through the
manual stroke. Those captured pieces remain attached; the previously reported
stock/ring separation is not visible in these samples. The muzzle and part of
the fore-end remain outside the left edge, so the gallery does not certify every
piece or every animation frame. This is emulated capture review, not a human
headset judgment of alignment, comfort or smoothness.

The depth path delivers239 valid pairs, composes174, and records no composition,
invalid-prop, missing-geometry or render failure. The warmup pair has no depth.
No additional native draw is issued. The owned game was closed after confirmed
hook detachment; other games and SteamVR were untouched.

## Reusable material investigation

`tools/bc2_shader_database.py` parses the installed DX11 database tables with
bounded counts, byte ranges, shader-container validation and exact material
references. It resolves mesh/material names through their pass links to shader
metadata, texture parameters and vertex declarations. It rejects missing or
ambiguous names and invalid references instead of selecting another weapon's
material. Serialized pointer values in vertex declarations are not treated as
runtime addresses. It neither executes nor exports original shader bytecode.
Five deterministic tests cover complete resolution and malformed boundaries.

The table-layout research reference was the author's
[BC2 map editor shader parser](https://github.com/Powback/Bad-Company-2-Map-Editor/tree/master/Assets/Scripts/Types/Graphics).
Its declarations were checked against this installation; the new reader consumes
the full SP03 and MP001 resources with no unexplained trailing bytes. No external
program was installed or run. Texture header references were also inspected in
[FBOneTools](https://github.com/AnirohDev/FBOneTools/blob/master/BFBC2Toolkit/Data/Files/TextureHeader.cs).

SP03 resolves32 of77 exact material pairs from the existing body-asset catalog.
MP001 resolves75 of77. The two unresolved entries are M16 kit-upgrade metal and
plastic variants. These are material-pair counts, not weapons or playable reloads.
The result illustrates map-specific shader coverage without loading an MP bundle
into the campaign or editing the installation.

SCAR's metal, plastic and ammo sections resolve to different texture combinations.
The weapon-named OcRGB texture alone is not a complete diffuse material. Vertex
declarations identify the packed channels; private disassembly corroborates that
different samples use different coordinate pairs. No speculative UV assignment
or material change is enabled in the player renderer. Gray body materials remain
unfinished. The next step is verified texture/UV decoding and local material
conversion, followed by renderer and native visual checks.

`tools/bc2_texture.py` now validates the observed92-byte PC2D texture header and
every BC1/BC2/BC3 mip size before wrapping a private DDS image. Its header follows
the [DDS structure specification](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header).
Four additional tests include an independent decoder and malformed mip chains.
Seven local textures decode successfully; cube textures and custom normal-map
encodings explicitly reject. The private preview shows shared `Ammo_C` contains
shell/cartridge/belt/magazine imagery, while SCAR's `OcRGB` contains packed masks.
No extracted pixels or bytecode are included in source packages. This decoder
still makes no claim about color space or which material channel uses a sample.

SCAR controller alignment still needs human acceptance. Intermittent graphics
crashes, player knife integration and remaining campaign/manual-action coverage
remain open. No whole-campaign or all-weapons acceptance is claimed.
