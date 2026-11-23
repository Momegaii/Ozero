#!/usr/bin/env python3
"""Lint the compatibility test corpus manifest.

The manifest is the contract for what will be tested and what evidence is
required. The lint enforces three things that are easy to erode:

* structure (ids, categories, required fields);
* honesty (a pass/fail claim needs an execution record; a not-run entry
  may not carry observed results);
* coverage (every required category has more than one candidate, and at
  least one executable entry that is not a supporting library).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
TIERS = {"fixture", "open-source", "commercial"}
EXECUTION_STATUSES = {"not-run", "blocked", "pass", "fail"}
STATIC_STATUSES = {"done", "not-run", "blocked"}
CLAIM_PHRASES = ("full compatibility", "fully compatible", "100% compatible")
EXECUTION_EVIDENCE_FIELDS = ("environment", "command", "date", "artifacts")
EVIDENCE_LEVELS = {"none", "L0", "L1", "L2", "L3", "L4"}


class Linter:
    def __init__(self, manifest: dict, root: Path, build_manifest: Path | None = None):
        self.manifest = manifest
        self.root = root
        self.build_manifest_path = build_manifest or (root / "fixtures" / "build-manifest.json")
        self.build_manifest: dict = {}
        if self.build_manifest_path.exists():
            try:
                self.build_manifest = json.loads(self.build_manifest_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                self.build_manifest = {}
        self.fixture_entries = {entry.get("id"): entry for entry in self.build_manifest.get("fixtures", [])}
        self.rows: list[dict] = []

    def add(self, scope: str, check: str, ok: bool, detail: str) -> None:
        self.rows.append({"scope": scope, "check": check, "ok": ok, "detail": detail})

    @property
    def failures(self) -> list[dict]:
        return [row for row in self.rows if not row["ok"]]

    def report(self) -> str:
        lines = [f"{'PASS' if row['ok'] else 'FAIL'} {row['scope']:<28} {row['check']:<40} {row['detail']}"
                 for row in self.rows]
        lines.append("")
        lines.append(f"{len(self.rows)} checks, {len(self.failures)} failed")
        return "\n".join(lines)

    # ------------------------------------------------------------------
    def run(self) -> None:
        manifest = self.manifest
        policy = manifest.get("policy", {})
        for key in ("honesty", "redistribution", "claims", "sandbox", "execution_gate"):
            self.add("manifest", f"policy.{key}", bool(policy.get(key)), "present" if policy.get(key) else "missing")

        categories = manifest.get("categories", {})
        required = manifest.get("required_categories", [])
        self.add("manifest", "required_categories_non_empty", bool(required), f"{required}")
        for category in required:
            self.add("manifest", f"category_defined.{category}", category in categories,
                     "defined" if category in categories else "missing definition")

        entries = manifest.get("entries", [])
        self.add("manifest", "entry_count", len(entries) >= 10, f"{len(entries)} entries")

        seen_ids: set[str] = set()
        seen_hashes: dict[str, str] = {}
        for entry in entries:
            entry_id = entry.get("id", "<missing>")
            self._check_entry(entry, entry_id, categories)
            if entry_id in seen_ids:
                self.add(entry_id, "id_unique", False, "duplicate id")
            seen_ids.add(entry_id)
            digest = entry.get("sha256")
            if digest:
                if digest in seen_hashes:
                    self.add(entry_id, "sha256_unique", False, f"also used by {seen_hashes[digest]}")
                seen_hashes[digest] = entry_id

        for category in required:
            members = [entry for entry in entries if entry.get("category") == category]
            self.add("coverage", f"{category}_has_two_entries", len(members) >= 2, f"{len(members)} entries")
            executable = [entry for entry in members if not entry.get("supporting")]
            self.add("coverage", f"{category}_has_executable_entry", bool(executable),
                     f"{len(executable)} non-supporting")

    # ------------------------------------------------------------------
    def _check_entry(self, entry: dict, entry_id: str, categories: dict) -> None:
        self.add(entry_id, "id_format", bool(ID_PATTERN.match(entry_id)), entry_id)
        self.add(entry_id, "category_known", entry.get("category") in categories, str(entry.get("category")))
        self.add(entry_id, "tier_known", entry.get("tier") in TIERS, str(entry.get("tier")))
        self.add(entry_id, "supporting_flag", isinstance(entry.get("supporting"), bool),
                 str(entry.get("supporting")))

        for field in ("name", "version", "license", "expected_behavior"):
            self.add(entry_id, f"field_{field}", isinstance(entry.get(field), str) and entry[field].strip() != "",
                     str(entry.get(field))[:60])
        for field in ("prerequisites", "apis_exercised", "pass_criteria", "fail_signals"):
            value = entry.get(field)
            self.add(entry_id, f"field_{field}", isinstance(value, list) and len(value) > 0,
                     f"{len(value) if isinstance(value, list) else 'missing'} items")

        source = entry.get("source", {})
        source_type = source.get("type")
        self.add(entry_id, "source_type",
                 source_type in {"repo", "external-repo", "download", "planned"}, str(source_type))
        if source_type == "repo":
            path = self.root / source.get("path", "")
            self.add(entry_id, "fixture_source_exists", path.exists(), source.get("path", ""))
        if source_type in {"download", "external-repo"}:
            self.add(entry_id, "source_url", str(source.get("url", "")).startswith("https://"),
                     str(source.get("url", "")))

        digest = entry.get("sha256")
        if entry.get("tier") == "fixture":
            self.add(entry_id, "fixture_has_no_hash", digest is None, "hash not required for locally built files")
        elif entry.get("hash_pending"):
            self.add(entry_id, "hash_pending_declared", bool(entry.get("hash_pending_reason")),
                     str(entry.get("hash_pending_reason"))[:60])
            executed = entry.get("execution", {}).get("status") != "not-run"
            self.add(entry_id, "unpinned_binary_not_executed", not executed,
                     "execution is not-run" if not executed else "cannot execute an unpinned binary")
        else:
            self.add(entry_id, "sha256_format", bool(digest and SHA256_PATTERN.match(digest)), str(digest))

        self._check_static_analysis(entry, entry_id)
        self._check_execution(entry, entry_id)
        self._check_claims(entry, entry_id)
        self._check_evidence_level(entry, entry_id)
        self._check_sandbox(entry, entry_id)
        self._check_provenance(entry, entry_id)
        self._check_fixture_manifest(entry, entry_id)

    def _check_evidence_level(self, entry: dict, entry_id: str) -> None:
        level = entry.get("evidence_level")
        self.add(entry_id, "evidence_level_known", level in EVIDENCE_LEVELS, str(level))
        static_status = entry.get("static_analysis", {}).get("status")
        execution_status = entry.get("execution", {}).get("status")
        if level == "L0":
            self.add(entry_id, "L0_requires_static_analysis", static_status == "done",
                     f"static={static_status}")
        if static_status == "done" and execution_status == "not-run":
            self.add(entry_id, "static_done_means_L0", level == "L0", f"level={level}")
        if entry.get("hash_pending"):
            self.add(entry_id, "unpinned_binary_is_unanalysed", level == "none", f"level={level}")

    def _check_sandbox(self, entry: dict, entry_id: str) -> None:
        sandbox = entry.get("sandbox_required")
        self.add(entry_id, "sandbox_flag_boolean", isinstance(sandbox, bool), str(sandbox))
        if entry.get("tier") != "fixture":
            self.add(entry_id, "third_party_requires_sandbox", sandbox is True, str(sandbox))
        executed = entry.get("execution", {}).get("status") != "not-run"
        if executed:
            self.add(entry_id, "executed_entries_require_sandbox", sandbox is True, str(sandbox))

    def _check_provenance(self, entry: dict, entry_id: str) -> None:
        if entry.get("tier") == "fixture":
            return
        provenance = entry.get("provenance")
        if not isinstance(provenance, dict):
            self.add(entry_id, "provenance_present", False, "missing provenance block")
            return
        self.add(entry_id, "provenance_present", True, "present")
        self.add(entry_id, "provenance_retrieved_key", "retrieved" in provenance,
                 str(provenance.get("retrieved")))
        self.add(entry_id, "provenance_no_redistribution", provenance.get("redistribute") is False,
                 str(provenance.get("redistribute")))
        self.add(entry_id, "provenance_verify_sha256", provenance.get("verify") == "sha256",
                 str(provenance.get("verify")))
        self.add(entry_id, "provenance_method", bool(str(provenance.get("method", "")).strip()),
                 str(provenance.get("method"))[:60])

    def _check_fixture_manifest(self, entry: dict, entry_id: str) -> None:
        if entry.get("tier") != "fixture" or entry.get("source", {}).get("type") != "repo":
            return
        fixture_id = entry.get("fixture_id")
        manifest_name = entry.get("build_manifest")
        self.add(entry_id, "fixture_has_manifest", bool(fixture_id and manifest_name),
                 f"fixture_id={fixture_id} manifest={manifest_name}")
        if not fixture_id or not manifest_name:
            return
        self.add(entry_id, "fixture_manifest_loaded", bool(self.fixture_entries),
                 str(self.build_manifest_path))
        record = self.fixture_entries.get(fixture_id)
        self.add(entry_id, "fixture_manifest_entry", record is not None, str(fixture_id))
        if not record:
            return
        source = self.root / record.get("source", "")
        self.add(entry_id, "fixture_source_path", source.exists(), record.get("source", ""))
        if source.exists():
            import hashlib

            actual = hashlib.sha256(source.read_bytes()).hexdigest()
            self.add(entry_id, "fixture_source_hash_matches", actual == record.get("source_sha256"),
                     f"actual={actual[:16]}… manifest={str(record.get('source_sha256'))[:16]}…")

    def _check_static_analysis(self, entry: dict, entry_id: str) -> None:
        analysis = entry.get("static_analysis", {})
        status = analysis.get("status")
        self.add(entry_id, "static_status_known", status in STATIC_STATUSES, str(status))
        if status == "done":
            for field in ("tool", "command", "date", "observed"):
                self.add(entry_id, f"static_{field}", bool(analysis.get(field)), str(analysis.get(field))[:60])
        if status == "not-run":
            self.add(entry_id, "static_not_run_has_reason", bool(analysis.get("reason")),
                     str(analysis.get("reason"))[:60])

    def _check_execution(self, entry: dict, entry_id: str) -> None:
        execution = entry.get("execution", {})
        status = execution.get("status")
        self.add(entry_id, "execution_status_known", status in EXECUTION_STATUSES, str(status))
        if status == "not-run":
            self.add(entry_id, "not_run_has_no_observed_results", "observed" not in execution,
                     "no observed results recorded")
            self.add(entry_id, "not_run_has_reason", bool(execution.get("reason")),
                     str(execution.get("reason"))[:60])
        if status in {"pass", "fail", "blocked"}:
            for field in EXECUTION_EVIDENCE_FIELDS:
                value = execution.get(field)
                self.add(entry_id, f"evidence_{field}",
                         bool(value) and (len(value) > 0 if isinstance(value, list) else True),
                         str(value)[:60])
            approval = str(execution.get("approval", ""))
            self.add(entry_id, "execution_requires_approval",
                     bool(re.fullmatch(r"(D|XB)-\d{3}", approval)), approval or "missing")

    def _check_claims(self, entry: dict, entry_id: str) -> None:
        text = " ".join(str(entry.get(field, "")) for field in ("name", "notes", "expected_behavior")).lower()
        found = [phrase for phrase in CLAIM_PHRASES if phrase in text]
        if not found:
            self.add(entry_id, "no_unmeasured_compatibility_claim", True, "no absolute claim")
            return
        claim = entry.get("claims", {}).get("full_compatibility", {})
        supported = claim.get("measured") is True and bool(claim.get("corpus_size"))
        self.add(entry_id, "no_unmeasured_compatibility_claim", supported,
                 f"phrase {found} requires measured corpus evidence")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path("data/test-corpus.json"))
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--build-manifest", type=Path, default=None)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"cannot read manifest: {error}", file=sys.stderr)
        return 2
    build_manifest = args.build_manifest or (args.root / "fixtures" / "build-manifest.json")
    linter = Linter(manifest, args.root, build_manifest)
    linter.run()
    if args.json:
        print(json.dumps({"rows": linter.rows, "failures": len(linter.failures)}, indent=2))
    else:
        print(linter.report())
    return 1 if linter.failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
