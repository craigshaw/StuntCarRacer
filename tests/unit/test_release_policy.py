"""Synthetic distribution checks; no original assets or compiler are needed."""
from pathlib import Path
import hashlib
import shutil
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/support'))
from check_release import NOTICES, PLAYER, check_release, player_resources
from test_paths import temporary_directory


def executable(resource_type=None, payload=b'synthetic resource', resource_id=101):
    raw_bytes = max(512, ((88 + len(payload) + 511) // 512) * 512)
    data = bytearray(512 + raw_bytes)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 1)
    struct.pack_into('<HH', data, 0x94, 240, 2)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<I', data, optional + 108, 16)
    struct.pack_into('<IIII', data, optional + 240 + 8, raw_bytes, 0x1000, raw_bytes, 512)
    if resource_type is not None:
        struct.pack_into('<II', data, optional + 128, 0x1000, 88 + len(payload))
        for offset, identifier, target in ((0, resource_type, 0x80000018),
                                           (24, resource_id, 0x80000030),
                                           (48, 1033, 72)):
            struct.pack_into('<HH', data, 512 + offset + 12, 0, 1)
            struct.pack_into('<II', data, 512 + offset + 16, identifier, target)
        struct.pack_into('<II', data, 512 + 72, 0x1000 + 88, len(payload))
        data[600:600 + len(payload)] = payload
    return data


class ReleasePolicyTests(unittest.TestCase):
    def bundle(self, directory, resource_type=None):
        root = Path(directory)
        (root / PLAYER).write_bytes(executable(resource_type))
        for name in NOTICES:
            shutil.copyfile(ROOT / name, root / name)
        return root

    def test_player_and_current_notices_are_accepted(self):
        with temporary_directory('release-policy') as directory:
            result = check_release(self.bundle(directory, 24))
            self.assertEqual(result['status'], 'passed')
            self.assertEqual(len(result['files']), 3)
            self.assertTrue(all(len(file['sha256']) == 64 for file in result['files']))

    def test_executable_without_resources_is_accepted(self):
        self.assertEqual(player_resources(executable()), [])

    def test_only_exact_approved_logo_is_accepted(self):
        payload = b'approved synthetic branding'
        branding = {'resource_type': 10, 'resource_id': 101,
                    'sha256': hashlib.sha256(payload).hexdigest()}
        for kind, identifier, contents, accepted in ((10, 101, payload, True),
                                                     (10, 102, payload, False),
                                                     (2, 101, payload, False),
                                                     (10, 101, payload + b'changed', False)):
            with self.subTest(kind=kind, identifier=identifier, accepted=accepted), temporary_directory('release-policy') as directory:
                root = self.bundle(directory)
                (root / PLAYER).write_bytes(executable(kind, contents, identifier))
                self.assertEqual(check_release(root, branding=branding)['status'] == 'passed', accepted)

    def test_asset_resource_types_are_rejected(self):
        for kind in (2, 3, 10, 14):
            with self.subTest(resource_type=kind), temporary_directory('release-policy') as directory:
                result = check_release(self.bundle(directory, kind))
                self.assertEqual(result['status'], 'failed')
                self.assertTrue(any('embedded PE resource' in reason for reason in result['problems']))

    def test_named_and_out_of_bounds_resources_are_rejected(self):
        named = executable(24)
        struct.pack_into('<HH', named, 512 + 12, 1, 0)
        outside = executable(24)
        struct.pack_into('<II', outside, 0x98 + 128, 0x2000, 40)
        for data in (named, outside):
            with self.subTest(data=data[:2]), self.assertRaises(ValueError):
                player_resources(data)

    def test_invalid_executables_are_rejected(self):
        wrong_cpu = executable()
        struct.pack_into('<H', wrong_cpu, 0x84, 0x14c)
        for data in (b'original disk bytes', b'MZ', executable()[:200], wrong_cpu):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                player_resources(data)

    def test_extra_files_and_directories_are_rejected(self):
        with temporary_directory('release-policy') as directory:
            root = self.bundle(directory)
            (root / 'assets').mkdir()
            (root / 'disk.adf').write_bytes(b'not real game data')
            result = check_release(root)
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(len(result['problems']), 2)

    def test_missing_and_changed_notices_are_rejected(self):
        with temporary_directory('release-policy') as directory:
            root = self.bundle(directory)
            (root / NOTICES[0]).unlink()
            (root / NOTICES[1]).write_text('outdated notice')
            result = check_release(root)
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(len(result['problems']), 2)

    def test_missing_directory_is_rejected(self):
        with temporary_directory('release-policy') as directory:
            self.assertEqual(check_release(Path(directory) / 'absent')['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
