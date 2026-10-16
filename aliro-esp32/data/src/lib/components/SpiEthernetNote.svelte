<script lang="ts">
  import { t } from '$lib/stores/i18n.svelte';
  let { spiNumBuses = 1, selectedBus = 1 } = $props<{
    spiNumBuses: number;
    selectedBus: number;
  }>();
</script>
<div class="alert alert-warning alert-sm mb-2">
  <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4 shrink-0 stroke-current" fill="none" viewBox="0 0 24 24">
    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
  </svg>
  <span class="text-xs">
    {t('Note:')} {t(spiNumBuses === 1 ? 'Only one SPI bus available' : 'Two SPI buses available')}.<br />
    {t(selectedBus === 1
      ? 'Ethernet uses {bus}. PN532 and Ethernet pins (except CS/SS) must match because they share a bus.'
      : 'Ethernet uses {bus}. PN532 and Ethernet pins must not conflict because they use separate buses.',
      { bus: t(selectedBus === 1 ? 'first (SPI2) bus' : 'second (SPI3) bus') })}
  </span>
</div>
