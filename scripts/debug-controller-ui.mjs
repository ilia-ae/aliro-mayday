#!/usr/bin/env bun
// Copyright (c) 2026 Mayday / Ilia Arestov. See ../LICENSE.
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { startDemoServer } from './controller-ui-demo.mjs';

export function parseArgs(args) {
  if (args.length === 1 && ['--help', '-h'].includes(args[0])) return { help: true };
  if (!args.length) return { port: 5173 };
  if (args.length === 2 && args[0] === '--port' && /^\d+$/.test(args[1])) {
    const port = Number(args[1]);
    if (port >= 1024 && port <= 65535) return { port };
  }
  throw new Error('Usage: bun scripts/debug-controller-ui.mjs [--port 5173 | --help] (port 1024–65535)');
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    console.log('Run the real controller web UI with synthetic read-only data and hot reload.\n'
      + 'Usage: bun scripts/debug-controller-ui.mjs [--port 5173 | --help]\n'
      + 'Open http://127.0.0.1:5173 (or the selected port). Ctrl+C stops both servers.\n'
      + 'Dependencies are installed from bun.lock on first run. Node.js 22.12+ (24 LTS recommended). No ESP-IDF or controller required.');
    return;
  }
  const runtime = Bun.spawn(["node", "-e", "const [m,n]=process.versions.node.split('.').map(Number);process.exit(m>22||(m===22&&n>=12)?0:1)"], {
    stdout: 'ignore', stderr: 'ignore',
  });
  if (await runtime.exited !== 0) throw new Error('Node.js 22.12+ is required (24 LTS recommended)');
  const ui = fileURLToPath(new URL('../aliro-esp32/data/', import.meta.url));
  const vite = join(ui, 'node_modules/vite/bin/vite.js');
  if (!existsSync(vite)) {
    const install = Bun.spawn([process.execPath, 'install', '--frozen-lockfile'], {
      cwd: ui, stdin: 'inherit', stdout: 'inherit', stderr: 'inherit',
    });
    if (await install.exited !== 0) throw new Error('UI dependency installation failed');
  }
  const demo = startDemoServer();
  let child;
  let stopping = false;
  const stop = () => {
    if (stopping) return;
    stopping = true;
    child?.kill('SIGTERM');
    void demo.stop();
  };
  process.once('SIGINT', stop);
  process.once('SIGTERM', stop);
  try {
    console.log(`Read-only demo — no controller connected. Open http://127.0.0.1:${options.port}/`);
    child = Bun.spawn(['node', vite, '--host', '127.0.0.1', '--port', String(options.port), '--strictPort'], {
      cwd: ui,
      env: { ...process.env, ALIRO_UI_DEMO: '1', ALIRO_UI_BACKEND_URL: String(demo.url) },
      stdin: 'inherit', stdout: 'inherit', stderr: 'inherit',
    });
    const code = await child.exited;
    if (!stopping) process.exitCode = code;
  } finally {
    stop();
    process.removeListener('SIGINT', stop);
    process.removeListener('SIGTERM', stop);
    await demo.stop();
  }
}

if (import.meta.main) main().catch((error) => { console.error(error.message); process.exitCode = 1; });
