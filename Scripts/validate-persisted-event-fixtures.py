#!/usr/bin/env python3
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "UnrealHog" / "Source" / "UnrealHog" / "Private" / "Tests" / "Fixtures"
FIXTURES = [
    FIXTURE_DIR / "PersistedEventCurrentV7.json",
    FIXTURE_DIR / "PersistedEventLegacyV4.json",
]
REQUIRED_STRING_FIELDS = ("uuid", "event", "distinct_id", "timestamp")
UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    sys.exit(1)


def uuid_version(uuid: str) -> str:
    return uuid.split("-")[2][0]


def main() -> None:
    seen_v7 = False
    seen_legacy_non_v7 = False

    for fixture in FIXTURES:
        try:
            data = json.loads(fixture.read_text(encoding="utf-8"))
        except Exception as exc:
            fail(f"{fixture}: invalid JSON: {exc}")

        if not isinstance(data, dict):
            fail(f"{fixture}: root must be an object")

        for field in REQUIRED_STRING_FIELDS:
            if not isinstance(data.get(field), str) or not data[field]:
                fail(f"{fixture}: {field} must be a non-empty string")

        if not isinstance(data.get("properties"), dict):
            fail(f"{fixture}: properties must be an object")

        uuid = data["uuid"]
        if not UUID_RE.match(uuid):
            fail(f"{fixture}: uuid is not canonical lowercase UUID text")

        if uuid_version(uuid) == "7":
            seen_v7 = True
        else:
            seen_legacy_non_v7 = True

    if not seen_v7:
        fail("missing UUIDv7 fixture")
    if not seen_legacy_non_v7:
        fail("missing legacy non-v7 fixture")


if __name__ == "__main__":
    main()
