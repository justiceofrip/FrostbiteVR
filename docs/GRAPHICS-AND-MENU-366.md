# Startup and checkpoint comparison — private364 / local365–366

The intermittent startup/restart crash remains unresolved. These comparisons
separate stock UI startup, VR attachment, menu control and test-infrastructure
failures. None establishes that the entire campaign is stable.

## Observations

- Normal363 crashed after the rendered checkpoint confirmation, before a new
  actor was observed. The null execute fault has the earlier AMD worker return
  site; that site does not identify the producer or prove a driver defect.
- A subsequent Continue-helper startup crashed before the private364 probe was
  attached. The probe cannot explain that particular startup crash.
- Stock365 loaded the campaign and restarted its checkpoint through the game's
  ordinary UI without injected DLLs. Both bounded external exception watches
  recorded zero exceptions. Exact actor replacement was observed. This is one
  stock comparison, not proof an intermittent stock crash cannot happen.
- Private364 attached to that stock-started game and delivered 240 stereo pairs,
  with successful native restoration/detachment. Its menu test did not begin:
  the external menu controller exited on status-file replacement error 5 before
  the probe could connect to it. Do not count this as a menu pass or game crash.
- A fresh stock UI startup and corrected controller in366 opened the VR menu,
  selected Restart Checkpoint, observed a different native actor, then opened,
  captured and closed the replacement menu. The external watcher recorded zero
  exceptions. The host emitted three downs and three automatic ups. The normal
  native menu summary was not saved, so these host counters are not presented as
  native release counters.
- The366 run exhausted the development drive during capture. Some image files
  are empty and the native report ends with a failed desktop-evidence write.
  Its explicit failure record confirms hooks disabled; the earlier complete
  shutdown object confirms native view-list restoration. The run is marked
  **partial, not passed**, despite observed menu/restart completion.

Both owned games were closed after proven detachment. Private364 source changes
were restored byte-for-byte to normal363 before continuing development.

## Corrected diagnostic file sharing

The controller now replaces an existing status file with `ReplaceFileW`; first
creation still uses a move without permission to overwrite a competing writer.
The Python reader explicitly shares deletion. The old snapshot remains readable
while a new reader sees the replacement. The actual Windows checks cover this,
transient incompatible readers and bounded failure for a persistent blocker.
Longer retry delays alone failed to solve the open-reader case on this host.
See [Microsoft's replacement contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
This change belongs to the external diagnostic controller, not BC2 gameplay.

## Graphics observation and limits

The private probe queried the existing device without changing settings. The
device reports flags1 (`D3D11_CREATE_DEVICE_SINGLETHREADED`). Querying
`ID3D11Multithread` returned `E_NOINTERFACE`; protection state is therefore
unknown, not a measured false value. No protection setting was changed.

The observed view, indexed-draw and Present boundaries used the same thread in
each run, with no recorded overlap. The bootstrap worker used a different
thread. Existing bootstrap code calls device/context and swap-chain methods
there, so thread ownership deserves correction/audit independently of whether
it caused these crashes. This observer does not cover every graphics API call,
resource lifetime or driver operation. The [single-thread device contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag)
does not make these worker calls acceptable merely because the three measured
render callbacks share a thread.

The successful366 restart used approximately 818MB of private process memory
after actor replacement, with a roughly 2GB contiguous free address region.
The executable already has Large Address Aware enabled. No executable, driver,
graphics setting, manifest or installation file was patched for these checks.

Private evidence: `stock365-start01/stock-audit.json`,
`menu364-recovery01/audit.json`, `menu366-recovery01/partial-audit.json`, matching
graphics/memory logs, `graphics364-private` source/binary receipts, and
`inspection366-validation` diagnostic scripts. The last saved player stage
remains361; this investigation does not promote363 or364.
