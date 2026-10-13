import { expect, test } from '@playwright/test';

/* Visual + computed-style audit. Captures full-page screenshots
   AND dumps computed margins/font-sizes/colors of every "at risk"
   element class so a class-vs-element specificity regression
   shows up as a number that doesn't match the CSS.
   Run: npx playwright test e2e/screenshot.spec.ts --project=chromium-desktop */
test.describe('visual', () => {
    test.use({ viewport: { width: 1440, height: 900 } });

    test('about + stack: computed margins match CSS', async ({ page }) => {
        await page.goto('/?lang=ru&theme=dark');
        await page.waitForLoadState('networkidle');

        // Scroll through every section so lazy content paints.
        for (const id of ['about', 'integrations', 'stack', 'comparison', 'competitors', 'roadmap', 'source', 'changelog', 'contact']) {
            await page.locator(`#${id}`).scrollIntoViewIfNeeded();
            await page.waitForTimeout(80);
        }
        await page.locator('#hero').scrollIntoViewIfNeeded();
        await page.waitForTimeout(200);

        // Capture #about as a screenshot for eyeballing.
        await page
            .locator('#about')
            .screenshot({ path: 'test-results/_about.png' });

        // Audit every at-risk descendant — anything where a class-based
        // override might silently lose to a "section <element>" rule.
        const audits: Record<string, Array<Record<string, string>>> = {
            'sub-titles in #about': [],
            'sub-titles in #stack': [],
            'card paragraphs (#why)': [],
            'card paragraphs (#integrations)': [],
            'tech-grid paragraphs': [],
            'changelog paragraphs': [],
            'faq paragraphs': []
        };

        const collect = async (selector: string, key: string) => {
            const els = await page.locator(selector).all();
            for (const el of els) {
                audits[key].push(
                    await el.evaluate((node) => {
                        const cs = getComputedStyle(node);
                        return {
                            text: (node.textContent ?? '').trim().slice(0, 50),
                            marginTop: cs.marginTop,
                            marginBottom: cs.marginBottom,
                            fontSize: cs.fontSize,
                            color: cs.color
                        };
                    })
                );
            }
        };

        await collect('#about .sub-title', 'sub-titles in #about');
        await collect('#stack .sub-title', 'sub-titles in #stack');
        await collect('#why article p', 'card paragraphs (#why)');
        await collect('#integrations article p', 'card paragraphs (#integrations)');
        await collect('#about .tech-grid article p', 'tech-grid paragraphs');
        await collect('#changelog li p', 'changelog paragraphs');
        await collect('#faq details p', 'faq paragraphs');

        for (const [k, items] of Object.entries(audits)) {
            console.log(`\n=== ${k} (${items.length}) ===`);
            for (const m of items) console.log('  ' + JSON.stringify(m));
        }

        // Hard expectations — if any of these regress, the test fails.
        // sub-title font-size MUST be the .sub-title 1.25rem (20px), not
        // the section h3 default 1.0625rem (17px).
        for (const m of audits['sub-titles in #about']) {
            expect(m.fontSize, `sub-title fontSize for "${m.text}"`).toBe('20px');
        }
        // Non-first sub-titles should have margin-top 48px (3rem).
        const nonFirst = audits['sub-titles in #about'].slice(1);
        for (const m of nonFirst) {
            expect(m.marginTop, `sub-title marginTop for "${m.text}"`).toBe(
                '48px'
            );
        }
    });
});
