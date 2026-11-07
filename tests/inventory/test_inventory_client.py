# SPDX-License-Identifier: Apache-2.0
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('inventory_client', Path(__file__).resolve().parents[2] / 'tools/inventory_client.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def elf(machine=40):
    header = b'\x7fELF\x01\x01\x01' + bytes(9)
    header += struct.pack('<HHIIIIIHHHHHH', 2, machine, 1, 0x10000, 52, 0, 0x05000000, 52, 32, 1, 40, 0, 0)
    return header + struct.pack('<IIIIIIII', 1, 0, 0x10000, 0x10000, 84, 84, 5, 4096)


class InventoryTests(unittest.TestCase):
    def test_arm_header_and_hash(self):
        data = elf()
        result = tool.inspect_elf(data, 'public.so')
        self.assertTrue(result['initial_arm32_profile'])
        self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(result['load_segments'][0]['address'], 0x10000)
        self.assertEqual(result['imports'], [])

    def test_other_architecture_is_not_claimed_arm(self):
        self.assertFalse(tool.inspect_elf(elf(3), 'x86.so')['initial_arm32_profile'])

    def test_invalid_elf_rejected(self):
        with self.assertRaises(Exception):
            tool.inspect_elf(b'not ELF', 'invalid.so')

    def test_apk_read_without_extraction(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            apk = root / 'public.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('lib/armeabi-v7a/libpublic.so', elf())
                archive.writestr('assets/content.txt', 'test asset')
            report = tool.inventory([], [apk], [], 'synthetic-1', 'source-built public fixture')
            self.assertEqual(len(report['libraries']), 1)
            self.assertEqual(list(root.iterdir()), [apk])
            self.assertEqual(report['version'], 'synthetic-1')

    def test_no_native_input_rejected(self):
        with self.assertRaisesRegex(ValueError, 'no native libraries'):
            tool.inventory([], [], [], 'none', 'none')


if __name__ == '__main__':
    unittest.main()
