import { expect, test } from '@playwright/test';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';

test('an open cached page updates without clearing browser data', async ({ page }) => {
    test.setTimeout(90_000);
    const directory = resolve('dist/aliro-mayday-software/browser');
    const manifest = JSON.parse(await readFile(resolve(directory, 'ngsw.json'), 'utf8'));
    const current = new Map<string, Buffer>();
    const previous = new Map<string, Buffer>();
    for (const language of ['en', 'ru']) {
        const path = `/i18n/${language}.json`;
        const bytes = await readFile(resolve(directory, path.slice(1)));
        current.set(path, bytes);
        const translations = JSON.parse(bytes.toString());
        translations.source.title = 'Previous cached release';
        previous.set(path, Buffer.from(JSON.stringify(translations)));
    }
    let updated = false;
    const server = createServer(async (request, response) => {
        try {
            const path = new URL(request.url!, 'http://localhost').pathname;
            let bytes: Buffer;
            let type = 'application/json';
            if (path === '/ngsw.json') {
                const version = structuredClone(manifest);
                version.appData = { revision: updated ? 'current' : 'previous' };
                for (const [url, contents] of updated ? current : previous) {
                    version.hashTable[url] = createHash('sha1').update(contents).digest('hex');
                }
                bytes = Buffer.from(JSON.stringify(version));
            } else if (previous.has(path)) {
                bytes = (updated ? current : previous).get(path)!;
            } else {
                const filename = path === '/' ? '/index.html' : path;
                const target = resolve(directory, `.${filename}`);
                if (!target.startsWith(`${directory}/`)) throw new Error('Invalid path');
                bytes = await readFile(target);
                if (filename.endsWith('.js')) type = 'text/javascript';
                else if (filename.endsWith('.css')) type = 'text/css';
                else if (filename.endsWith('.html')) type = 'text/html';
                else if (filename.endsWith('.svg')) type = 'image/svg+xml';
                else if (filename.endsWith('.png')) type = 'image/png';
            }
            response.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
            response.end(bytes);
        } catch {
            response.writeHead(404);
            response.end();
        }
    });
    await new Promise<void>((done) => server.listen(0, '127.0.0.1', done));
    const address = server.address();
    if (!address || typeof address === 'string') throw new Error('Missing test server');
    const url = `http://127.0.0.1:${address.port}/?lang=ru#source`;
    try {
        await page.goto(url);
        await expect(page.locator('#source h2')).toHaveText('Previous cached release');
        await expect.poll(
            () => page.evaluate(() => Boolean(navigator.serviceWorker.controller)),
            { timeout: 45_000 }
        ).toBe(true);
        await page.reload();
        await expect(page.locator('#source h2')).toHaveText('Previous cached release');

        updated = true;
        await page.evaluate(() => window.dispatchEvent(new Event('focus')));
        await expect(page.locator('#source h2')).toHaveText('Исходники уже опубликованы', {
            timeout: 25_000
        });
        await expect(page).toHaveURL(url);
        await expect(page.locator('html')).toHaveAttribute('lang', 'ru');
        await expect(page.locator('#source a[href="https://github.com/ilia-ae/aliro-mayday"]')).toBeVisible();
        await expect(page.locator('#source [role="timer"]')).toHaveCount(0);
    } finally {
        server.closeAllConnections();
        await new Promise<void>((done) => server.close(() => done()));
    }
});
