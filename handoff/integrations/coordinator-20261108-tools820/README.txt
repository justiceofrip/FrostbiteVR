Tools820: preserve the fixed staging limit after a larger normal freeze

Native80's authorized53GiB normal-artifact freeze was rejected by the staging
planner's equality check. A build allowance may now cover the fixed52GiB stage
allowance. Actual stage decision, fresh inventory,15GiB free floor and13roots
remain unchanged; no larger staging/session budget is granted.

ROOT verified the real53GiB plan retains52GiB staging and creates no stage;
a51GiB normal allowance refuses before writes. Independent review found no
blocking issue. Evidence: canonical reports/coordinator-20261108-feedback721/tools820.
Native80 source3b69bc6, its361/358 passing normal checks and the repaired private
artifacts remain unchanged. No native rebuild or normal retest. Installed77 and
fallback76 remain unchanged; staging and actual-game checks are still required.
