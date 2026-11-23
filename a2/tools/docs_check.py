#!/usr/bin/env python3
"""Check the Stage 0 deliverables for structure and citation discipline.

Rules enforced here:

* every required document exists and is linked from README.md;
* every document opens with the reporting fields the briefing asks for
  (Status, Evidence, Decisions needed, Risks, Next step);
* every document cites at least one source URL and has a Sources section;
* local links to other documents resolve.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REQUIRED_DOCS = [
    "docs/00-compatibility-scope.md",
    "docs/01-pe-loader-requirements.md",
    "docs/02-nt-win32-dependency-map.md",
    "docs/03-a1-interface-requirements.md",
    "docs/04-test-corpus.md",
    "docs/05-risk-report.md",
    "docs/06-reactos-review.md",
    "docs/07-stage7-milestone.md",
    "docs/08-a3-graphics-requirements.md",
    "docs/09-open-questions.md",
    "docs/10-technical-findings.md",
    "docs/11-stage0-review-response.md",
    "docs/12-a1-contract-review.md",
    "docs/13-if-009-pe-fixture-contract.md",
    "docs/14-review-ledger.md",
]
REPORTING_FIELDS = ("Status", "Evidence", "Decisions needed", "Risks", "Next step")
LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
URL_PATTERN = re.compile(r"https://[^\s)>\]]+")


class DocChecker:
    def __init__(self, root: Path):
        self.root = root
        self.rows: list[dict] = []

    def add(self, scope: str, check: str, ok: bool, detail: str) -> None:
        self.rows.append({"scope": scope, "check": check, "ok": ok, "detail": detail})

    @property
    def failures(self) -> list[dict]:
        return [row for row in self.rows if not row["ok"]]

    def report(self) -> str:
        lines = [f"{'PASS' if row['ok'] else 'FAIL'} {row['scope']:<42} {row['check']:<30} {row['detail']}"
                 for row in self.rows]
        lines.append("")
        lines.append(f"{len(self.rows)} checks, {len(self.failures)} failed")
        return "\n".join(lines)

    def run(self) -> None:
        readme = self.root / "README.md"
        readme_text = readme.read_text(encoding="utf-8") if readme.exists() else ""
        self.add("README.md", "exists", readme.exists(), str(readme))

        for relative in REQUIRED_DOCS:
            path = self.root / relative
            self.add(relative, "exists", path.exists(), str(path))
            if not path.exists():
                continue
            text = path.read_text(encoding="utf-8")
            self.add(relative, "linked_from_readme", relative in readme_text, "link present" if relative in readme_text else "missing link")
            head = "\n".join(text.splitlines()[:40])
            for field in REPORTING_FIELDS:
                present = re.search(rf"^[-*]\s*{re.escape(field)}\s*:", head, re.IGNORECASE | re.MULTILINE) is not None
                self.add(relative, f"reporting_field_{field.lower().replace(' ', '_')}", present,
                         "present" if present else "missing in header block")
            self.add(relative, "has_sources_section", re.search(r"^#+\s*Sources", text, re.IGNORECASE | re.MULTILINE) is not None, "sources section")
            urls = URL_PATTERN.findall(text)
            self.add(relative, "cites_a_source", len(urls) > 0, f"{len(urls)} urls")
            for target in LINK_PATTERN.findall(text):
                if target.startswith(("http://", "https://", "#", "mailto:")):
                    continue
                resolved = (path.parent / target.split("#")[0]).resolve()
                self.add(relative, "local_link_resolves", resolved.exists(), target)

        required_outputs = [
            "Compatibility scope map",
            "PE loader requirements",
            "NT/Win32 dependency map",
            "Test corpus proposal",
            "Risk report",
        ]
        for name in required_outputs:
            self.add("README.md", f"output_listed_{name.split()[0].lower()}", name.lower() in readme_text.lower(),
                     name)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    args = parser.parse_args(argv)
    checker = DocChecker(args.root)
    checker.run()
    print(checker.report())
    return 1 if checker.failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
