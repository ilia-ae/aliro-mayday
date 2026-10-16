# Controller web UI: local launch and debugging

[Русский](controller-ui-debug.ru.md) · [Documentation](README.md) · [Hardware photos](demo-stand.md)

Run the actual Svelte administration UI from `aliro-esp32/data` on your computer,
with synthetic HTTP/WebSocket responses. No controller, ESP-IDF, USB connection,
Wi-Fi credentials or saved keys are required. This previews the interface;
it does not emulate PN532 silicon, NFC radio/APDU traffic, Aliro cryptography,
a wallet or a physical lock. Example access outcomes are explicitly simulated.

## Live demo on the website

[Open the panel](https://aliro.mayday.software/demo/index.html?lang=en#/).
Explore all six sections and nested settings, edit fields without saving, and
view synthetic logs. The complete panel supports English and Russian with a
visible EN/RU switch. English is the default; a valid `?lang=en` / `?lang=ru`
parameter overrides the saved panel language.

This is a separate build of the actual UI. HTTP responses and device events are
created in the browser: there are no API requests, network WebSockets or controller
connections. A `connect-src 'none'` policy additionally prevents network connections.
Configuration saves, device resets, reboots, certificate changes and OTA uploads return a demo error.
Reloading the page restores the sample values.

[Demo bundle license notices](https://aliro.mayday.software/demo/NOTICES.txt).
Project commercial terms and component licenses remain unchanged.

The [homepage](https://aliro.mayday.software/?lang=en#controller-ui) embeds this
same isolated demo below the stand photo. The screenshots remain in a collapsible
gallery. The tap/door visualisation and panel share the same browser-only simulation
and event log; both remain independent of physical hardware.

## Language and appearance

EN/RU and the light, dark and system theme controls are shared by the controller,
local debug preview and hosted demo, including the initial network setup page.
Selections apply in the browser without reloading forms or sending device commands.
The ordinary panel saves them under `aliro-ui-language` and `aliro-ui-theme` in
browser storage; blocked storage does not prevent switching. Legacy dracula / autumn
choices remain supported. The system theme follows operating-system changes.

The embedded demo and homepage synchronize language and theme in both directions.
Messages must come from the same-origin parent/iframe window and use a supported
language or theme. Changing preferences keeps the selected route, edited fields,
files and simulated reader state. Standalone panels keep their own preferences.

English text is the source key; Russian translations are in
`aliro-esp32/data/src/lib/i18n/ru.json`. Raw firmware logs, IDs, SSIDs, topic names,
filenames and unknown backend diagnostics retain their original values. Known UI
errors and client-generated OTA messages are localized at display time. Preference
changes do not alter API fields, credentials, pairing or firmware configuration.
The standard itself and upstream documents are outside this interface translation.

## PN532 simulation, event trace and controller identity

On the homepage, choose a valid, revoked or unknown example key and tap the
reader. The panel offers the same controls. A valid example toggles logical
lock state; denied examples preserve the previous state. The visible trace and
Logs section contain the same explicitly labelled synthetic events. This does
not run real NFC, signature verification, Matter, GPIO or Telegram requests.
Resetting the simulation only resets temporary demo state.

The panel header shows the device name and identifier on desktop and mobile.
Long IDs are abbreviated in the mobile header; the Dashboard shows the full ID.
The ordinary UI reads its existing Reader ID from `/config?type=hkinfo`; this is
the Aliro reader identifier, not an ESP32 factory serial. The normal dashboard
also shows NFC diagnostics and recent events from its existing WebSocket log.
These common UI changes take effect when its assets are separately built and
installed; editing or publishing source does not flash a connected controller.

Hosted examples use synthetic IDs: `DEMO-01` by default, or `DEMO-02` with
`/demo/index.html?controller=DEMO-02&lang=en#/`. Each frame has independent state.
The homepage bridge checks message origin, source window, controller ID and an
allow-list of simulation commands. The CSP still forbids network connections.
Simulation controls are excluded from the ordinary controller UI bundle. Local
`bun run debug` also supports the synthetic controls through its loopback mock.

The technical diagram describes the prototype's ESP32-S3 → PN532/SPI → Aliro
checks → Matter DoorLock path. It is not a captured RF trace or evidence of a
hardware acceptance test. Telegram integration is not implemented in the
published Aliro firmware; no bot is connected by this demo.

## Quick start

Install [Bun](https://bun.sh/docs/installation) **1.3.13** and
[Node.js](https://nodejs.org/en/download) **22.12+** (24 LTS recommended).
From the repository root:

```sh
cd aliro-esp32/data
bun run debug
```

Open **http://127.0.0.1:5173/**. The first run installs dependencies using the
committed `bun.lock`; an internet connection is needed if packages are not cached.
The visible banner says **“Demo preview — sample data”**. “Online” refers to the
local demo server. Device, reader, issuer, network, uptime and logs are examples.
The existing **HomeKey** label is part of the current interface.

The launcher binds both servers to `127.0.0.1`. It starts a private demo API on an
available port and Vite on port 5173. It never discovers a device or forwards to
a controller. Configuration saves, certificates, reboot/reset commands and OTA
uploads return a read-only error; edits are not persisted. Sample GPIO values
are not wiring instructions: use the [wiring guide](wiring.md).

## Debugging and stopping

- Edit `aliro-esp32/data/src/`: Vite updates the page automatically.
- Use browser developer tools to set breakpoints and inspect the Console,
  Network requests and `/ws` messages. The Logs page receives synthetic entries.
- API fixtures and read-only guards are in
  [controller-ui-fixtures.mjs](../scripts/controller-ui-fixtures.mjs); the local server is
  [controller-ui-demo.mjs](../scripts/controller-ui-demo.mjs).
  Restart the command after changing this server file.
- **Ctrl+C** stops Vite and the demo API. No controller state is changed.
- If port 5173 is occupied, use `bun run debug --port 5174`.
  An occupied port produces an error instead of silently selecting another URL.
- For usage help: `bun run debug --help`.
- Ensure the required `node` and `bun` versions are on `PATH`. Vite runs under
  Node.js; the launcher and mock API run under Bun.

The same launcher works from the repository root:

```sh
bun scripts/debug-controller-ui.mjs --port 5173
```

## Checks and ordinary firmware assets

From `aliro-esp32/data`:

```sh
bun run debug:test
bun run build
```

The tests check browser isolation, API reads, rejection of writes and device commands, WebSocket
reads/ping, the actual Vite proxy and server shutdown. CI runs these checks and
the ordinary UI build. `bun run build` produces the usual compressed UI assets
under `aliro-esp32/data/dist/`; demo mode is off by default and no mock server is
bundled. `bun run dev` keeps the existing development proxy at localhost:8000.
Neither build command flashes a controller. Installing firmware is a separate
operation described in the [firmware guide](../aliro-esp32/README.md).

## Responsive layout checks

The shared panel and the embedded homepage demo are checked in **English and
Russian**, in **light and dark** themes. The layout matrix covers 320, 360, 390,
430, 768 and 1280 CSS pixels in Chromium, plus iPhone-size views in WebKit.
It includes all six sections, initial setup, expanded settings and log entries,
certificate inputs and long filenames. The checks measure text overlap and
horizontal overflow, not just whether a page opens.

From the repository root:

```sh
cd aliro-site/frontend
npx playwright install chromium --only-shell
npx playwright install --with-deps webkit
npx playwright test controller-responsive.spec.ts
```

These checks use synthetic browser data. They do not contact or flash a controller.

## Screenshot gallery

Six actual browser screenshots, captured on **16 October 2026** using the local
demo workflow. They show the English interface in the Mayday dark theme; the live panel also
supports Russian and light/system themes.
[Switch screenshots on the website](https://aliro.mayday.software/?lang=en#controller-ui),
or expand a section below. Click an image to open it at full size.

<details open>
<summary>Overview · Dashboard</summary>

[![Overview — synthetic demo data](../aliro-site/frontend/public/images/controller-dashboard-demo.jpg)](../aliro-site/frontend/public/images/controller-dashboard-demo.jpg)

</details>

<details>
<summary>MQTT · MQTT</summary>

[![MQTT — synthetic demo data](../aliro-site/frontend/public/images/controller-mqtt-demo.jpg)](../aliro-site/frontend/public/images/controller-mqtt-demo.jpg)

</details>

<details>
<summary>Actions · Actions</summary>

[![Actions — synthetic demo data](../aliro-site/frontend/public/images/controller-actions-demo.jpg)](../aliro-site/frontend/public/images/controller-actions-demo.jpg)

</details>

<details>
<summary>System · System</summary>

[![System — synthetic demo data](../aliro-site/frontend/public/images/controller-system-demo.jpg)](../aliro-site/frontend/public/images/controller-system-demo.jpg)

</details>

<details>
<summary>OTA update · OTA Update</summary>

[![OTA update — synthetic demo data](../aliro-site/frontend/public/images/controller-ota-demo.jpg)](../aliro-site/frontend/public/images/controller-ota-demo.jpg)

</details>

<details>
<summary>Logs · Logs</summary>

[![Logs — synthetic demo data](../aliro-site/frontend/public/images/controller-logs-demo.jpg)](../aliro-site/frontend/public/images/controller-logs-demo.jpg)

</details>

The images contain no real controller IP, pairing code, credentials or key
material. The banner identifies synthetic data; “Online” refers to the local
demo API. They demonstrate the web UI, not a live device acceptance test.
No firmware file was uploaded and no physical controller was accessed.

The upstream UI retains its [MIT license](../aliro-esp32/data/LICENSE).
Project-original launcher, mock server and tests follow the repository
[license scope](licensing.md). Existing commercial terms are unchanged.
