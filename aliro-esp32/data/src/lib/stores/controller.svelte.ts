// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
import type { HKInfo } from '$lib/types/api';

export const controllerIdentity = $state({ id: '' });
let pending: Promise<HKInfo> | null = null;
let cached: HKInfo | null = null;

function formatId(value: unknown): string {
  if (typeof value === 'string') return value;
  if (Array.isArray(value) && value.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255)) {
    return value.map(byte => byte.toString(16).padStart(2, '0')).join('');
  }
  return '';
}

// Read the existing reader identifier. This never creates or changes a key.
export function loadReaderInfo(refresh = false): Promise<HKInfo> {
  if (pending) return pending;
  if (cached && !refresh) return Promise.resolve(cached);
  pending = fetch('/config?type=hkinfo').then(response => response.json()).then(result => {
    if (!result?.success || !result.data || typeof result.data !== 'object' || Array.isArray(result.data)) {
      throw new Error(result?.error || 'Reader information unavailable');
    }
    cached = result.data as HKInfo;
    controllerIdentity.id = formatId(cached?.unique_identifier);
    return cached;
  }).finally(() => { pending = null; });
  return pending;
}
