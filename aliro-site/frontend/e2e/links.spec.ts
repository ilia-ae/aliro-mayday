import { expect, test } from '@playwright/test';

test.describe('links + privacy route', () => {
    test('all jump-nav anchors resolve to existing sections', async ({ page }) => {
        await page.goto('/');
        // Wait for hydration to attach jump-nav anchors before counting.
        await page
            .locator('.jump-nav a[href^="#"]')
            .first()
            .waitFor({ state: 'attached' });
        const hrefs = await page
            .locator('.jump-nav a[href^="#"]')
            .evaluateAll((els) => els.map((e) => (e as HTMLAnchorElement).hash));
        expect(hrefs.length).toBeGreaterThan(0);
        for (const h of hrefs) {
            const id = h.slice(1);
            await expect(
                page.locator(`#${id}`),
                `nav anchor ${h}`
            ).toBeAttached();
        }
    });

    test('all external links open in a new tab with rel=noopener', async ({
        page
    }) => {
        await page.goto('/');
        // Angular hydrates after the static shell paints; wait for the
        // lazy landing chunk to attach refs before counting links.
        await page
            .locator('.refs li a[href^="https://"]')
            .first()
            .waitFor({ state: 'attached' });
        const externals = page.locator('a[href^="https://"], a[href^="http://"]');
        const count = await externals.count();
        expect(count).toBeGreaterThan(0);

        for (let i = 0; i < count; i++) {
            const a = externals.nth(i);
            const target = await a.getAttribute('target');
            const rel = (await a.getAttribute('rel')) ?? '';
            const href = await a.getAttribute('href');
            // Anchor links inside the same hash and the canonical link
            // (which is in <head>, not in <main>) won't show up here.
            // Everything that DOES is genuinely external.
            expect(target, `external ${href} target`).toBe('_blank');
            expect(rel, `external ${href} rel`).toContain('noopener');
        }
    });

    test('no GitHub-button or razqqm/* repo URLs are still on the page', async ({
        page
    }) => {
        await page.goto('/');
        const html = await page.content();
        expect(html).not.toContain('razqqm/apple-homekey-pm3-reader');
        expect(html).not.toContain('razqqm/aliro-mayday-software');
    });

    test('privacy route loads, has its own title, and back-link returns home', async ({
        page
    }) => {
        await page.goto('/privacy');
        // Title should be the page-specific composite "Privacy · aliro.mayday"
        // (or RU equivalent) — proves MetaService picks the privacy_page
        // keys via the Router NavigationEnd subscription, not the homepage
        // meta.title default.
        await expect(page).toHaveTitle(/конфиденциальность|privacy/i);
        await expect(page).toHaveTitle(/aliro\.mayday/);

        // Description meta should also switch to privacy_page.lede.
        const desc = page.locator('meta[name="description"]');
        await expect(desc).toHaveAttribute(
            'content',
            /юридического шума|legal noise/i
        );
        await expect(page.locator('h1')).toContainText(
            /конфиденциальность|privacy/i
        );

        const back = page.getByRole('link', {
            name: /вернуться|back to homepage/i
        });
        await back.click();
        await expect(page).toHaveURL(/\/$/);
    });

    test('footer privacy link goes to /privacy', async ({ page }) => {
        await page.goto('/');
        const privacyLink = page.locator('footer a[href="/privacy"]').first();
        await expect(privacyLink).toBeVisible();
        await privacyLink.click();
        await expect(page).toHaveURL(/\/privacy/);
    });

    test('hamburger is hidden on desktop, visible on mobile', async ({
        page,
        viewport
    }) => {
        await page.goto('/');
        const hamburger = page.locator('.menu-btn');
        const wide = (viewport?.width ?? 0) > 960;
        // .ghost.icon.menu-btn has higher specificity than .ghost.icon —
        // without that, display:inline-flex from .ghost.icon would leak
        // through and the hamburger would render on desktop too.
        if (wide) {
            await expect(hamburger).toBeHidden();
        } else {
            await expect(hamburger).toBeVisible();
        }
    });

    test('mobile menu: hamburger opens overlay, item click closes it', async ({
        page,
        viewport
    }) => {
        // Hamburger only renders below 60rem (960px) — skip on desktop.
        if ((viewport?.width ?? 0) > 960) {
            test.skip();
            return;
        }
        await page.goto('/');

        const overlay = page.locator('.mob-overlay');
        const hamburger = page.locator('.menu-btn');

        await expect(hamburger).toBeVisible();
        await expect(overlay).not.toHaveClass(/is-open/);

        await hamburger.click();
        await expect(overlay).toHaveClass(/is-open/);

        // 6 numbered nav items in the page top→bottom order.
        const links = page.locator('.mob-overlay .mob-link');
        await expect(links).toHaveCount(6);
        const hrefs = await links.evaluateAll((els) =>
            els.map((e) => (e as HTMLAnchorElement).getAttribute('href'))
        );
        expect(hrefs).toEqual([
            '#migration',
            '#about',
            '#faq',
            '#roadmap',
            '#source',
            '#contact'
        ]);

        // Clicking a nav item closes the overlay.
        await links.first().click();
        await expect(overlay).not.toHaveClass(/is-open/);
    });

    test('mobile menu: Escape key closes the overlay', async ({
        page,
        viewport
    }) => {
        if ((viewport?.width ?? 0) > 960) {
            test.skip();
            return;
        }
        await page.goto('/');
        const overlay = page.locator('.mob-overlay');
        await page.locator('.menu-btn').click();
        await expect(overlay).toHaveClass(/is-open/);

        await page.keyboard.press('Escape');
        await expect(overlay).not.toHaveClass(/is-open/);
        // Body scroll lock must be released so the page is scrollable.
        const overflow = await page.evaluate(
            () => document.body.style.overflow
        );
        expect(overflow).toBe('');
    });

    test('mobile menu: backdrop tap closes the overlay', async ({
        page,
        viewport
    }) => {
        if ((viewport?.width ?? 0) > 960) {
            test.skip();
            return;
        }
        await page.goto('/');
        const overlay = page.locator('.mob-overlay');
        await page.locator('.menu-btn').click();
        await expect(overlay).toHaveClass(/is-open/);

        // Tap the left edge of the sheet well below the topbar (topbar
        // is ~64px tall and z-index 50, so anything above that gets
        // intercepted by the header). y:90 lands in the padding zone
        // above the nav block, which bubbles to the overlay close handler.
        await page.locator('.mob-sheet').click({ position: { x: 8, y: 90 } });
        await expect(overlay).not.toHaveClass(/is-open/);
    });

    test('mobile menu: resizing past breakpoint auto-closes', async ({
        page,
        viewport
    }) => {
        if ((viewport?.width ?? 0) > 960) {
            test.skip();
            return;
        }
        await page.goto('/');
        const overlay = page.locator('.mob-overlay');
        await page.locator('.menu-btn').click();
        await expect(overlay).toHaveClass(/is-open/);

        // Resize beyond 60rem (961px = 60.0625rem assuming 16px root).
        // The matchMedia listener should drop menuOpen back to false so
        // the body scroll lock and overlay state both clear.
        await page.setViewportSize({ width: 1024, height: 800 });
        await expect(overlay).not.toHaveClass(/is-open/);
        const overflow = await page.evaluate(
            () => document.body.style.overflow
        );
        expect(overflow).toBe('');
    });

    test('mobile menu: footer CTA links to the contact email', async ({
        page,
        viewport
    }) => {
        if ((viewport?.width ?? 0) > 960) {
            test.skip();
            return;
        }
        await page.goto('/');
        await page.locator('.menu-btn').click();
        const cta = page.locator('.mob-overlay .mob-cta');
        await expect(cta).toHaveAttribute(
            'href',
            'mailto:aliro.mayday@ilia.ae'
        );
    });

    test('footer has brand block, two nav columns, and bottom strip', async ({
        page
    }) => {
        await page.goto('/');
        // Brand block: logo + tagline + email
        await expect(page.locator('footer .footer-brand .brand')).toBeVisible();
        await expect(page.locator('footer .footer-brand .tagline')).toBeVisible();
        await expect(
            page.locator('footer .footer-brand .brand-email')
        ).toHaveAttribute('href', 'mailto:aliro.mayday@ilia.ae');

        // Two nav columns with at least one link each
        const cols = page.locator('footer .footer-col');
        await expect(cols).toHaveCount(2);
        for (let i = 0; i < 2; i++) {
            await expect(cols.nth(i).locator('a').first()).toBeVisible();
        }

        // Bottom strip carries copyright + license fine print
        await expect(page.locator('footer .copyright')).toContainText(
            /© 2026/
        );
        await expect(page.locator('footer .license')).toBeVisible();
    });
});
