# Launching the bounded optic observer

The observer has explicit launch options; it no longer requires hand-editing
probe flags. These options enable filter/resource observation only. They send
no ADS action and do not select or switch weapons.

For the existing neutral monitor diagnostic, after the appropriate campaign item
is already equipped:

```powershell
.\Test-NativeStream.ps1 -OpticFilterObserve -Seconds 15 -Pairs 240
```

This selects neutral controller observations with static asynchronous view input.
It does not schedule the animated hand test or fire/reload/weapon-cycle fixtures.
The native launcher automatically enables the existing controller, angular aim,
body follow, pose observer and tracked hand prerequisites. Those adapter features
are distinct from scheduling scripted controller movement.

For an already established tracked IPC channel:

```powershell
.\Start-NativeTrace.ps1 -OpticFilterObserve -FrameChannel $token -Seconds 15
```

The underlying native CLI accepts `--optic-filter-observe` alongside
`--stream CHANNEL --seconds N`. It sets the required flags and validates the full
configuration before filesystem checks or process access. The option remains
default off, requires stream mode and a maximum 15-second duration, and rejects
continuous mode, physical reload and native action diagnostics. Start-NativeTrace
also records `optic_filter_observe` in the run manifest. The neutral test wrapper
additionally rejects scripted action, weapon selection and motion fixtures.

Native ADS selection/acknowledgement is still unbound. An absent lens-filter
callback while hip firing is a valid negative observation; this switch does not
manufacture a zoom phase. Working XM8/ACOG rendering and the removed left-trigger
ADS mapping remain unchanged. Root schedules native use after reload fixtures.

Validation executes only script option prefixes (no process/file/launch portions)
and the actual compiled CLI with a guaranteed nonexistent DLL sentinel. Accepted
CLI parses stop before FindProcess/OpenProcess; no native attachment is attempted.
The deliverable contains narrow replacements for three shared files, preserving
the separate Test-NativeStream reload preflight insertion.
