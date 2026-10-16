// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { expect, test, type Page, type FrameLocator } from '@playwright/test';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { existsSync, readFileSync } from 'node:fs';
const catalogPath = ['../../aliro-esp32/data/src/lib/i18n/ru.json', '../aliro-esp32/data/src/lib/i18n/ru.json']
    .map((path) => resolve(path))
    .find(existsSync)!;
const ru: Record<string, string> = JSON.parse(readFileSync(catalogPath, 'utf8'));

// These tests inspect settled layouts; disable the host page's smooth
// anchor scrolling so automatic click scrolling cannot fight the animation.
test.use({ reducedMotion: 'reduce' });

// Measure real text fragments: page overflow alone misses labels drawn across
// neighbouring controls inside a fixed-width card. Raw log data stays unchanged.
async function measure(page: Page | FrameLocator) {
    return page.locator('main').evaluate((main) => {
        const bounds = main.getBoundingClientRect();
        const visible = (el: Element) => {
            for (let p: Element | null = el; p && p !== main; p = p.parentElement) {
                const s = getComputedStyle(p);
                if (
                    s.display === 'none' ||
                    s.visibility === 'hidden' ||
                    s.contentVisibility === 'hidden' ||
                    s.opacity === '0'
                )
                    return false;
            }
            return el.getClientRects().length > 0;
        };
        const text: { el: Element; value: string; rect: DOMRect }[] = [];
        const walker = document.createTreeWalker(main, NodeFilter.SHOW_TEXT);
        for (let node = walker.nextNode(); node; node = walker.nextNode()) {
            const el = node.parentElement,
                value = node.textContent?.trim();
            if (!el || !value || !visible(el) || el.closest('option, .sr-only, .truncate, [class*="line-clamp-"]'))
                continue;
            const range = document.createRange();
            range.selectNodeContents(node);
            for (const rect of range.getClientRects()) {
                let clipped = false;
                for (
                    let ancestor = el.parentElement;
                    ancestor && ancestor !== main;
                    ancestor = ancestor.parentElement
                ) {
                    const style = getComputedStyle(ancestor),
                        box = ancestor.getBoundingClientRect();
                    if (
                        /hidden|clip|auto|scroll/.test(style.overflowY) &&
                        (rect.bottom <= box.top || rect.top >= box.bottom)
                    ) {
                        clipped = true;
                        break;
                    }
                }
                if (rect.width && rect.height && !clipped) text.push({ el, value: value.slice(0, 85), rect });
            }
        }
        const issues: string[] = [];
        if (document.documentElement.scrollWidth > innerWidth + 1)
            issues.push(`page overflow ${document.documentElement.scrollWidth}/${innerWidth}`);
        if (main.scrollWidth > main.clientWidth + 1)
            issues.push(
                `main overflow ${main.scrollWidth}/${main.clientWidth}: ${[...main.querySelectorAll('*')]
                    .filter((el) => visible(el) && el.getBoundingClientRect().right > bounds.right + 1)
                    .slice(0, 5)
                    .map((el) => el.tagName + '.' + el.classList[0] + ':' + el.textContent?.trim().slice(0, 30))
                    .join('; ')}`,
            );
        for (const item of text) {
            const r = item.rect;
            if (r.left < bounds.left - 1 || r.right > bounds.right + 1) issues.push(`text outside main: ${item.value}`);
            const box = item.el.closest('button, .tab, .label, legend, .badge');
            if (box && visible(box)) {
                const b = box.getBoundingClientRect();
                if (r.left < b.left - 1 || r.right > b.right + 1 || r.top < b.top - 2 || r.bottom > b.bottom + 2)
                    issues.push(`text outside ${box.tagName}.${box.classList[0]}: ${item.value}`);
            }
        }
        for (let i = 0; i < text.length; i++)
            for (let j = i + 1; j < text.length; j++) {
                const a = text[i],
                    b = text[j];
                if (a.el === b.el || a.el.contains(b.el) || b.el.contains(a.el)) continue;
                if (
                    Math.min(a.rect.right, b.rect.right) - Math.max(a.rect.left, b.rect.left) > 2 &&
                    Math.min(a.rect.bottom, b.rect.bottom) - Math.max(a.rect.top, b.rect.top) > 2
                )
                    issues.push(`overlapping text: ${a.value} / ${b.value}`);
            }
        return [...new Set(issues)].slice(0, 40);
    });
}

