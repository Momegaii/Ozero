#!/usr/bin/env python3
"""Structural PE32/PE32+ reader used to check loader-facing assumptions.

This is an inspection tool, not a loader. It parses the headers and data
directories a user-mode loader must understand, and reports what an image
actually contains so compatibility claims can be tied to measured input
instead of assumptions.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

MACHINES = {0x014C: "i386", 0x8664: "amd64", 0xAA64: "arm64", 0x01C4: "armnt"}
SUBSYSTEMS = {1: "native", 2: "windows-gui", 3: "windows-cui", 9: "windows-ce", 10: "efi-application"}
FILE_DLL, FILE_EXECUTABLE, FILE_RELOCS_STRIPPED = 0x2000, 0x0002, 0x0001
FILE_LARGE_ADDRESS_AWARE = 0x0020
DLL_DYNAMIC_BASE, DLL_NX_COMPAT, DLL_NO_SEH = 0x0040, 0x0100, 0x0400
DLL_GUARD_CF, DLL_HIGH_ENTROPY_VA, DLL_FORCE_INTEGRITY = 0x4000, 0x0020, 0x0080
DIRECTORY_NAMES = [
    "export", "import", "resource", "exception", "certificate", "base_relocation",
    "debug", "architecture", "global_ptr", "tls", "load_config", "bound_import",
    "import_address_table", "delay_import", "clr_runtime", "reserved",
]
RELOCATION_TYPES = {
    0: "ABSOLUTE", 1: "HIGH", 2: "LOW", 3: "HIGHLOW", 4: "HIGHADJ",
    5: "MIPS_JMPADDR/ARM_MOV32/RISCV_HIGH20", 7: "THUMB_MOV32/RISCV_LOW12I",
    8: "RISCV_LOW12S", 9: "MIPS_JMPADDR16", 10: "DIR64",
}
DEBUG_TYPES = {
    0: "unknown", 1: "coff", 2: "codeview", 3: "fpo", 4: "misc", 5: "exception",
    6: "fixup", 7: "omap_to_src", 8: "omap_from_src", 9: "borland", 11: "clsid",
    12: "vc_feature", 13: "pogo", 14: "iltcg", 16: "repro", 17: "embedded_portable_pdb",
    19: "pdb_checksum", 20: "ex_dllcharacteristics",
}
UNWIND_FLAG_EHANDLER, UNWIND_FLAG_UHANDLER, UNWIND_FLAG_CHAININFO = 0x1, 0x2, 0x4


class ParseError(Exception):
    """Raised when a file cannot be interpreted as a PE image."""


class Reader:
    def __init__(self, data: bytes):
        self.data = data

    def u8(self, off: int) -> int:
        return self._unpack("<B", off)[0]

    def u16(self, off: int) -> int:
        return self._unpack("<H", off)[0]

    def u32(self, off: int) -> int:
        return self._unpack("<I", off)[0]

    def u64(self, off: int) -> int:
        return self._unpack("<Q", off)[0]

    def bytes(self, off: int, size: int) -> bytes:
        if off < 0 or size < 0 or off + size > len(self.data):
            raise ParseError(f"read of {size} bytes at {off:#x} is outside the file")
        return self.data[off:off + size]

    def cstring(self, off: int, limit: int = 512) -> str:
        end = off
        while end < len(self.data) and end - off < limit and self.data[end] != 0:
            end += 1
        return self.data[off:end].decode("ascii", "replace")

    def _unpack(self, fmt: str, off: int):
        size = struct.calcsize(fmt)
        if off < 0 or off + size > len(self.data):
            raise ParseError(f"read of {size} bytes at {off:#x} is outside the file")
        return struct.unpack_from(fmt, self.data, off)


class PEImage:
    def __init__(self, path: Path):
        self.path = Path(path)
        self.reader = Reader(self.path.read_bytes())
        self.warnings: list[str] = []
        self.sections: list[dict] = []
        self.directories: list[dict] = []
        self.parse()

    # ---- helpers -----------------------------------------------------
    def rva_to_offset(self, rva: int) -> int | None:
        if rva < self.size_of_headers:
            return rva if rva < len(self.reader.data) else None
        for section in self.sections:
            start = section["virtual_address"]
            span = max(section["virtual_size"], section["raw_size"])
            if start <= rva < start + span:
                delta = rva - start
                if delta >= section["raw_size"]:
                    return None
                offset = section["raw_offset"] + delta
                return offset if offset < len(self.reader.data) else None
        return None

    def directory(self, name: str) -> dict | None:
        for entry in self.directories:
            if entry["name"] == name and entry["virtual_address"]:
                return entry
        return None

    # ---- header parsing ----------------------------------------------
    def parse(self) -> None:
        r = self.reader
        if len(r.data) < 0x40 or r.data[:2] != b"MZ":
            raise ParseError("missing MZ signature")
        self.e_lfanew = r.u32(0x3C)
        if r.bytes(self.e_lfanew, 4) != b"PE\0\0":
            raise ParseError("missing PE signature")
        coff = self.e_lfanew + 4
        self.machine = r.u16(coff)
        self.number_of_sections = r.u16(coff + 2)
        self.timestamp = r.u32(coff + 4)
        self.pointer_to_symbols = r.u32(coff + 8)
        self.number_of_symbols = r.u32(coff + 12)
        self.size_of_optional_header = r.u16(coff + 16)
        self.characteristics = r.u16(coff + 18)
        if not 0 < self.number_of_sections <= 96:
            raise ParseError(f"implausible section count {self.number_of_sections}")
        optional = coff + 20
        self.magic = r.u16(optional)
        if self.magic == 0x20B:
            self.pe32_plus = True
            self._parse_optional64(optional)
        elif self.magic == 0x10B:
            self.pe32_plus = False
            self._parse_optional32(optional)
        else:
            raise ParseError(f"unsupported optional header magic {self.magic:#x}")
        self._parse_sections(optional + self.size_of_optional_header)
        self._parse_rich_header()

    def _parse_optional64(self, base: int) -> None:
        r = self.reader
        self.address_of_entry_point = r.u32(base + 16)
        self.base_of_code = r.u32(base + 20)
        self.image_base = r.u64(base + 24)
        self.section_alignment = r.u32(base + 32)
        self.file_alignment = r.u32(base + 36)
        self.size_of_image = r.u32(base + 56)
        self.size_of_headers = r.u32(base + 60)
        self.checksum = r.u32(base + 64)
        self.subsystem = r.u16(base + 68)
        self.dll_characteristics = r.u16(base + 70)
        self.stack_reserve = r.u64(base + 72)
        self.stack_commit = r.u64(base + 80)
        self.heap_reserve = r.u64(base + 88)
        self.heap_commit = r.u64(base + 96)
        self.number_of_rva_and_sizes = r.u32(base + 108)
        self._parse_directories(base + 112, self.number_of_rva_and_sizes)
        self.expected_optional_size = 112 + 8 * self.number_of_rva_and_sizes

    def _parse_optional32(self, base: int) -> None:
        r = self.reader
        self.address_of_entry_point = r.u32(base + 16)
        self.base_of_code = r.u32(base + 20)
        self.image_base = r.u32(base + 28)
        self.section_alignment = r.u32(base + 32)
        self.file_alignment = r.u32(base + 36)
        self.size_of_image = r.u32(base + 56)
        self.size_of_headers = r.u32(base + 60)
        self.checksum = r.u32(base + 64)
        self.subsystem = r.u16(base + 68)
        self.dll_characteristics = r.u16(base + 70)
        self.stack_reserve = r.u32(base + 72)
        self.stack_commit = r.u32(base + 76)
        self.heap_reserve = r.u32(base + 80)
        self.heap_commit = r.u32(base + 84)
        self.number_of_rva_and_sizes = r.u32(base + 92)
        self._parse_directories(base + 96, self.number_of_rva_and_sizes)
        self.expected_optional_size = 96 + 8 * self.number_of_rva_and_sizes

    def _parse_directories(self, base: int, count: int) -> None:
        if count > 16:
            self.warnings.append(f"NumberOfRvaAndSizes={count} exceeds the 16 defined entries")
        for index in range(min(count, 16)):
            rva, size = struct.unpack_from("<II", self.reader.data, base + index * 8)
            self.directories.append({
                "index": index,
                "name": DIRECTORY_NAMES[index],
                "virtual_address": rva,
                "size": size,
            })

    def _parse_sections(self, table: int) -> None:
        for index in range(self.number_of_sections):
            base = table + index * 40
            raw = self.reader.bytes(base, 40)
            name = raw[:8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", raw, 8)
            characteristics = struct.unpack_from("<I", raw, 36)[0]
            self.sections.append({
                "name": name,
                "virtual_size": virtual_size,
                "virtual_address": virtual_address,
                "raw_size": raw_size,
                "raw_offset": raw_offset,
                "characteristics": characteristics,
            })

    def _parse_rich_header(self) -> None:
        self.rich_header = None
        blob = self.reader.data[0x40:self.e_lfanew]
        index = blob.find(b"Rich")
        if index >= 0:
            self.rich_header = {"offset": 0x40 + index, "present": True}

    # ---- data directories --------------------------------------------
    def imports(self) -> list[dict]:
        directory = self.directory("import")
        if not directory:
            return []
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            self.warnings.append("import directory RVA is not mapped")
            return []
        thunk_size = 8 if self.pe32_plus else 4
        ordinal_flag = 1 << (63 if self.pe32_plus else 31)
        result = []
        for index in range(4096):
            entry = offset + index * 20
            if entry + 20 > len(self.reader.data):
                break
            original_thunk, stamp, chain, name_rva, first_thunk = struct.unpack_from(
                "<IIIII", self.reader.data, entry)
            if not any((original_thunk, stamp, chain, name_rva, first_thunk)):
                break
            name_off = self.rva_to_offset(name_rva)
            dll = self.reader.cstring(name_off) if name_off is not None else f"<unmapped rva {name_rva:#x}>"
            symbols = []
            thunk_rva = original_thunk or first_thunk
            thunk_off = self.rva_to_offset(thunk_rva) if thunk_rva else None
            if thunk_off is not None:
                for slot in range(65536):
                    position = thunk_off + slot * thunk_size
                    try:
                        value = self.reader.u64(position) if self.pe32_plus else self.reader.u32(position)
                    except ParseError:
                        break
                    if value == 0:
                        break
                    if value & ordinal_flag:
                        symbols.append({"ordinal": value & 0xFFFF})
                    else:
                        hint_name = self.rva_to_offset(value & 0x7FFFFFFF)
                        if hint_name is None:
                            symbols.append({"hint_name_rva": value})
                        else:
                            symbols.append({"hint": self.reader.u16(hint_name),
                                            "name": self.reader.cstring(hint_name + 2)})
            result.append({
                "dll": dll,
                "dll_rva": name_rva,
                "dll_offset": name_off,
                "time_date_stamp": stamp,
                "forwarder_chain": chain,
                "first_thunk_rva": first_thunk,
                "symbols": symbols,
            })
        return result

    def delay_imports(self) -> list[dict]:
        directory = self.directory("delay_import")
        if not directory:
            return []
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            return []
        result = []
        for index in range(256):
            entry = offset + index * 32
            if entry + 32 > len(self.reader.data):
                break
            fields = struct.unpack_from("<IIIIIIII", self.reader.data, entry)
            if not any(fields):
                break
            attributes, name_rva, module_rva, iat_rva, int_rva, bound_iat, unload_iat, stamp = fields
            uses_rva = bool(attributes & 1)
            if not uses_rva:
                name_rva = (name_rva - self.image_base) & 0xFFFFFFFF
            name_off = self.rva_to_offset(name_rva)
            result.append({
                "attributes": attributes,
                "attributes_are_rvas": uses_rva,
                "dll": self.reader.cstring(name_off) if name_off is not None else "<unmapped>",
                "module_handle_rva": module_rva,
                "iat_rva": iat_rva,
                "int_rva": int_rva,
                "bound_iat_rva": bound_iat,
                "unload_iat_rva": unload_iat,
                "time_date_stamp": stamp,
            })
        return result

    def exports(self) -> dict | None:
        directory = self.directory("export")
        if not directory:
            return None
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            return None
        (characteristics, stamp, major, minor, name_rva, base_ordinal, function_count,
         name_count, functions_rva, names_rva, ordinals_rva) = struct.unpack_from(
            "<IIHHIIIIIII", self.reader.data, offset)
        name_off = self.rva_to_offset(name_rva)
        dll_name = self.reader.cstring(name_off) if name_off is not None else "<unmapped>"
        names = []
        names_off = self.rva_to_offset(names_rva)
        ordinals_off = self.rva_to_offset(ordinals_rva)
        if names_off is not None and ordinals_off is not None:
            for index in range(min(name_count, 4096)):
                symbol_rva = self.reader.u32(names_off + index * 4)
                symbol_off = self.rva_to_offset(symbol_rva)
                ordinal = self.reader.u16(ordinals_off + index * 2)
                names.append({
                    "name": self.reader.cstring(symbol_off) if symbol_off is not None else None,
                    "ordinal": base_ordinal + ordinal,
                })
        forwarded = []
        functions_off = self.rva_to_offset(functions_rva)
        if functions_off is not None:
            for index in range(min(function_count, 65536)):
                function_rva = self.reader.u32(functions_off + index * 4)
                if directory["virtual_address"] <= function_rva < directory["virtual_address"] + directory["size"]:
                    target_off = self.rva_to_offset(function_rva)
                    if target_off is not None:
                        forwarded.append({"ordinal": base_ordinal + index,
                                          "forwarder": self.reader.cstring(target_off)})
        return {
            "dll_name": dll_name,
            "base_ordinal": base_ordinal,
            "number_of_functions": function_count,
            "number_of_names": name_count,
            "names": names,
            "forwarders": forwarded,
            "time_date_stamp": stamp,
        }

    def relocations(self) -> dict | None:
        directory = self.directory("base_relocation")
        if not directory:
            return None
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            return None
        end = offset + directory["size"]
        blocks = 0
        entries = 0
        histogram: dict[str, int] = {}
        truncated = False
        cursor = offset
        while cursor + 8 <= min(end, len(self.reader.data)):
            page_rva, block_size = struct.unpack_from("<II", self.reader.data, cursor)
            if block_size < 8 or cursor + block_size > len(self.reader.data):
                truncated = True
                break
            blocks += 1
            count = (block_size - 8) // 2
            for slot in range(count):
                value = self.reader.u16(cursor + 8 + slot * 2)
                kind = value >> 12
                name = RELOCATION_TYPES.get(kind, f"unknown({kind})")
                histogram[name] = histogram.get(name, 0) + 1
                entries += 1
            cursor += block_size
        return {"blocks": blocks, "entries": entries, "types": histogram, "truncated": truncated}

    def tls(self) -> dict | None:
        directory = self.directory("tls")
        if not directory:
            return None
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            return None
        if self.pe32_plus:
            start, end, index_va, callbacks_va, zero_fill, characteristics = struct.unpack_from(
                "<QQQQII", self.reader.data, offset)
        else:
            start, end, index_va, callbacks_va, zero_fill, characteristics = struct.unpack_from(
                "<IIIIII", self.reader.data, offset)
        callbacks = []
        callback_rva = None
        callback_off = None
        terminated = False
        if callbacks_va:
            mask = 0xFFFFFFFFFFFFFFFF if self.pe32_plus else 0xFFFFFFFF
            callback_rva = (callbacks_va - self.image_base) & mask
            callback_off = self.rva_to_offset(callback_rva)
            if callback_off is not None:
                size = 8 if self.pe32_plus else 4
                for slot in range(64):
                    position = callback_off + slot * size
                    try:
                        value = self.reader.u64(position) if self.pe32_plus else self.reader.u32(position)
                    except ParseError:
                        break
                    if value == 0:
                        terminated = True
                        break
                    callbacks.append(value)
        template_size = max(0, end - start) if end >= start else 0
        return {
            "directory_rva": directory["virtual_address"],
            "directory_offset": offset,
            "directory_size": directory["size"],
            "raw_data_start_va": start,
            "raw_data_end_va": end,
            "template_size": template_size,
            "size_of_zero_fill": zero_fill,
            "characteristics": characteristics,
            "callbacks_va": callbacks_va,
            "callbacks_rva": callback_rva,
            "callbacks_offset": callback_off,
            "callbacks": callbacks,
            "callbacks_terminated": terminated,
            "callbacks_truncated": bool(callbacks_va) and callback_off is not None and not terminated,
        }

    def exception_table(self) -> dict | None:
        directory = self.directory("exception")
        if not directory:
            return None
        count = directory["size"] // 12
        offset = self.rva_to_offset(directory["virtual_address"])
        versions: dict[str, int] = {}
        flags: dict[str, int] = {}
        chained = 0
        sorted_entries = True
        previous_begin = -1
        chained_flag_violations = 0
        version_violations = 0
        first_chained = None
        if offset is not None:
            for index in range(count):
                entry = offset + index * 12
                try:
                    begin, end, unwind_rva = struct.unpack_from("<III", self.reader.data, entry)
                except struct.error:
                    break
                if begin < previous_begin:
                    sorted_entries = False
                previous_begin = begin
                unwind_off = self.rva_to_offset(unwind_rva)
                if unwind_off is None or unwind_off >= len(self.reader.data):
                    versions["unmapped"] = versions.get("unmapped", 0) + 1
                    continue
                first = self.reader.u8(unwind_off)
                version = first & 0x7
                flag = first >> 3
                if version != 1:
                    version_violations += 1
                versions[str(version)] = versions.get(str(version), 0) + 1
                label = []
                if flag & UNWIND_FLAG_EHANDLER:
                    label.append("EHANDLER")
                if flag & UNWIND_FLAG_UHANDLER:
                    label.append("UHANDLER")
                if flag & UNWIND_FLAG_CHAININFO:
                    label.append("CHAININFO")
                    chained += 1
                    if flag & (UNWIND_FLAG_EHANDLER | UNWIND_FLAG_UHANDLER):
                        chained_flag_violations += 1
                    if first_chained is None:
                        first_chained = {
                            "index": index,
                            "entry_offset": entry,
                            "begin_rva": begin,
                            "end_rva": end,
                            "unwind_rva": unwind_rva,
                            "unwind_offset": unwind_off,
                            "first_byte": first,
                            "flags": flag,
                        }
                key = "|".join(label) or "none"
                flags[key] = flags.get(key, 0) + 1
        return {
            "directory_rva": directory["virtual_address"],
            "directory_offset": offset,
            "directory_size": directory["size"],
            "entries": count,
            "remainder_bytes": directory["size"] % 12,
            "unwind_versions": versions,
            "unwind_flags": flags,
            "chained_entries": chained,
            "sorted": sorted_entries,
            "chained_flag_violations": chained_flag_violations,
            "version_violations": version_violations,
            "first_chained": first_chained,
        }

    def load_config(self) -> dict | None:
        directory = self.directory("load_config")
        if not directory:
            return None
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            return None
        size = self.reader.u32(offset)
        result = {
            "directory_rva": directory["virtual_address"],
            "directory_offset": offset,
            "declared_size": size,
            "directory_size": directory["size"],
            "security_cookie": None,
            "guard_flags": None,
            "guard_cf_function_table": None,
            "guard_cf_function_count": None,
            "seh_handler_table": None,
        }
        if self.pe32_plus:
            if size >= 96:
                result["security_cookie"] = self.reader.u64(offset + 88)
            if size >= 144:
                result["guard_cf_function_table"] = self.reader.u64(offset + 128)
                result["guard_cf_function_count"] = self.reader.u64(offset + 136)
            if size >= 148:
                result["guard_flags"] = self.reader.u32(offset + 144)
        else:
            if size >= 64:
                result["security_cookie"] = self.reader.u32(offset + 60)
            if size >= 72:
                result["seh_handler_table"] = self.reader.u32(offset + 64)
            if size >= 84:
                result["guard_cf_function_table"] = self.reader.u32(offset + 80)
                result["guard_cf_function_count"] = self.reader.u32(offset + 84)
            if size >= 92:
                result["guard_flags"] = self.reader.u32(offset + 88)
        return result

    def clr_header(self) -> dict | None:
        directory = self.directory("clr_runtime")
        if not directory:
            return None
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            return None
        (cb, major, minor, meta_rva, meta_size, flags, entry_point,
         resources_rva, resources_size, strong_name_rva, strong_name_size,
         code_manager_rva, code_manager_size, vtable_fixups_rva, vtable_fixups_size,
         export_jumps_rva, export_jumps_size, managed_native_rva,
         managed_native_size) = struct.unpack_from(
            "<IHHIIIIIIIIIIIIIIII", self.reader.data, offset)
        return {
            "cb": cb,
            "runtime_version": f"{major}.{minor}",
            "metadata_rva": meta_rva,
            "metadata_size": meta_size,
            "flags": flags,
            "il_only": bool(flags & 0x1),
            "is_32bit_required": bool(flags & 0x2),
            "strong_name_signed": bool(flags & 0x8),
            "native_entry_point": bool(flags & 0x10),
            "entry_point": entry_point,
            "resources_size": resources_size,
            "vtable_fixups_size": vtable_fixups_size,
        }

    def debug_entries(self) -> list[dict]:
        directory = self.directory("debug")
        if not directory:
            return []
        offset = self.rva_to_offset(directory["virtual_address"])
        if offset is None:
            return []
        count = directory["size"] // 28
        result = []
        for index in range(min(count, 256)):
            entry = offset + index * 28
            try:
                (characteristics, stamp, major, minor, kind, size,
                 address_raw, pointer_raw) = struct.unpack_from("<IIHHIIII", self.reader.data, entry)
            except struct.error:
                break
            result.append({
                "type": DEBUG_TYPES.get(kind, f"unknown({kind})"),
                "size": size,
                "raw_offset": pointer_raw,
                "timestamp": stamp,
            })
        return result

    def resources(self) -> dict | None:
        directory = self.directory("resource")
        if not directory:
            return None
        base = self.rva_to_offset(directory["virtual_address"])
        if base is None:
            return None
        types: list[str] = []
        leaves = 0
        errors: list[str] = []

        def walk(relative: int, depth: int, limit: int) -> None:
            nonlocal leaves
            if depth > 3:
                return
            offset = base + relative
            try:
                named, ids = struct.unpack_from("<HH", self.reader.data, offset + 12)
            except struct.error:
                errors.append(f"resource directory at {offset:#x} is truncated")
                return
            for index in range(min(named + ids, limit)):
                entry = offset + 16 + index * 8
                try:
                    name_field, data_field = struct.unpack_from("<II", self.reader.data, entry)
                except struct.error:
                    errors.append("resource entry is truncated")
                    return
                if depth == 0:
                    if name_field & 0x80000000:
                        string_off = base + (name_field & 0x7FFFFFFF)
                        try:
                            length = self.reader.u16(string_off)
                            raw = self.reader.bytes(string_off + 2, length * 2)
                            types.append(raw.decode("utf-16-le", "replace"))
                        except ParseError:
                            types.append("<unreadable>")
                    else:
                        types.append(str(name_field))
                if data_field & 0x80000000:
                    walk(data_field & 0x7FFFFFFF, depth + 1, limit)
                else:
                    leaves += 1

        walk(0, 0, 4096)
        return {"types": types, "leaves": leaves, "errors": errors}

    def certificates(self) -> list[dict]:
        directory = self.directory("certificate")
        if not directory:
            return []
        offset = directory["virtual_address"]  # a file offset, not an RVA
        size = directory["size"]
        result = []
        cursor = offset
        while cursor + 8 <= offset + size and cursor + 8 <= len(self.reader.data):
            length, revision, kind = struct.unpack_from("<IHH", self.reader.data, cursor)
            if length < 8:
                break
            result.append({"length": length, "revision": revision, "type": kind})
            cursor += (length + 7) & ~7
        return result

    # ---- aggregate ---------------------------------------------------
    def summary(self) -> dict:
        imports = self.imports()
        exports = self.exports()
        relocations = self.relocations()
        tls = self.tls()
        exceptions = self.exception_table()
        load_config = self.load_config()
        clr = self.clr_header()
        resources = self.resources()
        delay = self.delay_imports()
        certificates = self.certificates()
        debug = self.debug_entries()

        features = []
        if clr:
            features.append("managed-clr")
        if tls:
            features.append("static-tls")
        if exceptions:
            features.append("seh-tables")
        if delay:
            features.append("delay-imports")
        if self.directory("bound_import"):
            features.append("bound-imports")
        if certificates:
            features.append("authenticode")
        if load_config and load_config.get("guard_flags"):
            features.append("control-flow-guard")
        if resources:
            features.append("resources")
        if self.dll_characteristics & DLL_DYNAMIC_BASE:
            features.append("aslr")
        if not (self.characteristics & FILE_RELOCS_STRIPPED):
            features.append("relocatable")

        return {
            "file": str(self.path),
            "size": len(self.reader.data),
            "machine": MACHINES.get(self.machine, f"{self.machine:#x}"),
            "magic": f"{self.magic:#x}",
            "pe32_plus": self.pe32_plus,
            "subsystem": SUBSYSTEMS.get(self.subsystem, str(self.subsystem)),
            "is_dll": bool(self.characteristics & FILE_DLL),
            "is_executable": bool(self.characteristics & FILE_EXECUTABLE),
            "relocs_stripped": bool(self.characteristics & FILE_RELOCS_STRIPPED),
            "large_address_aware": bool(self.characteristics & FILE_LARGE_ADDRESS_AWARE),
            "dll_characteristics": {
                "dynamic_base": bool(self.dll_characteristics & DLL_DYNAMIC_BASE),
                "nx_compat": bool(self.dll_characteristics & DLL_NX_COMPAT),
                "guard_cf": bool(self.dll_characteristics & DLL_GUARD_CF),
                "high_entropy_va": bool(self.dll_characteristics & DLL_HIGH_ENTROPY_VA),
                "force_integrity": bool(self.dll_characteristics & DLL_FORCE_INTEGRITY),
                "no_seh": bool(self.dll_characteristics & DLL_NO_SEH),
            },
            "image_base": self.image_base,
            "entry_point_rva": self.address_of_entry_point,
            "section_alignment": self.section_alignment,
            "file_alignment": self.file_alignment,
            "size_of_image": self.size_of_image,
            "size_of_headers": self.size_of_headers,
            "size_of_optional_header": self.size_of_optional_header,
            "expected_optional_size": self.expected_optional_size,
            "number_of_rva_and_sizes": self.number_of_rva_and_sizes,
            "checksum": self.checksum,
            "sections": self.sections,
            "directories": self.directories,
            "imports": imports,
            "delay_imports": delay,
            "exports": exports,
            "relocations": relocations,
            "tls": tls,
            "exceptions": exceptions,
            "load_config": load_config,
            "clr": clr,
            "resources": resources,
            "certificates": certificates,
            "debug": debug,
            "rich_header": self.rich_header,
            "features": features,
            "warnings": self.warnings,
        }


def describe(summary: dict) -> str:
    imported = sum(len(entry["symbols"]) for entry in summary["imports"])
    lines = [
        f"{summary['file']}",
        f"  format      : {summary['magic']} ({'PE32+' if summary['pe32_plus'] else 'PE32'}) "
        f"machine={summary['machine']} subsystem={summary['subsystem']}"
        f"{' dll' if summary['is_dll'] else ''}",
        f"  headers     : optional={summary['size_of_optional_header']} bytes "
        f"(expected {summary['expected_optional_size']}), "
        f"directories={summary['number_of_rva_and_sizes']}, sections={len(summary['sections'])}",
        f"  imports     : {len(summary['imports'])} dlls / {imported} symbols",
        f"  exports     : {summary['exports']['number_of_names'] if summary['exports'] else 0} named",
        f"  relocations : {summary['relocations']['entries'] if summary['relocations'] else 0} entries "
        f"({summary['relocations']['types'] if summary['relocations'] else {}})",
        f"  tls         : {summary['tls']['template_size'] if summary['tls'] else 0} bytes template, "
        f"{len(summary['tls']['callbacks']) if summary['tls'] else 0} callbacks",
        f"  exceptions  : {summary['exceptions']['entries'] if summary['exceptions'] else 0} entries "
        f"({summary['exceptions']['unwind_versions'] if summary['exceptions'] else {}})",
        f"  clr         : {summary['clr'] if summary['clr'] else 'absent'}",
        f"  features    : {', '.join(summary['features']) or 'none'}",
    ]
    if summary["warnings"]:
        lines.append(f"  warnings    : {'; '.join(summary['warnings'])}")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path)
    parser.add_argument("--json", action="store_true", help="emit JSON instead of a text summary")
    parser.add_argument("--fail-on-warning", action="store_true")
    args = parser.parse_args(argv)
    exit_code = 0
    for path in args.images:
        try:
            summary = PEImage(path).summary()
        except (ParseError, OSError) as error:
            print(f"{path}: ERROR {error}", file=sys.stderr)
            exit_code = 2
            continue
        if args.json:
            print(json.dumps(summary, indent=2))
        else:
            print(describe(summary))
        if args.fail_on_warning and summary["warnings"]:
            exit_code = 1
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
