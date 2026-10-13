import {
    ChangeDetectionStrategy,
    Component,
    OnInit,
    computed,
    inject,
    signal
} from '@angular/core';
import { DOCUMENT } from '@angular/common';
import { DomSanitizer, SafeHtml } from '@angular/platform-browser';
import { TranslatePipe, TranslateService } from '@ngx-translate/core';
import qrcode from 'qrcode-generator';
import { LanguageService } from '@/app/shared/services/language.service';
import { ThemeService } from '@/app/shared/services/theme.service';

type ShareKind =
    | 'native'
    | 'copy'
    | 'email'
    | 'linkedin'
    | 'facebook'
    | 'whatsapp'
    | 'telegram'
    | 'x'
    | 'vk';

interface ShareItem {
    kind: ShareKind;
    labelKey: string;
}

/* Standalone QR-business-card page at /qr.

   Distributed via printed QR codes (stickers, business cards) — visitors
   land here, see the brand, can scan-back the URL with another device, or
   use the share buttons to push the link to any messenger. The page is
   fullscreen by design (no topbar, no footer, no chrome) so the only
   visual is the card itself. */
@Component({
    selector: 'app-qr',
    standalone: true,
    imports: [TranslatePipe],
    changeDetection: ChangeDetectionStrategy.OnPush,
    template: `
        <div class="qr-bg" aria-hidden="true">
            <div class="qr-glow qr-glow--1"></div>
            <div class="qr-glow qr-glow--2"></div>
            <div class="qr-glow qr-glow--3"></div>
            <div class="qr-grid"></div>
            <div class="qr-noise"></div>
        </div>

        <main class="qr-main">
            <article class="qr-card">
                <header class="qr-head">
                    <span class="qr-logo">
                        <svg
                            class="qr-logo__mark"
                            viewBox="0 0 64 64"
                            width="32"
                            height="32"
                            aria-hidden="true"
                        >
                            <rect width="64" height="64" rx="14" fill="var(--accent)"/>
                            <path
                                d="M18 48 L32 16 L46 48 M23.5 38 L40.5 38"
                                fill="none"
                                stroke="#ffffff"
                                stroke-width="5"
                                stroke-linecap="round"
                                stroke-linejoin="round"
                            />
                        </svg>
                        <span class="qr-logo__text">
                            aliro<span class="qr-logo__dot">.</span>mayday
                        </span>
                    </span>
                    <div class="qr-controls">
                        <button
                            type="button"
                            class="qr-ctrl qr-ctrl--lang"
                            (click)="languageService.toggle()"
                            [attr.aria-label]="'nav.lang_aria_label' | translate"
                        >
                            {{ 'nav.lang_other' | translate }}
                        </button>
                        <button
                            type="button"
                            class="qr-ctrl qr-ctrl--theme"
                            (click)="themeService.cycle()"
                            [attr.aria-label]="themeAriaLabel() | translate"
                            [title]="themeAriaLabel() | translate"
                        >
                            @switch (themeService.theme()) {
                                @case ('light') {
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                        <circle cx="12" cy="12" r="4"/>
                                        <path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/>
                                    </svg>
                                }
                                @case ('dark') {
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                        <path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8Z"/>
                                    </svg>
                                }
                                @default {
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                                        <circle cx="12" cy="12" r="9"/>
                                        <path d="M12 3a9 9 0 0 1 0 18z" fill="currentColor" stroke="none"/>
                                    </svg>
                                }
                            }
                        </button>
                        <span class="qr-eyebrow">{{ 'qr.eyebrow' | translate }}</span>
                    </div>
                </header>

                <div class="qr-hero">
                    <h1 class="qr-title">aliro.mayday</h1>
                    <p class="qr-tagline">{{ 'qr.tagline' | translate }}</p>
                </div>

                <div class="qr-code-wrap">
                    <div class="qr-code">
                        <div
                            class="qr-code__svg"
                            role="img"
                            [attr.aria-label]="'qr.codeAlt' | translate"
                            [innerHTML]="qrSvg()"
                        ></div>
                        <div class="qr-code__badge" aria-hidden="true">
                            <svg
                                viewBox="0 0 64 64"
                                width="30"
                                height="30"
                            >
                                <rect width="64" height="64" rx="14" fill="var(--accent)"/>
                                <path
                                    d="M18 48 L32 16 L46 48 M23.5 38 L40.5 38"
                                    fill="none"
                                    stroke="#ffffff"
                                    stroke-width="5"
                                    stroke-linecap="round"
                                    stroke-linejoin="round"
                                />
                            </svg>
                        </div>
                    </div>
                    <p class="qr-scan-hint">{{ 'qr.scanHint' | translate }}</p>
                </div>

                <section class="qr-share">
                    <h2 class="qr-share__title">
                        {{ 'qr.shareTitle' | translate }}
                    </h2>
                    <div class="qr-share__grid">
                        @for (item of shareItems; track item.kind) {
                            <button
                                type="button"
                                class="qr-chip"
                                (click)="onShare(item.kind)"
                                [attr.aria-label]="item.labelKey | translate"
                            >
                                <span
                                    class="qr-chip__icon"
                                    [innerHTML]="
                                        item.kind === 'copy' && copied()
                                            ? checkSvg
                                            : iconFor(item.kind)
                                    "
                                ></span>
                                <span class="qr-chip__label">
                                    @if (item.kind === 'copy' && copied()) {
                                        {{ 'qr.shareCopied' | translate }}
                                    } @else {
                                        {{ item.labelKey | translate }}
                                    }
                                </span>
                            </button>
                        }
                    </div>
                </section>

                <section class="qr-links">
                    <h2 class="qr-links__title">
                        {{ 'qr.linksTitle' | translate }}
                    </h2>
                    <div class="qr-links__list">
                        <a class="qr-link" [href]="origin() + '/'">
                            <span class="qr-link__label">
                                {{ 'qr.linkSite' | translate }}
                            </span>
                            <span class="qr-link__arrow" aria-hidden="true">→</span>
                        </a>
                        <a class="qr-link" [href]="origin() + '/#contact'">
                            <span class="qr-link__label">
                                {{ 'qr.linkContact' | translate }}
                            </span>
                            <span class="qr-link__arrow" aria-hidden="true">→</span>
                        </a>
                        <a class="qr-link" [href]="origin() + '/#about'">
                            <span class="qr-link__label">
                                {{ 'qr.linkAbout' | translate }}
                            </span>
                            <span class="qr-link__arrow" aria-hidden="true">→</span>
                        </a>
                        <a class="qr-link" [href]="origin() + '/privacy'">
                            <span class="qr-link__label">
                                {{ 'qr.linkPrivacy' | translate }}
                            </span>
                            <span class="qr-link__arrow" aria-hidden="true">→</span>
                        </a>
                    </div>
                </section>
            </article>
        </main>
    `,
    styles: [
        `
            :host {
                display: block;
                min-height: 100dvh;
                background: var(--bg);
                color: var(--fg);
                position: relative;
                overflow-x: hidden;
            }

            /* ── animated mesh background ── */
            .qr-bg {
                position: fixed;
                inset: 0;
                pointer-events: none;
                z-index: 0;
                /* Contain the absolutely-positioned glows so blur() halos
                   that extend past the viewport edge can't trigger a
                   horizontal scrollbar on narrow widths. */
                overflow: hidden;
                background: linear-gradient(
                    180deg,
                    color-mix(in srgb, var(--accent) 8%, var(--bg)) 0%,
                    var(--bg) 45%,
                    color-mix(in srgb, var(--fg-muted) 6%, var(--bg)) 100%
                );
            }
            .qr-glow {
                position: absolute;
                border-radius: 50%;
                filter: blur(90px);
            }
            /* Dominant green glow — keeps the brand color front-and-center. */
            .qr-glow--1 {
                width: 480px;
                height: 480px;
                background: var(--accent);
                opacity: 0.32;
                top: -10%;
                left: -10%;
                animation: qr-float 20s ease-in-out infinite;
            }
            /* Neutral muted glow — adds depth without breaking the
               monochromatic-plus-green aliro palette. */
            .qr-glow--2 {
                width: 380px;
                height: 380px;
                background: var(--fg-muted);
                opacity: 0.18;
                bottom: -8%;
                right: -10%;
                animation: qr-float 24s ease-in-out infinite reverse;
            }
            .qr-glow--3 {
                width: 320px;
                height: 320px;
                background: var(--accent);
                opacity: 0.14;
                top: 50%;
                left: 60%;
                animation: qr-float 18s ease-in-out infinite;
            }
            .qr-grid {
                position: absolute;
                inset: 0;
                background-image:
                    linear-gradient(
                        color-mix(in srgb, var(--fg) 6%, transparent) 1px,
                        transparent 1px
                    ),
                    linear-gradient(
                        90deg,
                        color-mix(in srgb, var(--fg) 6%, transparent) 1px,
                        transparent 1px
                    );
                background-size: 56px 56px;
                mask-image: radial-gradient(
                    ellipse 70% 60% at 50% 40%,
                    #000 20%,
                    transparent 75%
                );
                -webkit-mask-image: radial-gradient(
                    ellipse 70% 60% at 50% 40%,
                    #000 20%,
                    transparent 75%
                );
            }
            .qr-noise {
                position: absolute;
                inset: 0;
                background: url("data:image/svg+xml,%3Csvg viewBox='0 0 256 256' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence baseFrequency='0.65' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.05'/%3E%3C/svg%3E");
                background-size: 200px;
                opacity: 0.5;
            }
            @keyframes qr-float {
                0%,
                100% {
                    transform: translate(0, 0) scale(1);
                }
                33% {
                    transform: translate(40px, -30px) scale(1.06);
                }
                66% {
                    transform: translate(-30px, 30px) scale(0.96);
                }
            }

            /* ── card ──
               Sized so the whole card (header → quick links) fits in a
               single laptop viewport (~720px content area). Tighter
               than the mayday.software original because aliro's quick-
               links list is denser (4 rows). */
            .qr-main {
                position: relative;
                z-index: 1;
                min-height: 100dvh;
                display: flex;
                align-items: center;
                justify-content: center;
                padding: 1rem;
            }
            .qr-card {
                width: 100%;
                max-width: 440px;
                background: color-mix(
                    in srgb,
                    var(--bg-elevated) 86%,
                    transparent
                );
                -webkit-backdrop-filter: blur(20px) saturate(160%);
                backdrop-filter: blur(20px) saturate(160%);
                border: 1px solid var(--border);
                border-radius: 1.25rem;
                padding: 1.25rem 1.25rem 1rem;
                box-shadow:
                    0 24px 48px -12px rgba(0, 0, 0, 0.18),
                    0 8px 16px -4px rgba(0, 0, 0, 0.08);
                display: flex;
                flex-direction: column;
                gap: 0.875rem;
                animation: qr-rise 600ms ease-out both;
            }
            @keyframes qr-rise {
                from {
                    opacity: 0;
                    transform: translateY(16px);
                }
                to {
                    opacity: 1;
                    transform: translateY(0);
                }
            }

            .qr-head {
                display: flex;
                align-items: center;
                justify-content: space-between;
                gap: 1rem;
            }
            .qr-logo {
                display: inline-flex;
                align-items: center;
                gap: 0.625rem;
            }
            .qr-logo__mark {
                flex-shrink: 0;
                display: block;
            }
            .qr-logo__text {
                font-family: var(--font-display);
                font-size: 1.0625rem;
                font-weight: 600;
                letter-spacing: -0.02em;
                color: var(--fg);
            }
            .qr-logo__dot {
                color: var(--accent);
            }
            .qr-eyebrow {
                font-size: 0.6875rem;
                font-weight: 700;
                letter-spacing: 0.14em;
                text-transform: uppercase;
                color: var(--accent);
                padding: 0.25rem 0.625rem;
                border: 1px solid
                    color-mix(in srgb, var(--accent) 35%, var(--border));
                border-radius: 999px;
                background: color-mix(in srgb, var(--accent) 8%, transparent);
                white-space: nowrap;
            }

            /* Compact lang/theme controls + eyebrow pill, packed tight
               on the right of the header row so the brand mark on the
               left has room to breathe. */
            .qr-controls {
                display: inline-flex;
                align-items: center;
                gap: 0.375rem;
                flex-shrink: 0;
            }
            .qr-ctrl {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                height: 28px;
                min-width: 28px;
                padding: 0 0.5rem;
                background: transparent;
                border: 1px solid var(--border);
                border-radius: 0.5rem;
                color: var(--fg-muted);
                font-size: 0.75rem;
                font-weight: 600;
                font-family: inherit;
                letter-spacing: 0.02em;
                cursor: pointer;
                transition:
                    color 150ms ease,
                    border-color 150ms ease,
                    background-color 150ms ease;
            }
            .qr-ctrl:hover {
                color: var(--fg);
                border-color: var(--border-strong);
            }
            .qr-ctrl--theme {
                padding: 0;
                width: 28px;
            }

            .qr-hero {
                display: flex;
                flex-direction: column;
                gap: 0.375rem;
            }
            .qr-title {
                font-size: clamp(1.375rem, 4.5vw, 1.625rem);
                font-weight: 700;
                letter-spacing: -0.025em;
                line-height: 1.1;
                background: linear-gradient(
                    135deg,
                    var(--accent) 0%,
                    var(--fg) 100%
                );
                -webkit-background-clip: text;
                -webkit-text-fill-color: transparent;
                background-clip: text;
                color: transparent;
            }
            .qr-tagline {
                font-size: 0.875rem;
                color: var(--fg-muted);
                line-height: 1.45;
            }

            .qr-code-wrap {
                display: flex;
                flex-direction: column;
                align-items: center;
                gap: 0.5rem;
            }
            .qr-code {
                position: relative;
                padding: 0.75rem;
                border-radius: 0.75rem;
                background: #ffffff;
                border: 1px solid var(--border);
                box-shadow: 0 6px 18px -6px rgba(0, 0, 0, 0.18);
                line-height: 0;
            }
            .qr-code__svg {
                display: block;
                width: 200px;
                height: 200px;
                border-radius: 0.375rem;
                overflow: hidden;
                line-height: 0;
            }
            .qr-code__svg svg {
                display: block;
                width: 100%;
                height: 100%;
            }
            /* Aliro brand-mark badge floating over the QR center. ECC
               level H gives ~30% data redundancy. With a 200×200 QR
               and a 44×44 badge, the badge occupies ~22% of the area
               — comfortably inside the safe margin so scans still
               resolve cleanly. */
            .qr-code__badge {
                position: absolute;
                top: 50%;
                left: 50%;
                transform: translate(-50%, -50%);
                width: 44px;
                height: 44px;
                display: grid;
                place-items: center;
                background: #ffffff;
                border-radius: 0.5rem;
                box-shadow: 0 4px 12px rgba(0, 0, 0, 0.12);
            }
            .qr-scan-hint {
                font-size: 0.75rem;
                color: var(--fg-dim);
                text-align: center;
                max-width: 36ch;
                line-height: 1.4;
            }

            /* ── share grid ── */
            .qr-share__title,
            .qr-links__title {
                font-size: 0.6875rem;
                font-weight: 700;
                letter-spacing: 0.14em;
                text-transform: uppercase;
                color: var(--fg-dim);
                margin: 0 0 0.5rem;
            }
            /* Fixed 3×3 grid — never auto-fill, otherwise the 9 cells
               break into 4+4+1 / 5+4 layouts on intermediate widths and
               the geometry stops feeling deliberate. */
            .qr-share__grid {
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                gap: 0.375rem;
            }
            .qr-chip {
                display: inline-flex;
                align-items: center;
                gap: 0.5rem;
                padding: 0.5rem 0.625rem;
                background: var(--bg-elevated);
                border: 1px solid var(--border);
                border-radius: 0.5rem;
                color: var(--fg);
                font-size: 0.8125rem;
                font-weight: 500;
                cursor: pointer;
                transition:
                    border-color 150ms ease,
                    background-color 150ms ease,
                    transform 150ms ease;
                text-align: left;
                font-family: inherit;
            }
            .qr-chip:hover {
                border-color: var(--border-strong);
                transform: translateY(-1px);
            }
            .qr-chip:active {
                transform: translateY(0);
            }
            .qr-chip__icon {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                width: 18px;
                height: 18px;
                color: var(--accent);
                flex-shrink: 0;
            }
            .qr-chip__label {
                white-space: nowrap;
                overflow: hidden;
                text-overflow: ellipsis;
            }

            /* ── quick links ── */
            .qr-links__list {
                display: flex;
                flex-direction: column;
                gap: 0.125rem;
            }
            .qr-link {
                display: flex;
                align-items: center;
                justify-content: space-between;
                gap: 0.75rem;
                padding: 0.4375rem 0.75rem;
                border-radius: 0.5rem;
                background: transparent;
                border: 1px solid transparent;
                color: var(--fg);
                font-size: 0.8125rem;
                font-weight: 500;
                text-decoration: none;
                transition:
                    border-color 150ms ease,
                    background-color 150ms ease;
            }
            .qr-link:hover {
                background: color-mix(in srgb, var(--bg-elevated) 70%, transparent);
                border-color: var(--border);
                color: var(--fg);
            }
            .qr-link__arrow {
                color: var(--fg-dim);
                font-size: 0.9375rem;
                transition:
                    transform 150ms ease,
                    color 150ms ease;
            }
            .qr-link:hover .qr-link__arrow {
                color: var(--accent);
                transform: translateX(2px);
            }

            @media (max-width: 30rem) {
                .qr-card {
                    padding: 1.5rem 1.125rem 1.25rem;
                    border-radius: 1rem;
                    gap: 1.25rem;
                }
                .qr-code__svg {
                    width: 240px;
                    height: 240px;
                }
                .qr-eyebrow {
                    display: none;
                }
                /* Long labels like "WhatsApp" / "LinkedIn" don't fit
                   horizontally in a 3-column grid on phones — stack
                   icon over label so each chip stays compact. */
                .qr-chip {
                    flex-direction: column;
                    align-items: center;
                    justify-content: center;
                    gap: 0.375rem;
                    padding: 0.625rem 0.375rem;
                    text-align: center;
                    font-size: 0.75rem;
                }
                .qr-chip__label {
                    white-space: nowrap;
                    overflow: hidden;
                    text-overflow: ellipsis;
                    max-width: 100%;
                }
            }
        `
    ]
})
export class QrPage implements OnInit {
    private readonly document = inject(DOCUMENT);
    private readonly translate = inject(TranslateService);
    /* Public so the template can call .toggle() / .cycle() directly on
       the lang/theme buttons in the card header. */
    readonly languageService = inject(LanguageService);
    readonly themeService = inject(ThemeService);
    private readonly sanitizer = inject(DomSanitizer);

