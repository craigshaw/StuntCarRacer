"""Shared CMake locations for the Windows verification wrappers.

Reports and private fixtures keep their existing analysis/ locations. Only
compiler output is shared; suites retain independent reports and receipts.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE_BUILD = ROOT / 'build/windows'
ORACLE_BUILD = ROOT / 'build/windows-oracle'


def configure_command(*, oracle=False):
    return ['cmake', '--preset', 'windows-oracle' if oracle else 'windows-release']


def build_sources():
    """Build definitions included in source identities, including CMake modules."""
    return {ROOT / 'CMakeLists.txt', ROOT / 'CMakePresets.json', ROOT / 'setup/branding.json', Path(__file__).resolve(),
            *sorted((ROOT / 'cmake').glob('*.cmake'))}
