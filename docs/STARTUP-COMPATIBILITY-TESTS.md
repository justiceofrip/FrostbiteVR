# Startup compatibility checks

Exercise observable launch behavior, not only individual helper functions.
Every reproduced tester failure should leave a regression at the boundary
where it occurred. The checkpoint429 reference is
[the menu-startup investigation](MENU-STARTUP-HOTFIX429.md).

| Scenario | Required observation | Existing coverage |
| --- | --- | --- |
| Main menu without starting campaign | Fresh menu connection becomes ready with zero world input samples | `Test-LauncherStartup.ps1`; bounded `BC2NativeMenuProbe --front-end-observe-long` in the actual game |
| 1024×768, 1280×800, 1920×1080, 2560×1080 | Actual presenter emits panel, cursor and laser; off-center pointer and trigger reach its menu channel | `OpenXrMenuTests::RasterAspects`, using GPU textures and fake swapchains |
| Native cursor conversion | Original mapped pixels reach cursor/down/up; changing display or owner rejects a new press | `Bc2MenuBindingTests`, including the installed-executable argument for signature checks |
| Headset focus/tracking loss and return | Button releases; reconnect with trigger held cannot create an unintended click | `OpenXrMenuTests`, `MenuPointerTests`, `InputMenuOwnershipTests` |
| Runtime texture differences | Menu upload handles supported UNORM/SRGB/typeless cases; failed swapchains do not churn or present uninitialized pixels | `OpenXrMenuTests` |
| Failed startup leaves game open | Retry gives restart guidance before opening another headset session; explicit restart choice remains required | `Test-PortableRestart.ps1`, `Test-PortableSession.ps1` |
| Previous supervisor still cleaning up | Launcher reports cleanup pending rather than successful “already running” | `Test-PortableRestart.ps1` |
| Different drives, spaces and edited settings | Player-selected paths remain usable; malformed settings produce recoverable errors | Existing installer/settings/storage and portable-runtime suites |
| Delivered update | Exact ZIP payload installs fresh and upgrades the previous release; validation and rollback preserve settings | Private exact-package fixture through the shipped installer and Worker |

Do not substitute a small pointer-math test for the presenter test. In0.1.9
the pointer math worked, while a later host-side aspect guard removed the
tracked aim pose. An older presenter test even expected that restriction.
The desired player behavior must determine the assertion.

Keep the test layers distinct in reports:

- Fake OpenXR calls test our handling of a runtime contract; they do not
  reproduce a particular headset driver, encoder, network or compositor.
- Original-instruction emulation tests native ABI/math against fixture objects;
  it does not establish live object ownership or visible headset behavior.
- Actual game tests with emulated controllers can establish native input,
  transport, lifecycle and gameplay effects for their recorded scenario.
- Real-headset acceptance remains necessary for perceived alignment, comfort,
  latency and streaming/runtime combinations.

Record the internal render dimensions. Resizing the desktop window alone can
leave the native render target unchanged. Preserve that result as inconclusive
for the requested render resolution, rather than labeling it a 4:3 game pass.
Use bounded logs and one or two relevant images; reuse prior unchanged checks
instead of rerunning unrelated campaign diagnostics for a startup-only change.
