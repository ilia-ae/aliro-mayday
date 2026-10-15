import { expect, test } from '@playwright/test';

const repository = 'https://github.com/ilia-ae/aliro-mayday';

test.describe('public source access', () => {
    for (const lang of ['ru', 'en']) {
        test(`visitors can open code, documentation and wiring in ${lang}`, async ({ page }) => {
            await page.goto(`/?lang=${lang}#source`);
            const section = page.locator('#source');
            const code = section.locator(`a[href="${repository}"]`);
            await expect(code).toBeVisible();
            await expect(code).toHaveAttribute('target', '_blank');
            const index = lang === 'ru' ? 'README.ru.md' : 'README.md';
            await expect(section.locator(`a[href$="/docs/${index}"]`)).toBeVisible();
            const wiring = lang === 'ru' ? 'wiring.ru.md' : 'wiring.md';
            await expect(section.locator(`a[href$="/docs/${wiring}"]`)).toBeVisible();
            await expect(section.locator('[role="timer"]')).toHaveCount(0);
            await expect(section.locator('a[href^="mailto:"]')).toHaveCount(0);
            await expect(section).toContainText(/source-available/i);
            await expect(page.locator('#status a')).toHaveAttribute('href', repository);
        });
    }
    test('wallet status distinguishes the bench iPhone from pending Samsung and Google tests', async ({ page }) => {
        await page.goto('/?lang=en');
        await expect(page.locator('#wallets')).toContainText('existing key tested');
        await expect(page.locator('#wallets')).toContainText('not yet tested');
        await expect(page.locator('#wallets')).toContainText('SmartThings testing pending');
        await expect(page.locator('#status')).toContainText('physical relay');
    });
});
