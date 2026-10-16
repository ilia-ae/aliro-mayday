<script lang="ts">
  import { t, panelHref } from '$lib/stores/i18n.svelte';
  // Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
  import { systemInfo } from '$lib/stores/system.svelte';
  import { controllerIdentity } from '$lib/stores/controller.svelte';
  import { logs } from '$lib/stores/logs.svelte';
  const recent = $derived(logs.slice(-100).filter(log => /nfc|pn532|aliro|matter/i.test(`${log.tag} ${log.msg}`)).slice(-8));
  const logsHref = $derived(panelHref('/logs'));
</script>

<section class="card bg-base-200 shadow-xl mt-4 max-w-6xl" aria-label={t("NFC diagnostics")}>
  <div class="card-body p-4 gap-3">
    <div class="flex flex-wrap justify-between gap-2 items-center">
      <h2 class="card-title text-lg">{t("NFC diagnostics")}</h2>
      <a class="link text-sm" href={logsHref}>{t("Full event log →")}</a>
    </div>
    <dl class="grid grid-cols-1 sm:grid-cols-2 gap-2 text-sm">
      <div><dt class="opacity-60">{t("Reader ID")}</dt><dd class="font-mono break-all">{systemInfo.controller_id || controllerIdentity.id || '—'}</dd></div>
      <div><dt class="opacity-60">{t("NFC transport")}</dt><dd>{__DEMO__ ? t('Browser simulation · no RF/SPI') : 'PN532 · SPI'}</dd></div>
      <div><dt class="opacity-60">{t("Reader status")}</dt><dd>{__DEMO__ ? t('Simulated reader') : systemInfo.nfc_connected ? t('Connected') : t('Disconnected / waiting for status')}</dd></div>
      <div><dt class="opacity-60">{t("Firmware")}</dt><dd>{systemInfo.version || '—'}</dd></div>
    </dl>
    {#if recent.length}
      <ol class="font-mono text-xs space-y-2 break-words" aria-label={t("Recent NFC events")}>
        {#each recent as entry (entry.id)}
          <li class:text-warning={entry.level === 'WARN'} class:text-error={entry.level === 'ERROR'}>
            <span class="opacity-60">[{entry.tag}]</span> {entry.msg}
          </li>
        {/each}
      </ol>
    {:else}
      <p class="text-sm opacity-60">{__DEMO__ ? t('Tap an example key to see the simulated trace.') : t('Waiting for NFC events from this controller.')}</p>
    {/if}
  </div>
</section>