    readonly origin = signal('https://aliro.mayday.software');
    readonly copied = signal(false);

    /* Aria/title for the theme cycler — reflects the *next* mode in the
       cycle (auto → light → dark → auto). */
    readonly themeAriaLabel = computed(() => {
        const t = this.themeService.theme();
        if (t === 'auto') return 'nav.theme_switch_to_light';
        if (t === 'light') return 'nav.theme_switch_to_dark';
        return 'nav.theme_switch_to_auto';
    });

    /* 9 chips on a fixed 3×3 grid (no auto-fill — keeps geometry
       identical at every viewport). Order is read row-by-row:
         row 1: native  | copy     | email      (universal actions)
         row 2: linkedin| facebook | whatsapp   (the western trio)
         row 3: telegram| x        | vk         (messaging + RU/X social)
       native is always rendered; on desktop where Web Share API is
       missing it falls back to copy so the chip is never dead. */
    readonly shareItems: ReadonlyArray<ShareItem> = [
        { kind: 'native', labelKey: 'qr.shareNative' },
        { kind: 'copy', labelKey: 'qr.shareCopy' },
        { kind: 'email', labelKey: 'qr.shareEmail' },
        { kind: 'linkedin', labelKey: 'qr.shareLinkedIn' },
        { kind: 'facebook', labelKey: 'qr.shareFacebook' },
        { kind: 'whatsapp', labelKey: 'qr.shareWhatsApp' },
        { kind: 'telegram', labelKey: 'qr.shareTelegram' },
        { kind: 'x', labelKey: 'qr.shareX' },
        { kind: 'vk', labelKey: 'qr.shareVK' }
    ];

