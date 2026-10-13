import { expect, test } from '@playwright/test';

test.describe('homepage smoke', () => {
    test('loads, has brand title, no console errors', async ({ page }) => {
        const errors: string[] = [];
        page.on('console', (m) => {
            if (m.type() === 'error') errors.push(m.text());
        });
        page.on('pageerror', (e) => errors.push(e.message));

        await page.goto('/');
        await expect(page).toHaveTitle(/aliro\.mayday/);

        // Hero must render the title from i18n (not the raw key).
        await expect(page.locator('h1')).toContainText(/[А-Яа-яA-Za-z]/);
        await expect(page.locator('h1')).not.toContainText('hero.title');

        // Hero CTAs render.
        await expect(page.getByRole('link', { name: /связаться|get in touch/i })).toBeVisible();

        // No console errors of any kind.
        expect(errors, errors.join('\n')).toEqual([]);
    });

    test('all main sections are present in the DOM', async ({ page }) => {
        await page.goto('/');
        for (const id of [
            'hero',
            'how-it-works',
            'wallets',
            'why',
            'use',
            'migration',
            'about',
            'integrations',
            'stack',
            'comparison',
            'competitors',
            'privacy',
            'faq',
            'roadmap',
            'source',
            'changelog',
            'status',
            'contact'
        ]) {
            await expect(page.locator(`#${id}`), `section #${id}`).toBeVisible();
        }
    });
});
