"""Disposable Python test output belongs to the build, not research evidence."""
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def test_output(suite):
    base = Path(os.environ.get('SCR_TEST_OUTPUT_ROOT', ROOT / 'build/windows/test-output/python'))
    directory = base / suite
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def temporary_directory(suite):
    return tempfile.TemporaryDirectory(dir=test_output(suite))
