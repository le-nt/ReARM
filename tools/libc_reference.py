"""Independent finite ARM libc test oracle; not linked to production output.

SPDX-License-Identifier: Apache-2.0
Python byte operations supply memory semantics. ARM executes guest instructions
and real BX LR callback returns. Unknown callbacks fail; no fixture results are
precomputed. Import arena addresses are a shared runner ABI contract.
"""
import struct


class ReferenceLibc:
    names = ("memcpy", "memmove", "memset", "memcmp", "strlen", "strnlen",
             "strcmp", "strncmp", "strcpy", "strncpy")
    base = 0xFFD00000
    vm = 0  # unused: arguments remain ordinary fixture seed words

    def __init__(self):
        self.trace = []

    def resolve(self, name):
        return self.base + 4 * self.names.index(name) if name in self.names else None

    @staticmethod
    def entryArgument(seed):
        return seed

    def install(self, machine):
        machine.mem_map(self.base, 4096)
        for index in range(len(self.names)):
            machine.mem_write(self.base + index * 4, struct.pack("<I", 0xE12FFF1E))

    @staticmethod
    def string(machine, address, limit=1024*1024, bounded=False):
        data = bytearray()
        for index in range(limit):
            byte = bytes(machine.mem_read(address + index, 1))[0]
            if not byte:
                return bytes(data)
            data.append(byte)
        if bounded:
            return bytes(data)
        raise RuntimeError("unterminated libc reference string")

    @staticmethod
    def compare(a, b):
        for x, y in zip(a, b):
            if x != y:
                return x - y
        return 0

    def instruction(self, machine, pc, arm):
        if not self.base <= pc < self.base + 4096:
            return
        index = (pc - self.base) // 4
        if pc % 4 or index >= len(self.names) or machine.reg_read(arm.UC_ARM_REG_CPSR) & 32:
            raise RuntimeError("invalid ARM libc reference callback")
        name = self.names[index]
        self.trace.append(name)
        a, b, n = [machine.reg_read(getattr(arm, f"UC_ARM_REG_R{i}")) for i in range(3)]
        if name in ("memcpy", "memmove", "memset", "memcmp", "strncpy") and n > 16*1024*1024:
            raise RuntimeError("libc reference transfer exceeds byte limit")
        if (name == "strnlen" and b > 1024*1024) or (name == "strncmp" and n > 1024*1024):
            raise RuntimeError("libc reference string exceeds byte limit")
        result = a
        if name in ("memcpy", "memmove"):
            if n:
                machine.mem_write(a, bytes(machine.mem_read(b, n)))
        elif name == "memset":
            if n:
                machine.mem_write(a, bytes([b & 255]) * n)
        elif name == "memcmp":
            result = self.compare(bytes(machine.mem_read(a, n)), bytes(machine.mem_read(b, n))) if n else 0
        elif name == "strlen":
            result = len(self.string(machine, a))
        elif name == "strnlen":
            result = len(self.string(machine, a, b, True))
        elif name in ("strcmp", "strncmp"):
            limit = n if name == "strncmp" else 1024*1024
            left = self.string(machine, a, limit, name == "strncmp")
            right = self.string(machine, b, limit, name == "strncmp")
            # Include a terminator when one side ended before the bounded count.
            left += b"\0" if len(left) < limit else b""
            right += b"\0" if len(right) < limit else b""
            result = self.compare(left, right)
        elif name == "strcpy":
            machine.mem_write(a, self.string(machine, b) + b"\0")
        elif name == "strncpy":
            if n:
                data = self.string(machine, b, n, True)
                machine.mem_write(a, data.ljust(n, b"\0"))
        else:
            raise RuntimeError("unknown libc reference callback")
        machine.reg_write(arm.UC_ARM_REG_R0, result & 0xFFFFFFFF)
