#!/usr/bin/env python3
"""Parse an Unreal Automation report (index.json) and exit nonzero on failure.

Usage: parse_automation_report.py <path-to-index.json>

Exit codes: 0 = all tests passed, 1 = failures or no tests ran, 2 = bad report.
Output is deliberately terse and machine-greppable so zeroshot validators
can cite it as reproducible evidence.
"""
import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: parse_automation_report.py <index.json>", file=sys.stderr)
        return 2

    try:
        with open(sys.argv[1], encoding="utf-8-sig") as fh:
            report = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: cannot read report: {exc}", file=sys.stderr)
        return 2

    succeeded = report.get("succeeded", 0)
    warned = report.get("succeededWithWarnings", 0)
    failed = report.get("failed", 0)
    not_run = report.get("notRun", 0)
    tests = report.get("tests", [])

    print(f"AUTOMATION RESULT: {succeeded} passed, {warned} passed-with-warnings, "
          f"{failed} failed, {not_run} not run")

    if not tests:
        print("AUTOMATION RESULT: FAIL (no tests were discovered/executed)")
        return 1

    for test in tests:
        state = str(test.get("state", "")).lower()
        if state in ("success", "skipped"):
            continue
        name = test.get("fullTestPath") or test.get("testDisplayName", "<unknown>")
        print(f"FAILED: {name}")
        for entry in test.get("entries", []):
            event = entry.get("event", {})
            if str(event.get("type", "")).lower() in ("error", "warning"):
                msg = event.get("message", "").strip()
                filename = entry.get("filename", "")
                line = entry.get("lineNumber", "")
                loc = f" ({filename}:{line})" if filename else ""
                print(f"  {event.get('type')}: {msg}{loc}")

    if failed > 0:
        print("AUTOMATION RESULT: FAIL")
        return 1

    print("AUTOMATION RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