    /* Client-side SVG QR — no external API. Type 0 lets the lib pick
       the smallest version that fits the URL; ECC level H gives ~30%
       data redundancy so the centered brand badge can occlude ~20% of
       the code without breaking scannability. The SVG scales to its
       container (preserveAspectRatio + 100% w/h) and is passed through
       bypassSecurityTrustHtml so Angular's DomSanitizer doesn't strip
       the <path>. The encoded payload is the site origin (root) — a
       scan from another phone lands on the homepage, not on /qr. */
    readonly qrSvg = computed<SafeHtml>(() => {
        const qr = qrcode(0, 'H');
        qr.addData(this.origin());
        qr.make();
        const count = qr.getModuleCount();
        const margin = 2;
        const size = count + margin * 2;

        let path = '';
        for (let r = 0; r < count; r++) {
            for (let c = 0; c < count; c++) {
                if (qr.isDark(r, c)) {
                    path += `M${c + margin} ${r + margin}h1v1h-1z`;
                }
            }
        }

        const svg =
            `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${size} ${size}" ` +
            `shape-rendering="crispEdges" preserveAspectRatio="xMidYMid meet">` +
            `<rect width="${size}" height="${size}" fill="#ffffff"/>` +
            `<path d="${path}" fill="#0a0a0c"/>` +
            `</svg>`;

        return this.sanitizer.bypassSecurityTrustHtml(svg);
    });

