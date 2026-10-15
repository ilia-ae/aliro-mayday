import { provideHttpClient, withFetch } from '@angular/common/http';
import {
    ApplicationConfig,
    inject,
    isDevMode,
    provideAppInitializer,
    provideZonelessChangeDetection
} from '@angular/core';
import { provideRouter, withInMemoryScrolling } from '@angular/router';
import { provideServiceWorker, SwUpdate } from '@angular/service-worker';
import { provideTranslateService } from '@ngx-translate/core';
import { provideTranslateHttpLoader } from '@ngx-translate/http-loader';
import { appRoutes } from './app.routes';
import { LanguageService } from './app/shared/services/language.service';
import { SITE_CONTENT_VERSION } from './content-version';
import { watchForSiteUpdates } from './site-updates';

// Generated from both translation files before each build.

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
            suffix: `.json?v=${SITE_CONTENT_VERSION}`
        }),
        // Block first paint until initial translations are loaded so the
        // user never sees raw keys like "hero.title" before real strings.
        provideAppInitializer(() => inject(LanguageService).init()),
        provideServiceWorker('ngsw-worker.js', {
            enabled: !isDevMode(),
            registrationStrategy: 'registerWhenStable:30000'
        }),
        provideAppInitializer(() => watchForSiteUpdates(inject(SwUpdate)))
    ]
};
