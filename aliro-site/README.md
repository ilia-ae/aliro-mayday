# aliro-site

Project site at `aliro.mayday.software`. Cloudflare Worker + Angular 21
SPA.

## Dual-location architecture (important)

- You edit files **here**, inside the monorepo. `aliro-site/` is a
  `git subtree` pointing at the private repo
  [`razqqm/aliro-mayday-software`](https://github.com/razqqm/aliro-mayday-software).
- **Cloudflare Workers Build clones the private repo**, not the monorepo.
  The monorepo's `homekey-esp32/` subtree used to carry stray submodule
  gitlinks that broke CF's clone phase; the private mirror is flat Angular
  content only.
- `just site-push` mirrors your local changes to the private repo
  → triggers a CF build + deploy.
- `just site-pull` goes the other way (rarely needed — only if someone
  edits via GitHub UI).
- `just site-check` shows whether monorepo and mirror are in sync.

```
edit aliro-site/ locally
        │
        │ just site-push
        ▼
github.com/razqqm/aliro-mayday-software  ─────▶  Cloudflare Workers
                                                 Build clones + deploys
                                                 to aliro.mayday.software
```

## Layout

```
aliro-site/
├── wrangler.toml              # Worker config + [assets] binding
├── package.json               # root orchestrator (build / deploy / dev)
├── worker/src/index.ts        # thin pass-through to Static Assets
└── frontend/                  # Angular app (builds to frontend/dist/aliro-mayday-software/browser)
    ├── angular.json
    ├── package.json           # Angular 21 + @ngx-translate; no PrimeNG / Tailwind
    ├── ngsw-config.json       # Service Worker manifest (prod only)
    ├── tsconfig.json, tsconfig.app.json
    ├── public/
    │   ├── robots.txt
    │   └── i18n/
    │       ├── ru.json        # RU translations (default language)
    │       └── en.json        # EN translations
    └── src/
        ├── index.html         # <app-root> shell + pre-paint theme/lang script
        ├── main.ts
        ├── app.component.ts   # calls LanguageService.init() on bootstrap
        ├── app.config.ts      # router, http, zoneless, SW, translate
        ├── app.routes.ts
        ├── styles.scss        # light + dark tokens, global reset
        └── app/
            ├── pages/landing/landing.ts   # landing (RU/EN via translate pipe)
            └── shared/services/
                ├── language.service.ts    # RU↔EN toggle + localStorage
                └── theme.service.ts       # light↔dark toggle + localStorage
```

Stack choices (bare Angular 21 + hand-written CSS + ngx-translate, no
PrimeNG/Tailwind) are independent of the mayday.software site — mayday
was only the reference for Worker-glue mechanics and the i18n / theme
patterns.

## Scripts

From `aliro-site/`:

```bash
# first time — generate package-lock inside frontend/
cd frontend && npm install && cd ..

# local dev (Worker + local asset server)
npm run dev

# manual prod deploy (skip if CF Git integration is already wired)
npm run deploy
```

Requires Node 20+ (Angular 21 minimum) and `wrangler` CLI.

## CF Workers Build configuration

Connect the Cloudflare Worker to **`razqqm/aliro-mayday-software`** (not
the monorepo). In the dashboard:

| Field | Value |
|---|---|
| Project name | `aliro-mayday-software` |
| Path | `/` |
| Build command | `npm install && npm run build` |
| Deploy command | `npx wrangler deploy` |
| Non-production deploy command | `npx wrangler versions upload` |

Custom domain (`aliro.mayday.software` → this Worker) is attached manually
via the Cloudflare dashboard.

## Status

- [x] Worker scaffold, wrangler config, Angular project layout
- [x] Subtree link to private mirror repo
- [x] Landing — hero, "как работает", кошельки, преимущества, CTA
- [x] i18n (RU + EN) via `@ngx-translate/core`; detection = localStorage →
      browser language → RU fallback. Toggle button in topbar.
- [x] Light/dark theme with system preference detection and pre-paint
      inline script in `index.html` to avoid FOUC. Toggle button in topbar.
- [x] `<title>` + `<meta description>` + OG tags localized via
      `MetaService`, synced on every language change.
- [x] APP_INITIALIZER waits for first i18n JSON load before paint —
      no flash of raw translation keys on cold start.
- [x] `:focus-visible` styles across interactive elements.
- [x] Accessible aria-labels for language and theme toggle buttons.
- [x] Favicon (SVG monogram), Apple touch icon (180×180), PWA icons
      (192/512), `site.webmanifest`, OG + Twitter image (1200×630).
      Source SVGs (`favicon.svg`, `og.svg`) + PNGs generated from them
      via `rsvg-convert`.
- [x] Brand lockup "aliro.mayday" in topbar (monogram + wordmark with
      accent-colored dot), OG image, page title + meta description.
      "Aliro" alone stays only as references to the CSA standard
      (e.g. in the "why" section).
- [x] Roadmap section — 7 visual steps (Hardware → Production) with
      per-step status badge (done / active / planned / goal).
- [ ] Real content for "status" section once the firmware progresses
      past the skeleton.

### Known SPA-without-SSR limitation

The `<meta property="og:locale">` tag in `index.html` is hard-coded as
`ru_RU` (with `og:locale:alternate = en_US`). For SPA-only delivery
without server-side rendering, social crawlers that do not execute
JavaScript will see the RU variant regardless of the user's actual
language preference. `MetaService` does update `og:locale` at runtime
for JS-capable clients; a proper fix would need SSR or a language-
sniffing Worker in front of the assets. Out of scope for now.

## Key config — Safari + Angular SW workaround

In [wrangler.toml](wrangler.toml):

```toml
[assets]
html_handling = "none"
```

Cloudflare's default `html_handling` auto-redirects `/index.html` → `/`
(307). Angular's Service Worker fetches `/index.html` as its index during
navigation; with the redirect, Safari caches the 307 in the SW Cache API
and replays it indefinitely, hanging the page. `"none"` serves each URL
verbatim. Keep this.
