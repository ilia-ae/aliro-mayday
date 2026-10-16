// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
import { initialTheme, isThemeMode, type ThemeMode } from '$lib/i18n/core';

export const theme = $state<{ mode: ThemeMode; resolved: 'light' | 'dark'; embedded: boolean }>({
  mode: 'auto', resolved: 'light', embedded: false,
});
const storageKey = 'aliro-ui-theme';
let initialized = false;
let media: MediaQueryList;

function paint(mode: ThemeMode, resolved?: 'light' | 'dark') {
  theme.mode = mode;
  theme.resolved = resolved || (mode === 'dark' || (mode === 'auto' && media.matches) ? 'dark' : 'light');
  document.documentElement.dataset.theme = theme.resolved === 'dark' ? 'dracula' : 'autumn';
  document.documentElement.style.colorScheme = theme.resolved;
}

export function setTheme(mode: ThemeMode): void {
  if (!isThemeMode(mode)) return;
  if (__WEB_DEMO__ && theme.embedded) {
    window.parent.postMessage({ type: 'aliro-demo-theme-request', mode }, location.origin);
    return;
  }
  paint(mode);
  try { localStorage.setItem(storageKey, mode); } catch { /* Storage is optional. */ }
}

export function initTheme(): void {
  if (initialized) return;
  initialized = true;
  media = window.matchMedia('(prefers-color-scheme: dark)');
  let saved: string | null = null, legacy: string | null = null;
  try { saved = localStorage.getItem(storageKey); legacy = localStorage.getItem('theme'); } catch { /* Use the system theme. */ }
  paint(initialTheme(saved, legacy));
  media.addEventListener('change', () => {
    if (theme.mode === 'auto' && !theme.embedded) paint('auto');
  });
  if (__WEB_DEMO__ && window.parent !== window) {
    theme.embedded = true;
    window.addEventListener('message', event => {
      const data = event.data;
      if (event.origin !== location.origin || event.source !== window.parent
          || data?.type !== 'aliro-demo-theme' || !isThemeMode(data.mode)
          || !['light', 'dark'].includes(data.resolved)) return;
      paint(data.mode, data.resolved);
    });
    window.parent.postMessage({ type: 'aliro-demo-theme-ready' }, location.origin);
  }
}

// Preserve the existing public helper API and stored dracula/autumn preference.
export const getCurrentTheme = () => theme.resolved === 'dark' ? 'dracula' : 'autumn';
export const toggleTheme = () => setTheme(theme.resolved === 'dark' ? 'light' : 'dark');
const logo = `data:image/svg+xml;charset=utf8,${encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#00723a"/><path d="M18 48 L32 16 L46 48 M23.5 38 L40.5 38" fill="none" stroke="#ffffff" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/></svg>`)}`;
export const logoSrc = () => logo;
