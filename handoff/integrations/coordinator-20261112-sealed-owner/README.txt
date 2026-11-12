Shared sealed command ownership — November 12, 2026

Normal83/04 stopped during neutral sight approach, before contact or selection.
Its final owner-file read interval took307.9067ms with245.1568ms remaining.
Command222 was not published; ACK polling never began. No native weapon failure
or precise path/open/read/close attribution follows from that interval.

The shared PendingOwner now seals verified bytes under its exclusive FILE_OBJECT
and validates live HANDLE/pathname identity. Feed and reload publication use this
shared custody instead of reopening/rereading the owner payload for every pose.
Current process/session, preceding command/state, original deadlines and ordinary
release/rollback rules remain. Cached bytes never grant new input authority.

Affected transport/ownership/reload/Resume checks pass. Real Windows tests cover
POSIX replacement refusal under exclusive custody, metadata-only pathname identity,
detachment with a permissive control, deadline failure and disposal. Independent
review958 found no remaining issue after restoring the original reload lock guard.
This is an offline-tested structural I/O reduction, not a demonstrated latency cure.

Implementation/tests: feedback956 receipt; independent Windows evidence:
WT/reports/coordinator-20261112-feedback957/README.txt. ROOT review and actual push:
reports/coordinator-20261108-feedback721/sealed-owner/.
Original timing diagnosis: reports/coordinator-20261112-feedback954/command-timing-diagnosis.json.
Original run: reports/coordinator-20261108-feedback721/normal83/xr-game04.
Game close, native retirement, profiles and launcher settings restoration passed.

Installed normal0.1.83/source59dfee1 remains unchanged; no native rebuild, reinstall,
public release or headset acceptance. Full reload/fire/switch remains open.
