// Use the installed REA package's SDK to test its configured stdio transport.
import { pathToFileURL } from 'node:url';
import { writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';

import { root, packageRoot } from './local-tools.mjs';
const sdkRoot = path.join(packageRoot, 'node_modules/@modelcontextprotocol/client/dist');
const { Client } = await import(pathToFileURL(path.join(sdkRoot, 'index.mjs')));
const { StdioClientTransport } = await import(pathToFileURL(path.join(sdkRoot, 'stdio.mjs')));
const client = new Client({ name: 'stunt-car-racer-setup', version: '1.0.0' });
const transport = new StdioClientTransport({
  command: process.execPath,
  args: [path.join(packageRoot, 'scripts/rea.mjs'), 'mcp'],
  env: { ...process.env },
  stderr: 'pipe',
});
transport.stderr?.on('data', chunk => process.stderr.write(chunk));
try {
  await client.connect(transport);
  const tools = [];
  let cursor;
  do {
    const page = await client.listTools(cursor ? { cursor } : {});
    tools.push(...page.tools);
    cursor = page.nextCursor;
  } while (cursor);
  for (const name of ['open_binary', 'close_binary']) {
    if (!tools.some(tool => tool.name === name)) throw new Error(`Missing MCP tool ${name}`);
  }
  const result = { server: client.getServerVersion(), toolCount: tools.length, toolNames: tools.map(t => t.name) };
  await mkdir(path.join(root, 'analysis/toolchain'), { recursive: true });
  await writeFile(path.join(root, 'analysis/toolchain/rea-mcp.json'), JSON.stringify(result, null, 2) + '\n');
  await writeFile(path.join(root, 'analysis/toolchain/rea-mcp-schemas.json'), JSON.stringify(tools, null, 2) + '\n');
  console.log(JSON.stringify({ server: result.server, toolCount: result.toolCount, handshake: 'passed' }));
  if (process.argv.includes('--native')) {
    // Windows REA admits 6502 raw images. This is only a bridge smoke test;
    // the independent Ghidra test verifies the Amiga's 68000 instruction family.
    const fixture = path.join(root, 'analysis/toolchain/return-five-6502.bin');
    await writeFile(fixture, Buffer.from([0xa9, 0x05, 0x60]));
    const opened = await client.callTool({ name: 'open_binary', arguments: {
      path: fixture, format: 'raw-binary', architecture: '6502', entry_point: '0x8000', provider_id: 'ghidra'
    } });
    if (opened.isError) throw new Error(JSON.stringify(opened));
    try {
      const analysis = await client.callTool({ name: 'analyze_function', arguments: { procedure: '0x8000' } }, undefined, { timeout: 450000 });
      await writeFile(path.join(root, 'analysis/toolchain/rea-native-smoke.json'), JSON.stringify(analysis, null, 2) + '\n');
      if (analysis.isError) throw new Error(JSON.stringify(analysis));
      const evidence = JSON.parse(analysis.content.find(c => c.type === 'text').text);
      const value = evidence.raw_result?.value;
      if (!value?.pseudocode?.includes('return 5;') || value?.procedure?.address !== '0x8000') {
        throw new Error('REA returned unexpected fixture analysis; inspect saved evidence.');
      }
      console.log('REA native analysis passed: expected entry and return value; evidence saved.');
    } finally {
      const closed = await client.callTool({ name: 'close_binary', arguments: {} });
      if (closed.isError) throw new Error(JSON.stringify(closed));
    }
  }
} finally {
  await client.close();
}
