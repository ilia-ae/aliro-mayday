import { expect, test, type Page } from '@playwright/test';

async function navigate(page: Page, name: string) {
    if ((page.viewportSize()?.width || 1280) < 1024) await page.locator('label.drawer-button[for="main-content-drawer"]').click();
    await page.getByRole('navigation').getByRole('link', { name, exact: true }).click();
}

const labels = {
    en: { overview: 'Dashboard', actions: 'Actions', actionsTitle: 'Hardware Actions', system: 'System', systemTitle: /^System Settings/, hardware: 'Hardware', hardwareTitle: 'Hardware Configuration', security: 'Security', securityTitle: 'Web Authentication', ota: 'OTA Update', logs: 'Logs', logsTitle: 'System Logs' },
    ru: { overview: 'Обзор', actions: 'Действия', actionsTitle: 'Аппаратные действия', system: 'Система', systemTitle: /^Системные настройки/, hardware: 'Оборудование', hardwareTitle: 'Настройки оборудования', security: 'Безопасность', securityTitle: 'Вход в веб-панель', ota: 'Обновление OTA', logs: 'Журнал', logsTitle: 'Системный журнал' }
};

for (const lang of ['en', 'ru'] as const) {
    test(`all controller sections, nested settings and initial setup use ${lang}`, async ({ page }) => {
        const text = labels[lang];
        await page.goto(`/demo/index.html?lang=${lang}#/`);
        await expect(page.locator('html')).toHaveAttribute('lang', lang);
        await expect(page.getByRole('heading', { name: text.overview, exact: true })).toBeVisible();
        await navigate(page, text.actions);
        await expect(page.getByRole('heading', { name: text.actionsTitle, exact: true })).toBeVisible();
        await navigate(page, text.system);
        await expect(page.getByRole('heading', { name: text.systemTitle })).toBeVisible();
        await page.getByRole('button', { name: text.hardware, exact: true }).click();
        await expect(page.getByRole('heading', { name: text.hardwareTitle, exact: true })).toBeVisible();
        await page.getByRole('button', { name: text.security, exact: true }).click();
        await expect(page.getByRole('heading', { name: text.securityTitle, exact: true })).toBeVisible();
        await navigate(page, text.ota);
        await expect(page.getByRole('heading', { name: text.ota, exact: true })).toBeVisible();
        await page.locator('input[type="file"]').first().setInputFiles({ name: 'demo-only.bin', mimeType: 'application/octet-stream', buffer: Buffer.from([1, 2, 3]) });
        await page.getByRole('button', { name: lang === 'ru' ? 'Загрузить прошивку' : 'Upload Firmware', exact: true }).click();
        await expect(page.getByText(lang === 'ru' ? /Демо только для просмотра/ : /Read-only demo/).last()).toBeVisible();
        await navigate(page, text.logs);
        await expect(page.getByRole('heading', { name: text.logsTitle, exact: true })).toBeVisible();
        expect(await page.locator('[role="log"]').evaluate(el => el.clientHeight)).toBeGreaterThan(30);
        await page.goto(`/demo/index.html?lang=${lang}#/captive-portal`);
        await expect(page.getByRole('heading', { name: lang === 'ru' ? 'Настройка устройства' : 'Configure Your Device', exact: true })).toBeVisible();
        await page.getByRole('button', { name: lang === 'ru' ? 'Найти сети' : 'Scan for networks' }).click();
        await expect(page.getByText(lang === 'ru' ? /Найдено сетей: 1/ : /Networks found: 1/)).toBeVisible();
    });
}

test('EN/RU changes preserve form values and selected tab, and survive navigation and reload', async ({ page }) => {
    await page.goto('/demo/index.html#/mqtt');
    await expect(page.locator('html')).toHaveAttribute('lang', 'en');
    await page.getByPlaceholder('192.168.1.100').fill('edited.example.invalid');
    await page.getByRole('button', { name: 'Topics', exact: true }).click();
    await page.getByPlaceholder('homekey/lock/state').fill('demo/edited');
    await page.getByRole('button', { name: 'Русский', exact: true }).click();
    await expect(page.locator('html')).toHaveAttribute('lang', 'ru');
    await expect(page.getByPlaceholder('homekey/lock/state')).toHaveValue('demo/edited');
    await page.getByRole('button', { name: 'Брокер', exact: true }).click();
    await expect(page.getByPlaceholder('192.168.1.100')).toHaveValue('edited.example.invalid');
    await navigate(page, 'Журнал');
    await page.reload();
    await expect(page.getByRole('heading', { name: 'Системный журнал', exact: true })).toBeVisible();
    await page.goto('/demo/index.html#/mqtt');
    await expect(page.locator('html')).toHaveAttribute('lang', 'ru');
    await page.getByRole('button', { name: 'English', exact: true }).click();
    await page.reload();
    await expect(page.locator('html')).toHaveAttribute('lang', 'en');
});

