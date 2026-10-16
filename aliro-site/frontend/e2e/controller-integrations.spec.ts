// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { expect, test } from '@playwright/test';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
const catalog = ['../../aliro-esp32/data/src/lib/i18n/ru.json', '../aliro-esp32/data/src/lib/i18n/ru.json'].map(p => resolve(p)).find(existsSync)!;
const ru = JSON.parse(readFileSync(catalog, 'utf8')) as Record<string, string>;
test.use({ reducedMotion: 'reduce' });
for (const lang of ['en', 'ru']) for (const theme of ['light', 'dark']) {
    test(`${lang} ${theme}: relay pulse and notification-only Telegram share logs`, async ({ page }) => {
        const t = (text: string) => lang === 'ru' ? ru[text] ?? text : text;
        const external: string[] = [];
        page.on('request', request => { if (!['localhost', '127.0.0.1'].includes(new URL(request.url()).hostname)) external.push(request.url()); });
        await page.addInitScript(mode => localStorage.setItem('aliro-ui-theme', mode), theme);
        await page.goto(`/demo/index.html?lang=${lang}#/actions`);
        await page.getByRole('button', { name: t('Relay control'), exact: true }).click();
        await page.getByRole('heading', { name: t('Relay control'), exact: true }).waitFor();
        await page.getByRole('spinbutton', { name: t('Pulse duration (ms)') }).fill('100');
        await page.getByRole('button', { name: t('Save integration settings'), exact: true }).click();
        await expect(page.getByRole('status').filter({ hasText: t('Integration settings saved. Relay output starts inactive.') })).toBeVisible();
        await page.getByRole('button', { name: t('Simulate unlock'), exact: true }).click();
        await expect(page.getByText(t('Logical state: {state}').replace('{state}', t('Unlocked')), { exact: false })).toBeVisible();
        await expect(page.getByText(t('Relay output: {state}').replace('{state}', t('Inactive')), { exact: false })).toBeVisible({ timeout: 6000 });
        await page.getByRole('button', { name: t('Simulate denied access'), exact: true }).click();
        await expect(page.getByText(t('Logical state: {state}').replace('{state}', t('Unlocked')), { exact: false })).toBeVisible();
        await page.getByRole('button', { name: t('Telegram integration'), exact: true }).click();
        await expect(page.getByLabel(t('Bot token'))).toBeDisabled();
        await expect(page.getByRole('button', { name: /\/unlock/ })).toHaveCount(0);
        await page.getByRole('button', { name: t('Queue test notification'), exact: true }).click();
        await expect(page.getByRole('status').filter({ hasText: t('Notification queued; check delivery status.') })).toBeVisible();
        await page.getByRole('link', { name: `${t('Open logs')} →`, exact: true }).click();
        await page.getByRole('textbox', { name: t('Search logs'), exact: true }).fill('DEMO_RELAY');
        await expect(page.getByRole('log')).toContainText('DEMO_RELAY');
        await page.getByRole('textbox', { name: t('Search logs'), exact: true }).fill('DEMO_TELEGRAM');
        await expect(page.getByRole('log')).toContainText('DEMO_TELEGRAM');
        expect(external).toEqual([]);
        expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    });
}
