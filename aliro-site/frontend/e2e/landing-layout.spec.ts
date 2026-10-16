import { expect, test } from '@playwright/test';

for (const lang of ['en', 'ru']) {
    for (const theme of ['light', 'dark']) {
        test(`every landing section stays readable across widths: ${lang}, ${theme}`, async ({ page }) => {
            for (const width of [360, 768, 1024, 1440, 1920]) {
                await page.setViewportSize({ width, height: 900 });
                await page.goto(`/?lang=${lang}&theme=${theme}#source`);
                await expect(page.locator('html')).toHaveAttribute('lang', lang);
                const overflow = await page.evaluate(() => {
                    return [...document.querySelectorAll('main section p, main section h2, main section h3, main section a, main section button, main section article, header.topbar a, header.topbar button')]
                        .filter(el => !el.closest('.table-scroll, .mob-overlay'))
                        .filter(el => {
                            const style = getComputedStyle(el);
                            return style.display !== 'none' && style.visibility !== 'hidden' && el.getClientRects().length;
                        })
                        .filter(el => [...el.getClientRects()].some(rect => rect.width && (rect.left < -1 || rect.right > innerWidth + 1)))
                        .map(el => `${el.tagName} ${(el.textContent || '').trim().slice(0, 70)}`);
                });
                expect(overflow, `${width}px ${lang} ${theme}`).toEqual([]);
                expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
                const gap = await page.locator('#source').evaluate(el => {
                    const buttons = el.querySelector('.source-links')!.getBoundingClientRect();
                    const licensing = el.querySelector('.source-license')!.getBoundingClientRect();
                    const note = el.querySelector('.source-license p')!.getBoundingClientRect();
                    const links = el.querySelector('.source-legal-links')!.getBoundingClientRect();
                    return { beforeLicense: licensing.top - buttons.bottom, afterNote: links.top - note.bottom };
                });
                expect(gap.beforeLicense).toBeGreaterThanOrEqual(24);
                expect(gap.afterNote).toBeGreaterThanOrEqual(12);
            }
        });
    }

    test(`embedded panel, source evidence and contact paths work in ${lang}`, async ({ page, request }) => {
        await page.goto(`/?lang=${lang}#controller-ui`);
        const frame = page.frameLocator('#controller-ui iframe');
        await expect(frame.getByRole('heading', { name: lang === 'ru' ? 'Обзор' : 'Dashboard', exact: true })).toBeVisible();
        await expect(frame.getByText('Demo reader', { exact: true })).toBeVisible();
        const iframe = page.locator('#controller-ui iframe');
        await expect(iframe).toHaveAttribute('src', `/demo/index.html?lang=${lang}#/`);
        if ((page.viewportSize()?.width || 1280) < 1024) await frame.locator('label.drawer-button[for="main-content-drawer"]').click();
        await frame.getByRole('navigation').getByRole('link', { name: 'MQTT', exact: true }).click();
        await expect(frame.getByRole('heading', { name: lang === 'ru' ? /^Настройки MQTT/ : /^MQTT Settings/ })).toBeVisible();
        await expect(frame.getByPlaceholder('192.168.1.100')).toHaveValue('mqtt.example.invalid');
        const evidence = page.locator('#evidence');
        await evidence.scrollIntoViewIfNeeded();
        await expect(evidence.locator('.deposit-visual img')).toHaveJSProperty('complete', true);
        await expect(evidence.locator('.deposit-visual img')).toHaveJSProperty('naturalWidth', 1000);
        await expect(evidence.locator('a[href="https://deposit.mayday.software/"]')).toBeVisible();
        const image = await request.get(`/images/aliro-deposit-summary-${lang}.svg`);
        const xml = await image.text();
        expect(xml).toContain('24e218e');
        expect(xml).toContain('9f51d5b701905d34c7a22910c80c2950');
        expect(xml).toContain('Mayday / Ilia Arestov');
        await expect(evidence).toContainText('Bitcoin');
        await expect(evidence).toContainText('DigiCert');
        const contact = page.locator('#contact');
        await expect(contact.locator('a[href="https://github.com/ilia-ae/aliro-mayday/issues"]')).toBeVisible();
        await expect(contact).not.toContainText(/24 hours|суток/);
    });

    test(`direct deposit link keeps its heading below the sticky header in ${lang}`, async ({ page }) => {
        await page.emulateMedia({ reducedMotion: 'reduce' });
        await page.goto(`/?lang=${lang}#evidence`);
        await expect(page.locator('#evidence h3')).toBeInViewport();
        await expect.poll(() => page.evaluate(() => {
            const heading = document.querySelector('#evidence h3')!.getBoundingClientRect();
            const header = document.querySelector('.topbar')!.getBoundingClientRect();
            return heading.top - header.bottom;
        })).toBeGreaterThanOrEqual(16);
    });
}

test('tap animation is keyboard controlled, progresses to open, and resets', async ({ page }) => {
    await page.goto('/?lang=en#how-it-works');
    const animation = page.locator('app-access-demo');
    const button = animation.getByRole('button');
    await expect(animation.locator('.access-demo')).toHaveAttribute('aria-busy', 'false');
    await expect(button).toBeEnabled();
    await button.focus();
    await expect(button).toBeFocused();
    await page.keyboard.press('Enter');
    await expect(animation.getByRole('status')).toContainText('Bring the phone');
    await expect(animation.getByRole('status')).toContainText('Check the key');
    await expect(animation.getByRole('status')).toContainText('Access granted');
    await expect(animation.getByRole('status')).toContainText('The door opens');
    await button.click();
    await expect(animation.getByRole('status')).toContainText('The lock is closed');
});

test('reduced motion shows the result immediately without animated transforms', async ({ page }) => {
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await page.goto('/?lang=ru#how-it-works');
    const animation = page.locator('app-access-demo');
    await animation.getByRole('button').click();
    await expect(animation.getByRole('status')).toContainText('Дверь открывается');
    expect(await animation.locator('.door').evaluate(el => getComputedStyle(el).transitionDuration)).toBe('0s');
    await animation.getByRole('button').click();
    await expect(animation.getByRole('status')).toContainText('Замок закрыт');
});
