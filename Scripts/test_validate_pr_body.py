#!/usr/bin/env python3

from __future__ import annotations

import unittest
from pathlib import Path

import validate_pr_body


VALID_TITLE = "chore(workflow): enforce pull request template"
VALID_BODY = """\
## Problem

Agents sometimes publish incomplete pull request bodies.

## Changes

I added deterministic validation for the published metadata.

## How did you test this code?

- **Command:** `Scripts/run-windows-tests.sh`
- **Environment:** WSL invoking Unreal Automation Tool on Windows
- **Result:** Passed
- **Relevant output:** All UnrealHog automation tests passed.

## Docs update

Not required – this only changes repository automation.

## 🤖 Agent context

> This issue or pull request was created by an automated coding agent, not a human.

**Autonomy:** Human-driven (agent-assisted)

Codex implemented the requested policy and chose a required metadata check.
"""


class PullRequestValidationTests(unittest.TestCase):
	def test_validator_headings_match_template(self) -> None:
		template_path = Path(__file__).resolve().parents[1] / ".github" / "pull_request_template.md"
		template = template_path.read_text(encoding="utf-8")
		headings, _ = validate_pr_body._section_ranges(template)
		self.assertEqual(headings, list(validate_pr_body.REQUIRED_HEADINGS))

	def test_valid_pull_request_passes(self) -> None:
		self.assertEqual(
			validate_pr_body.validate_pull_request(VALID_TITLE, VALID_BODY), []
		)

	def test_missing_heading_fails(self) -> None:
		body = VALID_BODY.replace("## Docs update", "### Docs update")
		errors = validate_pr_body.validate_body(body)
		self.assertTrue(any("Docs update" in error for error in errors))

	def test_empty_section_fails_after_comments_are_removed(self) -> None:
		body = VALID_BODY.replace(
			"Agents sometimes publish incomplete pull request bodies.",
			"<!-- Placeholder only. -->",
		)
		errors = validate_pr_body.validate_body(body)
		self.assertIn(
			"Section '## Problem' must contain reviewer-facing content.", errors
		)

	def test_duplicate_heading_fails(self) -> None:
		body = VALID_BODY.replace("## Changes", "## Changes\n\nFirst.\n\n## Changes")
		errors = validate_pr_body.validate_body(body)
		self.assertTrue(any("found 2" in error for error in errors))

	def test_reordered_headings_fail(self) -> None:
		body = VALID_BODY.replace("## Problem", "## Temporary", 1)
		body = body.replace("## Changes", "## Problem", 1)
		body = body.replace("## Temporary", "## Changes", 1)
		errors = validate_pr_body.validate_body(body)
		self.assertIn("Required headings must appear in template order.", errors)

	def test_unresolved_autonomy_placeholder_fails(self) -> None:
		body = VALID_BODY.replace(
			"**Autonomy:** Human-driven (agent-assisted)",
			"**Autonomy:** Human-driven (agent-assisted) - or - Fully autonomous",
		)
		errors = validate_pr_body.validate_body(body)
		self.assertTrue(any("Autonomy" in error for error in errors))

	def test_missing_agent_notice_fails(self) -> None:
		body = VALID_BODY.replace("> " + validate_pr_body.AGENT_NOTICE, "")
		errors = validate_pr_body.validate_body(body)
		self.assertTrue(any("automated-agent notice" in error for error in errors))

	def test_missing_test_result_fails(self) -> None:
		body = VALID_BODY.replace("- **Result:** Passed", "- **Result:**")
		errors = validate_pr_body.validate_body(body)
		self.assertTrue(any("Result" in error for error in errors))

	def test_missing_test_environment_or_output_fails(self) -> None:
		values = {
			"Environment": "WSL invoking Unreal Automation Tool on Windows",
			"Relevant output": "All UnrealHog automation tests passed.",
		}
		for field, value in values.items():
			with self.subTest(field=field):
				body = VALID_BODY.replace(
					f"- **{field}:** {value}",
					f"- **{field}:** <!-- Missing. -->",
				)
				errors = validate_pr_body.validate_body(body)
				self.assertTrue(any(field in error for error in errors))

	def test_invalid_title_fails(self) -> None:
		errors = validate_pr_body.validate_title("Added workflow.")
		self.assertTrue(errors)


if __name__ == "__main__":
	unittest.main()
