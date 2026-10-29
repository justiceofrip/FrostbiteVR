# Coordinator runtime03 — October 29, 2026

Offline follow-up to runtime02 scene02; no unchanged native build or XR retry.
The two black GDI captures are identical and their client coordinates are correct.
They cannot distinguish capture failure from black physical presentation. The user
was not watching. No matching application/display error was found in05:07–05:12UTC.
The weapon preflight's `Read out of bounds` is an address guard before a completed
gameplay sample; it does not establish a startup or DirectX failure.

The agreed scene03 check passed the existing read-only menu/D3D11 reader with
three stable1920x1080 observations. The user confirmed the physical display was
black. One reversible borderless placement corrected client bounds but did not
remove black output. No gameplay input or attachment occurred; close exited0 and
all profiles were restored. Cleanup exceeded the promised window by24seconds.
No matching crash/display log or conventional game-root graphics proxy was found.

Scene04 preserved settings.ini verbatim at1024x768/MSAA3; the user again reported
physical black output. Three stable D3D11/menu samples passed at those dimensions.
No mod attachment or gameplay input occurred. Close exited0 and all three profiles
were restored within the agreed3minute window. The116module snapshot contains no
named FrostbiteVR, ReShade, RTSS, Steam overlay or game-local DirectX proxy. Signed
AMD graphics/host modules and PunkBuster are present; their presence proves no cause.
This rules out restoration of the original graphics settings as a sufficient fix,
not all possible graphics issues. Menu objects do not prove frame presentation.
Scene05 recorded1775 DXGI/Composed:Flip rows for the exact original PID and one
swapchain over9.949s:532 have display timing,1243 have NA display timing. Nine ETW
samples report zero lost events/buffers/present overflows; collector/session cleanup
passed. The user saw black; close exited0 and profiles were restored within3minutes.
These are desktop presentation events, not proof of nonblack pixels or headset speed.
Its non-admin warning remains; exact PID filtering/lifetime checks succeeded.

Scene04/05 placement receipts expose a separate flaw: the legacy helper accepted
partial overlap as contained, preserving outer bounds(-2790,0)-(-1760,797) against
the left monitor(-1920,0)-(0,1080). Only a160pixel outer strip intersected the monitor.
That limits both observations; it does not explain scene03's corrected black output.
The original-graphics route now applies position-only full containment, preserving
size/style/foreground and refusing changed identity. Seventeen offline geometry,
state and observation cases pass; six Present proof-order/geometry cases also pass
after focused review. The recovery helper/default route remain unchanged.
Next: fresh scene06 with `ObserveWindow -CaptureName before-present` and
`after-present` bracketing `PresentCheck.ps1 -SceneName scene06`. A new agreed PC
window is required. No driver or native feature change is justified.
The helper also records the first failed identity operand/getter without retry or
guard relaxation; both prior startup refusals lacked that information. Six identity
comparison cases and three corrected receipt/preservation cases pass offline.

PresentMon2.6.0 standalone CLI was downloaded from the official GitHub release;
its SHA256 matches GitHub's digest and its Intel Authenticode signature is valid:
`b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af`.
`present-tool/provenance.json` and `offline-check02/result.json` under runtime03
retain acquisition and help-only/session-enumeration checks. Initial help exit-code
expectation failed; both original logs remain. No trace/game ran during preparation.
The helper binds the original PID/path/birth, filters CSV to that PID, records10s,
retains display and dropped outcomes plus ETW loss counters, and disables input/GPU
timing. Providers collect broader events for correlation; no ETL is saved. It uses
a unique session, a25s collector watchdog,8MiB monitored output threshold within
16MiB reserved storage, and verifies collector exit before final session absence.
Review corrected cleanup races and final-flush accounting. Exit0 alone is neither
loss-free measurement nor proof of visible pixels. See the official
[versioned CLI documentation](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md).

A private0.1.16 package was staged from batch01's frozen combined normal runtime,
including finished CHAT25/26 source and CHAT07/23 work. All90 package files and
82 runtime file hashes passed after clean extraction. ZIP17,945,341bytes;
expanded43,615,228bytes. No native rebuild, installation or publication occurred.
The installed mod remains0.1.12, previous0.1.11. Native/headset acceptance is pending.

Evidence: `reports/coordinator-20261029-runtime03/{offline-findings,package-verification,witnessed-check,original-graphics-check,present-check}.json`.
Candidate: `package/BC2VR-0.1.16-win64.zip` under that evidence folder, SHA256
`043f27944f151ad9c226dcd384080a424df1906ff0b7226f576c19111e939076`.
Normal receipt SHA `a196a0b16622fb85216bad8a9bfdf7598fadea37db20dc050284e0b456004fe4`.
Its packaged recipe has no generated local asset cache yet; prepare it through the
verified installed launcher when activation/smoke checks can run. Installer flips
the active pointer immediately, so staging alone does not authorize an update.
Knife changes remain regressions/default-off sound context, not a demonstrated
new jab, damage or audio improvement. CHAT25/26 and CHAT32 ownership stays reserved.
