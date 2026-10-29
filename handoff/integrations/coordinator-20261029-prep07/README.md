# Candidate preparation without a headset — October 29, 2026

The user confirms the headset remains unavailable and requests independent work.
Desktop black output is not a prerequisite for direct VR startup. No game, XR
session, native attachment or new normal binary build is needed for this step.

`PrepareCandidate.ps1` uses the existing installer to create a fresh private
preparation launcher under `reports/coordinator-20261029-prep07/run01`, with no
shortcut. It preserves the real launcher's active0.1.12/previous0.1.11/settings.
This private layout satisfies the original package and managed-cache guards;
frozen payload bytes and existing admission checks are unchanged.

Inputs: runtime03's verified private0.1.16 ZIP, batch01 normal runtime receipt and
the user's installed game. The existing decoder is checked against candidate pins;
there is no download. Both launcher locks are held during preparation. The Python
probe has a15s limit; asset generation has a300s limit and owned-child retirement.
AST parsing and independent wrapper review passed; two cleanup/probe review issues
were corrected before execution. All old normal binary checks remain applicable.

Recipe changes from active0.1.12 are the catalog and main-menu diagnostic script.
All prior48 catalog profiles match; the added profile is display-only Garand/Type5
holster data. Actual six-file output matches16,854,370 bytes. Its body cache is only
26,675 bytes below the existing16MiB limit; no limit was raised or feature enabled.
Cache data is private, derived from the installed game, and must not be committed
or distributed. Preparation alone provides no gameplay or headset acceptance.

Generation exited0; six file hashes/sizes and packaged cache admission passed.
Both owned Python children exited normally, locks disposed, primary pointer/settings
hashes unchanged. Receipt: `reports/coordinator-20261029-prep07/run01/receipt.json`,
SHA256 `91ee02ed341f71101e6fc8fc645fcf5e7cf661e85c57baf9b38161fc1698b25c`.

`StagePreparedCache.ps1` binds that receipt, copies only its six files plus metadata
into a fresh primary-cache staging folder, verifies each byte count/hash, then
atomically publishes the recipe folder and rechecks cache admission. Review caught
and corrected a missing join to the preparation receipt before execution.
Actual staging succeeded; `run01/cache-stage.json` records the result. The primary
cache recipe is `f31c3f3c1270f807f41b6947c2ffdb378528d0199c94e74c81212d39cbd09983`.
Active0.1.12/previous0.1.11/settings and the old cache remain in place. No candidate
runtime activation, installed startup/equipment smoke or headset acceptance occurred.
Future activation still uses the verified package/update workflow and its checks;
reuse this matching verified cache instead of regenerating it unchanged.
