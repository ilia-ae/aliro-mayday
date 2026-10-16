// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
import { initialLanguage, isLanguage, translate, type Language } from '$lib/i18n/core';

export const language = $state<{ current: Language }>({ current: 'en' });
const storageKey = 'aliro-ui-language';
let initialized = false;

function applyLanguage(value: Language, persist: boolean): void {
  language.current = value;
  document.documentElement.lang = value;
  if (persist) {
    try { localStorage.setItem(storageKey, value); } catch { /* Continue without persistence. */ }
  }
  const url = new URL(location.href);
  url.searchParams.set('lang', value);
  history.replaceState(history.state, '', url);
}

export function initLanguage(): void {
  if (initialized) return;
  initialized = true;
  let saved: string | null = null;
  try { saved = localStorage.getItem(storageKey); } catch { /* Browser storage is optional. */ }
  language.current = initialLanguage(location.search, saved);
  document.documentElement.lang = language.current;
  if (__WEB_DEMO__ && window.parent !== window) {
    window.addEventListener('message', event => {
      const data = event.data;
      if (event.origin !== location.origin || event.source !== window.parent
          || data?.type !== 'aliro-demo-language' || !isLanguage(data.language)) return;
      applyLanguage(data.language, false);
    });
  }
}

export function setLanguage(value: Language): void {
  if (!isLanguage(value)) return;
  applyLanguage(value, true);
  if (__WEB_DEMO__ && window.parent !== window) {
    window.parent.postMessage({ type: 'aliro-demo-language-request', language: value }, location.origin);
  }
}

export const locale = () => language.current === 'ru' ? 'ru-RU' : 'en-US';
export const t = (message: unknown, values?: Record<string, string | number>) => translate(message, language.current, values);

// Keep language and independent demo-reader IDs in every in-app link.
export function panelHref(path: string): string {
  const parameters = new URLSearchParams(location.search);
  parameters.set('lang', language.current);
  return __WEB_DEMO__ ? `${location.pathname}?${parameters}#${path}` : `${path}?${parameters}`;
}
