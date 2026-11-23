CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Iinclude

.PHONY: all check headers check-a1-headers matrix tests inventory clean

all: check

check: headers matrix tests inventory

headers:
	$(CC) $(CFLAGS) -fsyntax-only tests/header_smoke.c

check-a1-headers:
	@test -n "$(A1_INCLUDE)" || (echo "set A1_INCLUDE to the directory containing ozero/mm.h" >&2; exit 2)
	$(CC) -std=c11 -Wall -Wextra -Werror -I$(A1_INCLUDE) -Iinclude -fsyntax-only tests/header_smoke.c tests/a1_header_smoke.c

matrix:
	python3 tools/validate_matrix.py
	python3 tools/check_deliverables.py

tests:
	python3 -m unittest discover -s tests -p 'test_*.py'

inventory:
	sh -n tools/collect_inventory.sh

clean:
	rm -rf tools/__pycache__ tests/__pycache__
