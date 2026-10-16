// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { afterAll, beforeAll, expect, test } from 'bun:test';
import { startDemoServer } from '../controller-ui-demo.mjs';
import { parseArgs } from '../debug-controller-ui.mjs';
import { fileURLToPath } from 'node:url';

let demo;
beforeAll(() => { demo = startDemoServer(); });
afterAll(async () => { await demo.stop(); });
const request = (path, init) => fetch(new URL(path, demo.url), init);

test('dashboard and settings return synthetic data with the UI response shape', async () => {
  for (const type of ['hkinfo', 'mqtt', 'actions', 'misc']) {
    const response = await request(`/config?type=${type}`);
    const result = await response.json();
    expect(response.status).toBe(200);
    expect(result.success).toBe(true);
    expect(result.data).toBeObject();
  }
  const reader = await (await request('/config?type=hkinfo')).json();
  expect(reader.data.issuers[0].endpoints[0].endpointId).toBe('Demo key');
  const actions = await (await request('/config?type=actions')).json();
  expect(actions.data.neopixelSuccessColor).toEqual([[0, 0], [1, 255], [2, 0]]);
  expect((await request('/config?type=toString')).status).toBe(404);
});

test('network, certificates and captive portal reads stay synthetic', async () => {
  for (const path of ['/eth_get_config', '/nfc_get_presets', '/certificates', '/captive_portal_config', '/wifi_scan']) {
    expect((await (await request(path)).json()).success).toBe(true);
  }
  const config = await (await request('/captive_portal_config')).json();
  expect(config.data.wifiPassword).toBe('');
  expect(config.data.setupCode).toBe('');
  expect(config.data.ethernetEnabled).toBe(false);
});

test('writes, uploads and even GET device commands are rejected', async () => {
  for (const path of ['/reboot_device', '/reset_hk_pair', '/reset_wifi_cred', '/start_config_ap', '/config/save?type=misc', '/ota/upload', '/ota/littlefs']) {
    for (const method of ['GET', 'POST', 'DELETE']) {
      const response = await request(path, { method });
      expect(response.status).toBe(403);
      expect((await response.json()).error).toContain('Read-only demo');
    }
  }
  expect((await request('/certificates', { method: 'POST', body: 'not a certificate' })).status).toBe(403);
  expect((await request('/captive_portal_config', { method: 'POST', body: '{}' })).status).toBe(403);
});

test('WebSocket supports reads and rejects malformed or mutating commands', async () => {
  const messages = [];
  const ws = new WebSocket(String(demo.url).replace(/^http/, 'ws') + 'ws');
  const next = () => new Promise((resolve, reject) => {
    let retry;
    const deadline = setTimeout(() => {
      clearTimeout(retry);
      reject(new Error('No WebSocket response'));
    }, 2000);
    const check = () => {
      if (messages.length) { clearTimeout(deadline); resolve(messages.shift()); }
      else retry = setTimeout(check, 5);
    };
    check();
  });
  ws.onmessage = (event) => messages.push(JSON.parse(event.data));
  try {
    const info = await next();
    expect(info.deviceName).toBe('Aliro demo stand');
    expect(info.chip_model).toBe(9);
    expect(info.log_level).toBe(3);
    expect((await next()).nfc_connected).toBe(false);
    expect((await next()).current_version).toBe('Demo preview');
    expect((await next()).msg).toContain('no hardware connected');
    for (const type of ['ping', 'sysinfo', 'metrics', 'ota_info']) {
      ws.send(JSON.stringify({ type }));
      expect((await next()).type).toBe(type === 'ping' ? 'pong' : type);
    }
    for (const command of ['{"type":"set_log_level","data":"DEBUG"}', 'null', 'not JSON']) {
      ws.send(command);
      expect((await next()).type).toBe('error');
    }
  } finally { ws.close(); }
});

test('launcher validates options before installing or opening a server', () => {
  expect(parseArgs([]).port).toBe(5173);
  expect(parseArgs(['--port', '5432']).port).toBe(5432);
  expect(parseArgs(['--help']).help).toBe(true);
  for (const args of [['--port', '0'], ['--port', '65536'], ['--port', 'abc'], ['--host', '0.0.0.0']]) {
    expect(() => parseArgs(args)).toThrow('Usage:');
  }
});

test('stopping the demo closes open sockets and its listening port', async () => {
  const disposable = startDemoServer();
  const ws = new WebSocket(String(disposable.url).replace(/^http/, 'ws') + 'ws');
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  const closed = new Promise((resolve) => { ws.onclose = resolve; });
  await disposable.stop();
  await closed;
  await expect(fetch(disposable.url)).rejects.toThrow();
});

test('launcher serves the real UI and WebSocket through Vite, then cleans up', async () => {
  const reservation = startDemoServer();
  const port = reservation.url.port;
  await reservation.stop();
  const root = fileURLToPath(new URL('../../', import.meta.url));
  const child = Bun.spawn([process.execPath, 'scripts/debug-controller-ui.mjs', '--port', port], {
    cwd: root, stdout: 'pipe', stderr: 'pipe',
  });
  const output = new Response(child.stdout).text();
  const errors = new Response(child.stderr).text();
  const url = `http://127.0.0.1:${port}/`;
  let socket;
  try {
    let ready = false;
    for (let i = 0; i < 100; i++) {
      if (child.exitCode !== null) throw new Error(await errors);
      try {
        const response = await fetch(url);
        if (response.ok && (await response.text()).includes('/src/main.ts')) { ready = true; break; }
      } catch { /* Wait for Vite's listener. */ }
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    expect(ready).toBe(true);
    expect((await (await fetch(url + 'config?type=hkinfo')).json()).success).toBe(true);
    expect((await fetch(url + 'reboot_device')).status).toBe(403);
    socket = new WebSocket(url.replace(/^http/, 'ws') + 'ws');
    const device = await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('Vite WebSocket proxy timed out')), 3000);
      socket.onmessage = event => { clearTimeout(timeout); resolve(JSON.parse(event.data)); };
      socket.onerror = () => { clearTimeout(timeout); reject(new Error('Vite WebSocket proxy failed')); };
    });
    expect(device.deviceName).toBe('Aliro demo stand');
  } finally {
    socket?.close();
    child.kill('SIGTERM');
    await child.exited;
    await output;
    await errors;
  }
  await expect(fetch(url)).rejects.toThrow();
}, 20000);
