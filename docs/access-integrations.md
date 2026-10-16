# Relay and Telegram notifications

[Русский](access-integrations.ru.md)

These features share the controller UI and hosted demo. They are implemented in
source and disabled by default on a real controller. The working iPhone firmware
has **not** been replaced; physical relay operation and live Telegram delivery
still need a separate hardware acceptance test.

## Relay setup

Open **Actions → Relay control**. Choose an available GPIO, active HIGH/LOW,
then either an unlock pulse (100–30000 ms) or follow the logical lock state.
Save explicitly to enable the output. A save, boot or disable starts inactive;
a saved unlocked state does not energize the relay at boot. Existing legacy
`gpioAction*` settings do not enable this new driver automatically.

The conservative ESP32-S3 DevKitC allowlist is GPIO4–7, 14–18, 21, 38 and 47.
Pins used by configured NFC, Ethernet and control/status connections are also
reserved at boot. GPIO8/9, 10–13, 19/20, flash/PSRAM, strapping pins and GPIO48
cannot be selected. Check the actual board revision before wiring.

Connect the selected GPIO to the **logic input of an external relay driver**,
with a common signal ground and a compatible 3.3 V input. Use a separate supply
for the relay/lock, a suitable flyback path for an inductive load, and an external
pull resistor that holds the inactive level during reset or power loss. Do not
connect a coil directly to an ESP32 GPIO. See the [wiring guide](wiring.md).

Accepted Matter, physical-button and authenticated NFC state changes drive the
same output. Aliro credential validation and the seven-argument Matter
`SetLockState` / `LockOperation` event path are retained. A rejected state update
or denied NFC key cannot activate the relay. A monotonic timer releases a pulse
independently of Telegram/network latency. Locking cancels a pulse; a newer unlock
restarts its deadline. Ending a pulse **does not** invent a closed-door sensor
reading or silently change the Matter logical state.

The older NFC/NeoPixel and State Triggers controls are retained as legacy
configuration. The Arduino `HardwareManager` remains excluded from this target;
configure the new native driver in **Relay control** instead.

## Telegram: notifications only

Create a dedicated bot with Telegram's BotFather, start a chat with it and obtain
the numeric chat ID. Groups may receive alerts if the bot has permission. This
firmware does not poll incoming messages, install a webhook or accept bot commands.
Telegram cannot open the lock.

Enable panel authentication in **System → Security** and configure HTTPS with a
trusted certificate before entering the token. Then open **Actions → Telegram
integration**, enter the token/chat ID and enable notifications. Lock/unlock
state changes are sent; denied NFC access is a separate opt-in. A test button
**queues a real notification on a controller**. The panel shows successful
`sendMessage` responses and dropped events; queue acceptance is not delivery.
No actual bot was contacted during development of this change.

The token is write-only: a blank field keeps it, and explicit deletion removes
it after notifications are disabled. It is stored in a separate NVS namespace,
not returned by the API and not included in log messages. NVS is not encrypted
by the default development profile; physical flash access can expose secrets.
No existing keys, pairing data or configuration namespace is erased.

The worker uses the official `api.telegram.org` HTTPS endpoint, certificate/host
verification and a reusable HTTP session. Redirects are disabled. Events use a
bounded queue (16), at most one request per second, and backoff on errors/429.
Failed events are counted and dropped rather than replayed after reconnection.
TLS requires a synchronized system clock. Only a controller ID and generic
logical state/denial text are sent; no IP, credential IDs or cryptographic keys.
Telegram is an external service and receives these explicitly enabled events.

## Demo and API

The hosted demo uses synthetic settings and per-reader in-memory state. Use
**Relay control → Simulate unlock/lock/denied access**, or the existing PN532
example key. **Queue test notification** produces a simulated event. `DEMO_RELAY`
and `DEMO_TELEGRAM` appear in the shared Logs section. Reload resets the demo.
It rejects real tokens; no GPIO, controller or Bot API request is made.

Controller endpoints:

- `GET /api/integrations`: redacted settings and status; `Cache-Control: no-store`.
- `POST /api/integrations`: validate/persist settings, then start inactive.
- `POST /api/integrations/test`: queue a notification, without changing lock state.

Writes require panel authentication, JSON and `X-Aliro-Request: integrations-ui`.
Token writes additionally require panel HTTPS. Request bodies are limited to
2048 bytes. `/api/integrations/simulate` exists **only in the demo adapter**.

Host regression tests live in `aliro-esp32/components/access_integrations/tests`
and `scripts/tests/controller-ui-integrations.test.mjs`. They cover GPIO exclusions,
pulse deadlines/cancellation, settings validation, token redaction/deletion,
denied-key behavior and isolated browser notifications. Firmware compilation and
browser tests do not replace testing the physical driver and bot after a later
firmware update.

References: [Telegram Bot API](https://core.telegram.org/bots/api#sendmessage),
[ESP-IDF 5.4.4 HTTP client](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32s3/api-reference/protocols/esp_http_client.html),
[ESP32-S3 GPIO](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32s3/api-reference/peripherals/gpio.html).
