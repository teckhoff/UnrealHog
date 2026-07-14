#!/usr/bin/env bash
# Scripts/ci-paths.sh — shared resolver for machine-local CI paths.
# Sourced by gate scripts; do not execute directly.
#
# Resolution order for each path:
#   1. Explicit env override (what a CI runner without hooks uses)
#   2. This worktree's own CI/<name> symlink (normal case — hook ran)
#   3. The main repository's CI/<name> symlink (worktree missing hooks)
# Returns a fully resolved real path, never a symlink.
# Exit/return code 2 = environment error (do NOT treat as a test failure).

resolve_ci_path() {
    local name="$1" override_var="$2"

    # 1. Explicit override always wins.
    if [ -n "${!override_var:-}" ]; then
        readlink -f "${!override_var}"
        return 0
    fi

    # 2. Local worktree link.
    local local_link
    local_link="$(git rev-parse --show-toplevel)/CI/$name"
    if [ -e "$local_link" ]; then
        readlink -f "$local_link"
        return 0
    fi

    # 3. Main repository link (git-common-dir resolves from any worktree).
    local main_root
    main_root="$(dirname "$(git rev-parse --git-common-dir)")"
    if [ -e "$main_root/CI/$name" ]; then
        readlink -f "$main_root/CI/$name"
        return 0
    fi

    echo "error: cannot resolve CI/$name — no \$$override_var override and no" >&2
    echo "       local or main-repo symlink. Run the post-checkout hook once" >&2
    echo "       ('bash \"\$(git rev-parse --git-common-dir)/hooks/post-checkout\"')" >&2
    echo "       or set \$$override_var explicitly." >&2
    return 2
}
