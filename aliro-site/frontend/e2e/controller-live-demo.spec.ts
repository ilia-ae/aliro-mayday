import { expect, test, type Page } from '@playwright/test';

async function navigatePanel(page: Page, name: string) {
    if ((page.viewportSize()?.width || 1280) < 1024) {
        await page.locator('label.drawer-button[for="main-content-drawer"]').click();
    }
    await page.getByRole('navigation').getByRole('link', { name, exact: true }).click();
}

for (const lang of ['en', 'ru']) {
    test(`landing offers a working live demo link in ${lang}`, async ({ page }) => {
        await page.goto(`/?lang=${lang}#controller-ui`);
        const link = page.locator('#controller-ui .controller-ui-demo a');
        await expect(link).toBeVisible();
        await expect(link).toHaveAttribute('href', `/demo/index.html?lang=${lang}#/`);
        await expect(link).toContainText(lang === 'ru' ? 'Открыть панель отдельно' : 'Open panel in a new tab');
    });
}

test('live panel navigates all sections, nested settings and logs without network APIs', async ({ page }) => {
    const requests: string[] = [];
    const sockets: string[] = [];
    page.on('request', request => {
        if (['fetch', 'xhr'].includes(request.resourceType())) requests.push(request.url());
    });
    page.on('websocket', socket => sockets.push(socket.url()));
    await page.goto('/demo/index.html?lang=ru#/');
    await expect(page.getByRole('note')).toContainText('Демо-панель');
    await expect(page.getByRole('heading', { name: 'Обзор', exact: true })).toBeVisible();
    await expect(page.getByText('Demo reader', { exact: true })).toBeVisible();
    await expect(page.getByText('Aliro demo stand', { exact: true }).last()).toBeVisible();

    await navigatePanel(page, 'MQTT');
    await expect(page.getByRole('heading', { name: /^Настройки MQTT/ })).toBeVisible();
    await expect(page.getByPlaceholder('192.168.1.100')).toHaveValue('mqtt.example.invalid');
    await page.getByRole('button', { name: 'Топики', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Брокер', exact: true })).toBeVisible();
    await page.getByRole('button', { name: 'SSL/TLS', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Сохранить и применить', exact: true })).toBeVisible();

    await navigatePanel(page, 'Действия');
    await expect(page.getByRole('heading', { name: 'Аппаратные действия', exact: true })).toBeVisible();
    await page.getByRole('button', { name: 'События состояния', exact: true }).click();

    await navigatePanel(page, 'Система');
    await expect(page.getByRole('heading', { name: /^Системные настройки/ })).toBeVisible();
    await page.getByRole('button', { name: 'Оборудование', exact: true }).click();
    await expect(page.getByRole('heading', { name: 'Настройки оборудования', exact: true })).toBeVisible();
    await page.getByRole('button', { name: 'Безопасность', exact: true }).click();
    await expect(page.getByRole('heading', { name: 'Вход в веб-панель', exact: true })).toBeVisible();

    await navigatePanel(page, 'Обновление OTA');
    await expect(page.getByRole('heading', { name: 'Обновление OTA', exact: true })).toBeVisible();
    await expect(page.getByText('Demo preview', { exact: true })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Загрузить прошивку', exact: true })).toBeDisabled();

    await navigatePanel(page, 'Журнал');
    await expect(page.getByRole('heading', { name: 'Системный журнал', exact: true })).toBeVisible();
    await expect(page.getByText('Sample log stream — no hardware connected').first()).toBeVisible();
    await expect(page).toHaveURL(/demo\/index.html\?lang=ru#\/logs$/);
    await page.reload();
    await expect(page.getByRole('note')).toContainText('Демо-панель');
    await expect(page.getByRole('heading', { name: 'Системный журнал', exact: true })).toBeVisible();
    expect(requests).toEqual([]);
    expect(sockets).toEqual([]);
});

test('demo saves and OTA uploads fail locally and reload discards edits', async ({ page }) => {
    const requests: string[] = [];
    page.on('request', request => {
        if (['fetch', 'xhr'].includes(request.resourceType())) requests.push(request.url());
    });
    await page.goto('/demo/index.html?lang=en#/mqtt');
    await page.getByPlaceholder('192.168.1.100').fill('edited.example.invalid');
    await page.getByRole('button', { name: 'Save & Apply changes', exact: true }).click();
    await expect(page.getByRole('alert')).toContainText('Read-only demo');
    await page.reload();
    await expect(page.getByPlaceholder('192.168.1.100')).toHaveValue('mqtt.example.invalid');
    await navigatePanel(page, 'OTA Update');
    await page.locator('input[type="file"]').first().setInputFiles({ name: 'demo-only.bin', mimeType: 'application/octet-stream', buffer: Buffer.from([1, 2, 3]) });
    await page.getByRole('button', { name: 'Upload Firmware', exact: true }).click();
    await expect(page.getByText(/Read-only demo: no controller connected/).last()).toBeVisible();
    expect(requests).toEqual([]);
});

test('browser demo enforces a no-network CSP and serves bundled license notices', async ({ page, request }) => {
    await page.goto('/demo/index.html#/logs');
    await expect(page.locator('meta[http-equiv="Content-Security-Policy"]')).toHaveAttribute('content', /connect-src 'none'/);
    const notices = await request.get('/demo/NOTICES.txt');
    expect(notices.status()).toBe(200);
    expect(await notices.text()).toContain('rednblkx');
    expect(await notices.text()).toContain('TanStack Virtual Core');
});
