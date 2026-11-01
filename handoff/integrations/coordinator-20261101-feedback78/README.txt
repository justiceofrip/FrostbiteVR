Feedback78 - original fixed-container diagnostic lease handling

Actual37 admitted a frame whose independently aging selected-secondary family
expired during delivery. The fixed-container driver now includes that original
deadline in transport refresh, holds its pose for fresh evidence and rechecks
the same deadline before publishing input. Shared feed defaults are unchanged.
No lease, command, refresh or gameplay admission bound was extended.

Fixed-container17, feed observation10, pump17 and snapshot publication7 tests
pass. One independent review found no actionable issue. Evidence and preserved
failed episode: reports/coordinator-20261101-feedback78/fixed-deadline.json and
reports/coordinator-20261101-feedback77/xr-game01. Root owns actual continuation.

Python diagnostics and status only: no native rebuild or installation. Installed
private0.1.37/source14ef23ae and its passing native341/341 checks remain applicable.
Full reload and headset acceptance remain open.
