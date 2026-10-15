import {
    Component,
    DestroyRef,
    computed,
    effect,
    inject,
    signal
} from '@angular/core';
import { TranslatePipe } from '@ngx-translate/core';
import { RouterLink } from '@angular/router';
import { LanguageService } from '@/app/shared/services/language.service';
import { ThemeService } from '@/app/shared/services/theme.service';

// Keep type declarations ABOVE the @Component decorator. Anything
// placed between the decorator and the class it decorates breaks
// the Angular compiler (TS1206: Decorators are not valid here).
type RoadmapStatus = 'done' | 'active' | 'planned' | 'goal';
type ComparisonMark = 'yes' | 'partial' | 'no';
interface ComparisonRow {
    key: string;
    ours: ComparisonMark;
    them: ComparisonMark;
}

@Component({
    selector: 'app-landing',
    standalone: true,
    imports: [TranslatePipe, RouterLink],
    template: `
        <a class="skip-link" href="#main-content">
            {{ 'nav.skip_to_content' | translate }}
        </a>

        <header class="topbar" role="banner">
            <div class="wrap">
                <a class="brand" href="/" aria-label="aliro.mayday">
                    <svg class="brand-mark" viewBox="0 0 64 64" width="26" height="26" aria-hidden="true">
                        <rect width="64" height="64" rx="14" fill="var(--accent)"/>
                        <path d="M18 48 L32 16 L46 48 M23.5 38 L40.5 38"
                              fill="none" stroke="#ffffff" stroke-width="5"
                              stroke-linecap="round" stroke-linejoin="round"/>
                    </svg>
                    <span class="brand-text">aliro<span class="brand-dot">.</span>mayday</span>
                </a>

                <nav class="jump-nav" [attr.aria-label]="'nav.jump_nav_label' | translate">
                    <!-- Order matches the page top→bottom scroll: migration
                         (#6) → about (#7) → faq (#13) → roadmap (#14) →
                         source (#15) → contact (#18). -->
                    <a href="#migration">{{ 'nav.migration' | translate }}</a>
                    <a href="#about">{{ 'nav.about' | translate }}</a>
                    <a href="#faq">{{ 'nav.faq' | translate }}</a>
                    <a href="#roadmap">{{ 'nav.plan' | translate }}</a>
                    <a href="#source">{{ 'nav.source' | translate }}</a>
                    <a href="#contact">{{ 'nav.contact' | translate }}</a>
                </nav>

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
                                <!-- sun — light theme is active -->
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                    <circle cx="12" cy="12" r="4"/>
                                    <path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/>
                                </svg>
                            }
                            @case ('dark') {
                                <!-- moon — dark theme is active -->
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                    <path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8Z"/>
                                </svg>
                            }
                            @default {
                                <!-- half-circle — auto follows the system -->
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                    <circle cx="12" cy="12" r="9"/>
                                    <path d="M12 3a9 9 0 0 1 0 18z" fill="currentColor" stroke="none"/>
                                </svg>
                            }
                        }
                    </button>
                    <button
                        type="button"
                        class="ghost icon menu-btn"
                        (click)="menuOpen.set(!menuOpen()); $event.stopPropagation()"
                        [attr.aria-expanded]="menuOpen()"
                        [attr.aria-label]="'nav.menu_aria_label' | translate"
                    >
                        @if (menuOpen()) {
                            <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true">
                                <path d="M6 6l12 12M18 6l-12 12"/>
                            </svg>
                        } @else {
                            <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true">
                                <path d="M4 7h16M4 12h16M4 17h16"/>
                            </svg>
                        }
                    </button>
                </nav>
            </div>
        </header>

        <!-- Mobile fullscreen overlay menu (≤60rem). Pattern adapted
             from /Users/ilya/CODE/deposit.mayday.software public-topbar:
             backdrop-blurred sheet, numbered nav rows at the top, a
             contact CTA pinned to the bottom. Closes on link tap, Esc,
             or once the viewport widens past the breakpoint. -->
        <div
            class="mob-overlay"
            [class.is-open]="menuOpen()"
            (click)="menuOpen.set(false)"
            [attr.aria-hidden]="!menuOpen()"
        >
            <div class="mob-sheet">
                <nav
                    class="mob-nav"
                    (click)="$event.stopPropagation()"
                    [attr.aria-label]="'nav.menu_aria_label' | translate"
                >
                    @for (item of mobileNavItems; track item.href) {
                        <a
                            [attr.href]="item.href"
                            class="mob-link"
                            (click)="menuOpen.set(false)"
                        >
                            <span class="mob-num">{{ item.num }}</span>
                            <span class="mob-text">{{ item.key | translate }}</span>
                            <svg class="mob-arrow" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true">
                                <path d="M5 12h14M13 5l7 7-7 7"/>
                            </svg>
                        </a>
                    }
                </nav>
                <div class="mob-foot" (click)="$event.stopPropagation()">
                    <a
                        href="mailto:aliro.mayday@ilia.ae"
                        class="mob-cta"
                        (click)="menuOpen.set(false)"
                    >
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                            <rect x="3" y="5" width="18" height="14" rx="2"/>
                            <path d="M3 7l9 6 9-6"/>
                        </svg>
                        <span>aliro.mayday&#64;ilia.ae</span>
                    </a>
                </div>
            </div>
        </div>

        <main id="main-content">
            <section id="hero" class="hero">
                <div class="wrap hero-grid">
                    <div class="hero-copy">
                        <p class="eyebrow">{{ 'hero.eyebrow' | translate }}</p>
                        <h1>{{ 'hero.title' | translate }}</h1>
                        <p class="lede">{{ 'hero.lede' | translate }}</p>
                        <div class="hero-cta">
                            <a href="#contact" class="btn-primary">
                                {{ 'hero.cta_primary' | translate }}
                            </a>
                            <a href="#source" class="btn-secondary">
                                {{ 'hero.cta_secondary' | translate }} →
                            </a>
                        </div>
                    </div>
                    <div class="hero-visual" aria-hidden="true">
                        <svg viewBox="0 0 320 260" xmlns="http://www.w3.org/2000/svg">
                            <!-- phone silhouette -->
                            <rect x="10" y="30" width="90" height="180" rx="14" fill="none" stroke="currentColor" stroke-width="2.5" opacity="0.7"/>
                            <rect x="18" y="42" width="74" height="140" rx="6" fill="var(--accent)" opacity="0.08"/>
                            <circle cx="55" cy="195" r="3.5" fill="currentColor" opacity="0.5"/>
                            <!-- face id indicator -->
                            <circle cx="55" cy="112" r="18" fill="none" stroke="var(--accent)" stroke-width="2"/>
                            <path d="M47 108 L47 112 M63 108 L63 112 M50 120 Q55 124 60 120" stroke="var(--accent)" stroke-width="1.8" fill="none" stroke-linecap="round"/>
                            <!-- NFC waves -->
                            <path d="M115 110 Q128 120 115 130" fill="none" stroke="var(--accent)" stroke-width="2" stroke-linecap="round"/>
                            <path d="M130 100 Q150 120 130 140" fill="none" stroke="var(--accent)" stroke-width="2" stroke-linecap="round" opacity="0.65"/>
                            <path d="M145 90 Q175 120 145 150" fill="none" stroke="var(--accent)" stroke-width="2" stroke-linecap="round" opacity="0.4"/>
                            <!-- reader (monogram square) -->
                            <rect x="188" y="85" width="70" height="70" rx="14" fill="var(--accent)"/>
                            <path d="M205 142 L223 98 L241 142 M212 128 L234 128" fill="none" stroke="#ffffff" stroke-width="4.5" stroke-linecap="round" stroke-linejoin="round"/>
                            <!-- arrow -->
                            <path d="M268 120 L298 120 M290 112 L298 120 L290 128" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" opacity="0.5"/>
                            <!-- lock -->
                            <g transform="translate(258 180)">
                                <rect x="0" y="14" width="50" height="38" rx="4" fill="none" stroke="currentColor" stroke-width="2.5"/>
                                <path d="M12 14 V8 a13 13 0 0 1 26 0 V14" fill="none" stroke="currentColor" stroke-width="2.5"/>
                                <circle cx="25" cy="33" r="3.5" fill="currentColor"/>
                                <rect x="23" y="33" width="4" height="8" fill="currentColor"/>
                            </g>
                        </svg>
                    </div>
                </div>
            </section>

            <section id="how-it-works" class="steps">
                <div class="wrap">
                    <h2>{{ 'steps.title' | translate }}</h2>
                    <ol>
                        @for (n of [1, 2, 3]; track n) {
                            <li>
                                <span class="num">{{ n }}</span>
                                <h3>{{ 'steps.' + n + '.title' | translate }}</h3>
                                <p>{{ 'steps.' + n + '.text' | translate }}</p>
                            </li>
                        }
                    </ol>
                </div>
            </section>

            <section id="wallets" class="wallets">
                <div class="wrap">
                    <h2>{{ 'wallets.title' | translate }}</h2>
                    <ul>
                        @for (w of ['apple', 'google', 'samsung']; track w) {
                            <li>
                                <strong>{{ 'wallets.' + w + '.name' | translate }}</strong>
                                <span>{{ 'wallets.' + w + '.devices' | translate }}</span>
                            </li>
                        }
                    </ul>
                </div>
            </section>

            <section id="why" class="why">
                <div class="wrap">
                    <h2>{{ 'why.title' | translate }}</h2>
                    <div class="grid">
                        @for (n of [1, 2, 3, 4]; track n) {
                            <article>
                                <h3>{{ 'why.' + n + '.title' | translate }}</h3>
                                <p>{{ 'why.' + n + '.text' | translate }}</p>
                            </article>
                        }
                    </div>
                </div>
            </section>

            <section id="use" class="use">
                <div class="wrap">
                    <h2>{{ 'use.title' | translate }}</h2>
                    <ul>
                        @for (k of ['home', 'office', 'hotel', 'coworking']; track k) {
                            <li>{{ 'use.items.' + k | translate }}</li>
                        }
                    </ul>
                </div>
            </section>

            <section id="migration" class="migration">
                <div class="wrap">
                    <h2>{{ 'migration.title' | translate }}</h2>
                    <p class="lede">{{ 'migration.lede' | translate }}</p>
                    <ol>
                        @for (n of [1, 2, 3]; track n) {
                            <li>
                                <span class="num">{{ n }}</span>
                                <h3>{{ 'migration.' + n + '.title' | translate }}</h3>
                                <p>{{ 'migration.' + n + '.text' | translate }}</p>
                            </li>
                        }
                    </ol>
                    <p class="formats">{{ 'migration.formats' | translate }}</p>
                </div>
            </section>

            <section id="about" class="standard">
                <div class="wrap">
                    <h2>{{ 'standard.title' | translate }}</h2>
                    <p class="lede">{{ 'standard.lede' | translate }}</p>

                    <h3 class="sub-title">{{ 'standard.timeline_title' | translate }}</h3>
                    <ol class="timeline">
                        @for (n of [1, 2, 3]; track n) {
                            <li>
                                <span class="year">{{ 'standard.timeline.' + n + '.year' | translate }}</span>
                                <p>{{ 'standard.timeline.' + n + '.text' | translate }}</p>
                            </li>
                        }
                    </ol>

                    <h3 class="sub-title">{{ 'standard.tech_title' | translate }}</h3>
                    <div class="tech-grid">
                        @for (n of [1, 2, 3, 4]; track n) {
                            <article>
                                <h4>{{ 'standard.tech.' + n + '.title' | translate }}</h4>
                                <p>{{ 'standard.tech.' + n + '.text' | translate }}</p>
                            </article>
                        }
                    </div>

                    <h3 class="sub-title">{{ 'standard.ecosystem_title' | translate }}</h3>
                    <ul class="ecosystem-chips">
                        @for (company of ecosystemCompanies; track company) {
                            <li>{{ company }}</li>
                        }
                        <li class="ecosystem-more">{{ 'standard.ecosystem_more' | translate }}</li>
                    </ul>

                    <h3 class="sub-title">{{ 'standard.refs_title' | translate }}</h3>
                    <ul class="refs">
                        @for (ref of standardRefs; track ref.key) {
                            <li>
                                <a
                                    [attr.href]="ref.url"
                                    target="_blank"
                                    rel="noopener noreferrer"
                                >
                                    <span class="ref-title">{{ 'standard.refs.' + ref.key | translate }}</span>
                                    <span class="host">{{ ref.host }}</span>
                                    <svg class="ext-arrow" viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                        <path d="M6 4h6v6"/>
                                        <path d="M11.5 4.5L4.5 11.5"/>
                                    </svg>
                                </a>
                            </li>
                        }
                    </ul>

                    <div class="disclaimer">
                        {{ 'standard.disclaimer' | translate }}
                    </div>

                    <p class="source">
                        {{ 'standard.source' | translate }} —
                        <a href="https://csa-iot.org/all-solutions/aliro/" target="_blank" rel="noopener noreferrer">csa-iot.org</a>
                    </p>
                </div>
            </section>

            <section id="integrations" class="integrations">
                <div class="wrap">
                    <h2>{{ 'integrations.title' | translate }}</h2>
                    <p class="lede">{{ 'integrations.lede' | translate }}</p>
                    <div class="grid">
                        @for (n of [1, 2, 3, 4, 5, 6]; track n) {
                            <article>
                                <h3>{{ 'integrations.' + n + '.title' | translate }}</h3>
                                <p>{{ 'integrations.' + n + '.text' | translate }}</p>
                            </article>
                        }
                    </div>
                </div>
            </section>

            <section id="stack" class="stack">
                <div class="wrap">
                    <h2>{{ 'stack.title' | translate }}</h2>
                    <p class="lede">{{ 'stack.lede' | translate }}</p>
                    @for (group of stackGroups; track group.key) {
                        <h3 class="sub-title">
                            {{ 'stack.groups.' + group.key | translate }}
                        </h3>
                        <ul class="stack-list">
                            @for (item of group.items; track item.key) {
                                <li>
                                    @if (item.url) {
                                        <a [attr.href]="item.url" target="_blank" rel="noopener noreferrer">
                                            <strong>{{ 'stack.items.' + item.key + '.name' | translate }}</strong>
                                            <span>{{ 'stack.items.' + item.key + '.tag' | translate }}</span>
                                            <svg class="ext-arrow" viewBox="0 0 16 16" width="12" height="12" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                                <path d="M6 4h6v6"/>
                                                <path d="M11.5 4.5L4.5 11.5"/>
                                            </svg>
                                        </a>
                                    } @else {
                                        <strong>{{ 'stack.items.' + item.key + '.name' | translate }}</strong>
                                        <span>{{ 'stack.items.' + item.key + '.tag' | translate }}</span>
                                    }
                                </li>
                            }
                        </ul>
                    }
                </div>
            </section>

            <section id="comparison" class="comparison">
                <div class="wrap">
                    <h2>{{ 'comparison.title' | translate }}</h2>
                    <p class="lede">{{ 'comparison.lede' | translate }}</p>
                    <div class="table-scroll">
                        <table>
                            <thead>
                                <tr>
                                    <th>{{ 'comparison.headers.feature' | translate }}</th>
                                    <th>{{ 'comparison.headers.aliro_mayday' | translate }}</th>
                                    <th>{{ 'comparison.headers.proprietary' | translate }}</th>
                                </tr>
                            </thead>
                            <tbody>
                                @for (row of comparisonRows; track row.key) {
                                    <tr>
                                        <td>{{ 'comparison.rows.' + row.key | translate }}</td>
                                        <td class="mark-cell">
                                            <span class="mark mark-{{ row.ours }}">{{ 'comparison.marks.' + row.ours | translate }}</span>
                                        </td>
                                        <td class="mark-cell">
                                            <span class="mark mark-{{ row.them }}">{{ 'comparison.marks.' + row.them | translate }}</span>
                                        </td>
                                    </tr>
                                }
                            </tbody>
                        </table>
                    </div>
                    <p class="source">{{ 'comparison.footnote' | translate }}</p>
                </div>
            </section>

            <section id="competitors" class="competitors">
                <div class="wrap">
                    <h2>{{ 'competitors.title' | translate }}</h2>
                    <p class="lede">{{ 'competitors.lede' | translate }}</p>
                    <div class="grid">
                        @for (lock of competitorLocks; track lock.key) {
                            <article>
                                <header>
                                    <h3>{{ lock.name }}</h3>
                                    <span class="meta">{{ lock.priceRange }} · {{ lock.year }}</span>
                                </header>
                                <div class="block strengths">
                                    <h4>{{ 'competitors.strengths_label' | translate }}</h4>
                                    <ul>
                                        @for (i of [1, 2, 3]; track i) {
                                            <li>{{ 'competitors.' + lock.key + '.strengths_' + i | translate }}</li>
                                        }
                                    </ul>
                                </div>
                                <div class="block advantages">
                                    <h4>{{ 'competitors.advantages_label' | translate }}</h4>
                                    <ul>
                                        @for (i of [1, 2, 3]; track i) {
                                            <li>{{ 'competitors.' + lock.key + '.advantages_' + i | translate }}</li>
                                        }
                                    </ul>
                                </div>
                            </article>
                        }
                    </div>
                    <p class="source">{{ 'competitors.footnote' | translate }}</p>
                </div>
            </section>

            <section id="privacy" class="privacy">
                <div class="wrap">
                    <h2>{{ 'privacy.title' | translate }}</h2>
                    <p class="lede">{{ 'privacy.lede' | translate }}</p>
                    <ul class="privacy-list">
                        @for (n of [1, 2, 3, 4]; track n) {
                            <li>
                                <strong>{{ 'privacy.' + n + '.title' | translate }}</strong>
                                <p>{{ 'privacy.' + n + '.text' | translate }}</p>
                            </li>
                        }
                    </ul>
                </div>
            </section>

            <section id="faq" class="faq">
                <div class="wrap">
                    <h2>{{ 'faq.title' | translate }}</h2>
                    <div class="faq-list">
                        @for (n of [1, 2, 3, 4, 5, 6, 7, 8]; track n) {
                            <details>
                                <summary>{{ 'faq.' + n + '.q' | translate }}</summary>
                                <p>{{ 'faq.' + n + '.a' | translate }}</p>
                            </details>
                        }
                    </div>
                </div>
            </section>

            <section id="roadmap" class="roadmap">
                <div class="wrap">
                    <h2>{{ 'roadmap.title' | translate }}</h2>
                    <p class="lede">{{ 'roadmap.lede' | translate }}</p>
                    <ol>
                        @for (step of roadmap; track step.n) {
                            <li [attr.data-status]="step.status">
                                <span class="step-num">{{ step.n }}</span>
                                <div class="step-body">
                                    <div class="step-header">
                                        <h3>{{ 'roadmap.' + step.n + '.title' | translate }}</h3>
                                        <span class="badge" [class]="'badge badge-' + step.status">
                                            {{ 'roadmap.status.' + step.status | translate }}
                                        </span>
                                    </div>
                                    <p class="eta">{{ 'roadmap.' + step.n + '.eta' | translate }}</p>
                                    <p>{{ 'roadmap.' + step.n + '.text' | translate }}</p>
                                </div>
                            </li>
                        }
                    </ol>
                </div>
            </section>

            <section id="source" class="source">
                <div class="wrap">
                    <h2>{{ 'source.title' | translate }}</h2>
                    <p class="lede">{{ 'source.lede' | translate }}</p>
                    <p class="released">{{ 'source.released' | translate }}</p>
                    <p class="released-cta">
                        <a [attr.href]="sourceReleasedUrl" target="_blank" rel="noopener noreferrer">
                            {{ 'source.open_repo' | translate }} →
                        </a>
                    </p>
                    <p class="cta">
                        <a [attr.href]="sourceDocumentationUrl()" target="_blank" rel="noopener noreferrer">
                            {{ 'source.documentation' | translate }} →
                        </a>
                    </p>
                    <p class="cta">
                        <a [attr.href]="sourceWiringUrl()" target="_blank" rel="noopener noreferrer">
                            {{ 'source.wiring' | translate }} →
                        </a>
                    </p>
                    <p>{{ 'source.license_note' | translate }}</p>
                </div>
            </section>

            <section id="changelog" class="changelog">
                <div class="wrap">
                    <h2>{{ 'changelog.title' | translate }}</h2>
                    <ul>
                        @for (n of [1, 2, 3, 4]; track n) {
                            <li>
                                <span class="date">{{ 'changelog.' + n + '.date' | translate }}</span>
                                <div>
                                    <strong>{{ 'changelog.' + n + '.title' | translate }}</strong>
                                    <p>{{ 'changelog.' + n + '.text' | translate }}</p>
                                </div>
                            </li>
                        }
                    </ul>
                </div>
            </section>

            <section id="status" class="status">
                <div class="wrap">
                    <h2>{{ 'status.title' | translate }}</h2>
                    <p>{{ 'status.p1' | translate }}</p>
                    <p>{{ 'status.p2' | translate }}</p>
                    <p class="cta">
                        <a [attr.href]="sourceReleasedUrl" target="_blank" rel="noopener noreferrer">
                            {{ 'status.cta' | translate }}
                        </a>
                    </p>
                </div>
            </section>

            <section id="contact" class="contact">
                <div class="wrap">
                    <h2>{{ 'contact.title' | translate }}</h2>
                    <p class="lede">{{ 'contact.lede' | translate }}</p>
                    <ul class="contact-list">
                        <li>
                            <span class="label">{{ 'contact.email_label' | translate }}</span>
                            <a href="mailto:aliro.mayday&#64;ilia.ae">aliro.mayday&#64;ilia.ae</a>
                        </li>
                        <li>
                            <span class="label">{{ 'contact.mayday_label' | translate }}</span>
                            <a
                                [attr.href]="'contact.mayday_url' | translate"
                                target="_blank"
                                rel="noopener noreferrer"
                            >{{ 'contact.mayday_host' | translate }}</a>
                        </li>
                    </ul>
                </div>
            </section>
        </main>

        <footer>
            <div class="wrap">
                <div class="footer-grid">
                    <div class="footer-brand">
                        <a class="brand" href="/" aria-label="aliro.mayday">
                            <svg
                                class="brand-mark"
                                viewBox="0 0 64 64"
                                width="32"
                                height="32"
                                aria-hidden="true"
                            >
                                <rect width="64" height="64" rx="14" fill="var(--accent)"/>
                                <path d="M18 48 L32 16 L46 48 M23.5 38 L40.5 38"
                                      fill="none" stroke="#ffffff" stroke-width="5"
                                      stroke-linecap="round" stroke-linejoin="round"/>
                            </svg>
                            <span class="brand-text">aliro<span class="brand-dot">.</span>mayday</span>
                        </a>
                        <p class="tagline">{{ 'footer.tagline' | translate }}</p>
                        <a class="brand-email" href="mailto:aliro.mayday&#64;ilia.ae">
                            aliro.mayday&#64;ilia.ae
                        </a>
                    </div>

                    <nav class="footer-col" [attr.aria-label]="'footer.nav_label' | translate">
                        <h4 class="footer-heading">{{ 'footer.nav_label' | translate }}</h4>
                        <ul>
                            <li><a href="#about">{{ 'nav.about' | translate }}</a></li>
                            <li><a href="#roadmap">{{ 'nav.plan' | translate }}</a></li>
                            <li><a href="#source">{{ 'nav.source' | translate }}</a></li>
                            <li><a href="#faq">{{ 'nav.faq' | translate }}</a></li>
                            <li><a href="#contact">{{ 'nav.contact' | translate }}</a></li>
                        </ul>
                    </nav>

                    <nav class="footer-col" [attr.aria-label]="'footer.docs_label' | translate">
                        <h4 class="footer-heading">{{ 'footer.docs_label' | translate }}</h4>
                        <ul>
                            <li><a routerLink="/privacy">{{ 'footer.privacy_link' | translate }}</a></li>
                            <li>
                                <a
                                    [attr.href]="'footer.site_url' | translate"
                                    target="_blank"
                                    rel="noopener noreferrer"
                                >{{ 'footer.site_label' | translate }} ↗</a>
                            </li>
                        </ul>
                    </nav>
                </div>

                <div class="footer-bottom">
                    <!-- "2026" is wrapped in an anchor that looks like
                         plain copyright text but routes to /qr — the
                         business-card page used by printed QR stickers.
                         Hover tints the year accent-green; that's the
                         only visual hint it's clickable. -->
                    <span class="copyright">
                        ©
                        <a class="ft-secret" routerLink="/qr">2026</a>
                        {{ 'footer.copyright_holder' | translate }}
                    </span>
                    <span class="license">{{ 'footer.commercial' | translate }}</span>
                </div>
            </div>
        </footer>
    `,
    styles: [
        `
            :host {
                display: block;
                --section-y: 5rem;
                --wrap-max: 72rem;
                --wrap-x: 1.5rem;
                --topbar-h: 4rem;
                --card-radius: 1rem;
                --soft-radius: 0.75rem;
                --pill-radius: 0.5rem;
                --btn-radius: 0.5rem;
                --card-padding: 1.5rem;
                --card-padding-compact: 1rem 1.25rem;
            }

            .wrap {
                max-width: var(--wrap-max);
                margin: 0 auto;
                padding: 0 var(--wrap-x);
            }

            /* ================ Skip link ================ */
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
                outline: none;
                color: var(--accent-ink);
            }

            /* ================ Topbar ================ */
            .topbar {
                position: sticky;
                top: 0;
                z-index: 50;
                background: color-mix(in srgb, var(--bg) 82%, transparent);
                border-bottom: 1px solid var(--border);
                backdrop-filter: saturate(180%) blur(14px);
                -webkit-backdrop-filter: saturate(180%) blur(14px);
            }
            .topbar .wrap {
                display: flex;
                align-items: center;
                justify-content: space-between;
                gap: 1rem;
                height: var(--topbar-h);
            }

            .brand {
                display: inline-flex;
                align-items: center;
                gap: 0.75rem;
                font-family: var(--font-display);
                font-weight: 600;
                font-size: 1.0625rem;
                letter-spacing: -0.01em;
                color: var(--fg);
                text-decoration: none;
                flex-shrink: 0;
            }
            .brand:hover {
                color: var(--fg);
            }
            .brand-mark {
                flex-shrink: 0;
                display: block;
            }
            .brand-dot {
                color: var(--accent);
            }

            .jump-nav {
                display: flex;
                gap: 1.75rem;
                font-size: 0.875rem;
                font-weight: 500;
            }
            .jump-nav a {
                color: var(--fg-muted);
                text-decoration: none;
                transition: color 120ms ease;
                white-space: nowrap;
            }
            .jump-nav a:hover {
                color: var(--fg);
            }

            .actions {
                display: flex;
                align-items: center;
                gap: 0.5rem;
                flex-shrink: 0;
            }

            .ghost {
                background: transparent;
                border: 1px solid var(--border);
                color: var(--fg-muted);
                padding: 0.5rem 0.75rem;
                border-radius: var(--pill-radius);
                font-size: 0.8125rem;
                font-weight: 500;
                letter-spacing: 0.02em;
                text-decoration: none;
                transition:
                    color 120ms ease,
                    border-color 120ms ease,
                    background-color 120ms ease;
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
            /* Hamburger button — desktop hidden, shown at the same
               breakpoint where the inline jump-nav collapses. The
               selector is upgraded to .ghost.icon.menu-btn so it
               outranks the generic .ghost.icon rule above (which sets
               display:inline-flex) on equal specificity. Without this
               the hamburger leaks onto desktop. */
            .ghost.icon.menu-btn {
                display: none;
            }

            /* ================ Mobile menu overlay ================ */
            /* Full-screen menu (≤60rem) — pattern from deposit topbar:
               backdrop-blurred sheet with numbered nav rows on top and
               a contact CTA pinned to the bottom. The overlay is below
               the topbar (z-index 49 vs 50) so the close button stays
               tappable while the sheet is open. */
            .mob-overlay {
                position: fixed;
                inset: 0;
                z-index: 49;
                background: color-mix(in srgb, var(--bg) 92%, transparent);
                -webkit-backdrop-filter: blur(24px) saturate(180%);
                backdrop-filter: blur(24px) saturate(180%);
                opacity: 0;
                visibility: hidden;
                pointer-events: none;
                transition:
                    opacity 220ms ease,
                    visibility 0s 220ms;
            }
            .mob-overlay.is-open {
                opacity: 1;
                visibility: visible;
                pointer-events: auto;
                transition:
                    opacity 220ms ease,
                    visibility 0s;
            }
            /* Hidden entirely above the breakpoint — keeps the desktop
               layout free of any leftover overlay state when the user
               resizes from mobile. */
            @media (min-width: 60.001rem) {
                .mob-overlay {
                    display: none;
                }
            }
            .mob-sheet {
                display: flex;
                flex-direction: column;
                justify-content: space-between;
                gap: 1.5rem;
                height: 100%;
                padding: calc(var(--topbar-h) + 1.5rem) var(--wrap-x) 2rem;
                max-width: var(--wrap-max);
                margin: 0 auto;
                overflow-y: auto;
                -webkit-overflow-scrolling: touch;
            }
            .mob-nav {
                display: flex;
                flex-direction: column;
                gap: 0.5rem;
            }
            .mob-link {
                display: flex;
                align-items: center;
                gap: 1rem;
                padding: 1rem 1.25rem;
                border: 1px solid var(--border);
                border-radius: var(--soft-radius);
                background: var(--bg-elevated);
                color: var(--fg);
                text-decoration: none;
                transition:
                    border-color 150ms ease,
                    background-color 150ms ease,
                    transform 150ms ease;
            }
            .mob-link:hover,
            .mob-link:active {
                border-color: var(--accent);
                transform: translateX(2px);
            }
            .mob-num {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                width: 2.25rem;
                height: 2.25rem;
                border-radius: 50%;
                border: 1px solid var(--border-strong);
                background: var(--bg);
                font-family: ui-monospace, SFMono-Regular, Menlo,
                    'JetBrains Mono', monospace;
                font-size: 0.75rem;
                font-weight: 700;
                color: var(--fg-dim);
                letter-spacing: 0.04em;
                flex-shrink: 0;
            }
            .mob-text {
                flex: 1;
                font-size: 1.125rem;
                font-weight: 600;
                letter-spacing: -0.005em;
            }
            .mob-arrow {
                color: var(--fg-dim);
                opacity: 0;
                transform: translateX(-6px);
                transition:
                    opacity 150ms ease,
                    transform 150ms ease;
            }
            .mob-link:hover .mob-arrow,
            .mob-link:active .mob-arrow {
                color: var(--accent);
                opacity: 1;
                transform: translateX(0);
            }
            .mob-foot {
                display: flex;
                flex-direction: column;
                gap: 0.75rem;
                padding-top: 1rem;
                border-top: 1px solid var(--border);
            }
            .mob-cta {
                display: inline-flex;
                align-items: center;
                gap: 0.625rem;
                padding: 0.875rem 1.25rem;
                border-radius: var(--btn-radius);
                background: var(--accent);
                color: #ffffff;
                font-size: 0.9375rem;
                font-weight: 600;
                letter-spacing: -0.005em;
                text-decoration: none;
                justify-content: center;
                transition:
                    background-color 150ms ease,
                    transform 150ms ease;
            }
            .mob-cta:hover,
            .mob-cta:active {
                background: color-mix(in srgb, var(--accent) 88%, #000);
                transform: translateY(-1px);
            }
            .mob-cta svg {
                flex-shrink: 0;
            }

            /* ================ Section shell ================ */
            main > section {
                padding: var(--section-y) 0;
                border-bottom: 1px solid var(--border);
            }
            main > section:last-child {
                border-bottom: none;
            }

            /* :where() keeps section-scoped element defaults at
               specificity 0, so any class-based override (.something
               h2, etc.) wins the cascade cleanly. Without this,
               Angular's [_ngcontent] attr inflation makes "section h2"
               (0,0,2,2) outrank ".something" (0,0,1,0) silently. */
            section :where(h2) {
                font-size: clamp(1.75rem, 3.2vw, 2.125rem);
                font-weight: 600;
                letter-spacing: -0.015em;
                margin: 0 0 2rem;
                max-width: 34ch;
            }
            /* :where() keeps the specificity at zero so per-section
               h3 overrides (.sub-title, .roadmap h3, .competitors h3,
               etc.) win the cascade without needing !important.
               Without this, Angular ViewEncapsulation appends a
               [_ngcontent] attr to every selector part — making
               "section h3" (0,0,2,2) outrank ".sub-title" (0,0,2,0)
               and silently killing every margin override I attempted
               on the sub-title across multiple iterations. */
            section :where(h3) {
                font-size: 1.0625rem;
                font-weight: 600;
                margin: 0 0 0.5rem;
                color: var(--fg);
            }
            /* Don't include "section > .wrap > p" in the margin-zero
               reset — that selector beats ".lede" on specificity
               (0,1,2 vs 0,1,0) and silently kills its 2rem bottom
               margin, leaving lede paragraphs glued to the next
               block. The leaf-level paragraphs that DO need margin
               zero all live inside an article/li/details container
               which has its own gap. */
            /* :where() keeps these descendant resets at specificity
               0 so per-section class overrides (.changelog p,
               .faq-list details p, .why article p, etc.) can
               actually win the cascade. */
            section :where(article p, li p, details p) {
                color: var(--fg-muted);
                max-width: 62ch;
                margin: 0;
            }

            .lede {
                font-size: clamp(1.0625rem, 1.7vw, 1.1875rem);
                color: var(--fg-muted);
                max-width: 62ch;
                margin: 0 0 2rem;
                line-height: 1.6;
            }

            .eyebrow {
                font-size: 0.75rem;
                font-weight: 600;
                text-transform: uppercase;
                letter-spacing: 0.15em;
                color: var(--fg-dim);
                margin-bottom: 1rem;
            }

            /* Plain h3 sub-section heading. No colour, no caps, no
               divider line — just typography and generous whitespace
               on both sides. After several attempts at "decorated"
               versions (eyebrow caps, top divider, bottom divider,
               brand colour), the simplest pattern is the only one
               that doesn't compete with the section's content blocks
               for visual attention. */
            .sub-title {
                font-size: 1.25rem;
                font-weight: 600;
                color: var(--fg);
                letter-spacing: -0.01em;
                /* 3rem / 1.25rem ≈ 2.4:1 top:bottom ratio — within
                   the 2:1–2.5:1 range industry typography (Smashing,
                   24ways) recommends for sub-section headings.
                   Earlier 5rem top read as section-break territory
                   and orphaned the title from its content. */
                margin: 3rem 0 1.25rem;
            }
            /* First sub-title in a section follows the lede/h2 directly
               — drop the top gap so it bonds with the section opener. */
            .lede + .sub-title,
            .wrap > h2 + .sub-title {
                margin-top: 0;
            }

            /* ================ Hero ================ */
            .hero {
                padding: 6rem 0 5rem;
            }
            .hero-grid {
                display: grid;
                grid-template-columns: minmax(0, 1.3fr) minmax(0, 1fr);
                gap: 3rem;
                align-items: center;
            }
            .hero-copy h1 {
                font-family: var(--font-display);
                font-weight: 600;
                font-size: clamp(2.25rem, 5.5vw, 4rem);
                line-height: 1.08;
                letter-spacing: -0.02em;
                margin: 0 0 1.5rem;
                max-width: 18ch;
            }
            .hero-copy .lede {
                max-width: 38rem;
                margin-bottom: 0;
            }
            .hero-cta {
                margin-top: 2rem;
                display: flex;
                flex-wrap: wrap;
                gap: 0.75rem;
            }
            .btn-primary,
            .btn-secondary {
                display: inline-flex;
                align-items: center;
                padding: 0.875rem 1.5rem;
                border-radius: var(--btn-radius);
                font-weight: 600;
                font-size: 0.9375rem;
                text-decoration: none;
                transition:
                    background-color 150ms ease,
                    color 150ms ease,
                    border-color 150ms ease;
            }
            .btn-primary {
                background: var(--accent);
                color: var(--accent-ink);
                border: 1px solid var(--accent);
            }
            .btn-primary:hover {
                background: var(--accent-hover);
                border-color: var(--accent-hover);
                color: var(--accent-ink);
            }
            .btn-secondary {
                background: transparent;
                color: var(--fg);
                border: 1px solid var(--border-strong);
            }
            .btn-secondary:hover {
                color: var(--fg);
                border-color: var(--fg-muted);
            }
            .hero-visual {
                color: var(--fg);
                opacity: 0.85;
                display: flex;
                justify-content: flex-end;
            }
            .hero-visual svg {
                width: 100%;
                height: auto;
                max-width: 360px;
            }

            /* ================ Steps ================ */
            .steps ol {
                list-style: none;
                padding: 0;
                margin: 0;
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                gap: 1.5rem;
            }
            .steps li {
                padding: var(--card-padding);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
            }
            .steps .num {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                width: 2rem;
                height: 2rem;
                border-radius: 50%;
                background: var(--accent);
                color: var(--accent-ink);
                font-weight: 600;
                font-size: 0.9375rem;
                margin-bottom: 1.25rem;
            }

            /* ================ Wallets ================ */
            .wallets ul {
                list-style: none;
                padding: 0;
                margin: 0;
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                gap: 1.25rem;
            }
            .wallets li {
                padding: var(--card-padding);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
                display: flex;
                flex-direction: column;
                gap: 0.375rem;
            }
            .wallets strong {
                font-size: 1.0625rem;
                font-weight: 600;
                color: var(--fg);
            }
            .wallets span {
                color: var(--fg-dim);
                font-size: 0.9375rem;
            }

            /* ================ Why ================ */
            .why .grid {
                display: grid;
                grid-template-columns: repeat(2, 1fr);
                gap: 1.25rem;
            }
            .why article {
                padding: var(--card-padding);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
                background: var(--bg-elevated);
            }
            .why article p {
                color: var(--fg-muted);
                margin: 0;
            }

            /* ================ Use cases ================ */
            .use ul {
                list-style: none;
                padding: 0;
                margin: 0;
                display: grid;
                grid-template-columns: repeat(auto-fit, minmax(14rem, 1fr));
                gap: 0.75rem;
            }
            .use li {
                padding: var(--card-padding-compact);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--soft-radius);
                color: var(--fg);
                font-size: 0.9375rem;
            }

            /* ================ Migration ================ */
            /* Three-step gradual migration story for B2B: install →
               import card DB → swap users at any pace. Reuses the
               .steps numbered-card pattern (same visual = same
               comprehension). The .formats line below the cards
               carries the legacy-format list for credibility. */
            .migration ol {
                list-style: none;
                padding: 0;
                margin: 0;
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                gap: 1.25rem;
            }
            .migration li {
                padding: var(--card-padding);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
            }
            .migration .num {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                width: 2rem;
                height: 2rem;
                border-radius: 50%;
                background: var(--accent);
                color: var(--accent-ink);
                font-weight: 600;
                font-size: 0.9375rem;
                margin-bottom: 1.25rem;
            }
            .migration h3 {
                font-size: 1.0625rem;
                font-weight: 600;
                margin: 0 0 0.5rem;
                color: var(--fg);
            }
            .migration li p {
                color: var(--fg-muted);
                line-height: 1.55;
                margin: 0;
            }
            .migration .formats {
                margin-top: 1.5rem;
                font-family: ui-monospace, SFMono-Regular, Menlo,
                    'JetBrains Mono', monospace;
                font-size: 0.8125rem;
                color: var(--fg-dim);
                letter-spacing: 0.01em;
            }

            /* ================ About the standard ================ */
            /* Three milestones in a horizontal grid — each card leads
               with a giant accent-coloured year, then a hairline rule,
               then the body. Top accent border replaces the previous
               left rail since each card is now its own self-contained
               unit, not a linked sequence. */
            .standard .timeline {
                list-style: none;
                padding: 0;
                margin: 0;
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                gap: 1.25rem;
            }
            .standard .timeline li {
                display: flex;
                flex-direction: column;
                gap: 1rem;
                padding: var(--card-padding);
                border: 1px solid var(--border);
                border-top: 3px solid var(--accent);
                border-radius: var(--card-radius);
                background: var(--bg-elevated);
            }
            .standard .timeline .year {
                display: block;
                padding: 0 0 0.875rem;
                margin: 0;
                background: transparent;
                border-bottom: 1px solid var(--border);
                color: var(--accent);
                font-family: var(--font-display);
                font-weight: 700;
                font-size: 2.75rem;
                line-height: 1;
                letter-spacing: -0.025em;
            }
            .standard .timeline p {
                color: var(--fg-muted);
                margin: 0;
                line-height: 1.6;
            }
            .standard .tech-grid {
                display: grid;
                grid-template-columns: repeat(2, 1fr);
                gap: 1.25rem;
            }
            .standard .tech-grid article {
                padding: var(--card-padding);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
                background: var(--bg-elevated);
            }
            .standard .tech-grid h4 {
                font-size: 1.0625rem;
                font-weight: 600;
                margin: 0 0 0.5rem;
                color: var(--fg);
            }
            .standard .tech-grid p {
                color: var(--fg-muted);
                max-width: none;
                margin: 0;
            }
            .standard .ecosystem-chips {
                list-style: none;
                padding: 0;
                margin: 0;
                display: flex;
                flex-wrap: wrap;
                gap: 0.5rem;
                max-width: 64rem;
            }
            .standard .ecosystem-chips li {
                padding: 0.5rem 0.875rem;
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--pill-radius);
                font-size: 0.875rem;
                font-weight: 500;
                color: var(--fg);
                white-space: nowrap;
            }
            .standard .ecosystem-chips .ecosystem-more {
                max-width: 100%;
                white-space: normal;
                background: transparent;
                border-style: dashed;
                color: var(--fg-dim);
                font-weight: 400;
            }

            .standard .refs {
                list-style: none;
                padding: 0;
                margin: 0;
                display: grid;
                grid-template-columns: repeat(auto-fit, minmax(18rem, 1fr));
                gap: 0.75rem;
            }
            .standard .refs li {
                border: 1px solid var(--border);
                border-radius: var(--soft-radius);
                background: var(--bg-elevated);
                transition:
                    border-color 150ms ease,
                    transform 150ms ease;
            }
            .standard .refs li:hover {
                border-color: var(--accent);
                transform: translateY(-1px);
            }
            .standard .refs a {
                display: grid;
                grid-template-columns: 1fr auto;
                grid-template-rows: auto auto;
                column-gap: 0.75rem;
                padding: var(--card-padding-compact);
                color: var(--fg);
                text-decoration: none;
            }
            .standard .refs .ref-title {
                grid-column: 1;
                grid-row: 1;
                font-weight: 600;
                font-size: 0.9375rem;
                color: var(--fg);
            }
            .standard .refs li:hover .ref-title {
                color: var(--accent);
            }
            .standard .refs .ext-arrow {
                grid-column: 2;
                grid-row: 1 / 3;
                align-self: center;
                color: var(--fg-dim);
                flex-shrink: 0;
                transition: color 150ms ease, transform 150ms ease;
            }
            .standard .refs li:hover .ext-arrow {
                color: var(--accent);
                transform: translate(1px, -1px);
            }
            .standard .refs .host {
                grid-column: 1;
                grid-row: 2;
                font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
                font-size: 0.75rem;
                color: var(--fg-dim);
                margin-top: 0.25rem;
            }

            .standard .disclaimer {
                margin-top: 1.75rem;
                padding: var(--card-padding-compact);
                border: 1px dashed var(--border-strong);
                border-radius: var(--soft-radius);
                font-size: 0.875rem;
                color: var(--fg-muted);
                max-width: 62ch;
            }

            .standard .source {
                margin-top: 2rem !important;
                font-size: 0.875rem;
                color: var(--fg-dim);
            }
            .standard .source a {
                color: var(--fg-muted);
            }
            .standard .source a:hover {
                color: var(--fg);
            }

            /* ================ Integrations ================ */
            .integrations .grid {
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                gap: 1.25rem;
            }
            .integrations article {
                padding: var(--card-padding);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
                background: var(--bg-elevated);
            }
            .integrations article p {
                color: var(--fg-muted);
                margin: 0;
            }

            /* ================ Technology stack ================ */
            .stack-list {
                list-style: none;
                padding: 0;
                margin: 0 0 0.5rem;
                display: grid;
                grid-template-columns: repeat(auto-fit, minmax(14rem, 1fr));
                gap: 0.75rem;
            }
            .stack-list li {
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--soft-radius);
                transition:
                    border-color 150ms ease,
                    transform 150ms ease;
            }
            .stack-list li:has(a):hover {
                border-color: var(--accent);
                transform: translateY(-1px);
            }
            /* Linked item — anchor owns layout + padding. */
            .stack-list a {
                display: grid;
                grid-template-columns: 1fr auto;
                grid-template-rows: auto auto;
                column-gap: 0.5rem;
                padding: var(--card-padding-compact);
                color: inherit;
                text-decoration: none;
            }
            /* Non-linked item (fallback) — padding + stack on the li. */
            .stack-list li:not(:has(a)) {
                padding: var(--card-padding-compact);
                display: flex;
                flex-direction: column;
                gap: 0.25rem;
            }
            .stack-list strong {
                font-size: 0.9375rem;
                font-weight: 600;
                color: var(--fg);
                letter-spacing: -0.005em;
            }
            .stack-list a strong {
                grid-column: 1;
                grid-row: 1;
            }
            .stack-list li:has(a):hover strong {
                color: var(--accent);
            }
            .stack-list .ext-arrow {
                grid-column: 2;
                grid-row: 1 / 3;
                align-self: center;
                color: var(--fg-dim);
                flex-shrink: 0;
                transition: color 150ms ease, transform 150ms ease;
            }
            .stack-list li:has(a):hover .ext-arrow {
                color: var(--accent);
                transform: translate(1px, -1px);
            }
            .stack-list a span {
                grid-column: 1;
                grid-row: 2;
            }
            .stack-list span {
                font-size: 0.8125rem;
                color: var(--fg-dim);
            }

            /* ================ Comparison ================ */
            .comparison .table-scroll {
                overflow-x: auto;
                border-radius: var(--card-radius);
                border: 1px solid var(--border);
                -webkit-overflow-scrolling: touch;
            }
            .comparison table {
                width: 100%;
                border-collapse: collapse;
                font-size: 0.9375rem;
                min-width: 36rem;
            }
            .comparison th,
            .comparison td {
                text-align: left;
                padding: 0.875rem 1.25rem;
                border-bottom: 1px solid var(--border);
            }
            .comparison thead th {
                background: var(--bg-elevated);
                font-weight: 600;
                color: var(--fg);
                font-size: 0.75rem;
                text-transform: uppercase;
                letter-spacing: 0.08em;
            }
            .comparison thead th:nth-child(2) {
                color: var(--accent);
            }
            .comparison tbody tr:last-child td {
                border-bottom: none;
            }
            .comparison .mark-cell {
                white-space: nowrap;
            }
            .comparison .mark {
                display: inline-flex;
                align-items: center;
                gap: 0.5rem;
                padding: 0.25rem 0.75rem 0.25rem 0.5rem;
                border-radius: 999px;
                font-size: 0.8125rem;
                font-weight: 600;
                line-height: 1.2;
                border: 1px solid transparent;
            }
            .comparison .mark::before {
                content: '';
                width: 0.5rem;
                height: 0.5rem;
                border-radius: 999px;
                background: currentColor;
                flex-shrink: 0;
            }
            .comparison .mark-yes {
                background: var(--mark-yes-bg);
                color: var(--mark-yes-fg);
                border-color: var(--mark-yes-border);
            }
            .comparison .mark-partial {
                background: var(--mark-partial-bg);
                color: var(--mark-partial-fg);
                border-color: var(--mark-partial-border);
            }
            .comparison .mark-no {
                background: var(--mark-no-bg);
                color: var(--mark-no-fg);
                border-color: var(--mark-no-border);
            }
            .comparison .source {
                margin-top: 1rem;
                font-size: 0.8125rem;
                color: var(--fg-dim);
            }

            /* ================ Competitors ================ */
            /* One card per real product on the market. Two stacked
               blocks per card: neutral "strengths" (✓ marks in fg-dim)
               and an accent-tinted "advantages" block with + marks
               that calls out where aliro.mayday wins. */
            .competitors .grid {
                display: grid;
                grid-template-columns: repeat(2, 1fr);
                gap: 1.25rem;
            }
            .competitors article {
                padding: var(--card-padding);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
                display: flex;
                flex-direction: column;
                gap: 1.25rem;
            }
            .competitors header {
                display: flex;
                align-items: baseline;
                justify-content: space-between;
                flex-wrap: wrap;
                gap: 0.5rem 1rem;
                padding-bottom: 1rem;
                border-bottom: 1px solid var(--border);
            }
            /* Competitors h3 sits in a flex header next to the price/year
               meta — drop the default 0.5rem bottom margin so the row
               aligns on its baseline. Otherwise inherits section h3. */
            .competitors h3 {
                margin: 0;
            }
            .competitors .meta {
                font-family: ui-monospace, SFMono-Regular, Menlo,
                    'JetBrains Mono', monospace;
                font-size: 0.8125rem;
                color: var(--fg-dim);
                letter-spacing: 0.02em;
            }
            .competitors .block {
                display: flex;
                flex-direction: column;
                gap: 0.75rem;
            }
            .competitors h4 {
                font-size: 0.6875rem;
                font-weight: 600;
                text-transform: uppercase;
                letter-spacing: 0.08em;
                color: var(--fg-dim);
                margin: 0;
            }
            .competitors ul {
                list-style: none;
                padding: 0;
                margin: 0;
                display: flex;
                flex-direction: column;
                gap: 0.5rem;
            }
            .competitors li {
                position: relative;
                padding-left: 1.375rem;
                color: var(--fg-muted);
                font-size: 0.9375rem;
                line-height: 1.45;
            }
            .competitors .strengths li::before {
                content: '✓';
                position: absolute;
                left: 0;
                top: 0;
                color: var(--fg-dim);
                font-weight: 600;
            }
            .competitors .advantages {
                padding: 1rem 1.25rem;
                background: color-mix(
                    in srgb,
                    var(--accent) 8%,
                    transparent
                );
                border-radius: var(--soft-radius);
                border-left: 3px solid var(--accent);
            }
            .competitors .advantages h4 {
                color: var(--accent);
            }
            .competitors .advantages li {
                color: var(--fg);
            }
            .competitors .advantages li::before {
                content: '+';
                position: absolute;
                left: 0.0625rem;
                top: -0.0625rem;
                color: var(--accent);
                font-weight: 700;
                font-size: 1.0625rem;
                line-height: 1;
            }
            .competitors .source {
                margin-top: 1.25rem;
                font-size: 0.8125rem;
                color: var(--fg-dim);
            }

            /* ================ Privacy ================ */
            .privacy-list {
                list-style: none;
                padding: 0;
                margin: 0;
                display: grid;
                grid-template-columns: repeat(2, 1fr);
                gap: 1.25rem;
            }
            .privacy-list li {
                padding: var(--card-padding);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
            }
            .privacy-list strong {
                display: block;
                font-size: 1.0625rem;
                font-weight: 600;
                margin-bottom: 0.5rem;
                color: var(--fg);
            }

            /* ================ FAQ ================ */
            .faq-list {
                max-width: 52rem;
                display: flex;
                flex-direction: column;
                gap: 0.75rem;
            }
            .faq-list details {
                border: 1px solid var(--border);
                border-radius: var(--soft-radius);
                background: var(--bg-elevated);
                overflow: hidden;
                transition: border-color 150ms ease;
            }
            .faq-list details[open] {
                border-color: var(--border-strong);
            }
            .faq-list summary {
                padding: 1rem 3rem 1rem 1.25rem;
                cursor: pointer;
                font-weight: 600;
                font-size: 1rem;
                color: var(--fg);
                list-style: none;
                position: relative;
            }
            .faq-list summary::-webkit-details-marker {
                display: none;
            }
            .faq-list summary::after {
                content: '+';
                position: absolute;
                right: 1.25rem;
                top: 50%;
                transform: translateY(-50%);
                font-size: 1.375rem;
                color: var(--fg-muted);
                font-weight: 300;
                line-height: 1;
            }
            .faq-list details[open] summary::after {
                content: '−';
                color: var(--accent);
            }
            .faq-list details p {
                padding: 0 1.25rem 1.25rem;
            }

            /* ================ Roadmap ================ */
            /* Rail + circle badge + bordered body card per step.
               Pattern adapted from PrimeBlocks "Feature Timeline" block
               (UAE STARS / scr_primeblocks/.../feature.component.html
               lines 308-367) — circle step-num with vertical connector,
               body content as a framed card on the right.
               Status differentiates badge fill: done→accent solid,
               active→accent ring + glow, goal→dashed accent, planned→neutral. */
            .roadmap ol {
                list-style: none;
                padding: 0;
                margin: 0;
                max-width: 52rem;
                display: flex;
                flex-direction: column;
                gap: 1.25rem;
            }
            .roadmap li {
                position: relative;
                display: grid;
                grid-template-columns: 2.5rem 1fr;
                gap: 1.25rem;
                align-items: start;
            }
            /* Connecting line between badges: starts below the step-num,
               extends through the gap to the next li's step-num. */
            .roadmap li:not(:last-child)::before {
                content: '';
                position: absolute;
                left: calc(1.25rem - 1px);
                top: 2.75rem;
                bottom: -1.25rem;
                width: 2px;
                background: var(--border);
                z-index: 0;
            }
            /* Highlight the connector that leads into a goal step. */
            .roadmap li:has(+ li[data-status='goal'])::before {
                background: color-mix(
                    in srgb,
                    var(--accent) 50%,
                    transparent
                );
            }
            .roadmap .step-num {
                position: relative;
                z-index: 1;
                width: 2.5rem;
                height: 2.5rem;
                border-radius: 50%;
                border: 1px solid var(--border-strong);
                background: var(--bg-elevated);
                color: var(--fg);
                display: inline-flex;
                align-items: center;
                justify-content: center;
                font-weight: 700;
                font-size: 0.9375rem;
                font-family: var(--font-display);
            }
            .roadmap li[data-status='done'] .step-num {
                background: var(--accent);
                color: var(--accent-ink);
                border-color: var(--accent);
            }
            .roadmap li[data-status='active'] .step-num {
                border: 2px solid var(--accent);
                color: var(--accent);
                box-shadow: 0 0 0 4px
                    color-mix(in srgb, var(--accent) 18%, transparent);
            }
            .roadmap li[data-status='goal'] .step-num {
                border: 1.5px dashed var(--accent);
                color: var(--accent);
                background: color-mix(in srgb, var(--accent) 6%, var(--bg));
            }
            .roadmap .step-body {
                padding: var(--card-padding);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--card-radius);
            }
            .roadmap .step-header {
                display: flex;
                align-items: center;
                flex-wrap: wrap;
                gap: 0.75rem;
                margin-bottom: 0.5rem;
                min-height: 1.75rem;
            }
            /* Roadmap h3 lives in a flex .step-header next to the badge
               — kill the default 0.5rem margin so the row stays on a
               single baseline. Otherwise inherits section h3. */
            .roadmap h3 {
                margin: 0;
            }
            .roadmap .badge {
                font-size: 0.6875rem;
                font-weight: 600;
                letter-spacing: 0.08em;
                text-transform: uppercase;
                padding: 0.25rem 0.5rem;
                border-radius: 999px;
                background: var(--bg-elevated);
                color: var(--fg-dim);
                border: 1px solid var(--border);
                white-space: nowrap;
            }
            .roadmap .badge-done {
                background: var(--accent);
                color: var(--accent-ink);
                border-color: var(--accent);
            }
            .roadmap .badge-active {
                color: var(--accent);
                border-color: var(--accent);
                background: color-mix(
                    in srgb,
                    var(--accent) 10%,
                    transparent
                );
            }
            .roadmap .badge-goal {
                color: var(--accent);
                border: 1px dashed var(--accent);
                background: color-mix(
                    in srgb,
                    var(--accent) 6%,
                    transparent
                );
            }
            .roadmap .eta {
                font-family: ui-monospace, SFMono-Regular, Menlo,
                    'JetBrains Mono', monospace;
                font-size: 0.75rem;
                color: var(--fg-dim);
                letter-spacing: 0.02em;
                margin: 0 0 0.5rem;
            }
            .roadmap .step-body p:last-child {
                color: var(--fg-muted);
                margin: 0;
            }

            /* ================ Source release countdown ================ */
            /* Live countdown to the public source-code release. The
               component refreshes every second via a signal driven by
               setInterval. Cleaned up via DestroyRef in the constructor. */
            .source .countdown {
                display: grid;
                grid-template-columns: repeat(4, minmax(0, 1fr));
                gap: 0.75rem;
                max-width: 36rem;
                margin: 0.5rem 0 1.25rem;
            }
            .source .unit {
                display: flex;
                flex-direction: column;
                align-items: center;
                gap: 0.25rem;
                padding: 1.25rem 0.75rem;
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--soft-radius);
            }
            .source .num {
                font-family: var(--font-display);
                font-weight: 700;
                font-size: 2.25rem;
                line-height: 1;
                color: var(--accent);
                letter-spacing: -0.02em;
                font-variant-numeric: tabular-nums;
            }
            .source .label {
                font-size: 0.6875rem;
                font-weight: 600;
                text-transform: uppercase;
                letter-spacing: 0.08em;
                color: var(--fg-dim);
            }
            .source .target {
                font-size: 0.9375rem;
                color: var(--fg-muted);
                margin: 0 0 0.5rem;
            }
            .source .target strong {
                color: var(--fg);
                font-weight: 600;
            }
            .source .released {
                font-size: 1.0625rem;
                color: var(--accent);
                font-weight: 600;
                margin: 0.5rem 0;
            }
            .source .released-cta {
                margin: 0 0 1rem;
            }
            .source .released-cta a {
                display: inline-block;
                padding: 0.875rem 1.5rem;
                background: var(--accent);
                color: var(--accent-ink);
                border-radius: var(--btn-radius);
                font-weight: 600;
                font-size: 0.9375rem;
                text-decoration: none;
            }
            .source .released-cta a:hover {
                background: var(--accent-hover);
            }
            /* Primary "Get early access" CTA — same button proportions
               as the post-launch released-cta and the status CTA, so
               all three primary actions on the page share one visual
               language. mailto target is reactively rebuilt with a
               URL-encoded localized subject (see earlyAccessMailto). */
            .source .cta {
                margin: 1.5rem 0 0;
            }
            .source .cta a {
                display: inline-block;
                padding: 0.875rem 1.5rem;
                background: var(--accent);
                color: var(--accent-ink);
                border-radius: var(--btn-radius);
                font-weight: 600;
                font-size: 0.9375rem;
                text-decoration: none;
                transition: background-color 150ms ease;
            }
            .source .cta a:hover {
                background: var(--accent-hover);
            }

            /* ================ Changelog ================ */
            /* Release-notes feed: drop card framing entirely, render
               each entry as a date header with a hairline rule
               extending right + title + body. Editorial rhythm,
               easier to scan than a stack of identical cards. */
            .changelog ul {
                list-style: none;
                padding: 0;
                margin: 0;
                max-width: 52rem;
                display: flex;
                flex-direction: column;
                gap: 2.5rem;
            }
            .changelog li {
                display: flex;
                flex-direction: column;
                gap: 0.75rem;
            }
            .changelog .date {
                display: flex;
                align-items: center;
                gap: 1rem;
                font-family: ui-monospace, SFMono-Regular, Menlo,
                    'JetBrains Mono', monospace;
                font-size: 0.8125rem;
                color: var(--accent);
                letter-spacing: 0.06em;
                font-weight: 600;
            }
            /* Hairline rule extending from the date label to the
               right edge — a "section divider" for each entry. */
            .changelog .date::after {
                content: '';
                flex: 1;
                height: 1px;
                background: var(--border);
            }
            .changelog li > div {
                display: flex;
                flex-direction: column;
                gap: 0.375rem;
            }
            .changelog strong {
                font-size: 1.125rem;
                font-weight: 600;
                color: var(--fg);
                line-height: 1.3;
                letter-spacing: -0.005em;
            }
            .changelog p {
                color: var(--fg-muted);
                line-height: 1.55;
                margin: 0;
            }

            /* ================ Status ================ */
            .status p {
                margin-bottom: 1rem !important;
            }
            .status .cta {
                margin-top: 1.75rem !important;
            }
            .status .cta a {
                display: inline-block;
                padding: 0.875rem 1.5rem;
                background: var(--accent);
                color: var(--accent-ink);
                border-radius: var(--btn-radius);
                font-weight: 600;
                font-size: 0.9375rem;
                text-decoration: none;
                transition:
                    background-color 150ms ease,
                    color 150ms ease;
            }
            .status .cta a:hover {
                background: var(--accent-hover);
                color: var(--accent-ink);
            }

            /* ================ Contact ================ */
            .contact-list {
                list-style: none;
                padding: 0;
                margin: 0;
                max-width: 44rem;
                display: flex;
                flex-direction: column;
                gap: 0.75rem;
            }
            .contact-list li {
                display: grid;
                grid-template-columns: 10rem 1fr;
                gap: 1rem;
                align-items: center;
                padding: var(--card-padding-compact);
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: var(--soft-radius);
            }
            .contact-list .label {
                font-size: 0.75rem;
                text-transform: uppercase;
                letter-spacing: 0.1em;
                color: var(--fg-dim);
                font-weight: 600;
            }
            .contact-list a {
                font-weight: 500;
                word-break: break-all;
                color: var(--accent);
            }
            .contact-list a:hover {
                color: var(--accent-hover);
            }
            .contact-list .dim {
                color: var(--fg-dim);
                font-weight: 400;
            }

            /* ================ Footer ================ */
            /* Three-column landing-page footer (Linear/Vercel-style):
               brand block on the left (logo + tagline + email), two
               nav columns on the right (site sections + docs). Bottom
               strip carries copyright and the licence/release fine
               print, separated by a hairline rule. Collapses to a
               single column on mobile. */
            footer {
                padding: 4rem 0 2.5rem;
                color: var(--fg-dim);
                font-size: 0.875rem;
                border-top: 1px solid var(--border);
                background: var(--bg-elevated);
            }
            footer .wrap {
                display: flex;
                flex-direction: column;
                gap: 2.5rem;
            }
            .footer-grid {
                display: grid;
                grid-template-columns: 2fr 1fr 1fr;
                gap: 3rem;
                align-items: start;
            }
            .footer-brand {
                display: flex;
                flex-direction: column;
                gap: 1rem;
                max-width: 22rem;
            }
            .footer-brand .brand {
                display: inline-flex;
                align-items: center;
                gap: 0.75rem;
                color: var(--fg);
                text-decoration: none;
                font-weight: 600;
                font-size: 1.125rem;
                letter-spacing: -0.01em;
            }
            .footer-brand .brand-mark {
                flex-shrink: 0;
            }
            .footer-brand .brand-dot {
                color: var(--accent);
            }
            .footer-brand .tagline {
                color: var(--fg-muted);
                line-height: 1.55;
                margin: 0;
                font-size: 0.9375rem;
            }
            .footer-brand .brand-email {
                font-family: ui-monospace, SFMono-Regular, Menlo,
                    'JetBrains Mono', monospace;
                font-size: 0.8125rem;
                color: var(--fg-muted);
                text-decoration: none;
                transition: color 120ms ease;
                align-self: flex-start;
            }
            .footer-brand .brand-email:hover {
                color: var(--accent);
            }
            .footer-col {
                display: flex;
                flex-direction: column;
                gap: 1rem;
            }
            .footer-heading {
                font-size: 0.75rem;
                font-weight: 700;
                text-transform: uppercase;
                letter-spacing: 0.1em;
                color: var(--fg-dim);
                margin: 0;
            }
            .footer-col ul {
                list-style: none;
                padding: 0;
                margin: 0;
                display: flex;
                flex-direction: column;
                gap: 0.625rem;
            }
            footer :where(a) {
                color: var(--fg-muted);
                text-decoration: none;
                transition: color 120ms ease;
                font-size: 0.9375rem;
            }
            footer :where(a):hover {
                color: var(--accent);
            }
            footer .footer-bottom {
                display: flex;
                flex-wrap: wrap;
                column-gap: 1.25rem;
                row-gap: 0.5rem;
                align-items: baseline;
                font-size: 0.8125rem;
                color: var(--fg-dim);
                line-height: 1.5;
                padding-top: 2rem;
                border-top: 1px solid var(--border);
            }
            footer .copyright {
                font-weight: 500;
                color: var(--fg-muted);
            }
            /* Hidden /qr entry point — the year reads as plain copyright
               text. On hover the accent-green tint reveals it's a link;
               that's the only visual cue. */
            footer .ft-secret {
                color: inherit;
                text-decoration: none;
                transition: color 150ms ease;
            }
            footer .ft-secret:hover {
                color: var(--accent);
            }
            footer .license {
                flex: 1 1 auto;
            }

            /* ================ Focus ring (all elements) ================ */
            :focus {
                outline: none;
            }
            :focus-visible {
                outline: 2px solid var(--accent);
                outline-offset: 3px;
                border-radius: 0.375rem;
            }

            /* ================ Responsive ================ */
            @media (max-width: 60rem) {
                .jump-nav {
                    display: none;
                }
                .ghost.icon.menu-btn {
                    display: inline-flex;
                }
                .integrations .grid {
                    grid-template-columns: repeat(2, 1fr);
                }
                /* Footer: brand spans the full top row, the two nav
                   columns sit side-by-side underneath. */
                .footer-grid {
                    grid-template-columns: 1fr 1fr;
                    gap: 2.5rem;
                }
                .footer-brand {
                    grid-column: 1 / -1;
                    max-width: 30rem;
                }
            }

            @media (max-width: 48rem) {
                :host {
                    /* All vertical rhythm shrinks ~25% on mobile so the
                       2rem desktop margins don't dwarf the now-3.5rem
                       section padding. Card paddings shrink in lockstep
                       (1.625rem → 1.25rem / 1rem-1.25rem → 0.875rem-1rem)
                       so card chrome doesn't crowd content on narrow
                       viewports. */
                    --section-y: 3.5rem;
                    --card-padding: 1.25rem;
                    --card-padding-compact: 0.875rem 1rem;
                }
                .hero {
                    padding: 4rem 0 3rem;
                }
                .hero-grid {
                    grid-template-columns: 1fr;
                    gap: 2rem;
                }
                .hero-visual {
                    justify-content: center;
                }
                .hero-visual svg {
                    max-width: 280px;
                }
                .steps ol,
                .wallets ul,
                .why .grid,
                .integrations .grid,
                .standard .tech-grid,
                .standard .timeline,
                .competitors .grid,
                .migration ol,
                .privacy-list {
                    grid-template-columns: 1fr;
                }
                section h2 {
                    margin-bottom: 1.5rem;
                }
                .lede {
                    margin-bottom: 1.5rem;
                }
                .sub-title {
                    margin: 2.5rem 0 1rem;
                    font-size: 1.125rem;
                }
                .eyebrow {
                    margin-bottom: 0.75rem;
                }
                .standard .refs {
                    grid-template-columns: 1fr;
                }
                /* Countdown tiles tighten so 4 fit without crowding on
                   narrow viewports — 0.75rem h-padding leaves only ~42px
                   for the 2.25rem number, which is borderline. */
                .source .unit {
                    padding: 0.875rem 0.5rem;
                }
                .source .num {
                    font-size: 1.875rem;
                }
                .source .countdown {
                    gap: 0.5rem;
                }
                /* Footer fully stacks single-column on phones. */
                footer {
                    padding: 3rem 0 2rem;
                }
                .footer-grid {
                    grid-template-columns: 1fr;
                    gap: 2rem;
                }
                .footer-brand {
                    grid-column: auto;
                }
            }

            @media (max-width: 32rem) {
                :host {
                    --wrap-x: 1.125rem;
                }
                .topbar .wrap {
                    gap: 0.5rem;
                }
                .brand-text {
                    font-size: 0.9375rem;
                }
                .brand-mark {
                    width: 22px;
                    height: 22px;
                }
                .contact-list li {
                    grid-template-columns: 1fr;
                    gap: 0.375rem;
                }
                .roadmap li {
                    grid-template-columns: 2rem 1fr;
                    gap: 0.875rem;
                }
                .roadmap li:not(:last-child)::before {
                    left: calc(1rem - 1px);
                    top: 2.25rem;
                }
                .roadmap .step-num {
                    width: 2rem;
                    height: 2rem;
                    font-size: 0.8125rem;
                }
                .roadmap h3 {
                    font-size: 1rem;
                }
                .roadmap .badge {
                    font-size: 0.625rem;
                }
            }
        `
    ]
})
export class LandingPage {
    readonly language = inject(LanguageService);
    readonly theme = inject(ThemeService);

