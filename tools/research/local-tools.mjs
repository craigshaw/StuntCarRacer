// Optional machine configuration shared by the research transport checks.
import { readFileSync, existsSync } from 'node:fs';
import path from 'node:path';

export const root = path.resolve(import.meta.dirname, '../..');
const filename = path.join(root, 'setup/local-tools.json');
export const localTools = existsSync(filename) ? JSON.parse(readFileSync(filename, 'utf8').replace(/^\uFEFF/, '')) : {};
export const packageRoot = localTools.reaPackage || process.env.SCR_REA_PACKAGE ||
  path.join(process.env.APPDATA || path.resolve(path.dirname(process.execPath), '..', 'lib'),
    process.env.APPDATA ? 'npm/node_modules/rea-agents' : 'node_modules/rea-agents');

if (localTools.ghidra) process.env.GHIDRA_INSTALL_DIR = localTools.ghidra;
if (localTools.javaHome) process.env.JAVA_HOME = localTools.javaHome;
if (process.env.JAVA_HOME) process.env.PATH = path.join(process.env.JAVA_HOME, 'bin') + path.delimiter + (process.env.PATH || '');

export function wslArguments(...args) {
  const flags = [];
  if (localTools.wslDistribution) flags.push('--distribution', localTools.wslDistribution);
  if (localTools.wslUser) flags.push('--user', localTools.wslUser);
  return [...flags, '--exec', ...(localTools.wslRea ? [localTools.wslRea] :
    ['sh', '-lc', 'exec "$HOME/.local/bin/rea-wsl" "$@"', 'rea-wsl']), ...args];
}
