# Development history

These are the append-only development logs, newest entry first. They are kept
for reference and are not required reading. The current state is in
[CURRENT.md](../CURRENT.md).

| Log | Contents |
| --- | --- |
| [STATUS.md](STATUS.md) | Checkpoint status summaries |
| [HANDOFF.md](HANDOFF.md) | Per-checkpoint handoff notes, build results and launch details |
| [2142-TRANSFER.md](2142-TRANSFER.md) | Lessons and policies that transfer between adapters and projects |

The logs moved here from `docs/` on 2026-10-16. Their content is unchanged
except for top-level headings that said "Current..." or "Latest...", which now
say "Historical...". Each entry describes its own checkpoint, not the present.

Relative links inside these logs were written for `docs/`. Resolve them from
there: for example `CHECKPOINT-412.md` is `../CHECKPOINT-412.md` and
`../handoff/...` is `../../handoff/...`.

Only a decision worth keeping gets a new entry: a short dated section at the top
of the most relevant log. Status changes go in [CURRENT.md](../CURRENT.md),
which is overwritten rather than appended.

## Cleanup on 2026-10-28

- One-off investigation notes moved from `docs/` to [../archive/](../archive/).
- The per-candidate design log moved out of `docs/ARCHITECTURE.md` into [2026-10-28-architecture-log.md](2026-10-28-architecture-log.md).
- `handoff/checkpoint245/` (stale pre-integration source copies) was removed. Recover it with
  `git log --diff-filter=D --oneline -- handoff/checkpoint245`, then
  `git checkout <that commit>^ -- handoff/checkpoint245`.
