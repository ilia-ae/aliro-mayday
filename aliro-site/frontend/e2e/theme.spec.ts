import { expect, test } from '@playwright/test';

test.describe('theme', () => {
    test('?theme=dark applies dark mode immediately (no FOUC)', async ({
        page
    }) => {
        await page.goto('/?theme=dark');
        await expect(page.locator('html')).toHaveClass(/app-dark/);
    });

    test('?theme=light forces light mode regardless of system pref', async ({
        page
    }) => {
        await page.emulateMedia({ colorScheme: 'dark' });
        await page.goto('/?theme=light');
        await expect(page.locator('html')).not.toHaveClass(/app-dark/);
    });

    test('?theme=auto follows the system preference', async ({ page }) => {
        // System says dark → page paints dark.
        await page.emulateMedia({ colorScheme: 'dark' });
        await page.goto('/?theme=auto');
        await expect(page.locator('html')).toHaveClass(/app-dark/);

        // Flip system → page repaints light without a navigation.
        await page.emulateMedia({ colorScheme: 'light' });
        await expect(page.locator('html')).not.toHaveClass(/app-dark/);
    });

    test('cycle button walks light → dark → auto → light', async ({
        page
    }) => {
        // Force a light starting point so the cycle is deterministic.
        await page.emulateMedia({ colorScheme: 'light' });
        await page.goto('/?theme=light');

        const btn = page.getByRole('button', {
            name: /switch to (dark theme|тёмную тему)/i
        });
        await btn.click();
        await expect(page.locator('html')).toHaveClass(/app-dark/);

        // Now in dark mode. Aria reads "switch to auto" — click again.
        const next = page.getByRole('button', {
            name: /switch to (system theme|системную тему)/i
        });
        await next.click();
        // Light system + auto = light (no app-dark).
        await expect(page.locator('html')).not.toHaveClass(/app-dark/);

        // Auto → light per the cycle.
        const back = page.getByRole('button', {
            name: /switch to (light theme|светлую тему)/i
        });
        await back.click();
        await expect(page.locator('html')).not.toHaveClass(/app-dark/);
    });
});
