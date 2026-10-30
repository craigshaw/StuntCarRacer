"""Check a staged Windows player for extra files and embedded asset resources."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
PLAYER = 'Stunt Car Racer.exe'
NOTICES = ('LICENSE', 'THIRD_PARTY_NOTICES.md')


def player_resources(data):
    """Read PE resources without loading or executing the player."""
    def read(offset, fmt):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError('Truncated Windows executable')
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b'MZ':
        raise ValueError('Expected a Windows executable')
    pe, = read(0x3c, '<I')
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, sections = read(pe + 4, '<HH')
    optional_bytes, characteristics = read(pe + 20, '<HH')
    optional = pe + 24
    magic, = read(optional, '<H')
    if machine != 0x8664 or magic != 0x20b or not characteristics & 2 or characteristics & 0x2000:
        raise ValueError('Expected an x64 Windows player executable')
    if not 1 <= sections <= 96 or optional_bytes < 136:
        raise ValueError('Invalid PE headers')
    directories, = read(optional + 108, '<I')
    if directories < 3:
        raise ValueError('Missing PE resource directory descriptor')
    resource_rva, resource_bytes = read(optional + 128, '<II')
    section_table = optional + optional_bytes
    section_ranges = []
    for n in range(sections):
        read(section_table + n * 40, '<40s')
        _, address, raw_bytes, raw_offset = read(section_table + n * 40 + 8, '<IIII')
        section_ranges.append((address, raw_bytes, raw_offset))

    def file_offset(rva, size):
        for address, raw_bytes, raw_offset in section_ranges:
            relative = rva - address
            if 0 <= relative and relative + size <= raw_bytes:
                offset = raw_offset + relative
                if offset + size <= len(data):
                    return offset
        raise ValueError('PE resource data lies outside the file sections')

    if not resource_rva and not resource_bytes:
        return []
    if not resource_rva or resource_bytes < 16:
        raise ValueError('Invalid PE resource directory')
    resource = file_offset(resource_rva, resource_bytes)
    result = []
    visited = set()

    def resource_offset(relative, size):
        if relative < 0 or relative + size > resource_bytes:
            raise ValueError('PE resource entry lies outside its directory')
        return resource + relative

    def directory(relative, identifiers):
        if relative in visited:
            raise ValueError('Repeated PE resource directory')
        visited.add(relative)
        offset = resource_offset(relative, 16)
        named, numbered = read(offset + 12, '<HH')
        if named:
            raise ValueError('Named PE resources are not approved for release')
        resource_offset(relative + 16, numbered * 8)
        for n in range(numbered):
            identifier, target = read(offset + 16 + n * 8, '<II')
            if identifier & 0x80000000:
                raise ValueError('Invalid numbered PE resource')
            path = (*identifiers, identifier)
            child = target & 0x7fffffff
            if len(path) < 3 and target & 0x80000000:
                directory(child, path)
            elif len(path) == 3 and not target & 0x80000000:
                rva, size = read(resource_offset(child, 16), '<II')
                start = file_offset(rva, size)
                result.append((*path, data[start:start + size]))
            else:
                raise ValueError('Invalid PE resource tree depth')

    directory(0, ())
    return result


def check_release(directory, *, branding=None):
    directory = Path(directory)
    problems = []
    files = []
    if branding is None:
        branding = json.loads((ROOT / 'setup/branding.json').read_text(encoding='utf-8'))
    expected = {PLAYER, *NOTICES}
    if not directory.is_dir():
        return {'status': 'failed', 'problems': ['Missing staged player directory'], 'files': []}
    entries = {path.name: path for path in directory.iterdir()}
    for name in sorted(expected - entries.keys()):
        problems.append('Missing release file: ' + name)
    for name, path in sorted(entries.items()):
        if name not in expected:
            problems.append('Unexpected release entry: ' + name)
            continue
        if path.is_symlink() or not path.is_file():
            problems.append('Release entry must be a regular file: ' + name)
            continue
        data = path.read_bytes()
        files.append({'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        if name in NOTICES:
            if data != (ROOT / name).read_bytes():
                problems.append('Release notice differs from source: ' + name)
        else:
            try:
                for kind, identifier, language, payload in player_resources(data):
                    if kind in {16, 24}:  # Version information and Windows manifest.
                        continue
                    if (kind == branding['resource_type'] and identifier == branding['resource_id']
                            and hashlib.sha256(payload).hexdigest() == branding['sha256']):
                        continue
                    problems.append(f'Unapproved embedded PE resource: type {kind}, id {identifier}, language {language}')
            except ValueError as error:
                problems.append(str(error))
    return {'status': 'failed' if problems else 'passed', 'problems': problems, 'files': files}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    result = check_release(args.directory)
    print(json.dumps(result, indent=2))
    return result['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
