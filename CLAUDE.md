@AGENTS.md

## Claude Code specifics

- Wide searches across `src/` or `docs/` go to a subagent (Explore), which returns
  the conclusion instead of file dumps.
- Before reading a file over ~50 KB, Grep for the symbol and read only that range.
- At the end of a task, update the CURRENT doc as AGENTS.md says. Do not write a
  separate handoff file: the next session starts from CURRENT.
