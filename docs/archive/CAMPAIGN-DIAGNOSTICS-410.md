# Stereo postprocess correction 410

The 409 damage capture showed blood borders in both eyes, but different tint,
blur and late darkening. Its 84 complete paired observation groups showed 19
different values within 24 native numeric fields. Every value was stable across
the six draw/capture observations for its frame. That evidence motivates this
change; it does not establish visual acceptance.

The 410 adapter scopes three disjoint groups on the extra view: three floats at
`+1560`, five at `+1570`, and a sixteen-float matrix at `+1590`. Native setters,
copy helpers and downstream numeric parameter consumers verify their types.
The remaining padding and adjacent opaque state stay outside the write spans.
Camera transforms and native simulation behavior are unchanged.

The shared scalar scope retains exact view/request/world ownership, graphics
thread, frame, tracking-space and lease checks. All groups finish or abort even
when another group fails. Partial admission restores earlier writes before
Draw; exceptions, capture failure and phase changes retain exact restoration.
Publication requires all groups to finish. No resource pointer is copied.

The existing regression suite now includes the measured 409 values, exact
whole-fixture restoration, mutation of every native binding span, partial
admission, independent restore failures and all five active scopes under a
native structured exception. Independent static review passed. Both full builds
passed 283 suites (x86: 117.33 s; x64: 38.73 s).

Source: `ce2b78c698655c72515d7c727e3249fdff7758965f1144393c50519a45d5846d`.
The dependency receipt still has 509 files; native configuration ABI 1220 and
input/frame protocols 3/7 are unchanged. Prior 409 knife-close and 408 pump
mechanical/appearance evidence is retained without repeating unchanged runs.

Actual mission08 used the bounded 18-second input route and delivered 240 stereo
pairs. All 31 collection checks and 17 postprocess checks passed. The three new
groups restored exactly after 240, 240 and 103 writes respectively, with no
rejection or restoration failure. Across 103 complete observation groups, all
309 extra-eye observations matched the primary view's 24 scoped values.

Root reviewed all 24 saved eye pairs and a full-resolution late pair. Blood
borders, tint, darkening and blur now appear consistent between eyes throughout
this captured damage window; the prior pronounced eye0 mismatch is not visible.
This is a bounded visual pass, not headset or full death/restart acceptance.
Twenty-three saved pairs join complete observation groups; the first lies on the
collection boundary and remains unjoined. One Busy publication was not delivered
and remains recorded; strict delivery of every attempted publication did not pass.

Health reached 7.5. Of 400 samples, 320 contain reads, including four client/server
disagreements; 80 reads were missing and 316 samples were coherent. No sampled
zero-health event proves death. Positive health after the run belongs to a
replacement actor and does not independently prove a successful death/restart.
The next diagnostic needs an explicit native lethal-event witness and separate
restart evidence, rather than another repetition of this damage-window check.

Four remaining read-only fallback scalars differ between views. They remain
unmodified pending proof of their actual use; field inequality alone is not a
visual failure. The capture retained 24 pairs plus eight startup snapshots,
442.97 MiB raw. All owned game processes closed and exact profile files restored.
Full campaign, actual death/restart and headset acceptance remain outstanding.

