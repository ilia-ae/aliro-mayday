# Controller web UI: local launch and debugging

[Русский](controller-ui-debug.ru.md) · [Documentation](README.md) · [Hardware photos](demo-stand.md)

Run the actual Svelte administration UI from `aliro-esp32/data` on your computer,
with synthetic HTTP/WebSocket responses. No controller, ESP-IDF, USB connection,
Wi-Fi credentials or saved keys are required. This previews the interface;
it does not emulate NFC authentication, Matter, a wallet or a physical lock.

## Live demo on the website

[Open the panel](https://aliro.mayday.software/demo/index.html?lang=en#/).
Explore all six sections and nested settings, edit fields without saving, and
view synthetic logs. The panel itself is currently in English; the demo banner
and return link follow the selected EN/RU language.

This is a separate build of the actual UI. HTTP responses and device events are
created in the browser: there are no API requests, network WebSockets or controller
connections. A `connect-src 'none'` policy additionally prevents network connections.
Saves, resets, reboots, certificate changes and OTA uploads return a demo error.
Reloading the page restores the sample values.

[Demo bundle license notices](https://aliro.mayday.software/demo/NOTICES.txt).
Project commercial terms and component licenses remain unchanged.

The [homepage](https://aliro.mayday.software/?lang=en#controller-ui) embeds this
same isolated demo below the stand photo. The screenshots remain in a collapsible
gallery. The nearby tap/door animation is a schematic illustration, independent
of both the panel and physical hardware.

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

## Screenshot gallery

Six actual browser screenshots, captured on **16 October 2026** using the local
demo workflow. Each shows the initial view of a main navigation section.
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
