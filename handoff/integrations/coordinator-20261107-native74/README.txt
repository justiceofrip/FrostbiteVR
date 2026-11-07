Native74 candidate: protect frame-bridge shutdown lifetime

Close seals input/frame/feedback admission and takes the same gate used by an
active reader. A busy gate leaves mapping, feedback and graphics ownership intact;
normal graphics callbacks retry cleanup within the existing retirement budget.
BC2 retains the complete bridge until process exit when owner cleanup cannot
finish. Successful explicit close still releases its graphics resources.

The source defect is supported; actual646 crash causation is not established.
664 combined candidate and667 independent source review are retained. ROOT669
applies the eight reviewed files, preserving NativeProbe's existing line endings.
Required: normal x86/x64 builds and the new mapped-reader, hardware resource and
CRT-exit retention tests. No build, new installation or live-game pass yet.
Installed0.1.73/fallback72 is unchanged. No public release or weapon acceptance.
