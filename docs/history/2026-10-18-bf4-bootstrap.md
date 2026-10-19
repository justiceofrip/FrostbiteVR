# 2026-10-18 BF4 bootstrap decisions

- XR host stays out of process for BF4 (x64) too: shared with BC2, crash isolation.
- Loader is a session-scoped dxgi.dll proxy with GLOBALROOT forwarders; absent session
  block = no-op; launcher journals and removes it. Never overwrite a foreign dxgi.dll.
- Campaign only: SessionGuard fails closed; PunkBuster module, non-SP level or remote
  server locks the process for good.
- Stereo: engine's native two-pass path (BF3/BF4 S3D heritage) preferred, alternate-eye
  as fallback; both binding-gated.
- Per-frame logic moved into platform-neutral AdapterCore so the shipped logic is the
  emulated-tested logic; Win32 code is glue only.
- Windows sources type-checked on Linux against a declarations-only SDK shim.
- Holster/body-frame policy implemented ahead of bindings; persistent ownership, reset
  only on lifetime transitions.
- Stereo route changed: emulate the 3D Vision Direct driver (nvapi64.dll shim, NVIDIA's
  MIT ID table) so BF4 renders both eyes itself; capture at eye boundaries. RE of the
  renderer (R2) becomes the fallback, not the first step.
- dxgi proxy owns CreateDXGIFactory* for synchronous init before GPU enumeration.
