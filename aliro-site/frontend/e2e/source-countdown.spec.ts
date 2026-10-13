import { expect, test } from '@playwright/test';

test.describe('source countdown', () => {
    test('renders four tiles with non-negative numbers', async ({ page }) => {
        await page.goto('/#source');
        const tiles = page.locator('.source .countdown .unit');
        await expect(tiles).toHaveCount(4);

        for (let i = 0; i < 4; i++) {
            const num = await tiles.nth(i).locator('.num').innerText();
            const n = parseInt(num, 10);
            expect(Number.isFinite(n) && n >= 0, `tile ${i} = "${num}"`).toBe(true);
        }
    });

    test('seconds tile updates within 2s', async ({ page }) => {
        await page.goto('/#source');
        const seconds = page.locator(
            '.source .countdown .unit:last-child .num'
        );
        const a = await seconds.innerText();
        await page.waitForTimeout(2100);
        const b = await seconds.innerText();
        expect(b).not.toBe(a);
    });

    test('renders mailto request CTA with pre-filled subject', async ({
        page
    }) => {
        await page.goto('/#source');
        const link = page.locator('.source .cta a[href^="mailto:"]');
        await expect(link).toBeVisible();
        const href = await link.getAttribute('href');
        // Recipient + ?subject= URL-encoded so Cyrillic survives the
        // handoff to the email client.
        expect(href).toMatch(/^mailto:aliro\.mayday@ilia\.ae\?subject=/);
        expect(href).toContain('subject=');
        // Decoded subject must be non-empty (not the raw key).
        const subject = decodeURIComponent(
            new URL(href!).searchParams.get('subject') ?? ''
        );
        expect(subject.length, `subject "${subject}"`).toBeGreaterThan(5);
        expect(subject).not.toContain('source.request');
    });

    test('mailto subject switches with the language and includes anti-spam code', async ({
        page
    }) => {
        await page.goto('/?lang=ru#source');
        const linkRu = page.locator('.source .cta a[href^="mailto:"]');
        const ru = decodeURIComponent(
            new URL(await linkRu.getAttribute('href') ?? '')
                .searchParams.get('subject') ?? ''
        );
        expect(ru).toMatch(/[А-Яа-я]/);
        // Anti-spam code in the RU subject — uppercase token in
        // Russian script, lets the recipient filter inbox by sender
        // intent and rejects naive subject-stripping bots.
        expect(ru).toContain('РУ-2026');

        await page.goto('/?lang=en#source');
        const linkEn = page.locator('.source .cta a[href^="mailto:"]');
        const en = decodeURIComponent(
            new URL(await linkEn.getAttribute('href') ?? '')
                .searchParams.get('subject') ?? ''
        );
        expect(en).not.toMatch(/[А-Яа-я]/);
        expect(en.toLowerCase()).toContain('access');
        expect(en).toContain('EN-2026');
    });
});
