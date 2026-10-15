# Aliro Mayday website

[Русский](README.ru.md) · [Live website](https://aliro.mayday.software/) ·
[Public source](https://github.com/ilia-ae/aliro-mayday/tree/main/aliro-site)

Angular 21 SPA served by Cloudflare Workers Static Assets. The landing page
describes the prototype's verified scope: the owner's existing iPhone key passed
the updated-firmware bench test on 15 October 2026; host tests cover protocol and
policy changes. Samsung/Google Wallet, on-device revocation with a separate test
key, a physical relay and certification require separate verification. Publishing source does not establish compatibility.

## Demo stand photos

The hero shows the top view by default, with an accessible top/side view selector.
Transparent PNG presentation images and lighter WebP copies are in
`frontend/public/images/`. Photo captions and alt text follow the selected language.
Images are retouched presentation photos, with the IP hidden and external cables
removed; they do not replace the electrical wiring guide.

## Source and deployment

The canonical public source is `aliro-site/` in `ilia-ae/aliro-mayday`.
Cloudflare Workers Build currently watches the separate private deployment mirror
`ilia-ae/aliro-mayday-software`. Publishing a monorepo change alone does not deploy
the website. Synchronize only website source files to that mirror and push its
`main` after verification. Avoid copying local secrets, firmware images, NVS,
controller backups, build output or dependency directories.

The original workspace's subtree helpers depend on its own private Git history;
they are not a deployment mechanism for a clone of the public snapshot.

## Build and browser checks

The checked runtime is **Node 24.19.0**. From this website directory:

```sh
cd frontend
npm ci
npm run build
npx playwright install chromium --only-shell
npx playwright test
```

The complete browser suite passed **61 tests**, with **5 explicit skips**.
Source links and language switching were also checked after final copy edits.
Tests cover the website, not a connected lock or wallet.

`frontend/dist/aliro-mayday-software/browser` is generated locally. The root
package's scripts orchestrate the Angular build and Wrangler deployment.
Cloudflare's connected build uses `npm install && npm run build`, followed by
`npx wrangler deploy`. Configure hosting credentials locally or in Cloudflare.

## Layout and language

- `frontend/src/app/pages/landing/landing.ts`: landing page and source links.
- `frontend/public/i18n/en.json` and `ru.json`: matching English/Russian content.
- `frontend/e2e/`: browser tests, including public-source and status assertions.
- `worker/src/index.ts` and `wrangler.toml`: asset serving and hosting settings.

English is the primary repository documentation language; Russian uses `.ru.md`.
Website language detection retains the user's local preference, then browser
language, with Russian fallback. Both versions link to their matching public
documentation and wiring guide. Theme selection, QR sharing and offline caching remain supported.

## Safari and service-worker configuration

Keep `html_handling = "none"` and
`not_found_handling = "single-page-application"` in `wrangler.toml`. Redirecting
`/index.html` to `/` can leave Safari replaying a cached redirect during Angular
service-worker navigation.

Translation request versions are generated from the EN/RU file contents before
builds and development starts; do not edit or commit `frontend/src/content-version.ts`.
The service worker prefetches each complete application/translation version and
reloads an open page when `VERSION_READY` arrives. It checks on focus, network
reconnection and once per minute while visible. Navigation prefers the network,
with the cached application available offline. HTML, translation and worker
control files use revalidation headers. A tab still running code from before this
fix may need a normal reload to pick up the update handler.

The browser suite includes a real service-worker rollout: warm an older page,
change the served manifest/translations and confirm the open page updates without
clearing browser data, while preserving its language and URL. `ALIRO_TEST_PORT`
selects an isolated test port.

This SPA has no server-side rendering. Social crawlers that do not execute
JavaScript see the static Russian metadata; runtime metadata follows the selected
language. Component licenses retain their scope; public availability does not
make all project code OSI Open Source.

Project-original authorship: **Mayday / Ilia Arestov**. [License v1.1](LICENSE) clarifies permitted modifications and redistribution; [contribution terms](../CONTRIBUTING.md) cover new submissions. See [authorship and third-party scope](../AUTHORS.md).
