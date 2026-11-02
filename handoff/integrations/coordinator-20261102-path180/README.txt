Shared launcher path-check race correction — November2,2026.
Actual176/feed46/xr-game02 stopped before weapon input: state.json.tmp was renamed
between Test-Path and Get-Item in Assert-FvrNoLinks, causing watchdog shutdown.
Use one terminating lookup; tolerate only ItemNotFoundException and continue
checking every ancestor. Reparse points, access errors and I/O errors still refuse.
The new deterministic test fails on original code and passes14/14 with this fix;
existing desktop-launcher51/51 passes. No native build needed: native source unchanged.
Evidence: worktree reports/coordinator-20261102-feedback180/path-race/README.txt.
Core SHA256 ead96d17b0aba7b88080dfd6c1caef2236d7f19603d647635946f699fc77b7d7.
Earlier176 import failure is corrected in private179, with four installed-Python
launch/import smoke checks. Prior frozen inputs and failed episodes are preserved.
Installed0.1.46/45fallback unchanged. No reload, headset or public release claim.