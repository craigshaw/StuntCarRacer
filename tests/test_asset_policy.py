"""Synthetic checks for local-only extraction and Git index enforcement."""
import importlib.util
from contextlib import contextmanager
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


policy = load('check_repository_assets')
importer = load('import_native_assets')


@contextmanager
def local_repository():
    base = (ROOT / 'analysis/asset-policy-tests').resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=base) as directory:
        assert Path(directory).resolve().is_relative_to(base)
        yield directory


class AssetPolicyTests(unittest.TestCase):
    def test_source_is_allowed(self):
        self.assertEqual(policy.violations('src/startup.cpp', b'void startup() {}\n'), [])

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

    def test_encoded_media_is_detected(self):
        self.assertTrue(policy.violations('src/asset.h', b'"data:' + b'image/png;base64,AA"'))
        self.assertTrue(policy.violations('src/asset.h', b'"' + b'A' * 600 + b'"'))
        self.assertTrue(policy.violations('src/asset.h', b'{'+b'0x10,' * 300+b'}'))

    def test_unknown_file_type_requires_review(self):
        self.assertTrue(policy.violations('assets/car.unusual', b'opaque original data'))

    def test_wrong_original_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            importer.build_pack(b'not the original ADF')

    def test_pack_outputs_are_confined(self):
        with self.assertRaisesRegex(ValueError, 'analysis/'):
            importer.import_assets(ROOT / 'unused.adf', ROOT / 'src/disguised.cpp')

    def test_import_never_overwrites_its_source(self):
        path = ROOT / 'analysis/asset-policy-tests/source.adf'
        with self.assertRaisesRegex(ValueError, 'preserve'):
            importer.import_assets(path, path)


if __name__ == '__main__':
    unittest.main()
