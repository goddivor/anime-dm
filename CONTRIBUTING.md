# Contributing to Anime Download Manager

## Branching model

| Branch | Role |
|--------|------|
| `master` | Production. **Protected**: no direct push, pull requests only. |
| `dev` | Integration. **Protected** as well: no direct push, pull requests only. |
| `feature/<slug>` | New feature (branched **from `dev`**). |
| `fix/<slug>` | Bug fix. |
| `refactor/<slug>` | Rework with no behavior change. |
| `chore/<slug>` | Tooling, dependencies, CI, misc. |
| `docs/<slug>` | Documentation. |

`<slug>` in **kebab-case**, short and descriptive:
`feature/headless-voe`, `fix/mailru-403`, `refactor/gui-split`.

## Workflow

```bash
# 1. Start from an up-to-date dev
git checkout dev && git pull

# 2. Create the working branch
git checkout -b feature/my-feature

# 3. Develop + commit (see conventions below), then push
git push -u origin feature/my-feature

# 4. Open a Pull Request TARGETING dev (never master)
```

- Working branches **always target `dev`** (via PR).
- `dev` and `master` are **both protected**: no direct push, everything goes through a PR.
- `master` only receives **`dev` → `master`** PRs (releases), once `dev` is stable.

## Commit conventions

- **Conventional Commits**: `type(scope): description`
- Types: `feat`, `fix`, `refactor`, `style`, `docs`, `chore`, `test`, `perf`
- **Imperative**, **English**, lowercase after the colon
  - ✅ `feat(gui): add speed limiter dialog`
  - ❌ `Added a dialog`
- **No** `Co-Authored-By` or `Generated with` trailers
- **Selective staging**: never `git add .` / `git add -A`, name the files
- Never commit sensitive files (`.env*`, `*.key`, `credentials*`…)

## Code comments

- The code must be **self-documenting through naming**: no comment that paraphrases the code.
- A comment is allowed **only on a function** (doc-comment `///`), and only when its intent is not obvious.
- Every comment must be **short, precise, clear and non-verbose** — it states the *why*, never the *what*.
- No inline comments (`//`) and no descriptive module-header blocks.

## Pull Requests

- Fill in the **PR template** (`.github/PULL_REQUEST_TEMPLATE.md`).
- Link the related issue: `Closes #123`.
- Make sure **`cargo build` passes with no warning** and `cargo test` is green.
- For any visual change: attach a **before/after screenshot**.

## Issues

Two templates available when creating one (`.github/ISSUE_TEMPLATE/`):
- 🐛 **Bug report**
- ✨ **Feature request**