    /* Mobile-menu overlay state. Toggled by the hamburger button in
       the topbar; closed on backdrop tap or any nav-link click. */
    readonly menuOpen = signal(false);

    /* Order matches the topbar jump-nav (page top→bottom). The leading
       "01"–"06" labels are pre-formatted strings — Angular templates
       can't easily call padStart, and pre-computing keeps the markup
       declarative. */
    readonly mobileNavItems: ReadonlyArray<{
        num: string;
        key: string;
        href: string;
    }> = [
        { num: '01', key: 'nav.migration', href: '#migration' },
        { num: '02', key: 'nav.about', href: '#about' },
        { num: '03', key: 'nav.faq', href: '#faq' },
        { num: '04', key: 'nav.plan', href: '#roadmap' },
        { num: '05', key: 'nav.source', href: '#source' },
        { num: '06', key: 'nav.contact', href: '#contact' }
    ];

    /* Aria/title text for the theme cycler — reflects the *next* mode
       so it reads "switch to <next>" out loud. Cycle: auto → light →
       dark → auto. */
    readonly themeAriaLabel = computed(() => {
        const t = this.theme.theme();
        if (t === 'auto') return 'nav.theme_switch_to_light';
        if (t === 'light') return 'nav.theme_switch_to_dark';
        return 'nav.theme_switch_to_auto';
    });

