PY ?= python3
ZIG ?= $(PY) -m ziglang cc
TARGET ?= x86_64-windows-gnu

FIXTURES := build/console_hello.exe build/fileproc.exe build/gui_messagebox.exe build/tls_dll.dll build/cpp_throw.exe
SAMPLES := $(wildcard .cache/7zr.exe .cache/Newtonsoft.Json.dll .cache/_speedups.cp311-win_amd64.pyd)

.PHONY: all check check-static fixtures fixtures-check inspect structs mutations negative apiset-check lint docs tests evidence corpus-fetch clean

all: check

fixtures:
	$(PY) tools/build_fixtures.py --cc "$(ZIG)" --target $(TARGET)

fixtures-check: fixtures
	$(PY) tools/build_fixtures.py --cc "$(ZIG)" --target $(TARGET) --check

inspect: fixtures
	$(PY) tools/pe_inspect.py $(FIXTURES) $(SAMPLES)

structs: fixtures
	$(PY) tools/pe_structs_check.py $(FIXTURES) $(SAMPLES)

mutations: fixtures
	$(PY) tools/pe_mutate.py build/console_hello.exe --out-dir build/mutations
	$(PY) tools/pe_mutate.py .cache/_speedups.cp311-win_amd64.pyd --mutation chain_flags --mutation loadconfig_size --out-dir build/mutations

negative: fixtures
	$(PY) tools/negative_report.py

apiset-check: fixtures
	$(PY) tools/apiset.py --check-image build/console_hello.exe

lint:
	$(PY) tools/corpus_lint.py

docs:
	$(PY) tools/docs_check.py

tests: fixtures
	$(PY) -m unittest discover -s tests -v

check: structs negative apiset-check lint docs tests

check-static: lint docs
	$(PY) tools/pe_structs_check.py

corpus-fetch:
	$(PY) tools/fetch_corpus.py

evidence: fixtures
	mkdir -p docs/evidence
	{ sh tools/evidence_header.sh $@; $(PY) tools/pe_inspect.py $(FIXTURES) $(SAMPLES); } > docs/evidence/pe-inspect.txt
	{ sh tools/evidence_header.sh $@; $(PY) tools/pe_structs_check.py $(FIXTURES) $(SAMPLES); } > docs/evidence/pe-structure-checks.txt
	{ sh tools/evidence_header.sh $@; $(PY) tools/corpus_lint.py; } > docs/evidence/corpus-lint.txt
	{ sh tools/evidence_header.sh $@; $(PY) tools/finding_report.py; } > docs/evidence/technical-findings.txt
	{ sh tools/evidence_header.sh $@; $(PY) tools/negative_report.py; } > docs/evidence/negative-tests.txt
	{ sh tools/evidence_header.sh $@; $(PY) tools/apiset.py --check-image build/console_hello.exe; } > docs/evidence/apiset.txt
	{ sh tools/evidence_header.sh $@; $(PY) tools/build_fixtures.py --cc "$(ZIG)" --target $(TARGET) --check || true; } > docs/evidence/reproducibility.txt
	{ sh tools/evidence_header.sh $@; $(PY) tools/docs_check.py || true; } > docs/evidence/docs-check.txt
	{ sh tools/evidence_header.sh $@; $(PY) -m unittest discover -s tests -v 2>&1 || true; } > docs/evidence/unit-tests.txt

clean:
	rm -rf build
