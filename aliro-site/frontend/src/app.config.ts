import { provideHttpClient, withFetch } from '@angular/common/http';
import {
    ApplicationConfig,
    inject,
    isDevMode,
    provideAppInitializer,
    provideZonelessChangeDetection
} from '@angular/core';
import { provideRouter, withInMemoryScrolling } from '@angular/router';
import { provideServiceWorker } from '@angular/service-worker';
import { provideTranslateService } from '@ngx-translate/core';
import { provideTranslateHttpLoader } from '@ngx-translate/http-loader';
import { appRoutes } from './app.routes';
import { LanguageService } from './app/shared/services/language.service';

// Bump when i18n JSON content changes — busts CDN/HTTP caches on first fetch.
// Angular SW invalidates separately via ngsw.json hashes; this is for
// pre-SW and out-of-SW users (dev mode, private browsing, force refresh).
const I18N_VERSION = '2026-05-01.3';

export const appConfig: ApplicationConfig = {
    providers: [
        provideRouter(
            appRoutes,
            withInMemoryScrolling({
                anchorScrolling: 'enabled',
                scrollPositionRestoration: 'enabled'
            })
        ),
        provideHttpClient(withFetch()),
        provideZonelessChangeDetection(),
        provideTranslateService({
            // ngx-translate v17 renamed `defaultLanguage` → `fallbackLang`.
            fallbackLang: 'ru'
        }),
        provideTranslateHttpLoader({
            prefix: '/i18n/',
            suffix: `.json?v=${I18N_VERSION}`
        }),
        // Block first paint until initial translations are loaded so the
        // user never sees raw keys like "hero.title" before real strings.
        provideAppInitializer(() => inject(LanguageService).init()),
        provideServiceWorker('ngsw-worker.js', {
            enabled: !isDevMode(),
            registrationStrategy: 'registerWhenStable:30000'
        })
    ]
};
