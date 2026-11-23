#!/usr/bin/env python3
"""Parse and resolve API set contract names against a schema file.

Windows resolves names such as ``api-ms-win-crt-runtime-l1-1-0.dll`` through an
API set schema instead of a file on disk. This module implements the name rules
that matter to a loader and a deliberately small schema so the behaviour can be
tested without a Windows installation:

* names are case-insensitive;
* two forms exist, versioned (``...-l1-1-0``) and alias (``...-l1``);
* an optional group suffix is introduced by ``~``;
* unknown contracts are an error, not a silent fallback;
* a resolved host must be a plain DLL name, not a path.

The schema in ``data/apiset-schema.json`` is a test fixture, not Microsoft's.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

CONTRACT_PATTERN = re.compile(
    r"^api-ms-win-(?P<family>[a-z0-9]+(?:-[a-z0-9]+)*)-l(?P<major>\d+)"
    r"(?:-(?P<minor>\d+)-(?P<version>\d+))?$"
)
HOST_PATTERN = re.compile(r"^[a-z0-9._-]+\.dll$")
MAX_NAME = 255


class ApiSetError(Exception):
    """Base class for resolution failures."""


class InvalidName(ApiSetError):
    """The name is malformed and cannot be an API set contract."""


class UnknownContract(ApiSetError):
    """The name is well formed but absent from the schema."""


class InvalidSchema(ApiSetError):
    """The schema itself is malformed."""


def normalise(name: str) -> str:
    if not isinstance(name, str):
        raise InvalidName("name must be a string")
    candidate = name.strip().lower()
    if not candidate:
        raise InvalidName("empty name")
    if len(candidate) > MAX_NAME:
        raise InvalidName("name too long")
    if any(character in candidate for character in ("/", "\\", "\x00", ":", " ")):
        raise InvalidName(f"name contains an illegal character: {name!r}")
    return candidate


def split_group(candidate: str) -> tuple[str, str | None]:
    if "~" not in candidate:
        return candidate, None
    contract, _, group = candidate.partition("~")
    if not group or "~" in group or not re.fullmatch(r"[a-z0-9._-]+", group):
        raise InvalidName(f"invalid group suffix in {candidate!r}")
    return contract, group


def strip_extension(candidate: str) -> str:
    return candidate[:-4] if candidate.endswith(".dll") else candidate


def is_apiset(name: str) -> bool:
    try:
        candidate = strip_extension(normalise(name))
    except InvalidName:
        return False
    contract, _ = split_group(candidate)
    return contract.startswith("api-ms-win-")


def parse(name: str) -> dict:
    candidate = normalise(name)
    contract, group = split_group(strip_extension(candidate))
    if not contract.startswith("api-ms-win-"):
        raise InvalidName(f"not an API set name: {name!r}")
    match = CONTRACT_PATTERN.match(contract)
    if not match:
        raise InvalidName(f"malformed API set contract: {name!r}")
    versioned = match.group("minor") is not None
    return {
        "name": candidate,
        "stem": strip_extension(candidate),
        "contract": contract,
        "family": match.group("family"),
        "major": int(match.group("major")),
        "minor": int(match.group("minor")) if versioned else None,
        "version": int(match.group("version")) if versioned else None,
        "versioned": versioned,
        "alias_form": not versioned,
        "group": group,
    }


class Schema:
    def __init__(self, data: dict):
        self.contracts = data.get("contracts", {})
        self.aliases = data.get("aliases", {})
        self._validate()

    @classmethod
    def load(cls, path: Path) -> "Schema":
        try:
            return cls(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, json.JSONDecodeError) as error:
            raise InvalidSchema(f"cannot read schema {path}: {error}") from error

    def _validate(self) -> None:
        for contract, entry in self.contracts.items():
            parsed = parse(contract)
            if not parsed["versioned"]:
                raise InvalidSchema(f"contract {contract!r} must use the versioned form")
            host = str(entry.get("host", "")).lower()
            if not HOST_PATTERN.match(host):
                raise InvalidSchema(f"contract {contract!r} has an invalid host {host!r}")
        for alias, target in self.aliases.items():
            parsed = parse(alias)
            if not parsed["alias_form"]:
                raise InvalidSchema(f"alias {alias!r} must use the alias form")
            if target not in self.contracts:
                raise InvalidSchema(f"alias {alias!r} points at unknown contract {target!r}")

    def resolve(self, name: str) -> dict:
        parsed = parse(name)
        contract = parsed["contract"]
        via_alias = False
        if contract not in self.contracts:
            if contract in self.aliases:
                contract = self.aliases[contract]
                via_alias = True
            else:
                raise UnknownContract(f"no schema entry for {parsed['contract']!r}")
        entry = self.contracts[contract]
        return {
            "input": parsed["name"],
            "contract": parsed["contract"],
            "resolved_contract": contract,
            "host": entry["host"],
            "kind": entry.get("kind", "versioned"),
            "group": parsed["group"],
            "via_alias": via_alias,
        }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, default=Path("data/apiset-schema.json"))
    parser.add_argument("--resolve", action="append", default=[], metavar="NAME")
    parser.add_argument("--check-image", type=Path, help="resolve every API set imported by a PE image")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        schema = Schema.load(args.schema)
    except InvalidSchema as error:
        print(f"schema error: {error}", file=sys.stderr)
        return 2

    results = []
    failures = 0
    for name in args.resolve:
        try:
            results.append(schema.resolve(name))
        except ApiSetError as error:
            results.append({"input": name, "error": type(error).__name__, "detail": str(error)})
            failures += 1

    if args.check_image:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from pe_inspect import PEImage, ParseError  # local import keeps the module usable standalone

        try:
            image = PEImage(args.check_image)
        except (ParseError, OSError) as error:
            print(f"cannot read image: {error}", file=sys.stderr)
            return 2
        for entry in image.imports():
            name = entry["dll"]
            if not is_apiset(name):
                results.append({"input": name, "kind": "literal", "host": name})
                continue
            try:
                results.append(schema.resolve(name))
            except ApiSetError as error:
                results.append({"input": name, "error": type(error).__name__, "detail": str(error)})
                failures += 1

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        for item in results:
            if "error" in item:
                print(f"ERROR {item['input']}: {item['error']}: {item['detail']}")
            else:
                suffix = " (alias)" if item.get("via_alias") else ""
                print(f"{item['input']:<44} -> {item.get('host')} [{item.get('kind')}]{suffix}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
