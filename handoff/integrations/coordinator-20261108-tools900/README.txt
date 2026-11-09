BC2 VR tools900: neutral menu hover handoff

Actual private82/01 stopped before campaign entry: a fresh native sample lost
menu readiness after the neutral pointer was acknowledged. It sent no click.
The menu test now permits only that settled hover to hand off for fresh image
review. It keeps the input-stop latch; it cannot press or rearm on recovery.
Non-hover input and subsequent Resume retain their original admission checks.

Validation: 50 applied Python tests pass (40 retained, 10 new). Independent901
adds12 driver cases. Its parent-helper boundary finding is fixed in successor902;
ROOT reran12 supplied and15 independent parent/review cases, all passing.
The source tool does not alter the installed runtime or native feature behavior.

Native82 e59ba741 passed364x86/361x64 normal checks, freeze and private staging.
Those unchanged native binaries are reused; no duplicate native build is needed.
Actual82/01 closed normally and restored all profiles/settings, but proves no
reload success. Installed0.1.77 and fallback0.1.76 remain unchanged.

Evidence: reports/coordinator-20261108-feedback721/menu903-applied.stderr.txt,
menu902-root-review/parent-review-result.json and private82/xr-game01/result.json.
Report-only successor902 binds the exact reviewed tool/parent changes; consumed
packet01 and the original901 negative review are preserved.