    readonly checkSvg: SafeHtml = this.sanitizer.bypassSecurityTrustHtml(
        `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"/></svg>`
    );

    /* Inline SVG strings — passed through bypassSecurityTrustHtml so
       Angular's DomSanitizer doesn't strip them when we [innerHTML] into
       the chip icon slot. */
    private readonly icons: Record<ShareKind, string> = {
        native: `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 12v7a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-7"/><path d="M16 6l-4-4-4 4"/><path d="M12 2v14"/></svg>`,
        copy: `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>`,
        telegram: `<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M21.94 4.34c-.31-.26-.78-.3-1.18-.13L2.77 11.7c-.46.19-.74.66-.7 1.16.05.5.41.91.9 1.02l4.51 1.04 1.74 5.38c.13.39.46.67.86.74.4.06.81-.1 1.05-.42l2.33-3.05 4.59 3.39c.21.16.46.24.71.24.16 0 .31-.03.46-.09.4-.16.69-.51.76-.94l2.32-13.74c.07-.42-.11-.84-.46-1.09zM9.74 14.45l-.65 3.55-1.13-3.5 7.7-5.7-5.92 5.65z"/></svg>`,
        whatsapp: `<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M17.5 14.4c-.3-.15-1.77-.87-2.04-.97-.27-.1-.47-.15-.67.15-.2.3-.77.97-.95 1.17-.17.2-.35.22-.65.07-1.77-.88-2.93-1.58-4.1-3.58-.31-.53.31-.5.88-1.65.1-.2.05-.37-.02-.52-.07-.15-.67-1.62-.92-2.22-.24-.58-.49-.5-.67-.51l-.57-.01c-.2 0-.52.07-.79.37-.27.3-1.04 1.02-1.04 2.48 0 1.46 1.06 2.88 1.21 3.07.15.2 2.09 3.2 5.07 4.49.71.3 1.26.49 1.69.63.71.22 1.36.19 1.87.12.57-.08 1.77-.72 2.02-1.42.25-.7.25-1.3.17-1.42-.07-.13-.27-.2-.57-.35zM12 2C6.48 2 2 6.48 2 12c0 1.78.46 3.45 1.27 4.9L2 22l5.25-1.38A9.93 9.93 0 0 0 12 22c5.52 0 10-4.48 10-10S17.52 2 12 2zm5.83 14.83A8 8 0 0 1 7.7 18.04l-.4-.24-3.07.81.82-2.99-.26-.41A8 8 0 1 1 17.83 16.83z"/></svg>`,
        x: `<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-5.214-6.817L4.99 21.75H1.68l7.73-8.835L1.254 2.25H8.08l4.713 6.231 5.451-6.231zm-1.161 17.52h1.833L7.084 4.126H5.117L17.083 19.77z"/></svg>`,
        linkedin: `<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M20.45 20.45h-3.55v-5.57c0-1.33-.02-3.04-1.85-3.04-1.85 0-2.13 1.45-2.13 2.94v5.67H9.36V9h3.41v1.56h.05c.47-.9 1.64-1.85 3.37-1.85 3.6 0 4.27 2.37 4.27 5.46v6.28zM5.34 7.43a2.06 2.06 0 1 1 0-4.12 2.06 2.06 0 0 1 0 4.12zM7.12 20.45H3.56V9h3.56v11.45zM22.22 0H1.77C.79 0 0 .77 0 1.72v20.55C0 23.23.79 24 1.77 24h20.45c.98 0 1.78-.77 1.78-1.72V1.72C24 .77 23.2 0 22.22 0z"/></svg>`,
        facebook: `<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M22 12.07C22 6.51 17.52 2 12 2S2 6.51 2 12.07c0 5.02 3.66 9.18 8.44 9.93v-7.02H7.9v-2.91h2.54V9.85c0-2.52 1.49-3.91 3.78-3.91 1.09 0 2.24.2 2.24.2v2.47h-1.26c-1.24 0-1.63.78-1.63 1.57v1.88h2.78l-.45 2.91h-2.33V22c4.78-.75 8.43-4.91 8.43-9.93z"/></svg>`,
        vk: `<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M12.79 16.97c-4.43 0-7.31-3.04-7.43-8.07h2.23c.08 3.69 1.74 5.24 3.04 5.57V8.9h2.13v3.21c1.27-.13 2.6-1.6 3.05-3.21h2.1c-.34 1.99-1.79 3.46-2.83 4.07 1.04.49 2.69 1.78 3.32 4.1h-2.31c-.49-1.55-1.71-2.74-3.33-2.9v2.9h-.27z"/></svg>`,
        email: `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="4" width="20" height="16" rx="2"/><path d="M2 7l10 7 10-7"/></svg>`
    };

