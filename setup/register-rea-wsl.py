"""Add the verified WSL MCP transport to the Windows user's shared Codex config."""
from pathlib import Path
import os
import tempfile
import tomllib

config = Path(os.environ.get('CODEX_HOME') or Path.home() / '.codex') / 'config.toml'
before = config.read_bytes()
parsed = tomllib.loads(before.decode('utf-8-sig'))
if 'rea_wsl' in parsed.get('mcp_servers', {}):
    raise SystemExit('rea_wsl is already registered; inspect before changing it.')
addition = r'''

# Linux Ghidra bridge, verified for raw Motorola 68000 analysis.
[mcp_servers.rea_wsl]
command = "C:\\Windows\\System32\\wsl.exe"
args = ["--distribution", "Ubuntu-24.04", "--user", "craig", "--exec", "/home/craig/.local/bin/rea-wsl", "mcp"]
startup_timeout_sec = 60
tool_timeout_sec = 450
'''
after = before + addition.encode('utf-8')
updated = tomllib.loads(after.decode('utf-8-sig'))
updated['mcp_servers'].pop('rea_wsl')
assert updated == parsed, 'Unrelated settings changed.'
backup = config.with_name(config.name + '.rea-wsl.backup')
with backup.open('xb') as stream:
    stream.write(before)
if config.read_bytes() != before:
    raise SystemExit('Codex config changed during preparation; no update applied.')
with tempfile.NamedTemporaryFile(dir=config.parent, prefix='rea-wsl-', suffix='.tmp', delete=False) as stream:
    stream.write(after)
    staged = Path(stream.name)
try:
    os.replace(staged, config)
finally:
    if staged.exists():
        staged.unlink()
assert config.read_bytes() == after
print('Registered rea_wsl:', config)
print('Backup:', backup)