    readonly sourceReleasedUrl = 'https://github.com/ilia-ae/aliro-mayday';
    readonly sourceDocumentationUrl = computed(() =>
        `https://github.com/ilia-ae/aliro-mayday/blob/main/docs/README${this.language.lang() === 'ru' ? '.ru' : ''}.md`
    );
    readonly sourceWiringUrl = computed(() =>
        `https://github.com/ilia-ae/aliro-mayday/blob/main/docs/wiring${this.language.lang() === 'ru' ? '.ru' : ''}.md`
    );

    constructor() {
        const destroyRef = inject(DestroyRef);

        // Lock body scroll while the mobile menu overlay is open so
        // the page underneath doesn't slide around behind the sheet.
        effect(() => {
            document.body.style.overflow = this.menuOpen() ? 'hidden' : '';
        });
        destroyRef.onDestroy(() => {
            document.body.style.overflow = '';
        });

        // ── Mobile-menu cleanup hooks ──────────────────────────────
        // Close on Escape so users with keyboards aren't trapped.
        const onKey = (e: KeyboardEvent) => {
            if (e.key === 'Escape' && this.menuOpen()) {
                this.menuOpen.set(false);
            }
        };
        document.addEventListener('keydown', onKey);
        destroyRef.onDestroy(() =>
            document.removeEventListener('keydown', onKey)
        );

        // Close once the viewport widens past the 60rem breakpoint —
        // otherwise resizing while the sheet is open leaves a phantom
        // body-scroll lock on desktop and the @media display:none rule
        // hides the overlay but keeps menuOpen() === true.
        if (typeof window !== 'undefined' && window.matchMedia) {
            const mq = window.matchMedia('(min-width: 60.001rem)');
            const onWide = (e: MediaQueryListEvent | MediaQueryList) => {
                if (e.matches && this.menuOpen()) this.menuOpen.set(false);
            };
            mq.addEventListener('change', onWide);
            // Run once at boot in case Angular hydrates above the
            // breakpoint with a stale (cached) menuOpen signal.
            onWide(mq);
            destroyRef.onDestroy(() =>
                mq.removeEventListener('change', onWide)
            );
        }
    }

