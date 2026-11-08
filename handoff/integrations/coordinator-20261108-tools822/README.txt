Tools822: bounded startup publication recovery

Actual native80/01 failed replacing sim-startup.json while the menu coordinator
polled that file. The reader already closed before parsing and shared deletion;
Windows destination replacement can still fail while its handle is open.
Only sim-startup.json/session-link.json now use the existing bounded publisher,
with the original host, handshake and cleanup deadlines. Unrelated publication
remains one-shot. Failure records retain the exact write path/stage/error/count.
ROOT checks:63 startup cases/assertions and103 existing sharing assertions PASS.
Native80 source3b69bc6, its normal361x86/358x64 and frozen artifacts unchanged;
no native rebuild/retest. Installed77/fallback76 unchanged; no public release.
Actual01 is preserved failed: no native activation/gameplay/retirement claim.
All6owned processes exited; exact profiles/settings restored, normal game close.
Menu sources/options match working79. Early Stop can explain failed menu IPC;
causation unproved, so unchanged80 startup is next after this tool correction.
Evidence: canonical reports/coordinator-20261108-feedback721/tools822 and
private80/xr-game01; WT reports/coordinator-20261108-feedback822/startup-move,
feedback823/preactivation and feedback824/startup-review (same dated prefixes).
