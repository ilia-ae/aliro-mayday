<script lang="ts">
  import { t } from '$lib/stores/i18n.svelte';
  // Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
  import { systemInfo } from '$lib/stores/system.svelte';
  import { controllerIdentity } from '$lib/stores/controller.svelte';
  let { compact = false } = $props();
  const controllerId = $derived(systemInfo.controller_id || controllerIdentity.id);
  const displayId = $derived(compact && controllerId.length > 18
    ? `${controllerId.slice(0, 8)}…${controllerId.slice(-6)}` : controllerId);
</script>

<div class="min-w-0" data-controller-identity={controllerId}>
  {#if !compact}<span class="block text-xs opacity-60">aliro.mayday</span>{/if}
  <span class="block text-sm font-semibold break-words">{systemInfo.deviceName || t('Controller')}</span>
  <span class="block font-mono text-xs opacity-70 break-all" title={controllerId}>
    {compact ? 'ID' : __DEMO__ ? t('Demo ID') : t('Reader ID')}: {displayId || '—'}
  </span>
</div>
