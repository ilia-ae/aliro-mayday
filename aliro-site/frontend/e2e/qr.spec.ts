import { expect, test } from '@playwright/test';

test.describe('/qr business-card page', () => {
    test('renders card with hero, QR image, share grid, and quick links', async ({
        page
    }) => {
        await page.goto('/qr');

        // Hero title is the brand string itself.
        await expect(page.locator('h1')).toContainText('aliro.mayday');

        // QR is a client-rendered inline SVG (no external API). The
        // generated <svg> contains a <path> with a non-trivial d= so
        // we can use that as a smoke check that the matrix was drawn.
        const qrSvg = page.locator('.qr-code__svg svg');
        await expect(qrSvg).toBeVisible();
        const pathD = await qrSvg.locator('path').getAttribute('d');
        expect(pathD?.length ?? 0).toBeGreaterThan(100);

        // Document title is the brand (no homepage meta.title prefix).
        await expect(page).toHaveTitle('aliro.mayday');

        // Share grid is exactly 9 chips at every viewport — native is
        // always rendered (falls back to copy where Web Share is not
        // supported), so the auto-fill grid never has orphan cells.
        await expect(page.locator('.qr-chip')).toHaveCount(9);

        // Quick links: 4 entries (site, contact, about, privacy).
        const quickLinks = page.locator('.qr-link');
        await expect(quickLinks).toHaveCount(4);
    });

    test('no global topbar or footer on /qr (fullscreen page)', async ({
        page
    }) => {
        await page.goto('/qr');
        // The landing topbar / footer don't render — the QR card is the
        // entire page. Asserting their absence prevents a regression
        // where someone accidentally wraps /qr in PublicLayout.
        await expect(page.locator('header.topbar')).toHaveCount(0);
        await expect(page.locator('footer')).toHaveCount(0);
    });

    test('share-link buttons emit external messenger URLs', async ({
        page
    }) => {
        await page.goto('/qr');

        // One assertion per chip — each opens its target sharer in a
        // new tab with the local origin URL-encoded as the share param.
        // Each share chip must open its target service in a new tab.
        // The final popup URL is whatever the service redirects to
        // (wa.me → api.whatsapp.com, linkedin.com/sharing → uas/login,
        // etc.), so we only assert on the host domain. The URL may
        // be encoded once or twice depending on the redirect chain;
        // checking for a "localhost:4444" substring (any form) is
        // enough to prove our origin made it into the share payload.
        const cases: ReadonlyArray<{ name: RegExp; host: RegExp }> = [
            { name: /telegram/i, host: /t\.me/ },
            { name: /whatsapp/i, host: /wa\.me|api\.whatsapp\.com/ },
            { name: /^x$/i, host: /(twitter|x)\.com/ },
            { name: /linkedin/i, host: /linkedin\.com/ },
            { name: /facebook/i, host: /facebook\.com/ },
            { name: /^vk$/i, host: /vk\.com/ }
        ];

        for (const c of cases) {
            const btn = page.getByRole('button', { name: c.name });
            const [popup] = await Promise.all([
                page.waitForEvent('popup'),
                btn.click()
            ]);
            expect(popup.url()).toMatch(c.host);
            // Match the encoded origin in any form ("localhost%3A4444",
            // "localhost%253A4444" after a re-encode, or raw).
            expect(popup.url()).toMatch(/localhost(%(25)?3A|:)4444/);
            await popup.close();
        }
    });

    test('copy chip writes the origin URL to the clipboard', async ({
        page,
        context
    }) => {
        await context.grantPermissions(['clipboard-read', 'clipboard-write']);
        await page.goto('/qr');

        const copyBtn = page.getByRole('button', {
            name: /^copy$|^копировать$/i
        });
        await copyBtn.click();

        const clipboardText = await page.evaluate(() =>
            navigator.clipboard.readText()
        );
        expect(clipboardText).toContain('localhost:4444');

        // After 1.8s the chip flips back from "Copied" to its label.
        await expect(copyBtn).toContainText(
            /copied|скопировано/i
        );
    });

    test('lang and theme controls live in the card header', async ({
        page
    }) => {
        // Force RU + light so the toggles deterministically flip both.
        await page.emulateMedia({ colorScheme: 'light' });
        await page.goto('/qr?lang=ru&theme=light');

        // Lang button shows the *other* locale label ("EN" in RU mode).
        const langBtn = page.locator('.qr-ctrl--lang');
        await expect(langBtn).toContainText('EN');
        await langBtn.click();
        await expect(page.locator('html')).toHaveAttribute('lang', 'en');
        // After flip the button advertises switching back.
        await expect(langBtn).toContainText('RU');

        // Theme cycler walks light → dark.
        await expect(page.locator('html')).not.toHaveClass(/app-dark/);
        await page.locator('.qr-ctrl--theme').click();
        await expect(page.locator('html')).toHaveClass(/app-dark/);
    });

    test('hidden /qr entry: the copyright year on / routes to /qr', async ({
        page
    }) => {
        await page.goto('/');
        const yearLink = page.locator('footer .ft-secret');
        await expect(yearLink).toContainText('2026');
        await expect(yearLink).toHaveAttribute('href', '/qr');
        await yearLink.click();
        await expect(page).toHaveURL(/\/qr$/);
        await expect(page.locator('h1')).toContainText('aliro.mayday');
    });
});
