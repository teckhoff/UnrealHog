#!/usr/bin/env bash
# Recreate the gitignored Design/Reference/UnrealEngine symlink in a git worktree.
#
# The symlink is machine-local and untracked, so fresh worktrees do not have
# it. This script copies the symlink target from the main working tree, which
# is resolved at runtime; no machine-specific path is stored in the repo.
# If the main checkout has no symlink (engine source absent on this machine),
# the script exits successfully without creating anything.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
main_tree="$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")"

rel_path="Design/Reference/UnrealEngine"
src="$main_tree/$rel_path"
dst="$repo_root/$rel_path"

if [ "$src" = "$dst" ]; then
	exit 0 # already in the main working tree
fi

if [ ! -L "$src" ]; then
	echo "No engine source symlink in main checkout ($src); nothing to do."
	exit 0
fi

if [ -e "$dst" ] || [ -L "$dst" ]; then
	echo "$dst already exists; leaving it untouched."
	exit 0
fi

target="$(readlink "$src")"
mkdir -p "$(dirname "$dst")"
ln -s "$target" "$dst"
echo "Created $dst -> $target"