for (const lang of ['en', 'ru'] as const)
    for (const mode of ['light', 'dark']) {
        test(`${lang} ${mode}: every controller view fits`, async ({ page }, info) => {
            test.setTimeout(180_000);
            const widths =
                info.project.name === 'chromium-desktop'
                    ? [768, 1280]
                    : info.project.name === 'webkit-mobile'
                      ? [390, 430]
                      : [320, 360, 390, 430];
            for (const width of widths) {
                await page.setViewportSize({ width, height: 900 });
                await page.addInitScript(() => {
                    document.addEventListener('DOMContentLoaded', () => {
                        const style = document.createElement('style');
                        style.textContent =
                            '*,::before,::after{transition-duration:0s!important;animation-duration:0s!important}';
                        document.head.append(style);
                    });
                });
                await page.route('**/*', (route) =>
                    ['localhost', '127.0.0.1'].includes(new URL(route.request().url()).hostname)
                        ? route.continue()
                        : route.abort(),
                );
                const t = (value: string) => (lang === 'ru' ? ((ru as Record<string, string>)[value] ?? value) : value);
                const failures: { state: string; issues: string[] }[] = [];
                let states = 0;
                const audit = async (state: string) => {
                    await page.locator('main').waitFor();
                    // Finish finite Svelte/WAAPI disclosure transitions before measuring the
                    // settled view, just as Playwright does for screenshots with animations off.
                    await page.evaluate(() =>
                        document.getAnimations().forEach((animation) => {
                            if (Number.isFinite(Number(animation.effect?.getTiming().iterations))) animation.finish();
                        }),
                    );
                    // ResizeObserver-based virtual rows settle after layout and the next frame.
                    await page.evaluate(
                        () => new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve))),
                    );
                    const issues = await measure(page);
                    states++;
                    if (issues.length) {
                        failures.push({ state, issues });
                    }
                    if (process.env['ALIRO_LAYOUT_CAPTURE']) {
                        const dir = resolve(
                            process.env['ALIRO_LAYOUT_CAPTURE'],
                            `${info.project.name}-${lang}-${mode}-${width}`,
                        );
                        await mkdir(dir, { recursive: true });
                        await writeFile(resolve(dir, `${state}.json`), JSON.stringify({ state, issues }, null, 2));
                        if (width === 360 || issues.length)
                            await page
                                .locator('main')
                                .screenshot({ path: resolve(dir, `${state}.png`), animations: 'disabled' });
                    }
                };
                const open = async (route: string) => {
                    await page.goto(`/demo/index.html?lang=${lang}#${route}`);
                    await page.getByRole('combobox', { name: t('Theme'), exact: true }).selectOption(mode);
                };
                const tab = async (name: string) => page.getByRole('button', { name: t(name), exact: true }).click();
                const toggle = async (name: string) =>
                    page
                        .getByText(t(name), { exact: true })
                        .locator('..')
                        .locator('..')
                        .locator('input[type="checkbox"]')
                        .setChecked(true);
                await open('/');
                await audit('overview');
                for (const scenario of ['revoked', 'unknown']) {
                    await page.getByRole('combobox', { name: t('Example key'), exact: true }).selectOption(scenario);
                    await tab('Tap the phone');
                    await audit(`overview-${scenario}-key`);
                }
                await page.locator('input[name="info-collapse-0"]').check();
                await audit('overview-issuer');
                await open('/mqtt');
                await audit('mqtt-broker');
                await tab('Topics');
                await audit('mqtt-topics');
                await toggle('Custom Lock States');
                await audit('mqtt-custom-states');
                await tab('SSL/TLS');
                await toggle('Enable SSL/TLS');
                await audit('mqtt-tls-certificates');
                await page
                    .locator('#cert-file-input')
                    .setInputFiles({
                        name: 'demo-client-certificate-with-a-very-long-readable-filename.pem',
                        mimeType: 'text/plain',
                        buffer: Buffer.from('SYNTHETIC TEST FILE; NOT A CERTIFICATE'),
                    });
                await audit('mqtt-selected-certificate');
                await open('/actions');
                await audit('actions-pixel');
                for (const name of [
                    'simple-gpio-nfc--collapse',
                    'hk-auth-collapse',
                    '2nd-action-collapse',
                    'tag-event-collapse',
                ])
                    await page.locator(`input[name="${name}"]`).check();
                await audit('actions-gpio');
                await tab('State Triggers');
                await page.locator('input[name="state-collapse"]').check();
                await audit('actions-state');
                await open('/misc');
                await audit('system-homekit');
                await tab('Hardware');
                await audit('system-hardware');
                await page.locator('#nfcPreset').selectOption('255');
                await toggle('Enable Ethernet');
                await page.locator('#ethPreset').selectOption('255');
                await audit('system-ethernet');
                await tab('Security');
                await audit('system-security');
                await toggle('Enable Web Authentication');
                await audit('system-login-fields');
                await toggle('Enable HTTPS');
                await audit('system-https-certificates');
                await open('/ota');
                await audit('ota');
                await page
                    .locator('input[type="file"]')
                    .first()
                    .setInputFiles({
                        name: 'aliro-mayday-controller-preview-with-a-long-filename.bin',
                        mimeType: 'application/octet-stream',
                        buffer: Buffer.from([1, 2, 3]),
                    });
                await audit('ota-selected-file');
                await tab('Upload Firmware');
                await audit('ota-demo-error');
                await open('/logs');
                await audit('logs');
                if (width < 640) await tab('Toggle filters');
                await page.locator('[role="log"] button').first().click();
                await audit('logs-expanded');
                await open('/captive-portal');
                await audit('setup-wifi');
                await tab('Scan for networks');
                await audit('setup-scan');
                await tab('Hardware');
                await audit('setup-hardware');
                await info.attach('layout-report', {
                    body: JSON.stringify({ lang, mode, width, states, failures }, null, 2),
                    contentType: 'application/json',
                });
                expect.soft(failures, `${width}px: ${JSON.stringify(failures, null, 2)}`).toEqual([]);
            }
        });
    }

