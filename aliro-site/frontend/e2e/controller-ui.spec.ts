import { expect, test } from '@playwright/test';

// These tests inspect settled layouts; disable the host page's smooth
// anchor scrolling so automatic click scrolling cannot fight the animation.
test.use({ reducedMotion: 'reduce' });


for (const lang of ['en', 'ru']) {
    test(`controller interface screenshot and local launch guide in ${lang}`, async ({ page }) => {
        await page.goto(`/?lang=${lang}#controller-ui`);
        const section = page.locator('#controller-ui');
        await section.locator('.controller-ui-screenshots > summary').click();
        const title = lang === 'ru' ? 'Веб-интерфейс контроллера' : 'Controller web interface';
        await expect(section.getByRole('heading', { name: title })).toBeVisible();
        await expect(section.locator('figcaption')).toContainText(lang === 'ru' ? 'Демонстрационные данные' : 'Sample data');
        const image = section.locator('img');
        await expect(image).toBeVisible();
        await expect(image).toHaveJSProperty('naturalWidth', 1280);
        await expect(image).toHaveJSProperty('naturalHeight', 720);
        await expect(image).toHaveAttribute('alt', /.+/);
        const suffix = lang === 'ru' ? '.ru' : '';
        await expect(section.locator(`a[href$="/docs/controller-ui-debug${suffix}.md"]`)).toBeVisible();
        await expect(section.locator('a[href="#hero"]')).toBeVisible();
        await expect(page.locator('.hero-visual img')).toHaveCount(1);
        await expect(page.locator('.hero-visual a[href="#controller-ui"]')).toBeVisible();
        expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    });

    test(`all six screenshots switch, wrap and retain the selected view across languages in ${lang}`, async ({ page }) => {
        await page.goto(`/?lang=${lang}#controller-ui`);
        const section = page.locator('#controller-ui');
        await section.locator('.controller-ui-screenshots > summary').click();
        const views = lang === 'ru'
            ? ['Обзор', 'MQTT', 'Действия', 'Система', 'Обновление', 'Журнал']
            : ['Overview', 'MQTT', 'Actions', 'System', 'OTA update', 'Logs'];
        const keys = ['dashboard', 'mqtt', 'actions', 'system', 'ota', 'logs'];
        const buttons = section.locator('.controller-ui-views button');
        const image = section.locator('img');
        await expect(buttons).toHaveCount(6);
        for (let i = 0; i < views.length; i++) {
            const button = buttons.nth(i);
            await expect(button).toHaveText(views[i]);
            await button.click();
            await expect(button).toHaveAttribute('aria-pressed', 'true');
            await expect(section.locator('[aria-pressed="true"]')).toHaveCount(1);
            await expect(image).toHaveAttribute('src', `/images/controller-${keys[i]}-demo.jpg`);
            await expect(image).toHaveJSProperty('complete', true);
            await expect(image).toHaveJSProperty('naturalWidth', 1280);
            await expect(section.locator('figure > a')).toHaveAttribute('href', `/images/controller-${keys[i]}-demo.jpg`);
            await expect(section.locator('[aria-live="polite"]')).toContainText(`${i + 1} / 6`);
            await expect(image).toHaveAttribute('alt', /.+/);
        }
        const next = section.getByRole('button', { name: lang === 'ru' ? 'Следующий снимок' : 'Next screenshot', exact: true });
        const previous = section.getByRole('button', { name: lang === 'ru' ? 'Предыдущий снимок' : 'Previous screenshot', exact: true });
        await next.click();
        await expect(buttons.nth(0)).toHaveAttribute('aria-pressed', 'true');
        await previous.click();
        await expect(buttons.nth(5)).toHaveAttribute('aria-pressed', 'true');
        await buttons.nth(2).focus();
        await buttons.nth(2).press('Enter');
        await expect(buttons.nth(2)).toHaveAttribute('aria-pressed', 'true');
        await page.getByRole('button', { name: lang === 'ru' ? 'Переключить на английский' : 'Switch to Russian', exact: true }).click();
        await expect(buttons.nth(2)).toHaveAttribute('aria-pressed', 'true');
        await expect(buttons.nth(2)).toHaveText(lang === 'ru' ? 'Actions' : 'Действия');
        await expect(image).toHaveAttribute('src', '/images/controller-actions-demo.jpg');
        expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    });
}
