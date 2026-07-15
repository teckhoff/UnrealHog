#!/usr/bin/env python3
"""Validate a published pull request title and body against repository policy."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


REQUIRED_HEADINGS = (
	"Problem",
	"Changes",
	"How did you test this code?",
	"Docs update",
	"🤖 Agent context",
)
AGENT_NOTICE = (
	"This issue or pull request was created by an automated coding agent, not a human."
)
AUTONOMY_VALUES = (
	"Human-driven (agent-assisted)",
	"Fully autonomous",
)
TITLE_PATTERN = re.compile(
	r"^(feat|fix|chore)\([a-z0-9][a-z0-9-]*\): [a-z0-9](?:.*[^.])?$"
)
HEADING_PATTERN = re.compile(r"^##[ \t]+(.+?)[ \t]*$", re.MULTILINE)
COMMENT_PATTERN = re.compile(r"<!--[\s\S]*?-->")


def _visible_text(value: str) -> str:
	return COMMENT_PATTERN.sub("", value).strip()


def _section_ranges(body: str) -> tuple[list[str], dict[str, list[str]]]:
	matches = list(HEADING_PATTERN.finditer(body))
	headings = [match.group(1) for match in matches]
	sections: dict[str, list[str]] = {}
	for index, match in enumerate(matches):
		end = matches[index + 1].start() if index + 1 < len(matches) else len(body)
		sections.setdefault(match.group(1), []).append(body[match.end():end])
	return headings, sections


def validate_title(title: str) -> list[str]:
	errors: list[str] = []
	if not title:
		errors.append("PR title is empty.")
		return errors
	if len(title) >= 72:
		errors.append("PR title must be fewer than 72 characters.")
	if not TITLE_PATTERN.fullmatch(title):
		errors.append(
			"PR title must match '(feat|fix|chore)(<lowercase-scope>): "
			"<lowercase description>' and must not end with a period."
		)
	return errors


def validate_body(body: str) -> list[str]:
	errors: list[str] = []
	if not body:
		return ["PR body is empty."]

	headings, sections = _section_ranges(body)
	positions: list[int] = []
	for heading in REQUIRED_HEADINGS:
		count = headings.count(heading)
		if count != 1:
			errors.append(
				f"Required heading '## {heading}' must appear exactly once; found {count}."
			)
		else:
			positions.append(headings.index(heading))

	if len(positions) == len(REQUIRED_HEADINGS) and positions != sorted(positions):
		errors.append("Required headings must appear in template order.")

	for heading in REQUIRED_HEADINGS:
		values = sections.get(heading, [])
		if len(values) == 1 and not _visible_text(values[0]):
			errors.append(f"Section '## {heading}' must contain reviewer-facing content.")

	if AGENT_NOTICE not in _visible_text(body):
		errors.append("PR body must include the automated-agent notice verbatim.")

	agent_values = sections.get("🤖 Agent context", [])
	if len(agent_values) == 1:
		agent_context = _visible_text(agent_values[0])
		autonomy_matches = re.findall(
			r"^\*\*Autonomy:\*\*[ \t]+(.+?)[ \t]*$", agent_context, re.MULTILINE
		)
		if autonomy_matches not in ([AUTONOMY_VALUES[0]], [AUTONOMY_VALUES[1]]):
			errors.append(
				"Agent context must select exactly one supported Autonomy value."
			)

		reviewer_context = agent_context.replace("> " + AGENT_NOTICE, "")
		reviewer_context = reviewer_context.replace(AGENT_NOTICE, "")
		reviewer_context = re.sub(
			r"^\*\*Autonomy:\*\*.*$", "", reviewer_context, flags=re.MULTILINE
		).strip()
		if not reviewer_context:
			errors.append(
				"Agent context must name the agent/tool and summarize material decisions."
			)

	test_values = sections.get("How did you test this code?", [])
	if len(test_values) == 1:
		test_section = _visible_text(test_values[0])
		if not re.search(
			r"^- \*\*Command:\*\* `Scripts/run-windows-tests\.sh`\s*$",
			test_section,
			flags=re.MULTILINE,
		):
			errors.append(
				"Test section must record command `Scripts/run-windows-tests.sh`."
			)
		for field in ("Environment", "Relevant output"):
			if not re.search(
				rf"^- \*\*{re.escape(field)}:\*\*[ \t]+\S.*$",
				test_section,
				flags=re.MULTILINE,
			):
				errors.append(f"Test section must include a non-empty {field} field.")
		if not re.search(
			r"^- \*\*Result:\*\* (Passed|Failed)\s*$",
			test_section,
			flags=re.MULTILINE,
		):
			errors.append("Test section Result must be exactly Passed or Failed.")

	return errors


def validate_pull_request(title: str, body: str) -> list[str]:
	return validate_title(title) + validate_body(body)


def _load_json(path: str) -> dict[str, Any]:
	if path == "-":
		return json.load(sys.stdin)
	with Path(path).open(encoding="utf-8") as handle:
		return json.load(handle)


def _pull_request_from_event(event: dict[str, Any]) -> tuple[str, str]:
	pull_request = event.get("pull_request")
	if not isinstance(pull_request, dict):
		raise ValueError("Event JSON does not contain a pull_request object.")
	return str(pull_request.get("title") or ""), str(pull_request.get("body") or "")


def _pull_request_from_json(value: dict[str, Any]) -> tuple[str, str]:
	return str(value.get("title") or ""), str(value.get("body") or "")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	source = parser.add_mutually_exclusive_group(required=True)
	source.add_argument("--event", metavar="PATH", help="GitHub event JSON file")
	source.add_argument(
		"--json",
		metavar="PATH",
		help="JSON object containing title and body; use '-' for stdin",
	)
	args = parser.parse_args()

	try:
		if args.event:
			title, body = _pull_request_from_event(_load_json(args.event))
		else:
			title, body = _pull_request_from_json(_load_json(args.json))
	except (OSError, json.JSONDecodeError, ValueError) as error:
		print(f"Unable to read pull request metadata: {error}", file=sys.stderr)
		return 2

	errors = validate_pull_request(title, body)
	if errors:
		print("Pull request metadata validation failed:", file=sys.stderr)
		for error in errors:
			print(f"- {error}", file=sys.stderr)
		return 1

	print("Pull request title and body follow repository policy.")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
