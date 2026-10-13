import { Component, computed, inject } from '@angular/core';
import { TranslatePipe } from '@ngx-translate/core';
import { LanguageService } from '@/app/shared/services/language.service';
import { ThemeService } from '@/app/shared/services/theme.service';

@Component({
    selector: 'app-privacy',
    standalone: true,
    imports: [TranslatePipe],
    template: `
        <a class="skip-link" href="#main-content">
            {{ 'nav.skip_to_content' | translate }}
        </a>

        <header class="topbar" role="banner">
            <div class="wrap">
                <a class="brand" href="/" aria-label="aliro.mayday">
                    <svg
                        class="brand-mark"
                        viewBox="0 0 64 64"
                        width="26"
                        height="26"
                        aria-hidden="true"
                    >
                        <rect width="64" height="64" rx="14" fill="var(--accent)" />
                        <path
                            d="M18 48 L32 16 L46 48 M23.5 38 L40.5 38"
                            fill="none"
                            stroke="#ffffff"
                            stroke-width="5"
                            stroke-linecap="round"
                            stroke-linejoin="round"
                        />
                    </svg>
                    <span class="brand-text"
                        >aliro<span class="brand-dot">.</span>mayday</span
                    >
                </a>
                <nav class="actions">
                    <button
                        type="button"
                        class="ghost"
                        (click)="language.toggle()"
                        [attr.aria-label]="'nav.lang_aria_label' | translate"
                    >
                        {{ 'nav.lang_other' | translate }}
                    </button>
                    <button
                        type="button"
                        class="ghost icon"
                        (click)="theme.cycle()"
                        [attr.aria-label]="themeAriaLabel() | translate"
                        [title]="themeAriaLabel() | translate"
                    >
                        @switch (theme.theme()) {
                            @case ('light') {
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                    <circle cx="12" cy="12" r="4"/>
                                    <path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/>
                                </svg>
                            }
                            @case ('dark') {
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                    <path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8Z"/>
                                </svg>
                            }
                            @default {
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                    <circle cx="12" cy="12" r="9"/>
                                    <path d="M12 3a9 9 0 0 1 0 18z" fill="currentColor" stroke="none"/>
                                </svg>
                            }
                        }
                    </button>
                </nav>
            </div>
        </header>

        <main id="main-content" class="privacy-page">
            <div class="wrap">
                <h1>{{ 'privacy_page.title' | translate }}</h1>
                <p class="subtitle">{{ 'privacy_page.subtitle' | translate }}</p>
                <p class="lede">{{ 'privacy_page.lede' | translate }}</p>

                @for (key of sections; track key) {
                    <section>
                        <h2>
                            {{
                                'privacy_page.sections.' + key + '.title' | translate
                            }}
                        </h2>
                        <p>
                            {{
                                'privacy_page.sections.' + key + '.body' | translate
                            }}
                        </p>
                    </section>
                }

                <p class="contact-line">
                    {{ 'privacy_page.contact' | translate }}
                    <a href="mailto:aliro.mayday&#64;ilia.ae"
                        >aliro.mayday&#64;ilia.ae</a
                    >
                </p>
                <p class="back">
                    <a href="/">{{ 'privacy_page.back_link' | translate }}</a>
                </p>
            </div>
        </main>
    `,
    styles: [
        `
            :host {
                display: block;
                --section-y: 4rem;
                --wrap-max: 56rem;
                --wrap-x: 1.5rem;
                --topbar-h: 4rem;
                --pill-radius: 0.5rem;
                --btn-radius: 0.5rem;
            }
            .wrap {
                max-width: var(--wrap-max);
                margin: 0 auto;
                padding: 0 var(--wrap-x);
            }
            .skip-link {
                position: absolute;
                top: 0.5rem;
                left: 0.5rem;
                transform: translateY(-200%);
                z-index: 100;
                background: var(--accent);
                color: var(--accent-ink);
                padding: 0.75rem 1rem;
                border-radius: var(--btn-radius);
                font-weight: 600;
                text-decoration: none;
                transition: transform 150ms ease;
            }
            .skip-link:focus-visible {
                transform: translateY(0);
            }

            /* Reduced topbar — same brand mark + toggles, no jump-nav. */
            .topbar {
                position: sticky;
                top: 0;
                z-index: 50;
                background: color-mix(in srgb, var(--bg) 92%, transparent);
                backdrop-filter: blur(10px);
                -webkit-backdrop-filter: blur(10px);
                border-bottom: 1px solid var(--border);
                height: var(--topbar-h);
            }
            .topbar .wrap {
                height: 100%;
                display: flex;
                align-items: center;
                justify-content: space-between;
                gap: 1rem;
            }
            .brand {
                display: inline-flex;
                align-items: center;
                gap: 0.75rem;
                color: var(--fg);
                text-decoration: none;
                font-weight: 600;
            }
            .brand-text {
                font-size: 1rem;
                letter-spacing: -0.01em;
                color: var(--fg);
            }
            .brand-dot {
                color: var(--accent);
            }
            .actions {
                display: flex;
                gap: 0.5rem;
            }
            .ghost {
                background: transparent;
                border: 1px solid var(--border);
                color: var(--fg-muted);
                padding: 0.5rem 0.75rem;
                border-radius: var(--pill-radius);
                font-size: 0.8125rem;
                font-weight: 500;
                cursor: pointer;
                transition:
                    color 120ms ease,
                    border-color 120ms ease;
            }
            .ghost:hover {
                color: var(--fg);
                border-color: var(--border-strong);
            }
            .ghost.icon {
                padding: 0.5rem;
                display: inline-flex;
                align-items: center;
                justify-content: center;
            }

            .privacy-page {
                padding: var(--section-y) 0 6rem;
            }
            h1 {
                font-family: var(--font-display);
                font-weight: 600;
                letter-spacing: -0.02em;
                font-size: clamp(2rem, 4vw, 2.75rem);
                line-height: 1.1;
                color: var(--fg);
                margin: 0 0 0.5rem;
            }
            .subtitle {
                font-family:
                    ui-monospace, SFMono-Regular, Menlo, 'JetBrains Mono', monospace;
                font-size: 0.8125rem;
                color: var(--fg-dim);
                letter-spacing: 0.04em;
                margin: 0 0 1.5rem;
            }
            .lede {
                font-size: 1.0625rem;
                line-height: 1.6;
                color: var(--fg-muted);
                max-width: 50rem;
                margin: 0 0 2.5rem;
            }
            section {
                margin-bottom: 2rem;
            }
            h2 {
                font-family: var(--font-display);
                font-weight: 600;
                font-size: 1.25rem;
                color: var(--fg);
                margin: 0 0 0.5rem;
                letter-spacing: -0.005em;
            }
            /* :where() keeps specificity at 0 so .contact-line / .back
               can override without needing extra weight. Same
               ng-encap fix as on the landing page. */
            section :where(p) {
                color: var(--fg-muted);
                line-height: 1.6;
                margin: 0;
                max-width: 50rem;
            }
            .contact-line {
                margin: 2.5rem 0 1rem;
                color: var(--fg-muted);
                font-size: 0.9375rem;
            }
            .contact-line a {
                color: var(--accent);
                font-weight: 600;
                text-decoration: none;
            }
            .contact-line a:hover {
                color: var(--accent-hover);
            }
            .back {
                margin: 0;
                font-size: 0.9375rem;
            }
            .back a {
                color: var(--fg-dim);
                text-decoration: none;
            }
            .back a:hover {
                color: var(--accent);
            }

            /* Mobile: shrink the privacy-page rhythm in lockstep with
               the landing's mobile pass — section padding, lede, and
               section gap all reduce ~25-40%. */
            @media (max-width: 48rem) {
                :host {
                    --section-y: 2.5rem;
                }
                .privacy-page {
                    padding: var(--section-y) 0 4rem;
                }
                .lede {
                    margin-bottom: 1.75rem;
                }
                section {
                    margin-bottom: 1.5rem;
                }
                .contact-line {
                    margin-top: 1.75rem;
                }
            }
        `
    ]
})
export class PrivacyPage {
    readonly language = inject(LanguageService);
    readonly theme = inject(ThemeService);

    readonly themeAriaLabel = computed(() => {
        const t = this.theme.theme();
        if (t === 'auto') return 'nav.theme_switch_to_light';
        if (t === 'light') return 'nav.theme_switch_to_dark';
        return 'nav.theme_switch_to_auto';
    });

    readonly sections: ReadonlyArray<string> = [
        'data',
        'local',
        'email',
        'third_parties',
        'controller'
    ];
}
