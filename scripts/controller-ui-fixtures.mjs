// Copyright (c) 2026 Mayday / Ilia Arestov. See ../LICENSE.
// Shared synthetic data for the local API and browser-only demo.
const system = {
  type: 'sysinfo', deviceName: 'Aliro demo stand', version: 'Demo preview',
  eth_enabled: false, wifi_ssid: 'Demo network', log_level: 3, chip_model: 9,
};
const reader = {
  group_identifier: 'Demo group', unique_identifier: 'Demo reader',
  issuers: [{ issuerId: 'Demo issuer', endpoints: [{ endpointId: 'Demo key' }] }],
};
const misc = {
  deviceName: system.deviceName, otaPasswd: '', hk_key_color: 0, setupCode: '',
  lockAlwaysUnlock: false, lockAlwaysLock: false, hkAuthPrecomputeEnabled: false,
  controlPin: -1, hsStatusPin: -1, webAuthEnabled: false, webUsername: '',
  webPassword: '', webHttpsEnabled: false, nfcPinsPreset: 0,
  nfcGpioPins: [10, 12, 13, 11], btrLowStatusThreshold: 20, proxBatEnabled: false,
  ethernetEnabled: false, ethActivePreset: 0, ethPhyType: 0, ethSpiBus: 1,
  ethRmiiConfig: [0, -1, -1, -1, -1], ethSpiConfig: [20, -1, -1, -1, -1, -1, -1],
};
const mqtt = {
  mqttBroker: 'mqtt.example.invalid', mqttPort: 1883, mqttUsername: '', mqttPassword: '',
  mqttClientId: 'aliro-demo', lwtTopic: 'demo/status', hkTopic: 'demo/key',
  lockStateTopic: 'demo/lock', lockStateCmd: 'demo/lock/set', lockCStateCmd: '',
  lockTStateCmd: '', btrLvlCmdTopic: '', hkAltActionTopic: '', lockCustomStateTopic: '',
  lockCustomStateCmd: '', lockEnableCustomState: false, hassMqttDiscoveryEnabled: false,
  nfcTagNoPublish: true, useSSL: false, allowInsecure: false,
  customLockStates: {}, customLockActions: {},
};
const actions = {
  nfcNeopixelPin: -1, neoPixelType: 0,
  neopixelSuccessColor: [[0, 0], [1, 255], [2, 0]],
  neopixelFailureColor: [[0, 255], [1, 0], [2, 0]],
  neopixelTagEventColor: [[0, 0], [1, 0], [2, 255]],
  neopixelSuccessTime: 500, neopixelFailTime: 500, neopixelTagEventTime: 500,
  nfcSuccessPin: -1, nfcSuccessTime: 500, nfcSuccessHL: true,
  nfcFailPin: -1, nfcFailTime: 500, nfcFailHL: true,
  tagEventPin: -1, tagEventTimeout: 500, tagEventHL: true,
  gpioActionPin: -1, gpioActionLockState: false, gpioActionUnlockState: true,
  gpioActionMomentaryEnabled: 0, hkGpioControlledState: false,
  gpioActionMomentaryTimeout: 1000, hkDumbSwitchMode: false,
  hkAltActionInitPin: -1, hkAltActionInitLedPin: -1, hkAltActionInitTimeout: 1000,
  hkAltActionPin: -1, hkAltActionTimeout: 1000, hkAltActionGpioState: 0,
};
const ota = {
  type: 'ota_info', in_progress: false, bytes_written: 0, upload_type: '',
  current_version: system.version, running_partition: 'ota_0',
  next_update_partition: 'ota_1', progress_percent: 0, total_bytes: 0,
};
export const disabledMessage = 'Read-only demo: no controller connected; changes and device commands are disabled.';

