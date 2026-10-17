# Relay and Telegram notifications

[Русский](access-integrations.ru.md)

These features share the controller UI and hosted demo. They are implemented in
source and disabled by default on a real controller. The working iPhone firmware
has **not** been replaced; physical relay operation and live Telegram delivery
still need a separate hardware acceptance test.

<a id="relay-setup"></a>
## Relay setup, step by step

The [connection table and relay diagram](wiring.md#relay-gpio4-example) show
**GPIO4 → D on DFRobot DFR0473** and a separate **COM/NO lock-power circuit**.
The reference uses a 5 V module supply, HIGH control and R1 = 10 kΩ to ground,
plus a separate **SparkFun ROB-15324** bench latch (**12 V DC, 600 mA**),
which retracts when powered. GPIO4 still requires explicit
selection; another module must meet the documented compatibility requirements.

The [shopping list with product links and quantities](wiring.md#shopping-list)
is kept beside that diagram, in one place.

1. With power removed, identify DFR0473's `D` (control), `−` (GND), `+` (5 V)
   and contact `COM`/`NO`/`NC`; leave `N/A` unused. Verify input compatibility with **3.3 V**, the active HIGH/LOW
   level and the module/lock supply ratings.
2. Use the diagram only if its power-to-unlock DC lock example matches your
   hardware. Prepare the external driver supply, contact circuit and specified
   suppression. Leave the installed working controller unchanged for now.
3. **After the separately authorized firmware update**, enable panel
   authentication in **System → Security**. Open **Actions → Relay control**.
4. Choose **Relay GPIO = GPIO4** if free. Set **Active output level = HIGH** for
   the module in the example; use LOW only for an actual LOW-trigger module.
5. For ROB-15324, choose **Relay mode = Unlock pulse**, **Pulse duration = 1000 ms**.
   Its specified activation time is 1–10 seconds; do not use continuous output.
   For a different lock, check its permitted pulse and duty cycle. Turn **Relay output = Enabled** and select
   **Save integration settings**. The output starts inactive.
6. First check the relay indication/contacts without a powered lock, then
   connect the correctly rated lock circuit with power removed. On the later
   hardware test, an accepted NFC/Matter unlock activates the output, its timer
   releases it, and locking cancels it. A denied key must not activate it.

Do not use **Follow logical lock state** with ROB-15324. That mode is only
for an actuator intended for continuous operation: output active while logically unlocked, inactive while locked. A
pulse ending does not claim that the door is physically closed or change the
Matter logical state automatically.

A save, boot or disable starts inactive; a saved unlocked state does not energize
the relay at boot. Legacy `gpioAction*` settings do not enable the new driver.
The available DevKitC GPIOs are 4–7, 14–18, 21, 38 and 47, excluding additional
pins reserved by the board configuration. The diagram's GPIO4 must remain free.

### Implementation details

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

<a id="telegram-setup"></a>
## Telegram setup, step by step

**Notifications only:** lock/unlock state changes and optional NFC denials.
There are no incoming commands, webhook requirement, Mini App or server to
run separately. A new bot staying silent after `/start` is expected.

### 1. Create your notification bot

1. In Telegram, open the official [@BotFather](https://t.me/BotFather).
2. Send `/newbot`, choose its display name and an available username ending in
   `bot`. Keep the returned **bot token** private, like a password.
3. Open **your new bot**, press **Start** or send `/start`. This allows it to
   send notifications to your private chat. No answer is required from the bot.

Telegram's [bot tutorial](https://core.telegram.org/bots/tutorial#obtain-your-bot-token)
describes creation. Do not put a real token in screenshots, GitHub, this website's
demo or a browser URL. The real token belongs in the real controller's HTTPS panel.

### 2. Obtain the numeric chat ID locally

From the root of your downloaded/cloned repository, run:

```sh
python3 scripts/telegram-chat-id.py
```

Python 3 is the only requirement. Paste the token into the hidden prompt in
**your own local terminal**. The tool makes one verified HTTPS `getUpdates`
request to Telegram and prints only chat IDs/types. It does not save the token,
print messages/names, send messages, acknowledge updates or change webhook/filter
settings. No token belongs in the command itself or shell history.

Copy the ID beside `private`, for example `424242` (example only).
For a group: add your bot, send `/start@your_bot_username` in that group and run
the tool again. Copy the ID beside `group`/`supergroup`, **including its minus
sign**. Leave group privacy mode enabled; grant only the permission to send
messages. Forum topic-specific delivery is not configured by this panel.

An empty result means: send a fresh `/start` and retry manually. For an existing
webhook/poller conflict, use a new dedicated bot; the helper will not remove an
existing integration. Details: [getUpdates](https://core.telegram.org/bots/api#getupdates)
and [group privacy](https://core.telegram.org/bots/faq#what-messages-will-my-bot-get).

### 3. Prepare the real panel's login and HTTPS after the update

These actions are for the **later hardware setup**, not the public demo:

1. In **System → Security**, set a panel username/password, enable authentication,
   save and sign in again after its reboot.
2. Prepare a PEM server certificate for the controller's DNS name (with its
   chain if needed), its matching PEM private key, and a browser-trusted issuer.
   A local CA is possible when trusted on your device. A name mismatch or an
   untrusted certificate needs correcting before entering a bot token.
3. In your trusted local setup network, select **Enable HTTPS** to reveal the
   certificate controls. Upload **Certificate File** and **Private Key File**
   **before saving the HTTPS setting**. Leave **CA Certificate File (Optional)**
   empty for ordinary browser login: in this server it is a *client-authentication*
   CA, not a place to install the browser's trust anchor.
4. Save. After the reboot, open `https://<the certificate's controller DNS name>/`
   on port 443, sign in, and confirm the browser trusts the connection. Plain
   HTTP/AP recovery mode cannot accept a bot token.

### 4. Enable and test notifications

In **Actions → Telegram integration**, fill in:

| Panel field | Value |
| --- | --- |
| Telegram notifications | Enabled |
| Numeric chat ID | Your private/group ID from step 2 |
| Bot token | The token from BotFather |
| Notify about denied NFC access | Enable only if you want denial alerts |

Select **Save integration settings**, then **Queue test notification** once.
Expect a message like:

```text
Aliro A1B2C3
Telegram test notification
No door-position sensor is configured.
```

The controller ID will differ. `Delivered` increases after Telegram accepts
`sendMessage`; a queue confirmation alone is not delivery. On the later hardware
test, accepted lock/unlock changes generate notifications; a denied NFC tap
notifies only when enabled and does not activate the relay.

In the public demo, these buttons change **synthetic data only**. The bot token
field is disabled; do not use your real credentials there. You can rehearse the
UI now without a controller or a Telegram account.

### If something does not work

| Symptom | Check |
| --- | --- |
| Token field unavailable / authentication required | Use the real panel after its update; enable authentication and actual HTTPS. Demo token entry is intentionally disabled |
| Waiting for system time | Let the controller synchronize its clock and check Internet/DNS access |
| Queued, but Delivered does not increase | Check the Telegram status, bot token, chat ID including minus sign, whether Start was pressed, whether the bot is blocked and group send permissions |
| Dropped increases / rate limit | Check connectivity/credentials and wait for backoff; failed events are not a guaranteed delivery queue |
| Group stopped receiving after an upgrade to supergroup | Obtain its current numeric ID again and save it |

### Token storage and network behavior

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