for (const lang of ['en', 'ru'] as const)
    for (const mode of ['light', 'dark']) {
        test(`embedded ${lang} ${mode}: controller sections fit the homepage`, async ({ page }, info) => {
            test.setTimeout(180_000);
            const t = (value: string) => (lang === 'ru' ? (ru[value] ?? value) : value);
            const widths =
                info.project.name === 'chromium-desktop'
                    ? [768, 1280]
                    : info.project.name === 'webkit-mobile'
                      ? [390]
                      : [320, 360, 390, 430];
            for (const width of widths) {
                await page.setViewportSize({ width, height: 800 });
                await page.goto(`/?lang=${lang}&theme=${mode}#controller-ui`);
                const panel = page.frameLocator('#controller-ui iframe');
                // The host section heading can be in view while most of the
                // iframe is still below the fold. Reveal its own viewport before
                // opening the fixed mobile drawer inside it.
                await page.locator('#controller-ui iframe').scrollIntoViewIfNeeded();
                await expect(panel.locator('html')).toHaveAttribute('lang', lang);
                await expect(panel.locator('html')).toHaveAttribute(
                    'data-theme',
                    mode === 'dark' ? 'dracula' : 'autumn',
                );
                const errors: { state: string; issues: string[] }[] = [];
                const audit = async (state: string) => {
                    await panel.locator('main').evaluate(async () => {
                        for (const animation of document.getAnimations())
                            if (Number.isFinite(Number(animation.effect?.getTiming().iterations))) animation.finish();
                        await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
                    });
                    const issues = await measure(panel);
                    if (issues.length) errors.push({ state, issues });
                    if (process.env['ALIRO_LAYOUT_CAPTURE']) {
                        const dir = resolve(
                            process.env['ALIRO_LAYOUT_CAPTURE'],
                            `embedded-${info.project.name}-${lang}-${mode}-${width}`,
                        );
                        await mkdir(dir, { recursive: true });
                        await writeFile(resolve(dir, `${state}.json`), JSON.stringify({ state, issues }, null, 2));
                        if (issues.length)
                            await panel
                                .locator('main')
                                .screenshot({ path: resolve(dir, `${state}.png`), animations: 'disabled' });
                    }
                };
                const navigate = async (name: string) => {
                    const menu = panel.locator('label.drawer-button[for="main-content-drawer"]');
                    if (await menu.isVisible()) await menu.click();
                    await panel
                        .getByRole('navigation')
                        .getByRole('link', { name: t(name), exact: true })
                        .click();
                };
                await audit('overview');
                const navbar = panel.locator('.navbar');
                if (await navbar.isVisible())
                    expect(await navbar.evaluate((el) => el.scrollWidth <= el.clientWidth + 1)).toBe(true);
                await navigate('MQTT');
                await audit('mqtt');
                await panel.getByRole('button', { name: t('Topics'), exact: true }).click();
                await audit('mqtt-topics');
                await panel.getByRole('button', { name: 'SSL/TLS', exact: true }).click();
                await audit('mqtt-tls');
                await navigate('Actions');
                await audit('actions');
                await panel.getByRole('button', { name: t('State Triggers'), exact: true }).click();
                await audit('actions-state');
                await navigate('System');
                await audit('system');
                await panel.getByRole('button', { name: t('Hardware'), exact: true }).click();
                await audit('hardware');
                await panel.getByRole('button', { name: t('Security'), exact: true }).click();
                await audit('security');
                await navigate('OTA Update');
                await audit('ota');
                await navigate('Logs');
                await audit('logs');
                const filters = panel.getByRole('button', { name: t('Toggle filters'), exact: true });
                if (await filters.isVisible()) await filters.click();
                await panel.locator('[role="log"] button').first().click();
                await audit('logs-expanded');
                await info.attach(`embedded-layout-${width}`, {
                    body: JSON.stringify({ lang, mode, width, errors }, null, 2),
                    contentType: 'application/json',
                });
                expect.soft(errors, `${width}px embedded: ${JSON.stringify(errors, null, 2)}`).toEqual([]);
            }
        });
    }
