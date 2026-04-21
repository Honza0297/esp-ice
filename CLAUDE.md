# CLAUDE.md

Before writing or modifying code in this repository, read
[CONTRIBUTING.md](CONTRIBUTING.md).  It is the single source of
truth for:

- repository layout and what belongs at the root, under `cmd/`,
  under `platform/`, under `vendor/`, and under `deps/`;
- the rule that all OS-conditional code lives behind `platform.h`
  (no `#ifdef`s or raw POSIX calls outside `platform/`);
- the dispatcher and option-table conventions every `ice` command
  follows;
- step-by-step walkthroughs for adding a top-level command or a
  namespace subcommand;
- testing, commit-message, and lint rules enforced by CI.

Adhere to those conventions — they are load-bearing.
