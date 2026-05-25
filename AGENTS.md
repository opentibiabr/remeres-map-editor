## Git Safety

- Before committing or pushing, always check `git status --short --branch` and `git branch -vv`.
- Never push directly to `origin/main`, including from the local `main` branch. Changes targeting `main` must go through a pull request.
- A working branch must not track `origin/main` unless the current branch is exactly `main`.
- Create new working branches under the GitHub username prefix `dudantas/`, not `codex/`, unless the user explicitly asks for another name.
- For feature/fix branches, the upstream must point to the same remote branch name, for example `dudantas/example -> origin/dudantas/example`.
- If a branch is tracking the wrong upstream, stop and fix it before committing or pushing:
  - `git branch --unset-upstream <branch>`
  - then push explicitly with `git push -u origin <branch>` only when the branch should be published.
- Prefer explicit push targets for safety: `git push origin HEAD:<branch>`.
- Never push to `origin/main` from a feature/fix branch. The repository may allow bypassing main protections, so this check is mandatory.

## Commit Policy

- Use Conventional Commit style for commit titles: `<type>(optional-scope): <summary>`.
- Prefer these commit types: `feat`, `fix`, `perf`, `refactor`, `test`, `docs`, `build`, `ci`, `chore`, and `revert`.
- Keep commit titles concise, imperative, and lowercase after the type, for example `fix: prevent inbox overflow`.
- Do not end commit titles with a period.
- Use a scope when it adds useful context, for example `fix(container): avoid stale child updates`.
- For release-only changes, use `chore: update release version to X.Y.Z`.
- Do not mix unrelated changes in the same commit unless the user explicitly asks for a single combined commit.
- Before amending or rebasing commits that may already be pushed, check the remote state and prefer `--force-with-lease` when a rewrite is explicitly approved.

## Build Policy

- Do not compile, build, or run compile-triggering validation commands unless the user explicitly asks.
- Prefer non-build checks such as `git diff --check`, focused file inspection, and static reasoning when the user did not ask for a build.

## Precompiled Header Policy

- This project uses `source/main.h` as the precompiled header through `source/CMakeLists.txt`.
- Keep `#include "main.h"` as the first include in `.cpp` files that use the project precompiled header pattern.
- When adding standard-library, wxWidgets, or third-party headers to several `.cpp` files, decide whether the header belongs in `source/main.h` instead of repeating local includes.
- When adding a new local include only needed by one implementation file, keep it local to that `.cpp` and avoid adding it to `source/main.h`.
- Prefer forward declarations in headers when possible. Do not move heavy includes into public headers unless the type definition is required there.
- For hot-path or broad runtime changes, account for compile-time impact: do not scatter expensive includes across many translation units without updating the precompiled header strategy.

## PR Communication Policy

- Do not post any PR comments/reviews automatically.
- Only post PR comments/reviews when the user explicitly asks.
- All PR comments/reviews posted by agents must be in English.
