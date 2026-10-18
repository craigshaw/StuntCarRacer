"""Fetch/verify the pinned test-only Musashi sources; never build or run them."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'setup/musashi-source-lock.json'
DEST = ROOT / 'analysis/reference/musashi'


def prepare(download=False):
    lock = json.loads(LOCK.read_text(encoding='utf-8'))
    for item in lock['files']:
        target = DEST / item['path']
        if not target.exists():
            if not download:
                raise RuntimeError(f'Missing {target}; use --download to fetch pinned sources')
            url = f"https://raw.githubusercontent.com/kstenerud/Musashi/{lock['commit']}/{item['path']}"
            with urlopen(url, timeout=45) as response:
                data = response.read()
        else:
            data = target.read_bytes()
        if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise RuntimeError(f'Source identity mismatch: {target}')
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    return lock


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    pinned = prepare(args.download)
    print(f"Verified {len(pinned['files'])} Musashi source files at {pinned['commit']}")
