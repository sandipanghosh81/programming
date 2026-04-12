#!/usr/bin/env bash
# Commit (and optionally push) cpp_programs, python_programs, security, then the parent repo.
# Usage:
#   ./commit-all-repos.sh "Your message"
#   ./commit-all-repos.sh --push "Your message"
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

DO_PUSH=false
MSG=""

if [[ "${1:-}" == "--push" ]]; then
  DO_PUSH=true
  shift
fi

MSG="${*:-WIP: sync all repos}"

nested_commit() {
  local dir="$1"
  local label="$2"
  if [[ ! -d "$ROOT/$dir/.git" ]]; then
    echo "Skip $dir (not a git repo)"
    return 0
  fi
  git -C "$ROOT/$dir" add -A
  if git -C "$ROOT/$dir" diff --cached --quiet 2>/dev/null; then
    echo "[$label] nothing to commit"
    return 0
  fi
  git -C "$ROOT/$dir" commit -m "$MSG ($label)"
  echo "[$label] committed"
}

nested_push() {
  local dir="$1"
  local label="$2"
  if [[ ! -d "$ROOT/$dir/.git" ]]; then
    return 0
  fi
  git -C "$ROOT/$dir" push
  echo "[$label] pushed"
}

for pair in "cpp_programs:cpp_programs" "python_programs:python_programs" "security:security"; do
  dir="${pair%%:*}"
  label="${pair##*:}"
  nested_commit "$dir" "$label"
done

git add cpp_programs python_programs security 2>/dev/null || true
if git diff --cached --quiet 2>/dev/null; then
  echo "[programming] root: nothing to commit (gitlinks unchanged)"
else
  git commit -m "$MSG (root pointers)"
  echo "[programming] root committed"
fi

if [[ "$DO_PUSH" == true ]]; then
  for pair in "cpp_programs:cpp_programs" "python_programs:python_programs" "security:security"; do
    dir="${pair%%:*}"
    label="${pair##*:}"
    nested_push "$dir" "$label"
  done
  git push
  echo "[programming] root pushed"
fi
