// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
import russian from './ru.json';

export type Language = 'en' | 'ru';
export type ThemeMode = 'auto' | 'light' | 'dark';
export const messages: Readonly<Record<string, string>> = russian;
export const isLanguage = (value: unknown): value is Language => value === 'en' || value === 'ru';
export const isThemeMode = (value: unknown): value is ThemeMode => value === 'auto' || value === 'light' || value === 'dark';

export function initialLanguage(search: string, saved: unknown): Language {
  const requested = new URLSearchParams(search).get('lang');
  return isLanguage(requested) ? requested : isLanguage(saved) ? saved : 'en';
}

export function initialTheme(saved: unknown, legacy: unknown): ThemeMode {
  if (isThemeMode(saved)) return saved;
  return legacy === 'autumn' ? 'light' : legacy === 'dracula' ? 'dark' : 'auto';
}

const patterns = Object.keys(messages).filter(key => /\{\w+\}/.test(key)).map(key => {
  const names: string[] = [];
  const escaped = key.split(/(\{\w+\})/).map(part => {
    const match = /^\{(\w+)\}$/.exec(part);
    if (match) { names.push(match[1]); return '(.*?)'; }
    return part.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  }).join('');
  return { key, names, expression: new RegExp(`^${escaped}$`, 's') };
});

// Unknown device errors and raw diagnostics retain their original text. Values
// such as IDs, SSIDs, topic names and filenames are never translated implicitly.
export function translate(message: unknown, language: Language, values: Record<string, string | number> = {}): string {
  const source = message == null ? '' : String(message);
  let template = language === 'ru' && Object.hasOwn(messages, source) ? messages[source] : source;
  let parameters = values;
  if (language === 'ru' && template === source && source.length <= 4096 && !Object.keys(values).length) {
    for (const pattern of patterns) {
      const match = pattern.expression.exec(source);
      if (!match) continue;
      template = messages[pattern.key];
      parameters = Object.fromEntries(pattern.names.map((name, index) => [name,
        name === 'message' ? translate(match[index + 1], language) : match[index + 1],
      ]));
      break;
    }
  }
  return template.replace(/\{(\w+)\}/g, (placeholder, name) => Object.hasOwn(parameters, name) ? String(parameters[name]) : placeholder);
}