test('standalone panel supports saved light/dark and live system theme changes', async ({ page }) => {
    await page.emulateMedia({ colorScheme: 'light' });
    await page.goto('/demo/index.html?lang=en#/mqtt');
    const theme = page.getByRole('combobox', { name: 'Theme', exact: true });
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'autumn');
    await theme.selectOption('dark');
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dracula');
    await page.reload();
    await expect(theme).toHaveValue('dark');
    await theme.selectOption('auto');
    await page.emulateMedia({ colorScheme: 'dark' });
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dracula');
    await page.emulateMedia({ colorScheme: 'light' });
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'autumn');
});

test('embedded panel and homepage share theme in both directions without losing edits', async ({ page }) => {
    await page.emulateMedia({ colorScheme: 'light' });
    await page.goto('/?lang=ru&theme=light#controller-ui');
    const frame = page.frameLocator('#controller-ui iframe');
    await expect(frame.locator('html')).toHaveAttribute('data-theme', 'autumn');
    if ((page.viewportSize()?.width || 1280) < 1024) await frame.locator('label.drawer-button[for="main-content-drawer"]').click();
    await frame.getByRole('navigation').getByRole('link', { name: 'MQTT', exact: true }).click();
    await frame.getByPlaceholder('192.168.1.100').fill('kept.example.invalid');
    await page.getByRole('button', { name: 'Переключить на тёмную тему', exact: true }).click();
    await expect(frame.locator('html')).toHaveAttribute('data-theme', 'dracula');
    await expect(frame.getByPlaceholder('192.168.1.100')).toHaveValue('kept.example.invalid');
    await frame.getByRole('combobox', { name: 'Тема', exact: true }).selectOption('light');
    await expect(page.locator('html')).not.toHaveClass(/app-dark/);
    await expect(frame.locator('html')).toHaveAttribute('data-theme', 'autumn');
    await frame.getByRole('combobox', { name: 'Тема', exact: true }).selectOption('auto');
    await page.emulateMedia({ colorScheme: 'dark' });
    await expect(frame.locator('html')).toHaveAttribute('data-theme', 'dracula');
    await expect(page.locator('html')).toHaveClass(/app-dark/);
    await expect(frame.getByPlaceholder('192.168.1.100')).toHaveValue('kept.example.invalid');
    await page.evaluate(() => window.postMessage({ type: 'aliro-demo-theme-request', mode: 'light' }, location.origin));
    await expect(page.locator('html')).toHaveClass(/app-dark/);
    await page.reload();
    await expect(frame.getByRole('combobox', { name: 'Тема', exact: true })).toHaveValue('auto');
    await expect(frame.locator('html')).toHaveAttribute('data-theme', 'dracula');
});

test('language and theme controls work when browser storage is blocked', async ({ page }) => {
    await page.addInitScript(() => {
        Storage.prototype.getItem = () => { throw new Error('Storage blocked'); };
        Storage.prototype.setItem = () => { throw new Error('Storage blocked'); };
    });
    await page.goto('/demo/index.html?lang=ru#/');
    await expect(page.getByRole('heading', { name: 'Обзор', exact: true })).toBeVisible();
    await page.getByRole('button', { name: 'English', exact: true }).click();
    await page.getByRole('combobox', { name: 'Theme', exact: true }).selectOption('light');
    await expect(page.locator('html')).toHaveAttribute('lang', 'en');
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'autumn');
});

