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

    def __init__(self, registered=False, extra_args=()):
        self.pending = False
        self.trace = []
        self.registered = registered
        self.native_entry = None
        self.next_handle = 0x10000001
        self.class_handles = set()
        self.extra_args = tuple(extra_args)
        self.native_signature = "(" + "I" * (1 + len(self.extra_args)) + ")I"

    @staticmethod
    def string(machine, address):
        data = bytearray()
        for index in range(4096):
            byte = bytes(machine.mem_read(address + index, 1))[0]
            if not byte:
                return data.decode("ascii")
            data.append(byte)
        raise RuntimeError("unterminated reference JNI string")

    def native_invocation(self, machine, arm, seed):
        if not self.registered:
            return None
        if self.pending or machine.reg_read(arm.UC_ARM_REG_R0) != 0x10006 or self.native_entry is None:
            raise RuntimeError("reference JNI_OnLoad failed or native not registered")
        handle = self.next_handle
        self.next_handle += 1
        self.class_handles.add(handle)
        return self.native_entry, [self.env, handle, seed, *self.extra_args]

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
                name = self.string(machine, args[1])
                if name == "fixture/Bridge" and self.registered:
                    result = self.next_handle
                    self.next_handle += 1
                    self.class_handles.add(result)
                elif name == "fixture/Missing":
                    self.pending = True
                    result = 0
                else:
                    raise RuntimeError("unexpected reference class request")
            elif slot in (113, 215) and self.registered:
                if args[1] not in self.class_handles:
                    raise RuntimeError("reference native operation needs class handle")
                if slot == 113:
                    name, descriptor = self.string(machine, args[2]), self.string(machine, args[3])
                    result = 0x20000001
                else:
                    if args[3] != 1:
                        raise RuntimeError("reference fixture expected one native registration")
                    name_ptr, descriptor_ptr, entry = struct.unpack("<III", machine.mem_read(args[2], 12))
                    name, descriptor = self.string(machine, name_ptr), self.string(machine, descriptor_ptr)
                    if not entry or (not entry & 1 and entry & 3):
                        raise RuntimeError("invalid reference native function address")
                    self.native_entry = entry
                    result = 0
                if (name, descriptor) != ("transform", self.native_signature):
                    raise RuntimeError("reference undeclared native method")
            elif slot == 228:
                result = int(self.pending)
            elif slot == 17:
                self.pending = False
                result = 0
            else:
                raise RuntimeError(f"unsupported reference JNI slot {slot}")
        machine.reg_write(arm.UC_ARM_REG_R0, result)