    readonly roadmap: ReadonlyArray<{ n: number; status: RoadmapStatus }> = [
        { n: 1, status: 'done' },
        { n: 2, status: 'done' },
        { n: 3, status: 'active' },
        { n: 4, status: 'active' },
        { n: 5, status: 'planned' },
        { n: 6, status: 'planned' },
        { n: 7, status: 'goal' }
    ];

    readonly stackGroups: ReadonlyArray<{
        key: string;
        items: ReadonlyArray<{ key: string; url?: string }>;
    }> = [
        {
            key: 'standards',
            items: [
                { key: 'aliro', url: 'https://csa-iot.org/all-solutions/aliro/' },
                { key: 'matter', url: 'https://csa-iot.org/all-solutions/matter/' },
                { key: 'iso7816', url: 'https://en.wikipedia.org/wiki/ISO/IEC_7816' }
            ]
        },
        {
            key: 'hardware',
            items: [
                { key: 'esp32', url: 'https://www.espressif.com/en/products/socs/esp32-s3' },
                { key: 'pn532', url: 'https://nfc-tools.github.io/resources/chips/pn53x/' },
                { key: 'nfc', url: 'https://nfc-forum.org/' },
                { key: 'ble', url: 'https://www.bluetooth.com/' },
                { key: 'uwb', url: 'https://www.firaconsortium.org/' },
                { key: 'ecdsa', url: 'https://en.wikipedia.org/wiki/Elliptic_Curve_Digital_Signature_Algorithm' },
                { key: 'aesgcm', url: 'https://en.wikipedia.org/wiki/Galois/Counter_Mode' }
            ]
        },
        {
            key: 'integrations',
            items: [
                { key: 'mqtt', url: 'https://mqtt.org/' },
                { key: 'http', url: 'https://en.wikipedia.org/wiki/HTTP' },
                { key: 'telegram', url: 'https://core.telegram.org/bots/api' },
                { key: 'prometheus', url: 'https://prometheus.io/' },
                { key: 'grafana', url: 'https://grafana.com/' },
                { key: 'homeassistant', url: 'https://www.home-assistant.io/' },
                { key: 'frigate', url: 'https://frigate.video/' },
                { key: 'onvif', url: 'https://www.onvif.org/' }
            ]
        }
    ];

