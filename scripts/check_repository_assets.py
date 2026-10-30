"""Reject original media, generated artifacts and bulk encoded assets in Git's index.

Examines indexed blobs, including forced additions and staged content that
differs from the working tree. It needs no original game data.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

FORBIDDEN_DIRECTORIES = {'analysis', 'working', 'captures', 'build', 'out', 'dist'}
FORBIDDEN_SUFFIXES = {'.adf', '.adz', '.ipf', '.dms', '.hdf', '.rom', '.uss', '.bin', '.dump',
                      '.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.wav', '.mp3', '.ogg', '.flac',
                      '.scr-assets', '.scr-save', '.scr-career', '.scr-hall', '.scr-index', '.pack', '.exe', '.dll', '.lib', '.obj', '.o', '.pdb', '.gpr', '.zip', '.7z'}
TEXT_SUFFIXES = {'.cpp', '.h', '.c', '.java', '.py', '.md', '.json', '.csv', '.txt', '.cmake', '.cmd', '.ps1', '.sh', '.mjs', '.yml', '.yaml'}
TEXT_NAMES = {'.gitignore', '.gitattributes', 'CMakeLists.txt', 'pre-commit', 'LICENSE'}
SCREENSHOTS = {entry['path']: entry for entry in json.loads(
    (Path(__file__).resolve().parents[1] / 'setup/screenshots.json').read_text())['screenshots']}


def violations(path, blob):
    screenshot = SCREENSHOTS.get(path)
    if screenshot and hashlib.sha256(blob).hexdigest() == screenshot['sha256']:
        return []
    p = PurePosixPath(path)
    reasons = []
    if p.parts[0].lower() in FORBIDDEN_DIRECTORIES or path.lower().startswith('setup/upstream/'):
        reasons.append('local/generated data directory')
    if p.suffix.lower() in FORBIDDEN_SUFFIXES:
        reasons.append('original media, asset pack or generated binary extension')
    if p.suffix.lower() not in TEXT_SUFFIXES and p.name not in TEXT_NAMES:
        reasons.append('unapproved file type; review the source-only policy')
    if len(blob) > 256 * 1024:
        reasons.append('source file exceeds256KiB; review for embedded data')
    if blob.startswith((b'\x89PNG', b'RIFF', b'SCRDATA1', b'PK\x03\x04')):
        reasons.append('media/asset archive signature disguised as source')
    try:
        text = blob.decode('utf-8-sig')
        if '\0' in text:
            reasons.append('binary/NUL content')
        if re.search(r'data:(?:image|audio)/[^;]+;base64,', text, re.I):
            reasons.append('embedded media data URL')
        if re.search(r'[A-Za-z0-9+/]{512,}={0,2}', text):
            reasons.append('bulk base64/hex data')
        # Catch bulk numeric assets disguised as C++/JSON source. Ordinary
        # algorithm constants and small synthetic fixtures remain permitted.
        if re.search(r'(?:\b(?:0x[0-9a-f]+|\d+)\s*,\s*){256}', text, re.I):
            reasons.append('bulk numeric asset data')
    except UnicodeDecodeError:
        reasons.append('non-text/binary content')
    return reasons


def check_index(root=None):
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root)
    problems = []
    paths = git('ls-files', '--stage', '-z').split(b'\0')
    count = 0
    for entry in paths:
        if not entry:
            continue
        metadata, raw_path = entry.split(b'\t', 1)
        mode, oid, stage = metadata.decode().split()
        path = raw_path.decode('utf-8')
        reasons = ['symlink, submodule or unresolved merge stage'] if (mode not in {'100644', '100755'} or stage != '0') else []
        blob = git('cat-file', 'blob', oid)
        reasons += violations(path, blob)
        count += 1
        if reasons:
            problems.append({'path': path, 'reasons': reasons})
    return {'status': 'failed' if problems else 'passed', 'indexed_files': count, 'problems': problems}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staged', action='store_true', help='Index is always checked; accepted for hook clarity')
    parser.add_argument('--worktree', action='store_true', help='Also inspect current non-ignored source files')
    args = parser.parse_args()
    result = check_index()
    if args.worktree:
        paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z']).split(b'\0')
        from pathlib import Path
        result['working_files'] = 0
        for raw in sorted(set(paths)):
            if not raw:
                continue
            path = raw.decode('utf-8');file = Path(path)
            if not file.is_file():
                continue
            reasons = violations(path, file.read_bytes());result['working_files'] += 1
            if reasons:
                result['problems'].append({'path': path, 'reasons': reasons, 'scope': 'working tree'})
        if result['problems']:
            result['status'] = 'failed'
    print(json.dumps(result, indent=2))
    return result['status'] != 'passed'


if __name__ == '__main__':
    sys.exit(main())
