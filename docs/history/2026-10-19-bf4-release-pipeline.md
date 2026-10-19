# 2026-10-19: BF4 CI and release pipeline

- The Linux `bf4-emulated` job now runs only the portable launcher tests. Compiling the
  BF4 C++ there needs the framework (theater link, XInput, GDI) and the Windows SDK, and
  its source globs had started pulling in BC2 files. The C++ suites run in `build.yml`.
- Releases get the OpenXR loader from Khronos' `openxr_loader_windows-1.1.61.zip` and stop
  unless `x64/bin/openxr_loader.dll` matches the pinned SHA-256 (loader binaries are never
  committed). `make_release.py --require-xr-host` refuses a package without the host or
  loader, and packages ship the third-party notices under `licenses/`.
- Emulator panel copies that return `Busy` (zero-wait backpressure) are not errors; only
  Invalid/Unavailable/Faulted fail the check. One full parallel run failed the panel
  check; the cause was not captured and three later runs passed. The emulator now prints
  its busy/error copy counts when either is non-zero.
