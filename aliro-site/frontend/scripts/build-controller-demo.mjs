// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const frontend = resolve(dirname(fileURLToPath(import.meta.url)), '..');
// Public monorepo and the site's deployment mirror keep the same UI sources.
const ui = [resolve(frontend, '../../aliro-esp32/data'), resolve(frontend, '../aliro-esp32/data')]
  .find(path => existsSync(join(path, 'src/main.demo.ts')));
if (!ui) throw new Error('Controller UI source missing; see the website build instructions.');

let useInstalledBun = false;
try { useInstalledBun = execFileSync('bun', ['--version'], { encoding: 'utf8' }).trim() === '1.3.13'; }
catch { /* npm supplies the pinned official Bun binary on build hosts. */ }
execFileSync(useInstalledBun ? 'bun' : 'npm', useInstalledBun
  ? ['install', '--frozen-lockfile']
  : ['exec', '--yes', '--package=bun@1.3.13', '--', 'bun', 'install', '--frozen-lockfile'],
{ cwd: ui, stdio: 'inherit' });

const output = resolve(frontend, 'public/demo');
execFileSync(process.execPath, [join(ui, 'node_modules/vite/bin/vite.js'), 'build',
  '--mode', 'web-demo', '--outDir', output, '--emptyOutDir'], { cwd: ui, stdio: 'inherit' });

// Carry license texts with the served bundle; generated assets stay untracked.
const repository = resolve(ui, '../..');
const notices = [
  ['Project-original demo code', resolve(repository, 'LICENSE')],
  ['Upstream controller UI', join(ui, 'LICENSE')],
  ['Svelte', join(ui, 'node_modules/svelte/LICENSE.md')],
  ['esm-env', join(ui, 'node_modules/esm-env/LICENSE')],
  ['clsx', join(ui, 'node_modules/clsx/license')],
  ['Vite runtime helpers', join(ui, 'node_modules/vite/LICENSE.md')],
  ['sv-router', join(ui, 'node_modules/sv-router/LICENSE')],
  ['TanStack Svelte Virtual', join(ui, 'node_modules/@tanstack/svelte-virtual/LICENSE')],
  ['TanStack Virtual Core', join(ui, 'node_modules/@tanstack/virtual-core/LICENSE')],
  ['Tailwind CSS', join(ui, 'node_modules/tailwindcss/LICENSE')],
  ['daisyUI', join(ui, 'node_modules/daisyui/LICENSE')],
];
writeFileSync(join(output, 'NOTICES.txt'),
  'Aliro controller browser demo — source: https://github.com/ilia-ae/aliro-mayday\n\n'
  + notices.map(([name, path]) => `=== ${name} ===\n${readFileSync(path, 'utf8')}`).join('\n\n'));
