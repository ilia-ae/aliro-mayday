import { defineConfig, devices } from '@playwright/test';

/* Smoke + content suite for the aliro.mayday landing.
   Boots the production build (`ng build` → http-server) so the test
   target matches what Cloudflare actually serves, not the dev-mode
   Vite middleware. */
export default defineConfig({
    testDir: './e2e',
    fullyParallel: true,
    forbidOnly: !!process.env.CI,
    retries: process.env.CI ? 2 : 0,
    workers: process.env.CI ? 1 : undefined,
    reporter: [['list']],
    use: {
        baseURL: 'http://localhost:4444',
        trace: 'on-first-retry',
        screenshot: 'only-on-failure'
    },
    projects: [
        {
            name: 'chromium-desktop',
            use: { ...devices['Desktop Chrome'] }
        },
        {
            name: 'chromium-mobile',
            use: { ...devices['Pixel 7'] }
        }
    ],
    webServer: {
        command:
            'npm run build && npx http-server dist/aliro-mayday-software/browser -p 4444 -s --proxy http://localhost:4444? -c-1',
        url: 'http://localhost:4444',
        reuseExistingServer: !process.env.CI,
        timeout: 180_000
    }
});
