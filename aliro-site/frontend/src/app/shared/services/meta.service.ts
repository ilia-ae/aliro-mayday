import { DOCUMENT } from '@angular/common';
import { Injectable, inject } from '@angular/core';
import { Meta, Title } from '@angular/platform-browser';
import { NavigationEnd, Router } from '@angular/router';
import { TranslateService } from '@ngx-translate/core';
import { filter, firstValueFrom } from 'rxjs';

// Keeps <title>, meta description, OG equivalents, and Twitter equivalents
// in sync with the active language AND the current route. Called after
// every language change; also re-applies on every NavigationEnd so a route
// switch (e.g. landing → /privacy) refreshes the meta accordingly.
@Injectable({ providedIn: 'root' })
export class MetaService {
    private readonly title = inject(Title);
    private readonly meta = inject(Meta);
    private readonly translate = inject(TranslateService);
    private readonly document = inject(DOCUMENT);
    private readonly router = inject(Router);

    private wired = false;

    async syncForCurrentLang(): Promise<void> {
        if (!this.wired) {
            this.router.events
                .pipe(filter((e) => e instanceof NavigationEnd))
                .subscribe(() => {
                    void this.applyMeta();
                });
            this.wired = true;
        }
        await this.applyMeta();
    }

    private async applyMeta(): Promise<void> {
        const url = this.router.url;
        const keys = this.pageKeys(url);
        const isSubpage = keys.title !== 'meta.title';

        const [pageTitle, pageDescription] = await Promise.all([
            firstValueFrom(this.translate.get(keys.title)),
            firstValueFrom(this.translate.get(keys.description))
        ]);

        // Sub-pages get a "title · brand" composite so the brand is always
        // visible in tab titles and search snippets without needing a
        // dedicated meta_title key per page.
        const fullTitle = isSubpage ? `${pageTitle} · aliro.mayday` : pageTitle;

        this.title.setTitle(fullTitle);
        this.meta.updateTag({ name: 'description', content: pageDescription });
        this.meta.updateTag({ property: 'og:title', content: fullTitle });
        this.meta.updateTag({
            property: 'og:description',
            content: pageDescription
        });
        this.meta.updateTag({
            property: 'og:image:alt',
            content: fullTitle
        });
        this.meta.updateTag({ name: 'twitter:title', content: fullTitle });
        this.meta.updateTag({
            name: 'twitter:description',
            content: pageDescription
        });

        // og:locale reflects the current language. Static HTML uses ru_RU as
        // default for non-JS crawlers; this updates it for JS-capable clients.
        // ngx-translate v17 replaced the currentLang/defaultLang properties
        // with getter methods.
        const current =
            this.translate.getCurrentLang() ?? this.translate.getFallbackLang();
        this.meta.updateTag({
            property: 'og:locale',
            content: current === 'en' ? 'en_US' : 'ru_RU'
        });
    }

    /* Route → translation-key map. Add an entry here when introducing a
       new page that needs its own title/description. The default falls
       back to the homepage meta keys. */
    private pageKeys(url: string): { title: string; description: string } {
        if (url.startsWith('/privacy')) {
            return {
                title: 'privacy_page.title',
                description: 'privacy_page.lede'
            };
        }
        return { title: 'meta.title', description: 'meta.description' };
    }
}
