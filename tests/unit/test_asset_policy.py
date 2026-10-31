"""Synthetic checks for local-only extraction and Git index enforcement."""
import importlib.util
import struct
from contextlib import contextmanager
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/support'))
from test_paths import temporary_directory, test_output


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


policy = load('check_repository_assets')
importer = load('import_native_assets')


@contextmanager
def local_repository():
    with temporary_directory('asset-policy-tests') as directory:
        assert Path(directory).resolve().is_relative_to(test_output('asset-policy-tests').resolve())
        yield directory


class AssetPolicyTests(unittest.TestCase):
    def test_source_is_allowed(self):
        self.assertEqual(policy.violations('src/startup.cpp', b'void startup() {}\n'), [])
        self.assertEqual(policy.violations('LICENSE', b'MIT License\n'), [])

    def test_forced_additions_are_detected(self):
        with local_repository() as directory:
            subprocess.run(['git', 'init', '-q', directory], check=True)
            root = Path(directory)
            (root / '.gitignore').write_text('*.wav\n')
            (root / 'original.wav').write_bytes(b'RIFF\0WAVE')
            subprocess.run(['git', '-C', directory, 'add', '-f', 'original.wav'], check=True)
            result = policy.check_index(directory)
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(result['problems'][0]['path'], 'original.wav')

    def test_index_content_is_checked(self):
        with local_repository() as directory:
            subprocess.run(['git', 'init', '-q', directory], check=True)
            source = Path(directory) / 'asset.cpp'
            source.write_bytes(b'\0binary asset hidden as source')
            subprocess.run(['git', '-C', directory, 'add', 'asset.cpp'], check=True)
            source.write_text('void safe() {}\n')
            self.assertEqual(policy.check_index(directory)['status'], 'failed')

    def test_generated_directory_is_detected(self):
        self.assertTrue(policy.violations('analysis/assets/disguised.cpp', b'asset'))
        self.assertTrue(policy.violations('dist/disguised.cpp', b'asset'))
        self.assertTrue(policy.violations('ghosts/best.scr-ghost', b'local player recording'))

    def test_encoded_media_is_detected(self):
        self.assertTrue(policy.violations('src/asset.h', b'"data:' + b'image/png;base64,AA"'))
        self.assertTrue(policy.violations('src/asset.h', b'"' + b'A' * 600 + b'"'))
        self.assertTrue(policy.violations('src/asset.h', b'{'+b'0x10,' * 300+b'}'))

    def test_unknown_file_type_requires_review(self):
        self.assertTrue(policy.violations('assets/car.unusual', b'opaque original data'))

    def test_wrong_original_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            importer.build_pack(b'not the original ADF')

    def test_each_import_uses_its_own_source_and_payload_identity(self):
        for marker in (7, 19):
            disk = bytearray(importer.ADF_BYTES)
            disk[importer.DISK_OFFSET + 40] = marker
            digest = importer.sha(disk)
            payload_digest = importer.sha(disk[importer.DISK_OFFSET:importer.DISK_OFFSET + importer.PAYLOAD_BYTES])
            with patch.object(importer, 'ADF_SOURCES', {digest: {'name': 'Synthetic', 'payload_sha256': payload_digest}}):
                packed = importer.build_pack(disk)
                fields = struct.unpack('<8sII32s32sII', packed[:88])
                self.assertEqual(fields[3].hex(), digest)
                self.assertEqual(fields[4].hex(), payload_digest)
                self.assertEqual(packed[88:], disk[importer.DISK_OFFSET:importer.DISK_OFFSET + importer.PAYLOAD_BYTES])
            with patch.object(importer, 'ADF_SOURCES', {digest: {'name': 'Synthetic', 'payload_sha256': '0' * 64}}):
                with self.assertRaisesRegex(ValueError, 'payload identity'):
                    importer.build_pack(disk)

    def test_only_exact_pinned_screenshots_are_allowed(self):
        for name in policy.SCREENSHOTS:
            content = (ROOT / name).read_bytes()
            self.assertEqual(policy.violations(name, content), [])
            self.assertTrue(policy.violations(name, content + b'changed'))
            self.assertTrue(policy.violations('docs/screenshots/unreviewed.png', content))

    def test_staged_screenshot_replacement_is_checked(self):
        name = next(iter(policy.SCREENSHOTS))
        approved = (ROOT / name).read_bytes()
        with local_repository() as directory:
            subprocess.run(['git', 'init', '-q', directory], check=True)
            image = Path(directory) / name
            image.parent.mkdir(parents=True)
            image.write_bytes(approved + b'unreviewed')
            subprocess.run(['git', '-C', directory, 'add', name], check=True)
            image.write_bytes(approved)
            self.assertEqual(policy.check_index(directory)['status'], 'failed')

    def test_pack_outputs_are_confined(self):
        with self.assertRaisesRegex(ValueError, 'analysis/'):
            importer.import_assets(ROOT / 'unused.adf', ROOT / 'src/disguised.cpp')

    def test_import_never_overwrites_its_source(self):
        # This is a lexical validation only: no ADF is created in analysis/.
        path = ROOT / 'analysis/asset-policy-tests/source.adf'
        with self.assertRaisesRegex(ValueError, 'preserve'):
            importer.import_assets(path, path)


if __name__ == '__main__':
    unittest.main()
