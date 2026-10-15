# Aliro Mayday website

[Русский](README.ru.md) · [Live website](https://aliro.mayday.software/) ·
[Public source](https://github.com/ilia-ae/aliro-mayday/tree/main/aliro-site)

Angular 21 SPA served by Cloudflare Workers Static Assets. The landing page
describes the prototype's verified scope: the owner's existing iPhone key worked
on the bench; host tests cover protocol and policy changes; updated-firmware
iPhone acceptance, Samsung/Google Wallet, a physical relay and certification
require separate verification. Publishing source does not establish compatibility.

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

The complete browser suite passed **59 tests**, with **5 explicit skips**.
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
documentation and wiring guide. Theme selection, QR sharing and service-worker
behavior remain supported.

## Safari and service-worker configuration

Keep `html_handling = "none"` and
`not_found_handling = "single-page-application"` in `wrangler.toml`. Redirecting
`/index.html` to `/` can leave Safari replaying a cached redirect during Angular
service-worker navigation.

This SPA has no server-side rendering. Social crawlers that do not execute
JavaScript see the static Russian metadata; runtime metadata follows the selected
language. Component licenses retain their scope; public availability does not
make all project code OSI Open Source.