    readonly ecosystemCompanies: ReadonlyArray<string> = [
        'Apple',
        'Google',
        'Samsung',
        'ASSA ABLOY',
        'Allegion',
        'Kastle Systems',
        'Last Lock',
        'Aqara',
        'NXP',
        'Infineon',
        'Qualcomm',
        'STMicroelectronics'
    ];

    readonly standardRefs: ReadonlyArray<{ key: string; url: string; host: string }> = [
        {
            key: 'csa',
            url: 'https://csa-iot.org/all-solutions/aliro/',
            host: 'csa-iot.org'
        },
        {
            key: 'spec',
            url: 'https://csa-iot.org/wp-content/uploads/2026/02/26-42802-001_Aliro_1.0_specification.pdf',
            host: 'csa-iot.org · 26-42802-001 (PDF, 195 pp)'
        },
        {
            key: 'faq',
            url: 'https://csa-iot.org/all-solutions/aliro/aliro-faq/',
            host: 'csa-iot.org'
        },
        {
            key: 'matter',
            url: 'https://github.com/project-chip/connectedhomeip/blob/master/data_model/1.5/clusters/DoorLock.xml',
            host: 'github.com · project-chip/connectedhomeip'
        },
        {
            key: 'kormax',
            url: 'https://github.com/kormax/aliro',
            host: 'github.com · kormax/aliro'
        },
        {
            key: 'nordic',
            url: 'https://github.com/nrfconnect/ncs-door-lock-and-access-control',
            host: 'github.com · nrfconnect'
        }
    ];

