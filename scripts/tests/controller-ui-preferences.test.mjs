// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../LICENSE.
import { expect, test } from 'bun:test';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { parse } from '../../aliro-esp32/data/node_modules/svelte/compiler/index.js';
import { initialLanguage, initialTheme, translate, messages, isThemeMode } from '../../aliro-esp32/data/src/lib/i18n/core.ts';

test('English is the default; explicit valid language takes precedence over a saved choice', () => {
  expect(initialLanguage('', null)).toBe('en');
  expect(initialLanguage('', 'ru')).toBe('ru');
  expect(initialLanguage('?lang=en', 'ru')).toBe('en');
  expect(initialLanguage('?lang=ru', 'en')).toBe('ru');
  expect(initialLanguage('?lang=other', 'unsupported')).toBe('en');
  expect(initialTheme(null, 'autumn')).toBe('light');
  expect(initialTheme(null, 'dracula')).toBe('dark');
  expect(initialTheme('auto', 'dracula')).toBe('auto');
  expect(initialTheme(null, 'unsupported')).toBe('auto');
  expect(isThemeMode({ toString: () => 'dark' })).toBe(false);
});

test('Russian translations preserve all named parameters and English source text', () => {
  const parameters = value => [...value.matchAll(/\{(\w+)\}/g)].map(match => match[1]).sort();
  expect(Object.keys(messages).length).toBeGreaterThan(400);
  for (const [english, russian] of Object.entries(messages)) {
    expect(russian.trim().length).toBeGreaterThan(0);
    expect(parameters(russian)).toEqual(parameters(english));
    expect(translate(english, 'en')).toBe(english);
  }
  expect(translate('Missing {field}', 'en', { field: 'SSID' })).toBe('Missing SSID');
  expect(translate('Error saving config: Read-only demo: no controller connected; changes and device commands are disabled.', 'ru'))
    .toContain('Демо только для просмотра');
  expect(translate('Selected firmware: Firmware.bin (13 KB)', 'ru')).toBe('Выбрана прошивка: Firmware.bin (13 KB)');
  expect(translate('opaque hardware diagnostic 0x81', 'ru')).toBe('opaque hardware diagnostic 0x81');
});

test('panel templates have no untranslated prose; protocol tokens remain intact', () => {
  const root = fileURLToPath(new URL('../../aliro-esp32/data/src/', import.meta.url));
  const technical = new Set(['HomeKey','HomeKit','HomeSpan -','MQTT','PN532 NFC Reader','SSL/TLS','R','G','B','BGR','BRG','GBR','GRB','RBG','RGB','SPI2','SPI3','GPIO0_IN','GPIO0_OUT','GPIO16_OUT','GPIO17_OUT','INFO','WARN','ERROR','DEBUG','VERBOSE','aliro.mayday','dBm','(.pem, .crt, .cer)','(.pem, .key)','EN','RU']);
  const missing = [];
  function visit(node, file) {
    if (!node || typeof node !== 'object' || node.type === 'Attribute') return;
    if (node.type === 'Text') {
      const text = node.data.trim().replace(/\s+/g, ' ');
      if (/[A-Za-z]/.test(text) && !technical.has(text)) missing.push(`${file}: ${text}`);
      return;
    }
    for (const [key, value] of Object.entries(node)) {
      if (['metadata','loc','expression','test','context','key'].includes(key)) continue;
      if (Array.isArray(value)) value.forEach(child => visit(child,file));
      else if (value && typeof value === 'object') visit(value,file);
    }
  }
  function scan(directory) {
    for (const entry of readdirSync(directory,{withFileTypes:true})) {
      const path = join(directory,entry.name);
      if (entry.isDirectory()) scan(path);
      else if (entry.name.endsWith('.svelte')) visit(parse(readFileSync(path,'utf8'),{modern:true}).fragment,path.slice(root.length));
    }
  }
  scan(root);
  expect(missing).toEqual([]);
});
