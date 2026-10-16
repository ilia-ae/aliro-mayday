// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { expect, test } from 'bun:test';
import { installBrowserDemo } from '../controller-ui-browser-demo.mjs';

function createTarget() {
  let nativeCalls = 0;
  const target = Object.assign(new EventTarget(), {
    location: new URL('https://demo.example/demo/index.html#/'),
    fetch() { nativeCalls++; throw new Error('Native network must not be called'); },
    WebSocket: class { constructor() { nativeCalls++; throw new Error('Native socket must not be called'); } },
  });
  return { target, count: () => nativeCalls };
}

test('browser demo reads fixtures and blocks every mutation without a native network fallback', async () => {
  const { target, count } = createTarget();
  const demo = installBrowserDemo(target);
  try {
    const info = await (await target.fetch('/config?type=hkinfo')).json();
    expect(info.data.unique_identifier).toBe('Demo reader');
    for (const path of ['/config/save?type=misc', '/reset_hk_pair', '/reset_wifi_cred', '/reboot_device', '/start_config_ap', '/ota/upload', '/ota/littlefs']) {
      for (const method of ['GET', 'POST', 'DELETE']) expect((await target.fetch(path, { method })).status).toBe(403);
    }
    expect((await target.fetch('https://controller.invalid/config?type=hkinfo')).status).toBe(403);
    expect((await target.fetch(new Request('https://demo.example/certificates', { method: 'POST', body: 'demo' }))).status).toBe(403);
    expect((await target.fetch('/config?type=misc', { method: 'POST', body: '{"deviceName":"changed"}' })).status).toBe(403);
    expect((await (await target.fetch('/config?type=misc')).json()).data.deviceName).toBe('Aliro demo stand');
    expect(count()).toBe(0);
  } finally { demo.stop(); }
});

test('browser socket supplies events, responds to reads and rejects real device addresses', async () => {
  const { target, count } = createTarget();
  const demo = installBrowserDemo(target);
  const socket = new target.WebSocket('/ws');
  const messages = [];
  socket.addEventListener('message', event => messages.push(JSON.parse(event.data)));
  try {
    await new Promise(resolve => socket.addEventListener('open', resolve, { once: true }));
    expect(socket.readyState).toBe(target.WebSocket.OPEN);
    expect(messages[0].deviceName).toBe('Aliro demo stand');
    expect(messages[2].current_version).toBe('Demo preview');
    socket.send('{"type":"set_log_level","data":5}');
    await Promise.resolve();
    expect(messages.at(-1).error).toContain('Read-only demo');
    expect(() => new target.WebSocket('wss://controller.invalid/ws')).toThrow('Read-only demo');
    expect(() => new target.WebSocket('/other')).toThrow('Read-only demo');
    expect(count()).toBe(0);
  } finally { demo.stop(); }
  expect(socket.readyState).toBe(target.WebSocket.CLOSED);
});