    readonly competitorLocks: ReadonlyArray<{
        key: string;
        name: string;
        priceRange: string;
        year: string;
    }> = [
        { key: 'aqara_u200', name: 'Aqara U200', priceRange: '~$220', year: '2024' },
        { key: 'schlage_encode', name: 'Schlage Encode Plus', priceRange: '~$300', year: '2023' },
        { key: 'yale_assure', name: 'Yale Assure Lock 2', priceRange: '~$280', year: '2023' },
        { key: 'level_bolt', name: 'Level Lock+', priceRange: '~$365', year: '2022' }
    ];

    readonly comparisonRows: ReadonlyArray<ComparisonRow> = [
        { key: 'open_standards', ours: 'yes', them: 'partial' },
        { key: 'matter', ours: 'yes', them: 'partial' },
        { key: 'cross_wallet', ours: 'partial', them: 'partial' },
        { key: 'legacy_cards', ours: 'no', them: 'no' },
        { key: 'offline', ours: 'partial', them: 'partial' },
        { key: 'self_hosted', ours: 'partial', them: 'no' },
        { key: 'http_api', ours: 'partial', them: 'no' },
        { key: 'telegram', ours: 'no', them: 'no' },
        { key: 'cctv_triggers', ours: 'no', them: 'partial' },
        { key: 'tamper_alerts', ours: 'no', them: 'partial' },
        { key: 'prometheus', ours: 'no', them: 'no' }
    ];
}
