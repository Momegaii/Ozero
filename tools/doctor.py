'''Report Ozero host-tool readiness without pretending a kernel can be built yet.'''

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED_FILES = (
    'Makefile',
    'docs/ARCHITECTURE_BASELINE.md',
    'docs/decisions/0001-boot-and-kernel-language.md',
    'kernel/README.md',
    'arch/x86_64/README.md',
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ci', action='store_true', help='Use concise CI output.')
    args = parser.parse_args()

    missing = [name for name in REQUIRED_FILES if not (ROOT / name).is_file()]
    required_tools = ('python3', 'make', 'git')
    found = {name: shutil.which(name) for name in required_tools}

    if not args.ci:
        print('Ozero host readiness')
        print('====================')
    for name, path in found.items():
        print(f"{'OK' if path else 'MISSING'}: {name}" + (f' ({path})' if path else ''))
    for name in missing:
        print(f'MISSING: required repository file {name}')

    if missing:
        return 1
    if any(path is None for path in found.values()):
        print('ERROR: install the missing required host tools.')
        return 1

    print('INFO: repository/tooling scaffold is present.')
    print('INFO: kernel compiler, linker, boot protocol, and image build are NOT configured.')
    print('INFO: QEMU boot testing becomes available after the boot decision is approved.')
    print('STATUS: Stage 0 tooling scaffold; no kernel build is claimed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
