import { Injectable, computed, signal } from '@angular/core';

export type Theme = 'light' | 'dark' | 'auto';

const STORAGE_KEY = 'aliro-theme';
const ORDER: ReadonlyArray<Theme> = ['auto', 'light', 'dark'];

@Injectable({ providedIn: 'root' })
export class ThemeService {
    readonly theme = signal<Theme>(this.readInitial());

    /* Resolved boolean — what the page actually paints right now.
       Re-derives on every theme/system-pref change, so callers don't
       have to re-run matchMedia themselves. */
    readonly isDark = computed(() => {
        const t = this.theme();
        if (t === 'dark') return true;
        if (t === 'light') return false;
        return this.systemDark();
    });

    private readonly systemDark = signal(this.readSystemDark());

    constructor() {
        if (typeof window !== 'undefined' && window.matchMedia) {
            const mq = window.matchMedia('(prefers-color-scheme: dark)');
            mq.addEventListener('change', (e) => {
                this.systemDark.set(e.matches);
                if (this.theme() === 'auto') this.applyDark(e.matches);
            });
        }
    }

    /* auto → light → dark → auto. Matches the deposit.mayday cycle so
       the two sites feel like siblings to anyone toggling between them. */
    cycle(): void {
        const next = ORDER[(ORDER.indexOf(this.theme()) + 1) % ORDER.length];
        this.set(next);
    }

    /* Kept for back-compat callers (e.g. older tests, hotkeys) — flips
       between explicit light/dark and falls out of auto on the way. */
    toggle(): void {
        this.set(this.isDark() ? 'light' : 'dark');
    }

    set(theme: Theme): void {
        this.theme.set(theme);
        try {
            localStorage.setItem(STORAGE_KEY, theme);
        } catch {
            // localStorage may be blocked (private mode, some embeddings)
        }
        const dark =
            theme === 'dark' || (theme === 'auto' && this.systemDark());
        this.applyDark(dark);
    }

    private applyDark(dark: boolean): void {
        document.documentElement.classList.toggle('app-dark', dark);
    }

    /* The inline script in index.html already set .app-dark before paint
       and stored the resolved mode. We just read both back so the signal
       matches the DOM. */
    private readInitial(): Theme {
        try {
            const saved = localStorage.getItem(STORAGE_KEY);
            if (saved === 'light' || saved === 'dark' || saved === 'auto') {
                return saved;
            }
        } catch {
            // localStorage may be blocked
        }
        return 'auto';
    }

    private readSystemDark(): boolean {
        if (typeof window === 'undefined' || !window.matchMedia) return false;
        return window.matchMedia('(prefers-color-scheme: dark)').matches;
    }
}
