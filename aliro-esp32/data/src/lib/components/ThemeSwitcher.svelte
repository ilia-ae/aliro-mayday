<script lang="ts">
  // Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
  import { theme, setTheme } from '$lib/stores/theme.svelte';
  import { t } from '$lib/stores/i18n.svelte';
  import type { ThemeMode } from '$lib/i18n/core';
</script>

<label class="flex items-center gap-2 text-sm">
  <span class="sr-only">{t('Theme')}</span>
  <select class="select select-sm select-bordered min-h-10 w-36" aria-label={t('Theme')} value={theme.mode} onchange={event => setTheme(event.currentTarget.value as ThemeMode)} title={theme.embedded ? t('Follows website theme') : t('Appearance')}>
    <option value="auto">{t('System theme')}</option>
    <option value="light">{t('Light')}</option>
    <option value="dark">{t('Dark')}</option>
  </select>
</label>
