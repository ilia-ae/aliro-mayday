// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../../LICENSE.
import { Component, computed, inject } from '@angular/core';
import { TranslatePipe } from '@ngx-translate/core';
import { LanguageService } from '@/app/shared/services/language.service';

type ShoppingPart = { key: string; href: string; extraHref?: string };

@Component({
    selector: 'app-relay-shopping',
    standalone: true,
    imports: [TranslatePipe],
    template: `
        <section id="shopping-list" class="shopping-card" aria-labelledby="shopping-title">
            <h3 id="shopping-title">{{ 'lock_shopping.title' | translate }}</h3>
            <p class="shopping-intro">{{ 'lock_shopping.intro' | translate }}</p>
            <div class="shopping-grid">
                <figure>
                    <a class="diagram-link" [href]="diagramUrl()" target="_blank" rel="noopener noreferrer">
                        <img [src]="diagramUrl()" width="1200" height="1110" loading="lazy"
                             [alt]="'lock_shopping.diagram_alt' | translate" />
                    </a>
                    <figcaption>
                        <a [href]="diagramUrl()" target="_blank" rel="noopener noreferrer">
                            {{ 'lock_shopping.open_diagram' | translate }} ↗
                        </a>
                    </figcaption>
                    <p class="shopping-hardware-note">{{ 'lock_shopping.hardware_note' | translate }}</p>
                </figure>
                <div class="shopping-copy">
                    <ul class="shopping-items">
                        @for (part of parts; track part.key) {
                            <li [attr.data-part]="part.key">
                                <div class="shopping-product-line">
                                    <div class="shopping-product">
                                        <a [href]="part.href" target="_blank" rel="noopener noreferrer">
                                            {{ 'lock_shopping.items.' + part.key + '.name' | translate }} ↗
                                        </a>
                                        @if (part.extraHref) {
                                            <span aria-hidden="true"> / </span>
                                            <a [href]="part.extraHref" target="_blank" rel="noopener noreferrer">
                                                {{ 'lock_shopping.items.' + part.key + '.extra_name' | translate }} ↗
                                            </a>
                                        }
                                    </div>
                                    <span class="shopping-quantity">{{ 'lock_shopping.items.' + part.key + '.quantity' | translate }}</span>
                                </div>
                                <p>{{ 'lock_shopping.items.' + part.key + '.detail' | translate }}</p>
                            </li>
                        }
                    </ul>
                    <p class="shopping-footnote">{{ 'lock_shopping.note' | translate }}</p>
                    <a class="shopping-guide-link" [href]="guideUrl()" target="_blank" rel="noopener noreferrer">
                        {{ 'lock_shopping.guide' | translate }} →
                    </a>
                </div>
            </div>
        </section>
    `,
    styles: `
        :host { display: block; min-width: 0; }
        .shopping-card {
            margin-top: 2rem;
            padding: clamp(1rem, 3vw, 2rem);
            border: 1px solid var(--border);
            border-radius: var(--soft-radius);
            background: var(--bg-elevated);
            scroll-margin-top: 6rem;
        }
        h3 { margin: 0; font-size: clamp(1.25rem, 2vw, 1.5rem); line-height: 1.35; }
        .shopping-intro { margin: 0.75rem 0 1.5rem; max-width: 75ch; color: var(--fg-muted); line-height: 1.7; }
        .shopping-grid { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); gap: clamp(1.25rem, 3vw, 2rem); align-items: start; }
        figure, .shopping-copy { min-width: 0; margin: 0; }
        .diagram-link { display: block; overflow: hidden; border: 1px solid var(--border); border-radius: 0.75rem; background: #f6f8fb; }
        img { display: block; width: 100%; height: auto; }
        figcaption a, .shopping-guide-link { display: inline-flex; align-items: center; min-height: 44px; padding: 0.5rem 0; }
        .shopping-hardware-note { margin: 0.5rem 0 0; color: var(--fg-muted); font-size: 0.875rem; line-height: 1.65; }
        .shopping-items { margin: 0; padding: 0; list-style: none; }
        .shopping-items li { padding: 0.875rem 0; border-bottom: 1px solid var(--border); }
        .shopping-items li:first-child { padding-top: 0; }
        .shopping-product-line { display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: start; gap: 0.75rem; }
        .shopping-product { min-width: 0; font-weight: 600; line-height: 1.55; overflow-wrap: anywhere; }
        .shopping-quantity { color: var(--fg-dim); font-size: 0.8125rem; line-height: 1.8; white-space: nowrap; }
        .shopping-items p { margin: 0.25rem 0 0; color: var(--fg-muted); font-size: 0.875rem; line-height: 1.6; }
        .shopping-footnote { margin: 1rem 0 0; color: var(--fg-muted); font-size: 0.8125rem; line-height: 1.65; }
        a { color: var(--accent); text-underline-offset: 0.2em; }
        a:hover { text-decoration: underline; }
        a:focus-visible { outline: 2px solid var(--accent); outline-offset: 4px; border-radius: 0.25rem; }
        @media (max-width: 60rem) { .shopping-grid { grid-template-columns: minmax(0, 1fr); } }
    `
})
export class RelayShopping {
    private readonly language = inject(LanguageService);
    readonly diagramUrl = computed(() => `/images/aliro-relay.${this.language.lang()}.svg`);
    readonly guideUrl = computed(() =>
        `https://github.com/ilia-ae/aliro-mayday/blob/main/docs/wiring${this.language.lang() === 'ru' ? '.ru' : ''}.md#shopping-list`
    );
    readonly parts: ReadonlyArray<ShoppingPart> = [
        { key: 'lock', href: 'https://www.sparkfun.com/solenoid-12v-latch-lock.html' },
        { key: 'relay', href: 'https://www.dfrobot.com/product-1572.html' },
        { key: 'supply5', href: 'https://www.digikey.com/en/products/detail/mean-well-usa-inc/GST18E05-P1J/7703622' },
        { key: 'supply12', href: 'https://www.digikey.com/en/products/detail/mean-well-usa-inc/GST25E12-P1J/7703666' },
        { key: 'resistor', href: 'https://www.adafruit.com/product/2784' },
        { key: 'jumpers', href: 'https://www.adafruit.com/product/266', extraHref: 'https://www.adafruit.com/product/5018' },
        { key: 'wire', href: 'https://www.pololu.com/product/2652/' },
        { key: 'adapters', href: 'https://www.adafruit.com/product/368' },
        { key: 'diode', href: 'https://www.adafruit.com/product/755' }
    ];
}
