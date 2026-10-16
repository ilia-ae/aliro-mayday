// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { expect, test } from 'bun:test';
import { createDemoData } from '../controller-ui-fixtures.mjs';
import { installBrowserDemo } from '../controller-ui-browser-demo.mjs';
const url = path => new URL(path, 'https://demo.example');

test('virtual relay deadlines, denied keys and notification settings remain independent per controller', () => {
  let now = 1000;
  const demo = createDemoData({ clock: () => now });
  const other = createDemoData({ controllerId: 'DEMO-02' });
  const status = () => demo.request(url('/api/integrations')).body.data;
  const post = (path, body) => demo.request(url(path), 'POST', body);
  const tap = scenario => demo.message(JSON.stringify({ type: 'demo_nfc_tap', scenario }));
  const accepted = tap('valid');
  expect(accepted.locked).toBe(false);
  expect(status().relayActive).toBe(true);
  expect(accepted.events.map(event => event.tag)).toContain('DEMO_TELEGRAM');
  now = 1500;
  tap('revoked'); expect(status().relayActive).toBe(true); expect(status().locked).toBe(false);
  now = 2000; demo.tick(); expect(status().relayActive).toBe(false); expect(status().locked).toBe(false);
  expect(other.request(url('/api/integrations')).body.data.locked).toBe(true);
  expect(post('/api/integrations', { relayEnabled: false, telegramEnabled: false }).status).toBe(200);
  const before = status().delivered;
  tap('valid'); tap('valid');
  expect(status().relayActive).toBe(false); expect(status().delivered).toBe(before);
  expect(post('/api/integrations/test', {}).status).toBe(400);
});

test('virtual settings reject reserved pins and secrets; Telegram cannot actuate the lock', () => {
  const demo = createDemoData();
  const post = body => demo.request(url('/api/integrations'), 'POST', body);
  for (const pin of [0,8,9,10,11,12,13,19,20,33,48]) expect(post({ relayPin: pin }).status).toBe(400);
  for (const duration of [0,99,30001,0.5,NaN]) expect(post({ pulseMs: duration }).status).toBe(400);
  expect(post({ relayEnabled: 'true' }).status).toBe(400);
  expect(post({ token: 'do-not-store-this-example' }).status).toBe(400);
  expect(demo.request(url('/api/integrations')).body.data).not.toHaveProperty('token');
  expect(demo.request(url('/api/integrations/simulate'), 'POST', { action: 'telegram-unlock' }).status).toBe(400);
  const before = demo.request(url('/api/integrations')).body.data.locked;
  const testReply = demo.request(url('/api/integrations/test'), 'POST', {});
  expect(testReply.status).toBe(200); expect(testReply.body.data.locked).toBe(before);
});

test('integration simulation is network-isolated and publishes logs through the existing socket', async () => {
  let nativeCalls = 0;
  const target = Object.assign(new EventTarget(), {
    location: new URL('https://demo.example/demo/index.html'),
    fetch() { nativeCalls++; throw new Error('Unexpected real network'); },
  });
  const demo = installBrowserDemo(target);
  const socket = new target.WebSocket('/ws');
  const received = [];
  socket.addEventListener('message', event => received.push(JSON.parse(event.data)));
  try {
    await new Promise(resolve => socket.addEventListener('open', resolve, { once: true }));
    const response = await target.fetch('/api/integrations/simulate', { method: 'POST', body: JSON.stringify({ action: 'unlock' }) });
    expect(response.status).toBe(200);
    expect(received.at(-1).events.some(event => event.tag === 'DEMO_RELAY')).toBe(true);
    expect(received.at(-1).events.some(event => event.tag === 'DEMO_TELEGRAM')).toBe(true);
    expect((await target.fetch('https://api.telegram.org/anything', { method: 'POST', body: '{}' })).status).toBe(403);
    expect(nativeCalls).toBe(0);
  } finally { demo.stop(); }
});
