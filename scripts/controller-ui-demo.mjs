// Copyright (c) 2026 Mayday / Ilia Arestov. See ../LICENSE.
// Synthetic, read-only API for the controller UI. Never forwards to hardware.
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
const disabledMessage = 'Read-only demo: no controller connected; changes and device commands are disabled.';
const success = (data) => Response.json({ success: true, data, message: 'Synthetic demo data' });
const failure = (error, status) => Response.json({ success: false, error }, { status });

export function startDemoServer({ port = 0 } = {}) {
  const started = Date.now();
  const timers = new Set();
  const metrics = () => ({
    type: 'metrics', uptime: 2538000 + Date.now() - started, free_heap: 8283136,
    wifi_rssi: -48, nfc_connected: false, mqtt_connected: false,
  });
  const log = () => ({
    type: 'log', ts: Date.now(), uptime: metrics().uptime, level: 'INFO', tag: 'DEMO',
    msg: 'Sample log stream — no hardware connected',
  });
  const server = Bun.serve({
    hostname: '127.0.0.1', port,
    fetch(request, server) {
      const url = new URL(request.url);
      if (request.method !== 'GET') return failure(disabledMessage, 403);
      if (url.pathname === '/ws') {
        if (server.upgrade(request, { data: { timer: undefined } })) return;
        return failure('WebSocket upgrade required', 426);
      }
      // A read allowlist also rejects device commands that use GET.
      if (url.pathname === '/config') {
        const configs = { hkinfo: reader, mqtt, actions, misc };
        const type = url.searchParams.get('type');
        if (Object.hasOwn(configs, type)) return success(configs[type]);
        return failure('Unknown demo configuration', 404);
      }
      if (url.pathname === '/eth_get_config') {
        return success({ supportedChips: [], boardPresets: [], ethEnabled: false, numSpiBuses: 2 });
      }
      if (url.pathname === '/nfc_get_presets') {
        return success({ presets: [{ name: 'Demo SPI pins (example only)', gpioPins: misc.nfcGpioPins }] });
      }
      if (url.pathname === '/certificates') return success({});
      if (url.pathname === '/captive_portal_config') {
        return success({ ...misc, wifiSsid: 'Demo network', wifiPassword: '' });
      }
      if (url.pathname === '/wifi_scan') {
        return success([{ ssid: 'Demo network', rssi: -48, channel: 1, auth: 'WPA2_PSK' }]);
      }
      return failure(disabledMessage, 403);
    },
    websocket: {
      maxPayloadLength: 4096,
      open(ws) {
        for (const data of [system, metrics(), ota, log()]) ws.send(JSON.stringify(data));
        ws.data.timer = setInterval(() => {
          ws.send(JSON.stringify(metrics()));
          ws.send(JSON.stringify(log()));
        }, 5000);
        timers.add(ws.data.timer);
      },
      message(ws, message) {
        let data;
        try { data = JSON.parse(String(message)); }
        catch { ws.send(JSON.stringify({ type: 'error', error: 'Invalid JSON' })); return; }
        const reads = { ping: { type: 'pong' }, sysinfo: system, metrics: metrics(), ota_info: ota };
        const reply = data && Object.hasOwn(reads, data.type)
          ? reads[data.type] : { type: 'error', error: disabledMessage };
        ws.send(JSON.stringify(reply));
      },
      close(ws) { clearInterval(ws.data.timer); timers.delete(ws.data.timer); },
    },
  });
  let stopped;
  return {
    url: server.url,
    stop() {
      if (!stopped) {
        for (const timer of timers) clearInterval(timer);
        timers.clear();
        stopped = server.stop(true);
      }
      return stopped;
    },
  };
}
