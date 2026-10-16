<script lang="ts">
  // Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
  import ws from '$lib/services/ws.js';
  import { demoNfc, type DemoScenario } from '$lib/stores/demo-nfc.svelte';
  import { t, panelHref } from '$lib/stores/i18n.svelte';
  let scenario = $state<DemoScenario>('valid');
  const logsHref = $derived(panelHref('/logs'));
  const result = $derived(demoNfc.result === 'granted' ? 'Access granted'
    : demoNfc.result === 'idle' ? 'Waiting for a tap' : 'Access denied');
</script>

<section class="card shrink-0 border border-base-content/15 mt-3 bg-base-200" aria-label={t('PN532 simulation')}>
  <div class="card-body p-4 gap-3">
    <div class="flex flex-wrap justify-between items-center gap-3">
      <div>
        <h2 class="font-semibold">PN532 NFC Reader <span class="badge badge-outline badge-sm ml-1">{t('simulation')}</span></h2>
        <p class="text-xs opacity-65 mt-1">{t('Browser events only. No NFC, cryptographic validation or device commands.')}</p>
      </div>
      <div class="flex flex-wrap items-center gap-2">
        <select class="select select-bordered select-sm" aria-label={t('Example key')} bind:value={scenario}>
          <option value="valid">{t('Valid key')}</option>
          <option value="revoked">{t('Revoked key')}</option>
          <option value="unknown">{t('Unknown key')}</option>
        </select>
        <button class="btn btn-sm btn-primary" onclick={() => ws.send({ type: 'demo_nfc_tap', scenario })}>{t('Tap the phone')}</button>
        <a class="link text-sm" href={logsHref}>{t('Open logs')} →</a>
      </div>
    </div>
    <div class="flex flex-wrap gap-x-4 gap-y-1 text-sm" role="status" aria-live="polite">
      <strong class:text-success={demoNfc.result === 'granted'} class:text-warning={['revoked', 'unknown'].includes(demoNfc.result)}>{t(result)}</strong>
      <span>{t('Lock')}: {t(demoNfc.locked ? 'locked' : 'unlocked')}</span>
    </div>
  </div>
</section>
