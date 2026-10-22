// Test the same Windows -> WSL stdio transport used by Codex, on real m68k code.
import { pathToFileURL } from 'node:url';
import { writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import path from 'node:path';

import { root, packageRoot, wslArguments } from './local-tools.mjs';
const out = path.join(root, 'analysis/toolchain');
const sdk = path.join(packageRoot, 'node_modules/@modelcontextprotocol/client/dist');
const { Client } = await import(pathToFileURL(path.join(sdk, 'index.mjs')));
const { StdioClientTransport } = await import(pathToFileURL(path.join(sdk, 'stdio.mjs')));
const client = new Client({ name: 'stunt-car-racer-wsl-verification', version: '1.0.0' });
const transport = new StdioClientTransport({
  command: path.join(process.env.SystemRoot, 'System32/wsl.exe'),
  args: wslArguments('mcp'),
  stderr: 'pipe',
});
transport.stderr?.on('data', chunk => process.stderr.write(chunk));
await mkdir(out, { recursive: true });
const fixture = path.join(out, 'return-five.bin');
const bytes = Buffer.from([0x70, 0x05, 0x4e, 0x75]); // MOVEQ #5,D0; RTS
await writeFile(fixture, bytes);
const digest = createHash('sha256').update(bytes).digest('hex');
const linuxPath = '/mnt/' + fixture[0].toLowerCase() + fixture.slice(2).replaceAll('\\', '/');
const retained = { transport: 'Windows wsl.exe -> WSL -> REA MCP', wslArguments: wslArguments('mcp'), fixtureSha256: digest };
async function call(name, args = {}) {
  const result = await client.callTool({ name, arguments: args }, undefined, { timeout: 450000 });
  await writeFile(path.join(out, `rea-wsl-${name}.json`), JSON.stringify(result, null, 2) + '\n');
  if (result.isError) throw new Error(`${name}: ${JSON.stringify(result)}`);
  return JSON.parse(result.content.find(c => c.type === 'text').text);
}
try {
  await client.connect(transport);
  retained.server = client.getServerVersion();
  const tools = [];
  let cursor;
  do {
    const page = await client.listTools(cursor ? { cursor } : {});
    tools.push(...page.tools);
    cursor = page.nextCursor;
  } while (cursor);
  retained.toolCount = tools.length;
  await call('open_binary', { path: linuxPath, format: 'raw-binary', architecture: 'm68k', entry_point: '0x1000', provider_id: 'ghidra' });
  try {
    const evidence = await call('analyze_function', { procedure: '0x1000' });
    const value = evidence.raw_result?.value;
    if (evidence.subject?.architecture !== 'm68k' || evidence.subject?.digest?.sha256 !== digest
      || value?.procedure?.address !== '0x1000' || !value?.pseudocode?.includes('return 5;')) {
      throw new Error('Unexpected architecture, digest, entry or decompilation in retained evidence.');
    }
    retained.architecture = evidence.subject.architecture;
    retained.provider = evidence.provider;
    retained.assembly = value.assembly;
    retained.pseudocode = value.pseudocode;
    const mapping = await call('inspect_native_load_image');
    const verified = mapping.normalized_result;
    if (verified?.status !== 'verified' || verified?.target_sha256 !== digest
      || verified?.base_address !== '0x1000' || verified?.file_bytes !== bytes.length) {
      throw new Error('Load-image attestation did not verify the fixture mapping.');
    }
    retained.loadImage = { status: verified.status, baseAddress: verified.base_address,
      fileBytes: verified.file_bytes, checksPassed: verified.checks.filter(c => c.matched).length };
  } finally {
    await call('close_binary');
  }
  retained.status = 'passed';
  await writeFile(path.join(out, 'rea-wsl-m68k-verification.json'), JSON.stringify(retained, null, 2) + '\n');
  console.log(JSON.stringify(retained, null, 2));
} finally {
  await client.close();
}
