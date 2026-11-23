#!/usr/bin/env bash
# Record commit graph, branches, remotes and clean-tree state for this repo and an optional peer.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PEER="${1:-}"

audit() {
  local repo="$1"
  echo "=================================================================="
  echo "repository: $repo"
  if [[ ! -d "$repo/.git" ]]; then
    echo "state: NOT A GIT REPOSITORY"
    return
  fi
  echo "head:       $(git -C "$repo" rev-parse HEAD 2>/dev/null || echo none)"
  echo "branch:     $(git -C "$repo" branch --show-current 2>/dev/null || echo detached)"
  echo "remotes:"
  git -C "$repo" remote -v | sed 's/^/  /' || true
  echo "branches:"
  git -C "$repo" branch -a --format='  %(refname:short) %(objectname:short) %(upstream:short)' || true
  echo "tags:"
  git -C "$repo" tag --list | sed 's/^/  /' || true
  echo "commit graph (all refs):"
  git -C "$repo" --no-pager log --all --graph --decorate --oneline | sed 's/^/  /' || true
  echo "commit count (all refs): $(git -C "$repo" rev-list --all --count 2>/dev/null || echo 0)"
  echo "tracked files: $(git -C "$repo" ls-files | wc -l)"
  echo "top-level tracked entries:"
  git -C "$repo" ls-files | awk -F/ '{print $1}' | sort -u | sed 's/^/  /'
  if [[ -n "$(git -C "$repo" status --porcelain 2>/dev/null)" ]]; then
    echo "working tree: DIRTY"
    git -C "$repo" status --porcelain | sed 's/^/  /'
  else
    echo "working tree: clean"
  fi
  echo "A1/A2/A3 refs:"
  local refs
  refs="$(git -C "$repo" for-each-ref --format='%(refname)' | grep -Ei 'a1|a2|a3' || true)"
  if [[ -n "$refs" ]]; then echo "$refs" | sed 's/^/  /'; else echo "  none"; fi
  echo "A1/A2/A3 commit subjects:"
  local log
  log="$(git -C "$repo" --no-pager log --all --format='%h %s' | grep -Ei '(^| )A[123]( |:|/)' || true)"
  if [[ -n "$log" ]]; then echo "$log" | sed 's/^/  /'; else echo "  none"; fi
}

audit "$ROOT"
[[ -n "$PEER" ]] && audit "$PEER"
