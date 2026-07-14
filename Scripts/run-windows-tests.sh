#!/usr/bin/env bash
# Scripts/run-windows-tests.sh — zeroshot quality gate.
# Runs the UnrealHog automation tests on the Windows side from WSL2,
# resolving all machine-local paths through CI/ symlinks (see CI/README.md).
#
# Env overrides (used by CI runners that don't have the local hooks):
#   UNREALHOG_ENGINE_ROOT  engine root containing Engine/Binaries/...
#   UNREALHOG_STAGE        Windows-side staging host project directory
#   UNREALHOG_REPORTS      Windows-side automation report directory
#
# Exit codes: 0 = all tests passed
#             1 = test failures (agents: fix the code, rerun)
#             2 = environment error (agents: report it; a rerun cannot fix it)
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
# shellcheck source=Scripts/ci-paths.sh
source "$repo_root/Scripts/ci-paths.sh"

# --- Resolve machine-local geography ---------------------------------------
engine_root="$(resolve_ci_path UnrealEngine UNREALHOG_ENGINE_ROOT)" || exit 2
stage="$(resolve_ci_path HostProject UNREALHOG_STAGE)"              || exit 2
reports="$(resolve_ci_path Reports UNREALHOG_REPORTS)"              || exit 2
stagename="${stage##*/}"

editor_cmd="$engine_root/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
if [ ! -f "$editor_cmd" ]; then
    echo "error: editor binary not found at $editor_cmd" >&2
    echo "hint: is the engine drive mounted? (sudo mount -t drvfs U: /mnt/u)" >&2
    exit 2
fi

mkdir -p "$stage/Plugins"

uproject="$stage/$stagename.uproject"
if [ ! -f "$uproject" ]; then
    echo "error: expected $stagename.uproject in $stage but it wasn't found" >&2
    echo "hint: one-time setup — create the minimal Windows host project" >&2
    echo "      (see CI/README.md)." >&2
    exit 2
fi

mkdir -p "$reports"

# --- Sync plugins from this worktree into the host project -----------------
for plugin_dir in "$repo_root"/UnrealHog*/; do
    [ -d "$plugin_dir" ] || continue
    plugin_name="$(basename "$plugin_dir")"
    rsync -a --delete \
        --exclude 'Binaries/' --exclude 'Intermediate/' \
        "$plugin_dir" "$stage/Plugins/$plugin_name/"
done

# --- Serialize: one editor instance per staging dir ------------------------
lock="/tmp/unrealhog-ci.lock"
exec 9>"$lock"
flock 9

# --- Build the host project (compiles the synced plugins) ------------------
ubt="$engine_root/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe"
build_log="$reports/build.log"
uproject_win="$(wslpath -w "$uproject")"   # (already computed below; hoist it above this block)

set +e
"$ubt" "${stagename}Editor" Win64 Development \
    -Project="$uproject_win" -WaitMutex \
    > "$build_log" 2>&1
build_exit=$?
set -e

if [ "$build_exit" -ne 0 ]; then
    echo "BUILD RESULT: FAIL (UnrealBuildTool exit $build_exit)"
    grep -E "error [A-Z]+[0-9]+|: error|fatal error" "$build_log" | head -n 40
    echo "full log: $build_log"
    exit 1
fi
echo "BUILD RESULT: PASS"

# --- Invoke the Windows editor ----------------------------------------------
# Paths passed AS ARGUMENTS to the Windows process must be Windows-style.
uproject_win="$(wslpath -w "$uproject")"
reports_win="$(wslpath -w "$reports")"
editor_log="$reports/editor-stdout.log"

# Never parse a previous run's results.
rm -f "$reports/index.json"

echo "== UnrealHog automation gate =="
echo "   engine:  $engine_root"
echo "   project: $uproject_win"

# -stdout/-FullStdOutLogOutput put the editor in CI-style silent mode;
# the raw stream goes to a log file, NOT to the agent-visible output.
# The editor's exit code does not reliably reflect test results, so we
# parse the exported report instead.
set +e
"$editor_cmd" "$uproject_win" \
    -ExecCmds="Automation RunTests UnrealHog; Quit" \
    -unattended -nop4 -nosplash -NullRHI \
    -stdout -FullStdOutLogOutput \
    -ReportExportPath="$reports_win" \
    > "$editor_log" 2>&1
editor_exit=$?
set -e

if [ ! -f "$reports/index.json" ]; then
    echo "error: no automation report produced (editor exit $editor_exit)." >&2
    echo "--- last 40 lines of editor output ---" >&2
    tail -n 40 "$editor_log" >&2 || true
    echo "full log: $editor_log" >&2
    exit 2
fi

# --- Parse results (the only agent-facing output on the normal path) -------
python3 "$repo_root/Scripts/parse_automation_report.py" "$reports/index.json"
