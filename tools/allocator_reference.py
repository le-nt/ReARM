"""Independent bitmap guest allocator for finite ARM differential tests.

SPDX-License-Identifier: Apache-2.0
Production uses ordered free ranges; this oracle uses one occupancy byte per
eight-byte guest slot. Allocator addresses and zero-size policies are explicit
test-runner contracts, not Android allocator layout guarantees.
"""
import importlib.util
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location("libc_reference", Path(__file__).with_name("libc_reference.py"))
libc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(libc)


class ReferenceBasic(libc.ReferenceLibc):
    allocator_names = ("malloc", "calloc", "realloc", "free", "__errno")
    names = libc.ReferenceLibc.names + allocator_names
    heap_base = 0x60000000
    heap_size = 16*1024*1024
    errno = 0x70000000

    def __init__(self):
        super().__init__()
        self.occupied = bytearray(self.heap_size // 8)
        self.active = {}

    def resolve(self, name):
        if name in self.allocator_names:
            return self.base + 0x100 + 4*self.allocator_names.index(name)
        return super().resolve(name)

    def install(self, machine):
        super().install(machine)
        machine.mem_map(self.heap_base, self.heap_size)
        machine.mem_map(self.errno, 1024*1024)
        for name in self.allocator_names:
            machine.mem_write(self.resolve(name), struct.pack("<I", 0xE12FFF1E))

    def allocate(self, size):
        slots = (max(1, size) + 7) // 8
        if slots > len(self.occupied) or len(self.active) >= 65536:
            return 0
        at = self.occupied.find(bytes(slots))
        if at < 0:
            return 0
        self.occupied[at:at+slots] = b"\1"*slots
        pointer = self.heap_base + at*8
        self.active[pointer] = (size, slots)
        return pointer

    def release(self, pointer):
        if not pointer:
            return
        if pointer not in self.active:
            raise RuntimeError("invalid reference free")
        _, slots = self.active.pop(pointer)
        at = (pointer-self.heap_base)//8
        self.occupied[at:at+slots] = bytes(slots)

    def reallocate(self, machine, pointer, size):
        if not pointer:
            return self.allocate(size)
        if pointer not in self.active:
            raise RuntimeError("invalid reference realloc")
        if not size:
            self.release(pointer)
            return 0
        old_size, old_slots = self.active[pointer]
        slots = (size+7)//8
        index = (pointer-self.heap_base)//8
        if slots <= old_slots:
            self.occupied[index+slots:index+old_slots] = bytes(old_slots-slots)
            self.active[pointer] = (size, slots)
            return pointer
        end = index+slots
        if end <= len(self.occupied) and not any(self.occupied[index+old_slots:end]):
            self.occupied[index+old_slots:end] = b"\1"*(slots-old_slots)
            self.active[pointer] = (size, slots)
            return pointer
        fresh = self.allocate(size)
        if fresh:
            count = min(old_size, size)
            if count:
                machine.mem_write(fresh, bytes(machine.mem_read(pointer, count)))
            self.release(pointer)
        return fresh

    def instruction(self, machine, pc, arm):
        if not self.base+0x100 <= pc < self.base+0x114:
            return super().instruction(machine, pc, arm)
        if pc % 4 or machine.reg_read(arm.UC_ARM_REG_CPSR) & 32:
            raise RuntimeError("invalid reference allocator callback")
        name = self.allocator_names[(pc-self.base-0x100)//4]
        self.trace.append(name)
        a, b = [machine.reg_read(getattr(arm, f"UC_ARM_REG_R{i}")) for i in range(2)]
        failed = False
        if name == "malloc":
            result = self.allocate(a)
            failed = not result
        elif name == "calloc":
            count = a*b
            result = self.allocate(count) if count <= 0xFFFFFFFF else 0
            if result:
                machine.mem_write(result, bytes(max(1, count)))
            failed = not result
        elif name == "realloc":
            result = self.reallocate(machine, a, b)
            failed = not result and (b != 0 or a == 0)
        elif name == "free":
            self.release(a)
            result = 0
        elif name == "__errno":
            result = self.errno
        else:
            raise RuntimeError("unsupported reference allocator")
        if failed:
            machine.mem_write(self.errno, struct.pack("<I", 12))
        machine.reg_write(arm.UC_ARM_REG_R0, result)
