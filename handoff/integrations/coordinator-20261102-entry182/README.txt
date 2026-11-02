Shared ordinary-XR runner entry correction — November2,2026.
Actual176/feed46/xr-game04 completed ordinary rifle->Empty->QJU draw, then refused
before reload input: the self.run directory field shadowed the run method.
Rename execution to run_stimulus and update main; retain transport directory/lock
contract and all input, observation, deadline, custody and cleanup checks.
Real constructor/main regression reproduces old WindowsPath failure; corrected
code reaches the intended250ms fresh-boundary refusal with zero input/commands.
27 transport tests pass on installedPython;21 geometry checks reused unchanged.
Root added resolved fixture cleanup containment; that affected regression passes.
Evidence: worktree reports/coordinator-20261102-feedback182/entry-fix/RESULT.txt.
Tool SHA256 17ea9b84ad76a7973a1193b354b0363786a748fbed8adddcc0e3c9f5a7052bb4.
Native/installed0.1.46 unchanged; no build/install/reload/headset/public-release claim.