    private readonly trustedIcons: Record<ShareKind, SafeHtml> = Object.fromEntries(
        (Object.keys(this.icons) as ShareKind[]).map((k) => [
            k,
            this.sanitizer.bypassSecurityTrustHtml(this.icons[k])
        ])
    ) as Record<ShareKind, SafeHtml>;

    iconFor(kind: ShareKind): SafeHtml {
        return this.trustedIcons[kind];
    }

    ngOnInit(): void {
        // Both services are normally bootstrapped via APP_INITIALIZER on
        // the landing route. But /qr can be opened cold (printed QR scan
        // → fresh tab); reading the signals just guarantees DI hooked the
        // singletons up — no extra init calls needed because Language is
        // already wired in app.config.ts and Theme is initialized inline
        // in index.html before paint.
        void this.languageService.lang();
        void this.themeService.theme();

        const win = this.document.defaultView;
        if (win) {
            this.origin.set(win.location.origin);
            this.document.title = 'aliro.mayday';
        }
    }

    onShare(kind: ShareKind): void {
        const url = this.origin();
        const message = this.translate.instant('qr.shareMessage');
        const subject = this.translate.instant('qr.shareEmailSubject');
        const win = this.document.defaultView;
        if (!win) return;

        switch (kind) {
            case 'native': {
                // Web Share API is mobile-Safari and Chrome-Android only;
                // on desktop we fall back to copying the URL so the chip
                // is never a dead button.
                if (typeof win.navigator?.share === 'function') {
                    win.navigator
                        .share({ title: 'aliro.mayday', text: message, url })
                        .catch(() => undefined);
                } else {
                    this.copyToClipboard(url);
                }
                return;
            }
            case 'copy': {
                this.copyToClipboard(url);
                return;
            }
            case 'telegram': {
                this.openExternal(
                    `https://t.me/share/url?url=${encodeURIComponent(url)}&text=${encodeURIComponent(message)}`
                );
                return;
            }
            case 'whatsapp': {
                this.openExternal(
                    `https://wa.me/?text=${encodeURIComponent(message + ' ' + url)}`
                );
                return;
            }
            case 'x': {
                this.openExternal(
                    `https://twitter.com/intent/tweet?url=${encodeURIComponent(url)}&text=${encodeURIComponent(message)}`
                );
                return;
            }
            case 'linkedin': {
                this.openExternal(
                    `https://www.linkedin.com/sharing/share-offsite/?url=${encodeURIComponent(url)}`
                );
                return;
            }
            case 'facebook': {
                this.openExternal(
                    `https://www.facebook.com/sharer/sharer.php?u=${encodeURIComponent(url)}`
                );
                return;
            }
            case 'vk': {
                this.openExternal(
                    `https://vk.com/share.php?url=${encodeURIComponent(url)}&title=${encodeURIComponent(message)}`
                );
                return;
            }
            case 'email': {
                win.location.href = `mailto:?subject=${encodeURIComponent(subject)}&body=${encodeURIComponent(message + '\n\n' + url)}`;
                return;
            }
        }
    }

    private openExternal(href: string): void {
        const win = this.document.defaultView;
        win?.open(href, '_blank', 'noopener,noreferrer');
    }

    private copyToClipboard(text: string): void {
        const win = this.document.defaultView;
        const reset = () => setTimeout(() => this.copied.set(false), 1800);

        if (win?.navigator?.clipboard?.writeText) {
            win.navigator.clipboard
                .writeText(text)
                .then(() => {
                    this.copied.set(true);
                    reset();
                })
                .catch(() => this.copyFallback(text, reset));
            return;
        }
        this.copyFallback(text, reset);
    }

    private copyFallback(text: string, done: () => void): void {
        const ta = this.document.createElement('textarea');
        ta.value = text;
        ta.setAttribute('readonly', '');
        ta.style.position = 'fixed';
        ta.style.opacity = '0';
        this.document.body.appendChild(ta);
        ta.select();
        try {
            this.document.execCommand('copy');
            this.copied.set(true);
            done();
        } catch {
            /* noop */
        }
        this.document.body.removeChild(ta);
    }
}