test('website and embedded panel switch language together without a reload or lost form values', async ({ page }) => {
    await page.goto('/?lang=en#controller-ui');
    const frame = page.frameLocator('#controller-ui iframe');
    await expect(frame.getByRole('heading', { name: 'Dashboard', exact: true })).toBeVisible();
    if ((page.viewportSize()?.width || 1280) < 1024) await frame.locator('label.drawer-button[for="main-content-drawer"]').click();
    await frame.getByRole('navigation').getByRole('link', { name: 'MQTT', exact: true }).click();
    await frame.getByPlaceholder('192.168.1.100').fill('language-keeps.example.invalid');
    await page.getByRole('button', { name: 'Switch to Russian', exact: true }).click();
    await expect(frame.locator('html')).toHaveAttribute('lang', 'ru');
    await expect(frame.getByRole('heading', { name: /^Настройки MQTT/ })).toBeVisible();
    await expect(frame.getByPlaceholder('192.168.1.100')).toHaveValue('language-keeps.example.invalid');
    await frame.getByRole('button', { name: 'English', exact: true }).click();
    await expect(page.locator('html')).toHaveAttribute('lang', 'en');
    await expect(frame.locator('html')).toHaveAttribute('lang', 'en');
    await expect(frame.getByPlaceholder('192.168.1.100')).toHaveValue('language-keeps.example.invalid');
    await frame.getByRole('button', { name: 'Русский', exact: true }).click();
    await expect(page.locator('html')).toHaveAttribute('lang', 'ru');
    await page.reload();
    await expect(frame.locator('html')).toHaveAttribute('lang', 'ru');
});

test('changing language then opening the full NFC log preserves the simulated reader state', async ({ page }) => {
    await page.goto('/demo/index.html?controller=DEMO-02&lang=en#/');
    await page.getByRole('button', { name: 'Tap the phone', exact: true }).click();
    await page.getByRole('button', { name: 'Русский', exact: true }).click();
    await page.getByRole('link', { name: 'Открыть журнал →', exact: true }).click();
    await expect(page.getByRole('heading', { name: 'Системный журнал', exact: true })).toBeVisible();
    await expect(page.getByRole('status')).toContainText('открыт');
    await expect(page.locator('[role="log"]')).toContainText('SIMULATED DEMO-02');
    await expect(page).toHaveURL(/controller=DEMO-02&lang=ru#\/logs$/);
});

test('Russian labels fit every panel section on small and large screens in both themes', async ({ page }) => {
    for (const width of [360, 768, 1280]) {
        await page.setViewportSize({ width, height: 900 });
        for (const mode of ['light', 'dark']) {
            for (const route of ['/', '/mqtt', '/actions', '/misc', '/ota', '/logs']) {
                await page.goto(`/demo/index.html?lang=ru#${route}`);
                await page.getByRole('combobox', { name: 'Тема', exact: true }).selectOption(mode);
                if (route === '/mqtt') await page.getByRole('button', { name: 'SSL/TLS', exact: true }).click();
                if (route === '/misc') await page.getByRole('button', { name: 'Оборудование', exact: true }).click();
                const overflow = await page.locator('main').evaluate(element => ({ actual: element.scrollWidth, available: element.clientWidth }));
                const details = overflow.actual > overflow.available + 1 ? await page.locator('main').evaluate(main => [...main.querySelectorAll('*')].filter(element => element.getBoundingClientRect().right > main.getBoundingClientRect().right + 1).slice(0, 10).map(element => ({ tag: element.tagName, class: element.className, text: element.textContent?.trim().slice(0, 70) }))) : [];
                expect(overflow.actual, `${width}px ${mode} ${route}: ${JSON.stringify(details)}`).toBeLessThanOrEqual(overflow.available + 1);
                expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
            }
        }
    }
});


test('changing panel preferences keeps a selected OTA file without translating its name', async ({ page }) => {
    await page.goto('/demo/index.html?lang=en#/ota');
    await page.locator('input[type="file"]').first().setInputFiles({ name: 'Firmware-Lock.bin', mimeType: 'application/octet-stream', buffer: Buffer.from([1, 2, 3]) });
    await page.getByRole('button', { name: 'Русский', exact: true }).click();
    await page.getByRole('combobox', { name: 'Тема', exact: true }).selectOption('dark');
    expect(await page.locator('input[type="file"]').first().evaluate((input: HTMLInputElement) => input.files?.[0]?.name)).toBe('Firmware-Lock.bin');
    await expect(page.getByRole('button', { name: 'Загрузить прошивку', exact: true })).toBeEnabled();
    await page.getByRole('button', { name: 'English', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Upload Firmware', exact: true })).toBeEnabled();
});
