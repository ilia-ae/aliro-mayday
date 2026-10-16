<script lang="ts">
  // Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
  import { language, setLanguage, t } from '$lib/stores/i18n.svelte';
</script>

<div class="join" role="group" aria-label={t('Interface language')}>
  <button type="button" class="btn btn-sm join-item min-h-10" class:btn-primary={language.current === 'en'} aria-label="English" aria-pressed={language.current === 'en'} onclick={() => setLanguage('en')}>EN</button>
  <button type="button" class="btn btn-sm join-item min-h-10" class:btn-primary={language.current === 'ru'} aria-label="Русский" aria-pressed={language.current === 'ru'} onclick={() => setLanguage('ru')}>RU</button>
</div>
