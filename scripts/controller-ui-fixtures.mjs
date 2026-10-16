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

export function createDemoData() {
  const started = Date.now();
  const metrics = () => ({
    type: 'metrics', uptime: 2538000 + Date.now() - started, free_heap: 8283136,
    wifi_rssi: -48, nfc_connected: false, mqtt_connected: false,
  });
  const log = () => ({
    type: 'log', ts: Date.now(), uptime: metrics().uptime, level: 'INFO', tag: 'DEMO',
    msg: 'Sample log stream — no hardware connected',
  });
  const ok = data => ({ status: 200, body: { success: true, data: structuredClone(data), message: 'Synthetic demo data' } });
  const fail = (error = disabledMessage, status = 403) => ({ status, body: { success: false, error } });
  return {
    initial: () => [system, metrics(), ota, log()].map(data => structuredClone(data)),
    tick: () => [metrics(), log()],
    request(url, method = 'GET') {
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
      const reads = { ping: { type: 'pong' }, sysinfo: system, metrics: metrics(), ota_info: ota };
      return data && Object.hasOwn(reads, data.type)
        ? structuredClone(reads[data.type]) : { type: 'error', error: disabledMessage };
    },
  };
}
