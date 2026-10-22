"""Add the verified WSL MCP transport to the Windows user's shared Codex config."""
from pathlib import Path
import json
import os
import tempfile
import tomllib

config = Path(os.environ.get('CODEX_HOME') or Path.home() / '.codex') / 'config.toml'
before = config.read_bytes()
parsed = tomllib.loads(before.decode('utf-8-sig'))
if 'rea_wsl' in parsed.get('mcp_servers', {}):
    raise SystemExit('rea_wsl is already registered; inspect before changing it.')
tools_file = Path(__file__).resolve().parent / 'local-tools.json'
tools = json.loads(tools_file.read_text(encoding='utf-8-sig')) if tools_file.exists() else {}
wsl_args = []
if tools.get('wslDistribution'):
    wsl_args += ['--distribution', tools['wslDistribution']]
if tools.get('wslUser'):
    wsl_args += ['--user', tools['wslUser']]
wsl_args += ['--exec']
wsl_args += ([tools['wslRea']] if tools.get('wslRea') else
             ['sh', '-lc', 'exec "$HOME/.local/bin/rea-wsl" "$@"', 'rea-wsl'])
wsl_args += ['mcp']
command = str(Path(os.environ['SystemRoot']) / 'System32' / 'wsl.exe')
addition = ('\n\n# Linux Ghidra bridge for raw Motorola 68000 analysis.\n'
            '[mcp_servers.rea_wsl]\n'
            f'command = {json.dumps(command)}\nargs = {json.dumps(wsl_args)}\n'
            'startup_timeout_sec = 60\ntool_timeout_sec = 450\n')
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
