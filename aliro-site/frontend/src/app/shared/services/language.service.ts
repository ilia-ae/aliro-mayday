import { Injectable, inject, signal } from '@angular/core';
import { TranslateService } from '@ngx-translate/core';
import { firstValueFrom } from 'rxjs';
import { MetaService } from './meta.service';

export type Lang = 'ru' | 'en';

const STORAGE_KEY = 'aliro-lang';
const SUPPORTED: Lang[] = ['ru', 'en'];

@Injectable({ providedIn: 'root' })
export class LanguageService {
    private readonly translate = inject(TranslateService);
    private readonly metaService = inject(MetaService);
    readonly lang = signal<Lang>('ru');

    // Called from an APP_INITIALIZER — resolves AFTER the initial JSON has
    // loaded, so the first frame Angular renders already has real strings
    // (no flash of raw translation keys).
    async init(): Promise<void> {
        this.translate.addLangs(SUPPORTED);
        const initial = this.readSaved() ?? this.detectFromBrowser();
        await this.use(initial);
    }

    async toggle(): Promise<void> {
        await this.use(this.lang() === 'ru' ? 'en' : 'ru');
    }

    async use(lang: Lang): Promise<void> {
        this.lang.set(lang);
        document.documentElement.lang = lang;
        const url = new URL(location.href);
        if (url.searchParams.has('lang')) {
            url.searchParams.set('lang', lang);
            history.replaceState(history.state, '', url);
        }
        try {
            localStorage.setItem(STORAGE_KEY, lang);
        } catch {
            // localStorage may be blocked (private mode, some embeddings)
        }
        try {
            await firstValueFrom(this.translate.use(lang));
        } catch {
            // Fetching the JSON failed — ngx-translate falls back to keys.
            // Don't block the UI; user can retry by toggling.
        }
        await this.metaService.syncForCurrentLang();
    }

    private readSaved(): Lang | null {
        try {
            const v = localStorage.getItem(STORAGE_KEY);
            if (v === 'ru' || v === 'en') return v;
        } catch {
            // ignore
        }
        return null;
    }

    private detectFromBrowser(): Lang {
        const code = (navigator.language || 'ru').slice(0, 2).toLowerCase();
        return code === 'en' ? 'en' : 'ru';
    }
}
