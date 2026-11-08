"""Independent finite JNI test service for Unicorn; never production runtime.

SPDX-License-Identifier: Apache-2.0
Only the public JNI_OnLoad fixture's measured calls are implemented. Other calls
fail. Matching arena addresses are a runner ABI contract, not host pointers.
"""
import struct


class ReferenceJni:
    base = 0x70000000
    callbacks = 0xFFE00000
    env_table = base
    vm_table = base + 233 * 4
    vm = vm_table + 8 * 4
    env = vm + 4

    def __init__(self):
        self.pending = False
        self.trace = []

    @staticmethod
    def word(machine, address, value):
        machine.mem_write(address, struct.pack("<I", value))

    def install(self, machine):
        machine.mem_map(self.base, 0x100000)
        machine.mem_map(self.callbacks, 0x2000)
        self.word(machine, self.vm, self.vm_table)
        self.word(machine, self.env, self.env_table)
        for table, first, count, callback in ((self.env_table, 4, 233, self.callbacks),
                                              (self.vm_table, 3, 8, self.callbacks + 0x1000)):
            for slot in range(first, count):
                address = callback + slot * 4
                self.word(machine, table + slot * 4, address)
                # ARM BX LR performs the real reference interworking return.
                self.word(machine, address, 0xE12FFF1E)

    def instruction(self, machine, pc, arm):
        if not self.callbacks <= pc < self.callbacks + 0x2000:
            return
        if machine.reg_read(arm.UC_ARM_REG_CPSR) & 32:
            raise RuntimeError("JNI reference callback entered in Thumb mode")
        args = [machine.reg_read(getattr(arm, f"UC_ARM_REG_R{n}")) for n in range(4)]
        slot = (pc - self.callbacks) // 4
        self.trace.append(slot)
        if slot == 0x1000 // 4 + 6:
            if args[0] != self.vm or args[2] != 0x10006:
                raise RuntimeError("unsupported reference GetEnv arguments")
            self.word(machine, args[1], self.env)
            result = 0
        else:
            if args[0] != self.env:
                raise RuntimeError("invalid reference JNIEnv")
            if slot == 4:
                result = 0x10006
            elif slot == 219:
                self.word(machine, args[1], self.vm)
                result = 0
            elif slot == 6:
                # Fixture specifically requests an undeclared class.
                name = bytes(machine.mem_read(args[1], 16))
                if name != b"fixture/Missing\0":
                    raise RuntimeError("unexpected reference class request")
                self.pending = True
                result = 0
            elif slot == 228:
                result = int(self.pending)
            elif slot == 17:
                self.pending = False
                result = 0
            else:
                raise RuntimeError(f"unsupported reference JNI slot {slot}")
        machine.reg_write(arm.UC_ARM_REG_R0, result)
