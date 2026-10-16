// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { expect, test } from 'bun:test';
import { installBrowserDemo } from '../controller-ui-browser-demo.mjs';
import { createDemoData } from '../controller-ui-fixtures.mjs';

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

test('NFC simulation toggles only for valid example keys and keeps denied state unchanged', () => {
  const demo = createDemoData();
  const tap = scenario => demo.message(JSON.stringify({ type: 'demo_nfc_tap', scenario }));
  const allowed = tap('valid');
  expect(allowed.locked).toBe(false);
  expect(allowed.events.every(event => event.msg.startsWith('[SIMULATED DEMO-01'))).toBe(true);
  expect(tap('revoked').locked).toBe(false);
  expect(tap('unknown').locked).toBe(false);
  expect(tap('valid').locked).toBe(true);
  expect(demo.message('{"type":"demo_nfc_tap","scenario":"arbitrary"}').type).toBe('error');
  expect(demo.message('{"type":"demo_nfc_reset"}')).toMatchObject({ locked: true, tap_count: 0, result: 'idle' });
});

test('separate simulated readers retain independent identities and state', () => {
  const first = createDemoData({ controllerId: 'DEMO-01' });
  const second = createDemoData({ controllerId: 'DEMO-02' });
  first.message('{"type":"demo_nfc_tap","scenario":"valid"}');
  expect(first.message('{"type":"demo_nfc_state"}').locked).toBe(false);
  expect(second.message('{"type":"demo_nfc_state"}')).toMatchObject({ locked: true, tap_count: 0, controller_id: 'DEMO-02' });
  expect(createDemoData({ controllerId: 'arbitrary.example' }).controllerId).toBe('DEMO-01');
});

test('parent simulation bridge rejects other origins, windows and reader IDs', async () => {
  const { target, count } = createTarget();
  const responses = [];
  const parent = { postMessage: message => responses.push(message) };
  target.parent = parent;
  const installed = installBrowserDemo(target);
  const send = (data, origin = target.location.origin, source = parent) => {
    const event = new Event('message');
    Object.assign(event, { data, origin, source });
    target.dispatchEvent(event);
  };
  const command = { type: 'aliro-demo-command', controller_id: 'DEMO-01', command: 'tap', scenario: 'valid' };
  try {
    send(command, 'https://other.example');
    send(command, target.location.origin, {});
    send({ ...command, controller_id: 'DEMO-02' });
    send({ ...command, command: 'reboot' });
    expect(responses).toHaveLength(0);
    send(command);
    expect(responses.at(-1)).toMatchObject({ type: 'aliro-demo-state', locked: false, controller_id: 'DEMO-01' });
    send({ ...command, command: 'reset' });
    expect(responses.at(-1)).toMatchObject({ locked: true, tap_count: 0 });
    expect(count()).toBe(0);
  } finally { installed.stop(); }
  const before = responses.length;
  send(command);
  expect(responses).toHaveLength(before);
});
