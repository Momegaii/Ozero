PYTHON ?= python3

.PHONY: help doctor check clean

help:
	@printf '%s\n' 'Ozero developer targets:' \
	  '  make doctor  Check host tools and repository readiness' \
	  '  make check   Run repository checks' \
	  '  make clean   Remove generated local artifacts'

doctor:
	$(PYTHON) tools/doctor.py

check:
	$(PYTHON) -m compileall -q tools
	$(PYTHON) tools/doctor.py --ci

clean:
	rm -rf build dist iso
	find tools -type d -name __pycache__ -prune -exec rm -rf {} +