export function createDemoData({ controllerId = 'DEMO-01', clock = () => Date.now() } = {}) {
  if (!/^DEMO-\d{2}$/.test(controllerId)) controllerId = 'DEMO-01';
  const demoSystem = { ...system, controller_id: controllerId };
  const started = clock();
  let sampleAt = started;
  let locked = true;
  let tapCount = 0;
  let lastResult = 'idle';
  let relayUntil = 0;
  let integrationEvents = [];
  const integrations = {
    relayEnabled: true, relayPin: 4, activeHigh: true, pulse: true, pulseMs: 1000,
    telegramEnabled: true, notifyDenied: true,
    chatId: '424242', tokenConfigured: true,
    relayActive: false, locked: true, telegramStatus: 'ready', delivered: 0, dropped: 0,
    authEnabled: true, httpsEnabled: true, simulated: true,
  };
  function integrationLog(tag, msg) {
    integrationEvents.push({ type: 'log', ts: clock(), uptime: clock() - started,
      level: 'INFO', tag, msg: `[SIMULATED ${controllerId}] ${msg}` });
  }
  function expireRelay() {
    if (relayUntil && clock() >= relayUntil) {
      relayUntil = 0; integrations.relayActive = false;
      integrationLog('DEMO_RELAY', 'Pulse completed; output inactive. Logical lock state unchanged.');
    }
  }
  function notify(text) {
    if (!integrations.telegramEnabled) return;
    integrations.delivered++;
    integrationLog('DEMO_TELEGRAM', `${text}. Browser simulation; no Bot API request.`);
  }
  function applyState(nextLocked, source) {
    const changed = locked !== nextLocked;
    locked = nextLocked; integrations.locked = locked;
    integrations.relayActive = integrations.relayEnabled && !locked;
    relayUntil = integrations.relayActive && integrations.pulse ? clock() + integrations.pulseMs : 0;
    integrationLog('DEMO_RELAY', `Output ${integrations.relayActive ? 'ACTIVE' : 'INACTIVE'}; source=${source}. No GPIO command.`);
    if (changed) notify(`Logical state ${locked ? 'LOCKED' : 'UNLOCKED'}`);
  }
  function saveIntegrations(body) {
    if (!body || typeof body !== 'object' || Array.isArray(body)) return 'Invalid integration settings.';
    if (body.token || body.clearToken) return 'Do not enter real bot credentials in the demo.';
    const allowed = ['relayEnabled', 'relayPin', 'activeHigh', 'pulse', 'pulseMs', 'telegramEnabled', 'notifyDenied', 'chatId'];
    const candidate = { ...integrations };
    for (const key of allowed) if (Object.hasOwn(body, key)) {
      if (typeof body[key] !== typeof candidate[key]) return 'Invalid integration settings.';
      candidate[key] = body[key];
    }
    if (![-1,4,5,6,7,14,15,16,17,18,21,38,47].includes(candidate.relayPin) || candidate.relayEnabled && candidate.relayPin === -1) return 'Choose an available relay GPIO.';
    if (!Number.isInteger(candidate.pulseMs) || candidate.pulseMs < 100 || candidate.pulseMs > 30000) return 'Pulse duration must be 100–30000 ms.';
    if (!/^-?[1-9][0-9]{0,14}$/.test(candidate.chatId)) return 'Use a numeric Telegram chat ID.';
    Object.assign(integrations, candidate, { relayActive: false, telegramStatus: candidate.telegramEnabled ? 'ready' : 'disabled' });
    relayUntil = 0;
    integrationLog('DEMO_RELAY', 'Settings saved; output starts inactive.');
    return null;
  }
  const nfcState = (events = []) => ({
    type: 'demo_nfc', controller_id: controllerId, simulated: true,
    locked, result: lastResult, tap_count: tapCount, events,
  });
  const metrics = () => ({
    type: 'metrics', uptime: 2538000 + clock() - started, free_heap: 8283136,
    wifi_rssi: -48, nfc_connected: false, mqtt_connected: false,
  });
  const log = () => ({
    type: 'log', ts: Date.now(), uptime: metrics().uptime, level: 'INFO', tag: 'DEMO',
    msg: 'Sample log stream — no hardware connected',
  });
  const ok = data => ({ status: 200, body: { success: true, data: structuredClone(data), message: 'Synthetic demo data' } });
  const fail = (error = disabledMessage, status = 403) => ({ status, body: { success: false, error } });
  return {
    controllerId,
    initial: () => [demoSystem, metrics(), ota, log(), nfcState()].map(data => structuredClone(data)),
    tick() {
      expireRelay(); const events = integrationEvents; integrationEvents = [];
      const output = [metrics()];
      if (clock() - sampleAt >= 5000) { sampleAt = clock(); output.push(log()); }
      if (events.length) output.push(nfcState(events));
      return output;
    },
    drain() { const events = integrationEvents; integrationEvents = []; return nfcState(events); },
    request(url, method = 'GET', body) {
      expireRelay();
      if (url.pathname === '/api/integrations' && method === 'GET') return ok(integrations);
      if (method === 'POST' && ['/api/integrations', '/api/integrations/test', '/api/integrations/simulate'].includes(url.pathname)) {
        if (url.pathname === '/api/integrations') {
          const error = saveIntegrations(body);
          if (error) return fail(error, 400);
        } else if (url.pathname.endsWith('/test')) {
          if (!integrations.telegramEnabled) return fail('Configure Telegram first.', 400);
          notify('Test notification');
        } else {
          const action = body?.action;
          if (!['lock', 'unlock', 'denied'].includes(action)) return fail('Invalid integration request.', 400);
          if (action === 'denied') {
            integrationLog('DEMO_ALIRO', 'Access denied; relay unchanged.');
            if (integrations.notifyDenied) notify('NFC access denied');
          } else applyState(action === 'lock', 'Matter');
        }
        return ok(integrations);
      }
      if (method !== 'GET') return fail();
      if (url.pathname === '/config') {
        const configs = { hkinfo: reader, mqtt, actions, misc };
        const type = url.searchParams.get('type');
        return Object.hasOwn(configs, type) ? ok(configs[type]) : fail('Unknown demo configuration', 404);
      }
      if (url.pathname === '/eth_get_config') return ok({ supportedChips: [], boardPresets: [], ethEnabled: false, numSpiBuses: 2 });
      if (url.pathname === '/nfc_get_presets') return ok({ presets: [{ name: 'Demo SPI pins (example only)', gpioPins: misc.nfcGpioPins }] });
      if (url.pathname === '/certificates') return ok({});
      if (url.pathname === '/captive_portal_config') return ok({ ...misc, wifiSsid: 'Demo network', wifiPassword: '' });
      if (url.pathname === '/wifi_scan') return ok([{ ssid: 'Demo network', rssi: -48, channel: 1, auth: 'WPA2_PSK' }]);
      return fail();
    },
    message(message) {
      let data;
      try { data = JSON.parse(String(message)); }
      catch { return { type: 'error', error: 'Invalid JSON' }; }
      if (data?.type === 'demo_nfc_reset') {
        locked = true; integrations.locked = true; integrations.relayActive = false; relayUntil = 0; tapCount = 0; lastResult = 'idle';
        return nfcState();
      }
      if (data?.type === 'demo_nfc_tap') {
        if (!['valid', 'revoked', 'unknown'].includes(data.scenario)) return { type: 'error', error: 'Unknown demo key scenario' };
        tapCount++;
        lastResult = data.scenario === 'valid' ? 'granted' : data.scenario;
        const events = [];
        const add = (tag, msg, level = 'INFO') => events.push({
          type: 'log', ts: Date.now(), uptime: metrics().uptime, level, tag,
          msg: `[SIMULATED ${controllerId} #${tapCount}] ${msg}`,
        });
        add('DEMO_PN532', 'Phone detected at the virtual PN532 reader; no NFC or SPI traffic.');
        if (lastResult === 'granted') {
          add('DEMO_ALIRO', 'Access granted for the valid example key. No cryptographic verification is performed.');
          applyState(!locked, 'Aliro');
          add('DEMO_MATTER', `Logical lock state: ${locked ? 'LOCKED' : 'UNLOCKED'}. No Matter or GPIO command sent.`);
        } else {
          add('DEMO_ALIRO', `Access denied: ${lastResult} example key.`, 'WARN');
          if (integrations.notifyDenied) notify('NFC access denied');
          add('DEMO_MATTER', `Lock state remains ${locked ? 'LOCKED' : 'UNLOCKED'}. No device command sent.`);
        }
        events.push(...integrationEvents); integrationEvents = [];
        return nfcState(events);
      }
      const reads = { ping: { type: 'pong' }, sysinfo: demoSystem, metrics: metrics(), ota_info: ota, demo_nfc_state: nfcState() };
      return data && Object.hasOwn(reads, data.type)
        ? structuredClone(reads[data.type]) : { type: 'error', error: disabledMessage };
    },
  };
}
