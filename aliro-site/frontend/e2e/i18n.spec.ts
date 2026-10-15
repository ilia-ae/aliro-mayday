import { expect, test } from '@playwright/test';

test.describe('i18n', () => {
    test('?lang=en switches the title and brand-mark text', async ({ page }) => {
        await page.goto('/?lang=en');
        await expect(page).toHaveTitle(/your door key/i);
        await expect(page.locator('html')).toHaveAttribute('lang', 'en');
        await expect(page.locator('h1')).toContainText(/your door key/i);
    });

    test('?lang=ru forces Russian', async ({ page }) => {
        await page.goto('/?lang=ru');
        await expect(page).toHaveTitle(/ключ от двери/i);
        await expect(page.locator('html')).toHaveAttribute('lang', 'ru');
    });

    test('language toggle persists across reloads via localStorage', async ({
        page
    }) => {
        // Pin RU first so the toggle deterministically goes RU → EN
        // regardless of the test browser's preferred locale.
        await page.goto('/?lang=ru');
        await expect(page.locator('html')).toHaveAttribute('lang', 'ru');

        // The aria-label on the lang-toggle is the only stable accessible
        // name across both locales: "Switch to Russian" / "Переключить на
        // английский". Match those exact phrases (the theme cycler also
        // uses "switch to …" / "переключить на …" so we can't rely on
        // the verb alone).
        const toggle = page.getByRole('button', {
            name: /switch to (russian|english)|переключить на (русский|английский)/i
        });
        await toggle.click();

        // Wait for the lang attribute to flip.
        await expect(page.locator('html')).toHaveAttribute('lang', 'en');

        // Navigate to a query-param-free URL — the inline ?lang=
        // bootstrapper in index.html would otherwise re-write the saved
        // value on every reload. We want to verify localStorage alone
        // carries the toggle decision forward.
        await page.goto('/');
        await expect(page.locator('html')).toHaveAttribute('lang', 'en');
        await expect(page).toHaveTitle(/your door key/i);
    });

    test('meta description switches with the language', async ({ page }) => {
        await page.goto('/?lang=en');
        const desc = page.locator('meta[name="description"]');
        await expect(desc).toHaveAttribute(
            'content',
            /experimental Aliro NFC/i
        );

        await page.goto('/?lang=ru');
        await expect(desc).toHaveAttribute('content', /Экспериментальный Aliro NFC/i);
    });
});
