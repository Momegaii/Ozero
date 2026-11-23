# XB-012 branch-protection and CODEOWNERS audit

| Field | Value |
| --- | --- |
| Status | Audit complete. **No repository setting was changed.** D-031 remains Proposed. |
| Scope | `main` protection, repository rulesets, required checks, `CODEOWNERS` |
| Repository | `Momegaii/Ozero` (public, owner type **User**) |
| Evidence | [`evidence/stage0-review/github-settings-a4-2026-11-23.txt`](../../evidence/stage0-review/github-settings-a4-2026-11-23.txt) |
| Decision | D-031, Project Director; repository admin is `@Momegaii` |
| Stage 0 | **NOT ACCEPTED**; this audit does not change D-006 |
| Date | 2026-11-23 |

## 1. Live findings

| Item | Observed | Consequence |
| --- | --- | --- |
| `main` branch metadata | `protected: false`; `protection.enabled: false`; required checks `[]` | Direct pushes, force pushes and deletion are not blocked by branch protection |
| Repository rulesets | `[]` | No ruleset supplies a required check or PR gate |
| `CODEOWNERS` on `main` | HTTP 404 | GitHub cannot enforce code-owner review on `main` |
| `CODEOWNERS` on the A4 branch | Exists, but every rule uses `@ozero/...` team slugs | The repository is owned by a **User**, not an organization; those teams do not exist, so the rules cannot resolve |
| Collaborators | Only `Momegaii` with admin permission | A rule requiring one approval would block the sole author from merging their own PR |
| Workflow | `.github/workflows/stage0.yml` runs on `pull_request` and on `push` to `main` | The check exists, but it is not required by protection |
| Check context | `gh pr checks 3` reports `build-and-boot` pass in 4m40s (run `45202583407`) | The exact required-check context should be `build-and-boot` |
| PR #3 | Open, non-draft, head `a5fbbf9`, `MERGEABLE` / `CLEAN` | The PR gate is advisory until protection is enabled |

`main` is still `147cc86`. The audit head is `a5fbbf9`. No branch-protection, ruleset or
`CODEOWNERS` setting was changed.

## 2. Recommended settings

Two configurations are safe. Choose one explicitly; do not mix them.

### 2.1 Current single-maintainer baseline (recommended now)

This configuration blocks direct pushes and requires the existing check, while acknowledging
that the only collaborator cannot approve their own PR.

```json
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["build-and-boot"]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": true,
    "require_code_owner_reviews": false,
    "required_approving_review_count": 0,
    "require_last_push_approval": false
  },
  "restrictions": null,
  "required_linear_history": false,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "block_creations": false,
  "required_conversation_resolution": true,
  "lock_branch": false,
  "allow_fork_syncing": true
}
```

Why each choice:

- `strict: true` requires the branch to be up to date with `main` before merge.
- `build-and-boot` is the check name GitHub reports for the `stage0` workflow job. Do not use the
  workflow name `stage0` unless the check context is verified after a test PR.
- `enforce_admins: true` removes the administrator bypass. With one admin, the admin must still
  use a PR and pass the check.
- `required_approving_review_count: 0` is deliberate. Requiring one approval with only one
  collaborator would make every PR unmergeable, including this one. This is a single-maintainer
  compromise, not a claim of independent review.
- `require_code_owner_reviews: false` is required until the `CODEOWNERS` team slugs are replaced
  with a real user or the repository moves to an organization with those teams.
- `required_linear_history: false` preserves the merge commits used to integrate A2 and A3 and to
  preserve their authorship.
- Force pushes and deletions are blocked; conversation resolution is required.

Exact command, **not executed**:

```sh
gh api --method PUT repos/Momegaii/Ozero/branches/main/protection \
  --input branch-protection-single-maintainer.json
```

### 2.2 Two-reviewer target (before Stage 1 acceptance)

Use this only after a second collaborator or a real organization team exists.

```json
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["build-and-boot"]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": true,
    "require_code_owner_reviews": true,
    "required_approving_review_count": 1,
    "require_last_push_approval": true
  },
  "restrictions": null,
  "required_linear_history": false,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "block_creations": false,
  "required_conversation_resolution": true,
  "lock_branch": false,
  "allow_fork_syncing": true
}
```

This is the configuration that makes `CODEOWNERS` meaningful. It requires at least two people
who can review, and the author must not be the approver.

## 3. CODEOWNERS recommendation

The current file is a proposal, not an enforceable ownership map. The team slugs
`@ozero/a4-integration`, `@ozero/a1-kernel`, `@ozero/a2-platform` and `@ozero/a3-userland`
cannot resolve on a user-owned repository.

Recommended sequence:

1. **Now:** replace the team slugs with `@Momegaii` for every rule, and add `/a2/` for the A2
   package. This makes the file syntactically valid and records the path map, but it does not
   provide independent review.
2. **Before Stage 1 acceptance:** create an organization or add a second collaborator, create
   the four teams, and replace the placeholder slugs with the real teams. Then enable
   `require_code_owner_reviews`.

Suggested `/a2/` rule for the current file:

```text
/a2/                                @ozero/a2-platform @ozero/a4-integration
```

The A4 branch already carries this rule. It becomes effective only when the file lands on
`main` and protection requires code-owner review.

## 4. What must not be claimed

- Protection is **not enabled** now.
- Required checks are **not enabled** now.
- `CODEOWNERS` is **not enforced** on `main`.
- A passing `build-and-boot` check is not a code review and is not Stage 0 acceptance.
- The MBR harness check is not kernel boot.

## 5. Required approval

| Action | Owner | Precondition |
| --- | --- | --- |
| Enable protection and required check | Project Director, via D-031 | Explicit approval; repository admin action |
| Add a second reviewer or organization teams | Project Director | Named reviewer or organization plan |
| Change `CODEOWNERS` to real teams | A4 with Director approval | Teams exist and can review |

Until D-031 is approved, XB-012 stays **Open**.

## Sources

- Live GitHub API output: [`github-settings-a4-2026-11-23.txt`](../../evidence/stage0-review/github-settings-a4-2026-11-23.txt).
- GitHub Docs, "About protected branches": <https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches>
- GitHub Docs, "About code owners": <https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners>
- GitHub Docs, "REST API endpoints for branch protection": <https://docs.github.com/en/rest/branches/branch-protection>
