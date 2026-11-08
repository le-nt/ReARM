#!/usr/bin/env python3
"""Independent ARM fixture execution, state comparison, and pipeline measurements.

SPDX-License-Identifier: Apache-2.0
Unicorn is required only for reference execution. psutil is optional: without it,
child peak RSS is reported as unavailable, never as a measured zero.
"""
from __future__ import annotations

import argparse
import functools
import hashlib
import http.server
import json
from pathlib import Path
import struct
import subprocess
import sys
import threading
import time
import uuid

PAGE = 4096
STACK_TOP = 0x7FF00000
STACK_SIZE = 0x10000
RETURN_SENTINEL = 0xFFFF0000
DEFAULT_CPSR_MASK = 0xF0000020


class FixtureError(RuntimeError):
    pass


class PipelineStepError(FixtureError):
    def __init__(self, result: dict):
        self.result = result
        super().__init__("pipeline step failed:\n" + json.dumps(result, indent=2))


def write_report(path: Path, report: dict) -> None:
    """Replace report atomically, with bounded retries for Windows reader locks."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for attempt in range(5):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt < 4:
                time.sleep(0.05)
    pending = temporary.with_suffix(".pending.json")
    temporary.rename(pending)
    raise FixtureError(f"cannot replace report {path} after five attempts; close conflicting readers; complete pending report preserved at {pending}")


class ElfFixture:
    """Small independent fixture reader; deliberately does not import ReARM code."""

    def __init__(self, data: bytes, load_bias: int = 0, defer_unresolved: bool = False):
        self.data = data
        self.sha256 = hashlib.sha256(data).hexdigest()
        self.defer_unresolved = defer_unresolved
        self.resolver = None
        self.dynamic_symbols = {}
        if type(load_bias) is not int or not 0 <= load_bias <= 0xFFFFFFFF or load_bias % PAGE:
            raise FixtureError("reference load bias must be a page-aligned unsigned ARM32 address")
        self.load_bias = load_bias
        if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
            raise FixtureError("reference input must be ELF32 little-endian version 1")
        h = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        typ, machine, version, self.entry, phoff, shoff, _, ehsize, phsize, phnum, shsize, shnum, _ = h
        self.elf_type = typ
        if typ not in (2, 3) or machine != 40 or version != 1 or ehsize != 52:
            raise FixtureError("reference fixture must be an ARM ELF32 ET_EXEC or ET_DYN")
        if typ == 2 and load_bias:
            raise FixtureError("reference ET_EXEC fixtures do not accept a nonzero load bias")
        self.entry = self.rebase(self.entry)
        if phsize != 32 or phnum > 4096:
            raise FixtureError("invalid program header table")
        self.segments = []
        self.file_segments = []
        dynamic = []
        for i in range(phnum):
            p = struct.unpack("<IIIIIIII", self.read(phoff + i * phsize, 32))
            ptype, off, va, _, filesz, memsz, flags, _ = p
            if ptype == 1:
                if filesz > memsz or va + memsz > 2**32 or va + load_bias + memsz > 2**32:
                    raise FixtureError("invalid PT_LOAD bounds")
                contents = self.read(off, filesz)
                self.file_segments.append((va, off, filesz, memsz))
                self.segments.append((self.rebase(va), contents, memsz, flags))
            elif ptype == 2:
                if filesz % 8:
                    raise FixtureError("invalid PT_DYNAMIC size")
                dynamic.append(self.read(off, filesz))
        if not self.segments:
            raise FixtureError("fixture has no loadable segments")
        self.symbols = {}
        if shnum:
            if shsize != 40 or shnum > 65535:
                raise FixtureError("invalid section header table")
            sections = [struct.unpack("<IIIIIIIIII", self.read(shoff + i * shsize, 40)) for i in range(shnum)]
            for s in sections:
                if s[1] not in (2, 11):
                    continue
                if s[6] >= len(sections) or s[9] != 16 or s[5] % 16:
                    raise FixtureError("invalid symbol table")
                strings = sections[s[6]]
                names = self.read(strings[4], strings[5])
                for off in range(s[4], s[4] + s[5], 16):
                    name, value, size, info, other, index = struct.unpack("<IIIBBH", self.read(off, 16))
                    if name >= len(names):
                        raise FixtureError("invalid symbol name offset")
                    end = names.find(b"\0", name)
                    if end < 0:
                        raise FixtureError("unterminated symbol name")
                    if index:
                        self.symbols[names[name:end].decode("utf-8")] = (self.symbol_value(value, info, index), size)
                    if s[1] == 11:
                        self.dynamic_symbols[(off - s[4]) // 16] = {
                            "name": names[name:end].decode("utf-8"),
                            "value": self.symbol_value(value, info, index) if index else None,
                            "size": size, "binding": info >> 4, "type": info & 15, "visibility": other & 3}
        self.dynamic = {}
        needed_offsets = []
        for table in dynamic:
            terminated = False
            for at in range(0, len(table), 8):
                tag, value = struct.unpack_from("<II", table, at)
                if tag == 0:
                    terminated = True
                    break
                if tag == 1:
                    needed_offsets.append(value)
                self.dynamic[tag] = value
            if not terminated:
                raise FixtureError("unterminated reference dynamic table")
        self.needed = [self.dynamic_string(at) for at in needed_offsets]
        self.soname = self.dynamic_string(self.dynamic[14]) if 14 in self.dynamic else ""
        if self.dynamic.get(8, 0):
            raise FixtureError("reference ARM profile does not implement RELA")
        self.relocations = []
        relocation_tables = []
        if self.dynamic.get(18, 0):
            if self.dynamic.get(19, 8) != 8 or 17 not in self.dynamic:
                raise FixtureError("invalid DT_REL metadata")
            relocation_tables.append((self.dynamic[17], self.dynamic[18]))
        if self.dynamic.get(2, 0):
            if self.dynamic.get(20) != 17 or 23 not in self.dynamic:
                raise FixtureError("reference PLT relocations must use DT_REL")
            relocation_tables.append((self.dynamic[23], self.dynamic[2]))
        seen = set()
        for address, size in relocation_tables:
            if size % 8:
                raise FixtureError("invalid reference REL table size")
            table = self.read_virtual(address, size)
            for at in range(0, size, 8):
                place, info = struct.unpack_from("<II", table, at)
                identity = (place, info)
                if identity in seen:
                    continue
                seen.add(identity)
                kind, symbol_index = info & 255, info >> 8
                if kind not in (0, 2, 3, 21, 22, 23):
                    raise FixtureError(f"unsupported reference ARM relocation {kind} at {place:#x}")
                if kind == 0:
                    continue
                symbol, name = (0, "")
                if symbol_index:
                    symbol, _, name = self.dynamic_symbol(symbol_index)
                if kind == 23 and symbol_index:
                    raise FixtureError("reference profile does not support symbol-based R_ARM_RELATIVE")
                self.read_virtual(place, 4)  # Validate the complete guest destination.
                self.relocations.append({"address": self.rebase(place), "type": kind, "symbol": symbol,
                                         "name": name, "symbol_index": symbol_index})
        # SysV nchain provides a bounded dynamic symbol inventory even without sections.
        if 4 in self.dynamic:
            _, count = struct.unpack("<II", self.read_virtual(self.dynamic[4], 8))
            if count > 1_000_000:
                raise FixtureError("reference dynamic symbol count exceeds limit")
            for index in range(count):
                value, size, name = self.dynamic_symbol(index, allow_undefined=True)
                if name and value is not None:
                    self.symbols[name] = (value, size)
        self.module_identities = [{"name": self.soname or "main", "sha256": self.sha256,
                                   "load_bias": self.load_bias, "needed": self.needed}]

    def rebase(self, value: int) -> int:
        result = value + self.load_bias
        if not 0 <= result <= 0xFFFFFFFF:
            raise FixtureError("reference rebased address overflows ARM32")
        return result

    def symbol_value(self, value: int, info: int, index: int) -> int:
        # Absolute symbols and STT_TLS symbol values are not virtual addresses.
        return value if index == 0xFFF1 or info & 15 == 6 else self.rebase(value)

    def read_virtual(self, address: int, size: int) -> bytes:
        if address < 0 or size < 0 or address + size > 2**32:
            raise FixtureError("invalid reference virtual range")
        for va, off, filesz, memsz in self.file_segments:
            if address >= va and address - va <= memsz and size <= memsz - (address - va):
                relative = address - va
                file_bytes = min(size, max(0, filesz - relative))
                return (self.read(off + relative, file_bytes) if file_bytes else b"") + bytes(size - file_bytes)
        raise FixtureError(f"reference virtual range is unmapped: {address:#x}+{size:#x}")

    def dynamic_symbol(self, index: int, allow_undefined: bool = False) -> tuple[int | None, int, str]:
        if self.dynamic.get(11, 16) != 16 or 6 not in self.dynamic or 5 not in self.dynamic or 10 not in self.dynamic:
            raise FixtureError("reference dynamic symbols lack valid table metadata")
        name_at, value, size, info, other, section = struct.unpack("<IIIBBH", self.read_virtual(self.dynamic[6] + index * 16, 16))
        name = self.dynamic_string(name_at)
        own_value = self.symbol_value(value, info, section) if section else None
        self.dynamic_symbols[index] = {"name": name, "value": own_value, "size": size,
                                       "binding": info >> 4, "type": info & 15, "visibility": other & 3}
        if allow_undefined:
            return own_value, size, name
        if info & 15 == 6:
            raise FixtureError(f"TLS relocation is outside the initial reference profile: {name!r}")
        symbolic = 16 in self.dynamic or bool(self.dynamic.get(30, 0) & 2)
        if self.resolver and info >> 4 in (1, 2) and (not section or (other & 3 == 0 and not symbolic)):
            resolved = self.resolver(name)
            if resolved is not None:
                return resolved, size, name
        if not section:
            if info >> 4 == 2:  # An unresolved weak symbol has value zero.
                return 0, size, name
            if self.defer_unresolved:
                return None, size, name
            raise FixtureError(f"unresolved reference dynamic symbol {name!r}")
        return own_value, size, name

    def dynamic_string(self, offset: int) -> str:
        if 5 not in self.dynamic or 10 not in self.dynamic or offset >= self.dynamic[10]:
            raise FixtureError("reference dynamic string is outside DT_STRTAB")
        if not hasattr(self, "dynamic_strings"):
            self.dynamic_strings = self.read_virtual(self.dynamic[5], self.dynamic[10])
        strings = self.dynamic_strings
        end = strings.find(b"\0", offset)
        if end < 0:
            raise FixtureError("reference dynamic string is unterminated")
        return strings[offset:end].decode("utf-8")

    def bind(self, resolver) -> None:
        self.resolver = resolver
        self.defer_unresolved = False
        for relocation in self.relocations:
            if relocation["symbol_index"]:
                relocation["symbol"] = self.dynamic_symbol(relocation["symbol_index"])[0]

    def initializers(self, read) -> list[int]:
        result = [self.rebase(self.dynamic[12])] if self.dynamic.get(12) else []
        size = self.dynamic.get(27, 0)
        if size:
            if size % 4 or 25 not in self.dynamic or size > 1024 * 1024:
                raise FixtureError("reference init array metadata is invalid")
            self.read_virtual(self.dynamic[25], size)
            array = bytes(read(self.rebase(self.dynamic[25]), size))
            result += list(struct.unpack("<" + "I" * (size // 4), array))
        return [address for address in result if address not in (0, 0xFFFFFFFF)]

    def relocate(self, read, write) -> None:
        for relocation in self.relocations:
            place, kind, symbol = relocation["address"], relocation["type"], relocation["symbol"]
            if symbol is None:
                raise FixtureError(f"unresolved reference dynamic symbol {relocation['name']!r}")
            if kind == 0:
                continue
            addend = struct.unpack("<I", bytes(read(place, 4)))[0]
            if kind == 23:
                result = self.load_bias + addend
            elif kind == 2:
                result = symbol + addend
            elif kind == 3:
                result = symbol + addend - place
            else:  # GLOB_DAT and JUMP_SLOT replace rather than add the old word.
                result = symbol
            write(place, struct.pack("<I", result & 0xFFFFFFFF))

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise FixtureError(f"ELF range outside file: offset={offset:#x}, size={size:#x}")
        return self.data[offset:offset + size]

    def address(self, name: str) -> int:
        if name in self.symbols:
            return self.symbols[name][0]
        try:
            return int(name, 0)
        except ValueError:
            raise FixtureError(f"missing entry or memory symbol: {name}") from None


class LinkedFixture:
    """Independent eager namespace over explicitly supplied main-first modules."""
    def __init__(self, modules: list[tuple[str, bytes, int]]):
        if not modules:
            raise FixtureError("reference namespace has no main module")
        self.modules = [(name, ElfFixture(data, bias, defer_unresolved=True)) for name, data, bias in modules]
        self.module_identities = [{"name": name, "soname": module.soname, "sha256": module.sha256,
                                   "load_bias": module.load_bias, "needed": module.needed} for name, module in self.modules]
        self.load_bias = self.modules[0][1].load_bias
        self.entry = self.modules[0][1].entry
        self.segments = [segment for _, module in self.modules for segment in module.segments]
        ranges = sorted((va, va + size) for va, _, size, _ in self.segments if size)
        if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
            raise FixtureError("reference module PT_LOAD mappings overlap")
        self.symbols = {}
        aliases = {}
        for index, (name, module) in enumerate(self.modules):
            for alias in (name, module.soname):
                if alias:
                    if alias in aliases and aliases[alias] != index:
                        raise FixtureError(f"duplicate reference module alias {alias!r}")
                    aliases[alias] = index
            for name, value in module.symbols.items():
                self.symbols.setdefault(name, value)
        for _, module in self.modules:
            for needed in module.needed:
                if needed not in aliases:
                    raise FixtureError(f"missing reference DT_NEEDED module {needed!r}")

        def resolve(name):
            weak = None
            for _, module in self.modules:
                for symbol in module.dynamic_symbols.values():
                    if (symbol["name"] != name or symbol["value"] is None or symbol["binding"] not in (1, 2)
                            or symbol["visibility"] not in (0, 3) or symbol["type"] == 6):
                        continue
                    if symbol["binding"] == 1:
                        return symbol["value"]
                    if weak is None:
                        weak = symbol["value"]
            return weak

        for _, module in self.modules:
            module.bind(resolve)
        self.relocations = [relocation for _, module in self.modules for relocation in module.relocations]
        self.init_order = []
        visited = set()
        active = set()
        def visit(index):
            if index in visited or index in active:
                return
            active.add(index)
            for needed in self.modules[index][1].needed:
                visit(aliases[needed])
            active.remove(index)
            visited.add(index)
            self.init_order.append(index)
        for index in range(len(self.modules)):
            visit(index)

    def relocate(self, read, write):
        for _, module in self.modules:
            module.relocate(read, write)

    def initializers(self, read):
        return [address for index in self.init_order for address in self.modules[index][1].initializers(read)]

    def address(self, name):
        if name in self.symbols:
            return self.symbols[name][0]
        try:
            return int(name, 0)
        except ValueError:
            raise FixtureError(f"missing linked reference symbol {name!r}") from None


def reference_state(elf: ElfFixture | LinkedFixture, entry: str, observed: list[tuple[int, int]], limit: int,
                    seed: int = 0, initialize: bool = True, host=None) -> dict:
    try:
        import unicorn as uc
        from unicorn import arm_const as arm
    except ImportError:
        raise FixtureError("reference execution requires the pinned unicorn package") from None
    address = elf.address(entry)
    machine = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_THUMB if address & 1 else uc.UC_MODE_ARM)
    pages = set()
    for va, data, memsz, flags in elf.segments:
        if memsz > 64 * 1024 * 1024:
            raise FixtureError("reference fixture segment exceeds 64-MiB limit")
        pages.update(range(va & -PAGE, (va + memsz + PAGE - 1) & -PAGE, PAGE))
    stack_pages = set(range(STACK_TOP - STACK_SIZE, STACK_TOP, PAGE))
    if pages & stack_pages or RETURN_SENTINEL in pages:
        raise FixtureError("fixture overlaps reserved reference stack or return sentinel")
    for page in sorted(pages | stack_pages | {RETURN_SENTINEL}):
        machine.mem_map(page, PAGE)
    for va, data, memsz, flags in elf.segments:
        if data:
            machine.mem_write(va, data)
    elf.relocate(machine.mem_read, machine.mem_write)
    if host is not None:
        host.install(machine)
    regs = [getattr(arm, f"UC_ARM_REG_R{i}") for i in range(13)] + [arm.UC_ARM_REG_SP, arm.UC_ARM_REG_LR, arm.UC_ARM_REG_PC]
    count = 0

    def instruction_hook(engine, pc, size, user):
        nonlocal count
        count += 1
        if host is not None:
            host.instruction(engine, pc, arm)

    machine.hook_add(uc.UC_HOOK_CODE, instruction_hook)
    started = time.perf_counter()
    initializers = elf.initializers(machine.mem_read) if initialize else []
    argument = seed if host is None else host.entryArgument(seed) if hasattr(host, "entryArgument") else host.vm
    calls = [(function, [0]) for function in initializers] + [(address, [argument])]
    for call_index, (function, arguments) in enumerate(calls):
        # Fixture contract: independent invocation registers, persistent guest
        # memory, and one aggregate instruction budget across initialization.
        machine.reg_write(arm.UC_ARM_REG_CPSR, 0x30 if function & 1 else 0x10)
        for reg in regs:
            machine.reg_write(reg, 0)
        stack_bytes = ((max(0, len(arguments) - 4) * 4 + 7) // 8) * 8
        stack = STACK_TOP - stack_bytes
        for index, argument in enumerate(arguments):
            if index < 4:
                machine.reg_write(regs[index], argument)
            else:
                machine.mem_write(stack + (index - 4) * 4, struct.pack("<I", argument))
        machine.reg_write(arm.UC_ARM_REG_SP, stack)
        machine.reg_write(arm.UC_ARM_REG_LR, RETURN_SENTINEL)
        remaining = limit - count
        if remaining <= 0:
            raise FixtureError(f"reference aggregate instruction budget exhausted before {function:#x}")
        try:
            machine.emu_start(function, RETURN_SENTINEL, count=remaining)
        except uc.UcError as error:
            pc = machine.reg_read(arm.UC_ARM_REG_PC)
            raise FixtureError(f"ARM reference fault at {pc:#x} invoking {function:#x}: {error}") from error
        if machine.reg_read(arm.UC_ARM_REG_PC) != RETURN_SENTINEL:
            raise FixtureError(f"reference did not return to sentinel within aggregate {limit} instructions")
        if host is not None and call_index == len(initializers) and hasattr(host, "native_invocation"):
            extra = host.native_invocation(machine, arm, seed)
            if extra is not None:
                calls.append(extra)
    elapsed = time.perf_counter() - started
    return {
        "registers": [machine.reg_read(reg) & 0xFFFFFFFF for reg in regs],
        "cpsr": machine.reg_read(arm.UC_ARM_REG_CPSR),
        "memory": [{"address": va, "bytes": bytes(machine.mem_read(va, size)).hex()} for va, size in observed],
        "reference": {"engine": "unicorn", "version": uc.__version__, "instructions": count, "elapsed_seconds": elapsed,
                      "load_bias": elf.load_bias, "relocations": elf.relocations,
                      "initializers": initializers, "initialization_enabled": initialize,
                      "modules": elf.module_identities},
    }


def dependency_spec(value: str) -> tuple[Path, int]:
    try:
        filename, bias = value.rsplit("@", 1)
        result = (Path(filename), int(bias, 0))
    except (ValueError, TypeError):
        raise FixtureError("dependency must be PATH@LOAD_BIAS") from None
    if not filename or not 0 <= result[1] <= 0xFFFFFFFF or result[1] % PAGE:
        raise FixtureError("dependency needs a path and page-aligned ARM32 load bias")
    return result


def load_fixture(path: Path, load_bias: int, dependencies: list[str]) -> ElfFixture | LinkedFixture:
    modules = [(path.name, path.read_bytes(), load_bias)]
    for dependency in dependencies:
        library, bias = dependency_spec(dependency)
        modules.append((library.name, library.read_bytes(), bias))
    return LinkedFixture(modules)


def canonical_state(state: dict) -> tuple[list[int], int, dict[int, int]]:
    if not isinstance(state, dict):
        raise FixtureError("runner state must be a JSON object")
    registers = state.get("registers")
    cpsr = state.get("cpsr")
    if not isinstance(registers, list) or len(registers) != 16:
        raise FixtureError("state must contain exactly 16 registers")
    if any(type(x) is not int or not 0 <= x <= 0xFFFFFFFF for x in registers):
        raise FixtureError("register values must be unsigned 32-bit integers")
    if type(cpsr) is not int or not 0 <= cpsr <= 0xFFFFFFFF:
        raise FixtureError("state cpsr must be an unsigned 32-bit integer")
    memory = {}
    regions = state.get("memory", [])
    if not isinstance(regions, list):
        raise FixtureError("state memory must be an array")
    for region in regions:
        if not isinstance(region, dict):
            raise FixtureError("state memory region must be an object")
        address = region.get("address")
        if type(address) is not int or not 0 <= address <= 0xFFFFFFFF:
            raise FixtureError("invalid state memory address")
        try:
            data = bytes.fromhex(region["bytes"])
        except (KeyError, ValueError, TypeError):
            raise FixtureError("state memory bytes must be hexadecimal") from None
        if address + len(data) > 2**32:
            raise FixtureError("state memory range overflows ARM32")
        for i, byte in enumerate(data):
            if address + i in memory:
                raise FixtureError("state has overlapping memory ranges")
            memory[address + i] = byte
    return registers, cpsr, memory


def compare_states(expected: dict, actual: dict, cpsr_mask: int = DEFAULT_CPSR_MASK) -> list[str]:
    er, ec, em = canonical_state(expected)
    ar, ac, am = canonical_state(actual)
    differences = [f"r{i}: expected {x:#010x}, actual {y:#010x}" for i, (x, y) in enumerate(zip(er, ar)) if x != y]
    if (ec ^ ac) & cpsr_mask:
        differences.append(f"cpsr mask {cpsr_mask:#010x}: expected {ec:#010x}, actual {ac:#010x}")
    if em.keys() != am.keys():
        differences.append(f"observed memory address set differs: expected {len(em)} bytes, actual {len(am)} bytes")
    for address in sorted(em.keys() & am.keys()):
        if em[address] != am[address]:
            differences.append(f"memory[{address:#010x}]: expected {em[address]:02x}, actual {am[address]:02x}")
    return differences


def run_measured(argv: list[str], timeout: float = 300) -> dict:
    if not isinstance(argv, list) or not argv or any(not isinstance(item, str) for item in argv):
        raise FixtureError("step command must be a nonempty JSON array of strings")
    try:
        import psutil
    except ImportError:
        psutil = None
    started = time.perf_counter()
    process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    peak = None
    # communicate(timeout) drains pipes and avoids a compiler-output deadlock.
    while True:
        if time.perf_counter() - started > timeout:
            if psutil:
                try:
                    for child in psutil.Process(process.pid).children(recursive=True):
                        try:
                            child.kill()
                        except psutil.Error:
                            pass
                except psutil.Error:
                    pass
            process.kill()
            stdout, stderr = process.communicate()
            raise PipelineStepError({"command": argv, "elapsed_seconds": time.perf_counter() - started,
                                     "peak_sampled_rss_bytes": peak, "timed_out": True,
                                     "exit_code": process.returncode, "stdout": stdout, "stderr": stderr})
        if psutil:
            try:
                root = psutil.Process(process.pid)
                rss = sum(p.memory_info().rss for p in [root, *root.children(recursive=True)] if p.is_running())
                if rss > 0:
                    peak = max(peak or 0, rss)
            except psutil.Error:
                pass
        try:
            stdout, stderr = process.communicate(timeout=0.02)
            break
        except subprocess.TimeoutExpired:
            continue
    result = {"command": argv, "elapsed_seconds": time.perf_counter() - started,
              "peak_sampled_rss_bytes": peak, "rss_sampling_seconds": 0.02 if psutil else None,
              "exit_code": process.returncode, "stdout": stdout, "stderr": stderr}
    if process.returncode:
        raise PipelineStepError(result)
    return result


def parse_range(value: str, elf: ElfFixture) -> tuple[int, int]:
    if ":" in value:
        address, size = value.split(":", 1)
        result = (elf.address(address), int(size, 0))
    elif value in elf.symbols:
        result = elf.symbols[value]
    else:
        raise FixtureError("observed memory must be symbol or address:size")
    if result[1] <= 0 or result[1] > 1024 * 1024 or result[0] + result[1] > 2**32:
        raise FixtureError("invalid observed memory size")
    return result


def seeds(count: int) -> list[int]:
    if count <= 0:
        raise FixtureError("case count must be positive")
    result = [0, 1, 0xFFFFFFFF, 0x80000000][:count]
    value = 0xC0DEC0DE
    while len(result) < count:
        value ^= (value << 13) & 0xFFFFFFFF
        value ^= value >> 17
        value ^= (value << 5) & 0xFFFFFFFF
        result.append(value & 0xFFFFFFFF)
    return result


def parse_runner_output(stdout: str) -> dict:
    try:
        result = json.loads(stdout)
        canonical_state(result)
        return result
    except (ValueError, TypeError, AttributeError) as error:
        raise FixtureError(f"runner did not emit one valid JSON state: {stdout[:2000]!r}") from error


def browser_state(module: Path, seed: int, extra_args=()) -> tuple[dict, float]:
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        raise FixtureError("--browser requires pinned playwright and its Chromium installation") from None

    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(module.parent)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    started = time.perf_counter()
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page()
            page.set_default_timeout(30000)
            page.goto(f"http://127.0.0.1:{server.server_port}/")
            page.add_script_tag(url=module.name)
            lines = page.evaluate("""async args => {
                const lines = [];
                const module = await createRearmModule({noInitialRun: true,
                    print: line => lines.push(line), printErr: line => console.error(line)});
                module.callMain(args.map(String));
                return lines;
            }""", [seed, *extra_args])
            browser.close()
        return parse_runner_output("\n".join(lines)), time.perf_counter() - started
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def translation_metrics(stdout: str, elapsed: float) -> dict:
    """Only derive rates from actual emitter-reported unique instruction counts."""
    try:
        summary = json.loads(stdout)
    except ValueError:
        return {"summary_unavailable": "translator stdout was not JSON"}
    if not isinstance(summary, dict):
        return {"summary_unavailable": "translator summary was not an object"}
    result = {"summary": summary}
    for field, rate in (("instructions", "instructions_per_second"), ("pcode_ops", "pcode_ops_per_second")):
        count = summary.get(field)
        if type(count) is int and count >= 0 and elapsed > 0:
            result[rate] = count / elapsed
    return result


def run_pipeline_body(args, root: Path, out: Path, report: dict, checkpoint) -> None:
    for mode in args.modes:
        for optimization in args.optimizations:
            tag = f"{mode}-o{optimization}" + (f"-shared-b{args.load_bias:08x}" if args.shared else "")
            elf_path = out / f"{tag}.elf"
            source = out / f"{tag}.cpp"
            native = out / (f"{tag}.exe" if sys.platform == "win32" else tag)
            wasm_js = out / f"{tag}.js"
            compile_command = [args.clang, "--target=armv7-linux-android21" if args.shared else "--target=armv7-none-eabi",
                 "-march=armv7-a", f"-m{mode}", f"-O{optimization}",
                 "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                 "-nostdlib", "-fuse-ld=lld"]
            if args.shared:
                compile_command += ["-shared", "-fPIC", "-Wl,--hash-style=both"]
            else:
                compile_command += [f"-Wl,-T,{root / 'tests/fixtures/early_pipeline.ld'}"]
            compile_command += [str(args.fixture or root / "tests/fixtures/early_pipeline.c"), "-o", str(elf_path)]
            for dependency in args.dependency:
                compile_command.append(str(dependency_spec(dependency)[0]))
            commands = [compile_command,
                [args.rearm, "--input", str(elf_path), "--entry", "fixture_entry", "--output", str(source),
                 "--sla", args.sla, "--observe", "fixture_output:64", "--max-instructions", str(args.max_instructions),
                 "--load-bias", hex(args.load_bias), "--dispatch", args.dispatch, "--initialize", args.initialize],
                [args.cxx, str(source), "-std=c++20", "-O2", "-o", str(native)],
                [args.emxx, str(source), "-std=c++20", "-O2", "-sMODULARIZE=1", "-sEXPORT_NAME=createRearmModule",
                 "-sENVIRONMENT=node,web", "-sEXIT_RUNTIME=0", "-sINVOKE_RUN=0",
                 "-sEXPORTED_RUNTIME_METHODS=callMain", "-sALLOW_MEMORY_GROWTH=1", "-o", str(wasm_js)],
            ]
            if args.dispatch == "musttail":
                commands[3].insert(1, "-mtail-call")
            for dependency in args.dependency:
                commands[1] += ["--dependency", dependency]
            for kind, command, produced in zip(("arm_compile", "translate", "native_compile", "wasm_compile"), commands,
                                                ((elf_path,), (source,), (native,), (wasm_js, wasm_js.with_suffix(".wasm")))):
                report["active"] = {"fixture": tag, "stage": kind, "command": command}
                checkpoint()
                try:
                    measured = run_measured(command)
                except PipelineStepError as error:
                    error.result.update({"fixture": tag, "stage": kind})
                    report["steps"].append(error.result)
                    raise
                measured.update({"fixture": tag, "stage": kind})
                if kind == "translate":
                    measured["translation"] = translation_metrics(measured["stdout"], measured["elapsed_seconds"])
                report["steps"].append(measured)
                for artifact in produced:
                    content = artifact.read_bytes()
                    report["artifacts"].append({"path": str(artifact), "size_bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()})
                checkpoint()
            elf = load_fixture(elf_path, args.load_bias, args.dependency)
            observed = [parse_range("fixture_output", elf)]
            for seed in seeds(args.cases):
                report["active"] = {"fixture": tag, "seed": seed, "stage": "reference"}
                checkpoint()
                expected = reference_state(elf, "fixture_entry", observed, args.max_instructions, seed, args.initialize == "yes")
                case = {"fixture": tag, "seed": seed, "reference": expected, "backends": {}}
                report["cases"].append(case)
                checkpoint()
                for backend, command in (
                    ("native", [str(native), str(seed)]),
                    ("node", [args.node, str(root / "tests/reference/run_wasm.cjs"), str(wasm_js), str(seed)]),
                ):
                    report["active"] = {"fixture": tag, "seed": seed, "stage": backend, "command": command}
                    checkpoint()
                    try:
                        measured = run_measured(command, timeout=30)
                    except PipelineStepError as error:
                        case["backends"][backend] = {"passed": False, "process": error.result}
                        raise
                    case["backends"][backend] = {"passed": False, "process": measured}
                    checkpoint()
                    actual = parse_runner_output(measured["stdout"])
                    differences = compare_states(expected, actual, args.cpsr_mask)
                    case["backends"][backend] = {"actual": actual, "differences": differences, "passed": not differences,
                                                  "elapsed_seconds": measured["elapsed_seconds"],
                                                  "peak_sampled_rss_bytes": measured["peak_sampled_rss_bytes"]}
                    checkpoint()
                if args.browser:
                    report["active"] = {"fixture": tag, "seed": seed, "stage": "chromium"}
                    checkpoint()
                    actual, elapsed = browser_state(wasm_js, seed)
                    differences = compare_states(expected, actual, args.cpsr_mask)
                    case["backends"]["chromium"] = {"actual": actual, "differences": differences, "passed": not differences,
                                                       "elapsed_seconds": elapsed}
                    checkpoint()


def pipeline(args) -> int:
    for name in ("clang", "cxx", "emxx", "sla"):
        if not getattr(args, name):
            raise FixtureError(f"--rearm requires --{name}")
    # Validate before invoking any compiler. Empty suites cannot report success.
    seeds(args.cases)
    if args.load_bias and not args.shared:
        raise FixtureError("--load-bias requires --shared for pipeline compilation")
    if not args.modes or not args.optimizations:
        raise FixtureError("pipeline needs at least one mode and optimization")
    root = Path(__file__).resolve().parents[1]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"schema_version": 1, "complete": False, "passed": False, "cases": [], "steps": [], "artifacts": []}
    report_path = args.metrics_out or out / "metrics.json"

    def checkpoint():
        write_report(report_path, report)

    checkpoint()
    try:
        run_pipeline_body(args, root, out, report, checkpoint)
        expected_cases = len(args.modes) * len(args.optimizations) * args.cases
        expected_backends = {"native", "node", "chromium"} if args.browser else {"native", "node"}
        report["complete"] = len(report["cases"]) == expected_cases and all(set(case["backends"]) == expected_backends for case in report["cases"])
        report["passed"] = report["complete"] and bool(report["cases"]) and all(
            backend["passed"] for case in report["cases"] for backend in case["backends"].values())
        report.pop("active", None)
    except BaseException as error:
        report["failure"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        checkpoint()
    print(json.dumps({"passed": report["passed"], "cases": len(report["cases"]), "report": str(report_path)}, indent=2))
    return 0 if report["passed"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--entry", default="fixture_entry")
    parser.add_argument("--observe", action="append", help="symbol or address:size; default fixture_output")
    parser.add_argument("--max-instructions", type=int, default=1_000_000)
    parser.add_argument("--cpsr-mask", type=lambda x: int(x, 0), default=DEFAULT_CPSR_MASK)
    parser.add_argument("--reference-out", type=Path)
    parser.add_argument("--actual", action="append", type=Path, default=[], help="runner JSON to compare (repeat for native, Node, Chromium)")
    parser.add_argument("--step", action="append", default=[], help='measured command as JSON argv array; no shell; executes before comparison')
    parser.add_argument("--artifact", action="append", type=Path, default=[], help="generated source/object/WASM file to measure")
    parser.add_argument("--metrics-out", type=Path)
    parser.add_argument("--seed", type=lambda x: int(x, 0), default=0)
    parser.add_argument("--rearm", help="enable full ARM compile/translate/native/WASM validation pipeline")
    parser.add_argument("--clang")
    parser.add_argument("--cxx")
    parser.add_argument("--emxx")
    parser.add_argument("--sla")
    parser.add_argument("--node", default="node")
    parser.add_argument("--browser", action="store_true")
    parser.add_argument("--cases", type=int, default=4)
    parser.add_argument("--modes", nargs="+", choices=("arm", "thumb"), default=["arm", "thumb"])
    parser.add_argument("--optimizations", nargs="+", choices=("0", "2"), default=["0", "2"])
    parser.add_argument("--out", type=Path, default=Path("build/e2e"))
    parser.add_argument("--fixture", type=Path, help="freestanding C/assembly fixture with fixture_entry and 64-byte fixture_output")
    parser.add_argument("--shared", action="store_true", help="compile PIC ET_DYN using the Android ARM linker profile")
    parser.add_argument("--load-bias", type=lambda x: int(x, 0), default=0)
    parser.add_argument("--dispatch", choices=("iterative", "musttail"), default="iterative")
    parser.add_argument("--dependency", action="append", default=[], help="explicit guest library PATH@LOAD_BIAS; repeat for multiple modules")
    parser.add_argument("--initialize", choices=("yes", "no"), default="yes")
    args = parser.parse_args()
    if args.max_instructions <= 0:
        raise FixtureError("instruction limit must be positive")
    if args.rearm:
        return pipeline(args)
    if args.elf is None:
        parser.error("--elf or --rearm is required")
    metrics = {"steps": [], "artifacts": [], "comparisons": []}
    for step in args.step:
        metrics["steps"].append(run_measured(json.loads(step)))
    data = args.elf.read_bytes()
    elf = load_fixture(args.elf, args.load_bias, args.dependency)
    observed = [parse_range(value, elf) for value in (args.observe or ["fixture_output"])]
    expected = reference_state(elf, args.entry, observed, args.max_instructions, args.seed & 0xFFFFFFFF, args.initialize == "yes")
    expected["input_sha256"] = hashlib.sha256(data).hexdigest()
    if args.reference_out:
        args.reference_out.parent.mkdir(parents=True, exist_ok=True)
        args.reference_out.write_text(json.dumps(expected, indent=2) + "\n", encoding="utf-8")
    passed = True
    for actual_path in args.actual:
        differences = compare_states(expected, json.loads(actual_path.read_text(encoding="utf-8")), args.cpsr_mask)
        metrics["comparisons"].append({"path": str(actual_path), "passed": not differences, "differences": differences})
        passed &= not differences
    for artifact in args.artifact:
        contents = artifact.read_bytes()
        metrics["artifacts"].append({"path": str(artifact), "size_bytes": len(contents), "sha256": hashlib.sha256(contents).hexdigest()})
    metrics["reference"] = expected["reference"]
    metrics["input_sha256"] = expected["input_sha256"]
    metrics["reference_completed"] = True
    metrics["passed"] = passed if args.actual else None
    metrics["comparison_count"] = len(args.actual)
    if args.metrics_out:
        args.metrics_out.parent.mkdir(parents=True, exist_ok=True)
        args.metrics_out.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metrics, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FixtureError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"validate_e2e: {error}", file=sys.stderr)
        raise SystemExit(2)
