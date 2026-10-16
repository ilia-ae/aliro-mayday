import { expect, test, type Page } from '@playwright/test';

async function visitLogs(page: Page) {
    if ((page.viewportSize()?.width || 1280) < 1024) await page.locator('label[aria-label="Menu"]').click();
    await page.getByRole('navigation', { name: 'Main navigation' }).getByRole('link', { name: 'Logs', exact: true }).click();
}

for (const lang of ['en', 'ru']) {
    test(`PN532 scenarios update the visible state and full event log without network commands in ${lang}`, async ({ page }) => {
        const requests: string[] = [];
        const sockets: string[] = [];
        page.on('request', request => { if (['fetch', 'xhr'].includes(request.resourceType())) requests.push(request.url()); });
        page.on('websocket', socket => sockets.push(socket.url()));
        await page.goto(`/demo/index.html?lang=${lang}#/`);
        const simulator = page.getByRole('region', { name: lang === 'ru' ? 'Симуляция PN532' : 'PN532 simulation' });
        const tap = simulator.getByRole('button', { name: lang === 'ru' ? 'Приложить телефон' : 'Tap the phone', exact: true });
        const scenario = simulator.getByRole('combobox');
        await expect(page.locator('[data-controller-identity]:visible')).toContainText('DEMO-01');
        await expect(page.getByRole('region', { name: 'NFC diagnostics' })).toBeVisible();
        await tap.click();
        await expect(simulator.getByRole('status')).toContainText(lang === 'ru' ? 'Доступ разрешён' : 'Access granted');
        await expect(simulator.getByRole('status')).toContainText(lang === 'ru' ? 'открыт' : 'unlocked');
        await scenario.selectOption('revoked');
        await tap.click();
        await expect(simulator.getByRole('status')).toContainText(lang === 'ru' ? 'Доступ запрещён' : 'Access denied');
        await expect(simulator.getByRole('status')).toContainText(lang === 'ru' ? 'открыт' : 'unlocked');
        await expect(page.getByRole('region', { name: 'NFC diagnostics' })).toContainText('revoked example key');
        await scenario.selectOption('valid');
        await tap.click();
        await expect(simulator.getByRole('status')).toContainText(lang === 'ru' ? 'закрыт' : 'locked');
        await visitLogs(page);
        await expect(page.getByRole('log', { name: 'Controller event log' })).toContainText('SIMULATED DEMO-01');
        await expect(page.locator('[data-controller-identity]:visible')).toContainText('DEMO-01');
        expect(requests).toEqual([]);
        expect(sockets).toEqual([]);
    });

    test(`homepage tap animation and panel share the same isolated reader and log in ${lang}`, async ({ page }) => {
        await page.emulateMedia({ reducedMotion: 'reduce' });
        await page.goto(`/?lang=${lang}&theme=light#how-it-works`);
        const animation = page.locator('app-access-demo');
        const button = animation.getByRole('button');
        await expect(button).toBeEnabled();
        await animation.getByRole('combobox').selectOption('revoked');
        await button.click();
        await expect(animation.getByRole('status')).toContainText(lang === 'ru' ? 'Доступ запрещён' : 'Access denied');
        await expect(animation.locator('.trace')).toContainText('revoked example key');
        await expect(animation.locator('.access-demo')).not.toHaveClass(/is-open/);
        const panel = page.frameLocator('#controller-ui iframe');
        const simulator = panel.getByRole('region', { name: lang === 'ru' ? 'Симуляция PN532' : 'PN532 simulation' });
        await expect(simulator.getByRole('status')).toContainText(lang === 'ru' ? 'Доступ запрещён' : 'Access denied');
        await button.click();
        await animation.getByRole('combobox').selectOption('valid');
        await button.click();
        await expect(animation.getByRole('status')).toContainText(lang === 'ru' ? 'Дверь открывается' : 'The door opens');
        await expect(animation.locator('.trace')).toContainText('No Matter or GPIO command sent');
        await expect(simulator.getByRole('status')).toContainText(lang === 'ru' ? 'открыт' : 'unlocked');
        await expect(animation.locator('.tech-data')).toContainText('DEMO-01');
        const colors = await animation.locator('.door-shadow').evaluate(el => ({ fill: getComputedStyle(el).fill, background: getComputedStyle(document.body).backgroundColor }));
        expect(colors.fill).not.toBe(colors.background);
        await expect(page.locator('.demo-stand-links a')).toHaveCount(2);
        await expect(page.locator('.demo-stand-meta')).not.toContainText('Mayday / Ilia Arestov');
    });
}

test('two demo controllers show different IDs and retain independent simulated state', async ({ page, context }) => {
    const second = await context.newPage();
    await page.goto('/demo/index.html?controller=DEMO-01&lang=en#/');
    await second.goto('/demo/index.html?controller=DEMO-02&lang=en#/');
    await expect(page.locator('[data-controller-identity]:visible')).toContainText('DEMO-01');
    await expect(second.locator('[data-controller-identity]:visible')).toContainText('DEMO-02');
    await page.getByRole('button', { name: 'Tap the phone', exact: true }).click();
    await expect(page.getByRole('status')).toContainText('unlocked');
    await expect(second.getByRole('status')).toContainText('Waiting for a tap');
    await expect(second.getByRole('status')).toContainText('locked');
    await second.close();
});
