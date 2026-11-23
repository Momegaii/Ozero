"""Tests for the Stage 0 tooling, including the checks that must fail.

The negative cases matter more than the positive ones: a validator that cannot
reject a bad manifest, a bad offset or an unsupported claim is not a validator.
"""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import apiset  # noqa: E402
import corpus_lint  # noqa: E402
import docs_check  # noqa: E402
import pe_inspect  # noqa: E402
import pe_mutate  # noqa: E402
import pe_structs_check  # noqa: E402

FIXTURE = ROOT / "build" / "console_hello.exe"
INVENTORY = ROOT / "data" / "pe32plus-structures.json"
MANIFEST = ROOT / "data" / "test-corpus.json"
CACHE = ROOT / ".cache"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


class InspectorTests(unittest.TestCase):
    @unittest.skipUnless(FIXTURE.exists(), "fixtures not built")
    def test_parses_pe32_plus_fixture(self) -> None:
        summary = pe_inspect.PEImage(FIXTURE).summary()
        self.assertTrue(summary["pe32_plus"])
        self.assertEqual(summary["magic"], "0x20b")
        self.assertEqual(summary["subsystem"], "windows-cui")
        self.assertGreater(len(summary["imports"]), 0)
        self.assertGreater(summary["relocations"]["entries"], 0)
        self.assertGreater(summary["exceptions"]["entries"], 0)
        self.assertIsNotNone(summary["tls"])

    def test_rejects_non_pe_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "not-pe.bin"
            path.write_bytes(b"this is not an executable")
            with self.assertRaises(pe_inspect.ParseError):
                pe_inspect.PEImage(path)

    @unittest.skipUnless(FIXTURE.exists(), "fixtures not built")
    def test_rejects_truncated_image(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "truncated.exe"
            path.write_bytes(FIXTURE.read_bytes()[:200])
            with self.assertRaises(pe_inspect.ParseError):
                pe_inspect.PEImage(path)


class StructureCheckTests(unittest.TestCase):
    def run_checks(self, inventory: dict, images: list[Path]) -> pe_structs_check.Results:
        results = pe_structs_check.Results()
        by_name = pe_structs_check.check_inventory(inventory, results)
        for image in images:
            pe_structs_check.check_image(image, inventory, by_name, results)
        return results

    def test_inventory_and_images_pass(self) -> None:
        inventory = load_json(INVENTORY)
        images = [FIXTURE] if FIXTURE.exists() else []
        results = self.run_checks(inventory, images)
        self.assertEqual(results.failures, [], msg=results.report())

    def test_detects_wrong_optional_header_offset(self) -> None:
        if not FIXTURE.exists():
            self.skipTest("fixtures not built")
        inventory = copy.deepcopy(load_json(INVENTORY))
        for structure in inventory["structures"]:
            if structure["name"] == "IMAGE_OPTIONAL_HEADER64":
                for field in structure["fields"]:
                    if field["name"] == "ImageBase":
                        field["offset"] = 25
        results = self.run_checks(inventory, [FIXTURE])
        failed_names = {row["check"] for row in results.failures}
        self.assertIn("inventory_offset_ImageBase", failed_names)

    def test_detects_missing_citation(self) -> None:
        inventory = copy.deepcopy(load_json(INVENTORY))
        inventory["structures"][0]["source"] = "a-document-that-does-not-exist"
        results = self.run_checks(inventory, [])
        failed = [row for row in results.failures if "citation" in row["check"]]
        self.assertTrue(failed)

    def test_detects_overlapping_fields(self) -> None:
        inventory = copy.deepcopy(load_json(INVENTORY))
        for structure in inventory["structures"]:
            if structure["name"] == "IMAGE_SECTION_HEADER":
                structure["fields"][1]["offset"] = 7
        results = self.run_checks(inventory, [])
        failed = [row for row in results.failures if "fields_contiguous" in row["check"]]
        self.assertTrue(failed)


class CorpusLintTests(unittest.TestCase):
    def lint(self, manifest: dict) -> corpus_lint.Linter:
        linter = corpus_lint.Linter(manifest, ROOT)
        linter.run()
        return linter

    def test_real_manifest_passes(self) -> None:
        linter = self.lint(load_json(MANIFEST))
        self.assertEqual(linter.failures, [], msg=linter.report())

    def test_no_entry_claims_to_have_run(self) -> None:
        manifest = load_json(MANIFEST)
        executed = [entry["id"] for entry in manifest["entries"]
                    if entry["execution"]["status"] != "not-run"]
        self.assertEqual(executed, [], "no execution may be recorded without evidence")

    def test_rejects_pass_without_evidence(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        manifest["entries"][0]["execution"] = {"status": "pass"}
        linter = self.lint(manifest)
        failed = {row["check"] for row in linter.failures}
        self.assertIn("evidence_command", failed)
        self.assertIn("evidence_environment", failed)

    def test_rejects_not_run_with_observed_results(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        manifest["entries"][0]["execution"] = {"status": "not-run", "reason": "x", "observed": "it worked"}
        linter = self.lint(manifest)
        failed = {row["check"] for row in linter.failures}
        self.assertIn("not_run_has_no_observed_results", failed)

    def test_rejects_duplicate_ids(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        manifest["entries"][1]["id"] = manifest["entries"][0]["id"]
        linter = self.lint(manifest)
        failed = [row for row in linter.failures if row["check"] == "id_unique"]
        self.assertTrue(failed)

    def test_rejects_executing_unpinned_binary(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        for entry in manifest["entries"]:
            if entry.get("hash_pending"):
                entry["execution"] = {"status": "blocked", "environment": "host", "command": "run",
                                      "date": "2026-11-23", "artifacts": ["log"]}
                break
        linter = self.lint(manifest)
        failed = {row["check"] for row in linter.failures}
        self.assertIn("unpinned_binary_not_executed", failed)

    def test_rejects_unmeasured_compatibility_claim(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        manifest["entries"][0]["notes"] = "full compatibility with Windows achieved"
        linter = self.lint(manifest)
        failed = [row for row in linter.failures
                  if row["check"] == "no_unmeasured_compatibility_claim"]
        self.assertTrue(failed)

    def test_rejects_missing_category_coverage(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        manifest["entries"] = [entry for entry in manifest["entries"] if entry["category"] != "directx"]
        linter = self.lint(manifest)
        failed = [row for row in linter.failures if "directx" in row["check"]]
        self.assertTrue(failed)


class DocsCheckTests(unittest.TestCase):
    def test_real_docs_pass(self) -> None:
        checker = docs_check.DocChecker(ROOT)
        checker.run()
        self.assertEqual(checker.failures, [], msg=checker.report())

    def test_detects_missing_reporting_field(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(ROOT / "docs", root / "docs")
            shutil.copy2(ROOT / "README.md", root / "README.md")
            target = root / "docs" / "00-compatibility-scope.md"
            text = target.read_text(encoding="utf-8")
            target.write_text(text.replace("- Status:", "- Removed field:", 1), encoding="utf-8")
            checker = docs_check.DocChecker(root)
            checker.run()
            failed = {row["check"] for row in checker.failures}
            self.assertIn("reporting_field_status", failed)

    def test_detects_missing_sources(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(ROOT / "docs", root / "docs")
            shutil.copy2(ROOT / "README.md", root / "README.md")
            target = root / "docs" / "05-risk-report.md"
            text = target.read_text(encoding="utf-8")
            target.write_text(text.replace("## Sources", "## References"), encoding="utf-8")
            checker = docs_check.DocChecker(root)
            checker.run()
            failed = {row["check"] for row in checker.failures}
            self.assertIn("has_sources_section", failed)


class CorpusPinTests(unittest.TestCase):
    def test_cached_samples_match_manifest_hashes(self) -> None:
        manifest = load_json(MANIFEST)
        checked = 0
        for entry in manifest["entries"]:
            digest = entry.get("sha256")
            fetch = entry.get("source", {}).get("fetch", {})
            if not digest or fetch.get("method") != "http":
                continue
            output = fetch.get("output") or fetch.get("filename")
            path = CACHE / output
            if not path.exists():
                continue
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(actual, digest, f"{entry['id']} hash mismatch")
            checked += 1
        if checked == 0:
            self.skipTest("no cached samples")




class ApiSetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.schema_path = ROOT / "data" / "apiset-schema.json"
        cls.schema = apiset.Schema.load(cls.schema_path)
        cls.raw = load_json(cls.schema_path)

    def test_resolves_versioned_contract(self) -> None:
        result = self.schema.resolve("api-ms-win-crt-runtime-l1-1-0.dll")
        self.assertEqual(result["host"], "ucrtbase.dll")
        self.assertFalse(result["via_alias"])

    def test_resolves_alias_form(self) -> None:
        result = self.schema.resolve("api-ms-win-crt-stdio-l1.dll")
        self.assertTrue(result["via_alias"])
        self.assertEqual(result["resolved_contract"], "api-ms-win-crt-stdio-l1-1-0")

    def test_is_case_insensitive(self) -> None:
        result = self.schema.resolve("API-MS-WIN-CRT-HEAP-L1-1-0.DLL")
        self.assertEqual(result["host"], "ucrtbase.dll")

    def test_group_suffix_is_preserved(self) -> None:
        result = self.schema.resolve("api-ms-win-crt-runtime-l1-1-0~x.dll")
        self.assertEqual(result["group"], "x")
        self.assertEqual(result["contract"], "api-ms-win-crt-runtime-l1-1-0")

    def test_unknown_contract_is_an_error(self) -> None:
        with self.assertRaises(apiset.UnknownContract):
            self.schema.resolve("api-ms-win-nope-l1-1-0.dll")

    def test_malformed_version_is_an_error(self) -> None:
        with self.assertRaises(apiset.InvalidName):
            self.schema.resolve("api-ms-win-crt-runtime-l1-1.dll")

    def test_path_is_rejected(self) -> None:
        with self.assertRaises(apiset.InvalidName):
            self.schema.resolve("C:\\Windows\\api-ms-win-crt-runtime-l1-1-0.dll")

    def test_schema_rejects_invalid_host(self) -> None:
        broken = copy.deepcopy(self.raw)
        broken["contracts"]["api-ms-win-crt-runtime-l1-1-0"]["host"] = "../ucrtbase.dll"
        with self.assertRaises(apiset.InvalidSchema):
            apiset.Schema(broken)

    @unittest.skipUnless(FIXTURE.exists(), "fixtures not built")
    def test_every_fixture_contract_resolves(self) -> None:
        image = pe_inspect.PEImage(FIXTURE)
        contracts = [entry["dll"] for entry in image.imports() if apiset.is_apiset(entry["dll"])]
        self.assertGreaterEqual(len(contracts), 5)
        for name in contracts:
            self.assertEqual(self.schema.resolve(name)["host"], "ucrtbase.dll")


class MutationTests(unittest.TestCase):
    CASES = [
        ("build/console_hello.exe", "sections_swap", "sections_ascending"),
        ("build/console_hello.exe", "reloc_block_size", "relocation_blocks_consistent"),
        ("build/console_hello.exe", "import_terminator", "import_descriptors_terminated"),
        ("build/console_hello.exe", "pdata_remainder", "exception_table_entry_size"),
        ("build/console_hello.exe", "pdata_swap", "exception_table_sorted"),
        ("build/console_hello.exe", "tls_callbacks_unmapped", "tls_callbacks_terminated"),
        (".cache/_speedups.cp311-win_amd64.pyd", "chain_flags", "chained_unwind_flags_clear"),
        (".cache/_speedups.cp311-win_amd64.pyd", "loadconfig_size", "load_config_size_sane"),
        ("build/console_hello.exe", "truncate_dos_header", "parse"),
        ("build/console_hello.exe", "truncate_nt_headers", "parse"),
        ("build/console_hello.exe", "truncate_optional_header", "parse"),
        ("build/console_hello.exe", "truncate_section_table", "parse"),
        ("build/console_hello.exe", "import_name_rva_zero", "import_names_mapped"),
        ("build/console_hello.exe", "import_name_rva_oob", "import_names_mapped"),
    ]

    def test_mutations_trigger_the_expected_failure(self) -> None:
        inventory = load_json(INVENTORY)
        for source_name, mutation, expected in self.CASES:
            source = ROOT / source_name
            if not source.exists():
                continue
            with self.subTest(source=source_name, mutation=mutation):
                baseline = pe_structs_check.Results()
                by_name = pe_structs_check.check_inventory(inventory, baseline)
                pe_structs_check.check_image(source, inventory, by_name, baseline)
                self.assertEqual(baseline.failures, [], "baseline must pass")
                image = pe_inspect.PEImage(source)
                data = bytearray(source.read_bytes())
                patch = pe_mutate.apply(image, data, mutation)
                payload = bytes(data[:patch["truncate_to"]]) if "truncate_to" in patch else bytes(data)
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / source.name
                    path.write_bytes(payload)
                    results = pe_structs_check.Results()
                    pe_structs_check.check_image(path, inventory, by_name, results)
                failed = {row["check"] for row in results.failures}
                self.assertIn(expected, failed, f"{mutation} did not fail {expected}: {sorted(failed)}")


class CorpusEvidenceRuleTests(unittest.TestCase):
    def lint(self, manifest: dict) -> corpus_lint.Linter:
        linter = corpus_lint.Linter(manifest, ROOT)
        linter.run()
        return linter

    def failed(self, linter: corpus_lint.Linter) -> set[str]:
        return {row["check"] for row in linter.failures}

    def test_rejects_missing_evidence_level(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        manifest["entries"][0].pop("evidence_level")
        self.assertIn("evidence_level_known", self.failed(self.lint(manifest)))

    def test_rejects_static_done_without_l0(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        for entry in manifest["entries"]:
            if entry["static_analysis"]["status"] == "done":
                entry["evidence_level"] = "none"
                break
        self.assertIn("static_done_means_L0", self.failed(self.lint(manifest)))

    def test_rejects_third_party_without_sandbox(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        for entry in manifest["entries"]:
            if entry["tier"] != "fixture":
                entry["sandbox_required"] = False
                break
        self.assertIn("third_party_requires_sandbox", self.failed(self.lint(manifest)))

    def test_rejects_analysis_of_unpinned_binary(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        for entry in manifest["entries"]:
            if entry.get("hash_pending"):
                entry["evidence_level"] = "L0"
                break
        self.assertIn("unpinned_binary_is_unanalysed", self.failed(self.lint(manifest)))

    def test_rejects_missing_provenance(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        for entry in manifest["entries"]:
            if entry["tier"] != "fixture":
                entry.pop("provenance")
                break
        self.assertIn("provenance_present", self.failed(self.lint(manifest)))

    def test_fixture_sources_match_build_manifest(self) -> None:
        build_manifest = load_json(ROOT / "fixtures" / "build-manifest.json")
        for record in build_manifest["fixtures"]:
            source = ROOT / record["source"]
            self.assertTrue(source.exists(), record["source"])
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),
                             record["source_sha256"], record["source"])
            output = ROOT / record["output"]
            if output.exists():
                self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(),
                                 record["output_sha256"], record["output"])




class StageGateTests(unittest.TestCase):
    def test_corpus_entries_remain_not_run(self) -> None:
        manifest = load_json(MANIFEST)
        for entry in manifest["entries"]:
            self.assertEqual(entry["execution"]["status"], "not-run", entry["id"])
            self.assertIn(entry["evidence_level"], {"none", "L0"}, entry["id"])

    def test_execution_requires_an_approval_id(self) -> None:
        manifest = copy.deepcopy(load_json(MANIFEST))
        entry = manifest["entries"][0]
        entry["execution"].update({
            "status": "pass", "environment": "sandbox", "command": "fixture",
            "date": "2026-11-23", "artifacts": ["stdout.txt"],
        })
        entry["evidence_level"] = "L2"
        linter = corpus_lint.Linter(manifest, ROOT)
        linter.run()
        self.assertIn("execution_requires_approval", {row["check"] for row in linter.failures})

    def test_milestones_are_gated(self) -> None:
        text = (ROOT / "docs" / "07-stage7-milestone.md").read_text(encoding="utf-8")
        for token in ("M0 and M1 are unimplemented", "D-006", "XB-002", "XB-003"):
            self.assertIn(token, text)

    def test_open_questions_use_a2_local_ids(self) -> None:
        text = (ROOT / "docs" / "09-open-questions.md").read_text(encoding="utf-8")
        for number in range(1, 16):
            self.assertIn(f"A2-Q{number}", text)
        self.assertNotIn("D-009", text)
        self.assertNotIn("D-010", text)


    def test_if_009_contract_is_specific(self) -> None:
        text = (ROOT / "docs" / "13-if-009-pe-fixture-contract.md").read_text(encoding="utf-8")
        for token in ("IF-009", "IF9-A1-1", "IF9-A3-2", "XB-003", "Not in scope",
                      "a17d126", "307b919", "IF-008", "Truncated headers"):
            self.assertIn(token, text)

    def test_fixture_manifest_has_no_host_paths(self) -> None:
        text = MANIFEST.read_text(encoding="utf-8")
        self.assertNotIn("/workspace/", text)
        self.assertNotIn(str(ROOT), text)


    def test_a1_answers_are_recorded(self) -> None:
        text = (ROOT / "docs" / "12-a1-contract-review.md").read_text(encoding="utf-8")
        for token in ("74b1471", "a17d126", "4 Accept", "6 Change required", "2 Defer",
                      "A2-Q3", "A2-Q4", "A2-Q12"):
            self.assertIn(token, text)

    def test_review_ledger_is_explicit(self) -> None:
        text = (ROOT / "docs" / "14-review-ledger.md").read_text(encoding="utf-8")
        for token in ("XB-002", "XB-003", "XB-009", "not loader approval",
                      "Not approved", "74b1471", "307b919"):
            self.assertIn(token, text)

    def test_evidence_revision_history_is_labelled(self) -> None:
        text = (ROOT / "docs" / "11-stage0-review-response.md").read_text(encoding="utf-8")
        self.assertIn("## Revision history", text)
        self.assertIn("historical, superseded", text)

    def test_a1_review_request_is_specific(self) -> None:
        text = (ROOT / "docs" / "12-a1-contract-review.md").read_text(encoding="utf-8")
        for token in ("XB-002", "A1-REV-01", "A1-REV-12", "A1-PROC-1", "A1-THR-4", "A1-EXC-2"):
            self.assertIn(token, text)


if __name__ == "__main__":
    unittest.main()
