<!-- Copyright (c) 2026 Mayday / Ilia Arestov. See /LICENSE. -->
<script lang="ts">
  import { onMount } from 'svelte';
  import { t } from '$lib/stores/i18n.svelte';
  let { section }: { section: 'relay' | 'telegram' } = $props();
  type Settings = {
    relayEnabled: boolean; relayPin: number; activeHigh: boolean; pulse: boolean; pulseMs: number;
    telegramEnabled: boolean; notifyDenied: boolean; chatId: string;
    tokenConfigured: boolean; relayActive: boolean; locked: boolean; telegramStatus: string;
    delivered: number; dropped: number; authEnabled: boolean; httpsEnabled: boolean;
  };
  let settings = $state<Settings>();
  let status = $state<Settings>();
  let token = $state('');
  let clearToken = $state(false);
  let busy = $state(false);
  let error = $state('');
  let message = $state('');
  const pins = [-1, 4, 5, 6, 7, 14, 15, 16, 17, 18, 21, 38, 47];
  const statusLabels: Record<string, string> = {
    disabled: 'Disabled', waiting: 'Waiting for an event', ready: 'Connected',
    error: 'Telegram request failed; retrying later', rate_limited: 'Telegram rate limit; waiting',
    clock_required: 'Waiting for system time',
  };
  async function request(path: string, body?: object) {
    const response = await fetch(path, body ? {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Aliro-Request': 'integrations-ui' },
      body: JSON.stringify(body),
    } : undefined);
    const result = await response.json();
    if (!response.ok || !result.success) throw new Error(result.error || 'Request Failed');
    return result.data as Settings;
  }
  onMount(() => {
    let stopped = false, pending = false;
    async function refresh() {
      if (pending) return;
      pending = true;
      try {
        const data = await request('/api/integrations');
        if (!stopped) { status = data; if (!settings) settings = { ...data }; }
      } catch (e) { if (!stopped) error = e instanceof Error ? e.message : 'Request Failed'; }
      finally { pending = false; }
    }
    void refresh();
    const timer = setInterval(refresh, 3000);
    return () => { stopped = true; clearInterval(timer); };
  });
  async function act(action: 'save' | 'test' | 'lock' | 'unlock' | 'denied') {
    if (!settings || busy) return;
    busy = true; error = ''; message = '';
    try {
      const { relayEnabled, relayPin, activeHigh, pulse, pulseMs, telegramEnabled, notifyDenied, chatId } = settings;
      const endpoint = action === 'save' ? '/api/integrations' : action === 'test' ? '/api/integrations/test' : __WEB_DEMO__ ? '/api/integrations/simulate' : null;
      if (!endpoint) return;
      const data = await request(endpoint,
        action === 'save' ? { relayEnabled, relayPin, activeHigh, pulse, pulseMs, telegramEnabled, notifyDenied, chatId, token, clearToken } : { action });
      status = data;
      if (action === 'save') { settings = { ...data }; token = ''; clearToken = false; }
      message = action === 'save' ? 'Integration settings saved. Relay output starts inactive.' : action === 'test' ? 'Notification queued; check delivery status.' : 'Simulation completed. No device or Telegram request was sent.';
    } catch (e) { error = e instanceof Error ? e.message : 'Request Failed'; }
    finally { busy = false; }
  }
</script>

<section class="space-y-5 min-w-0" aria-label={t(section === 'relay' ? 'Relay control' : 'Telegram integration')}>
  {#if __WEB_DEMO__}
    <p class="alert alert-info text-sm">{t('Browser simulation only. No relay, controller or Telegram bot is connected.')}</p>
  {/if}
  {#if error}<p role="alert" class="alert alert-error break-words">{t(error)}</p>{/if}
  {#if message}<p role="status" class="alert alert-success break-words">{t(message)}</p>{/if}
  {#if settings && status}
    {#if !status.authEnabled}<p class="alert alert-warning">{t('Enable panel authentication before changing integrations.')}</p>{/if}
    {#if section === 'relay'}
      <h2 class="text-xl font-bold">{t('Relay control')}</h2>
      <p class="text-sm text-base-content/70">{t('Runs after an accepted Matter or NFC lock-state change. Disabled by default; existing keys and pairing are unchanged.')}</p>
      <div class="grid sm:grid-cols-2 gap-4">
        <label class="fieldset min-w-0"><span class="label">{t('Relay output')}</span><select class="select w-full" bind:value={settings.relayEnabled}><option value={false}>{t('Disabled')}</option><option value={true}>{t('Enabled')}</option></select></label>
        <label class="fieldset min-w-0"><span class="label">{t('Relay GPIO')}</span><select class="select w-full" bind:value={settings.relayPin}>{#each pins as pin}<option value={pin}>{pin === -1 ? t('Not assigned') : `GPIO ${pin}`}</option>{/each}</select></label>
        <label class="fieldset min-w-0"><span class="label">{t('Active output level')}</span><select class="select w-full" bind:value={settings.activeHigh}><option value={true}>{t('HIGH')}</option><option value={false}>{t('LOW')}</option></select></label>
        <label class="fieldset min-w-0"><span class="label">{t('Relay mode')}</span><select class="select w-full" bind:value={settings.pulse}><option value={true}>{t('Unlock pulse')}</option><option value={false}>{t('Follow logical lock state')}</option></select></label>
        {#if settings.pulse}<label class="fieldset min-w-0"><span class="label">{t('Pulse duration (ms)')}</span><input type="number" min="100" max="30000" step="100" class="input w-full" bind:value={settings.pulseMs} /></label>{/if}
      </div>
      <p class="text-sm text-base-content/70">{t('Use an external relay driver and power supply. The GPIO is a control signal, not a power supply for the lock. Reserve an inactive pull resistor for boot.')}</p>
      <p class="rounded-xl bg-base-200 p-4 break-words">{t('Relay output: {state}', { state: t(status.relayActive ? 'Active' : 'Inactive') })}<br />{t('Logical state: {state}', { state: t(status.locked ? 'Locked' : 'Unlocked') })}</p>
      <p class="text-sm text-base-content/70">{t('A pulse only releases the output; it does not report a physically closed door or automatically change the Matter lock state.')}</p>
    {:else}
      <h2 class="text-xl font-bold">{t('Telegram integration')}</h2>
      <p class="text-sm text-base-content/70">{t('Notifications use verified HTTPS in a background worker. Bot tokens are write-only and are never returned by the API.')}</p>
      {#if !status.httpsEnabled}<p class="alert alert-warning">{t('Enable panel HTTPS before entering a bot token.')}</p>{/if}
      <div class="grid sm:grid-cols-2 gap-4">
        <label class="fieldset min-w-0"><span class="label">{t('Telegram notifications')}</span><select class="select w-full" bind:value={settings.telegramEnabled}><option value={false}>{t('Disabled')}</option><option value={true}>{t('Enabled')}</option></select></label>
        <label class="fieldset min-w-0"><span class="label">{t('Numeric chat ID')}</span><input class="input w-full" inputmode="numeric" autocomplete="off" bind:value={settings.chatId} /></label>
        <label class="fieldset min-w-0"><span class="label">{t('Bot token')}</span><input class="input w-full" type="password" autocomplete="new-password" disabled={!status.httpsEnabled || __WEB_DEMO__} bind:value={token} placeholder={t(status.tokenConfigured ? 'Token saved; leave blank to keep it' : 'Enter a bot token')} /></label>
        <label class="fieldset min-w-0"><span class="label">{t('Notify about denied NFC access')}</span><select class="select w-full" bind:value={settings.notifyDenied}><option value={false}>{t('Disabled')}</option><option value={true}>{t('Enabled')}</option></select></label>
      </div>
      <label class="flex items-start gap-3"><input type="checkbox" class="checkbox shrink-0" bind:checked={clearToken} disabled={__WEB_DEMO__} /><span>{t('Delete the saved token when saving (disable Telegram first)')}</span></label>
      <p class="rounded-xl bg-base-200 p-4 break-words">{t('Telegram: {state}', { state: t(statusLabels[status.telegramStatus] || 'Unknown') })}<br />{t('Delivered: {sent} · Dropped: {dropped}', { sent: status.delivered, dropped: status.dropped })}</p>
    {/if}
    <div class="flex flex-wrap gap-3">
      <button type="button" class="btn btn-primary" disabled={busy || !status.authEnabled} onclick={() => act('save')}>{t('Save integration settings')}</button>
      {#if section === 'telegram'}<button type="button" class="btn btn-outline" disabled={busy || !status.authEnabled || !status.telegramEnabled} onclick={() => act('test')}>{t('Queue test notification')}</button>{/if}
    </div>
    {#if __WEB_DEMO__ && section === 'relay'}
      <div class="border-t border-base-300 pt-4 space-y-3">
        <h3 class="font-semibold">{t('Try the simulation')}</h3>
        <div class="flex flex-wrap gap-3">
          {#if section === 'relay'}
            <button type="button" class="btn btn-outline" disabled={busy} onclick={() => act('unlock')}>{t('Simulate unlock')}</button>
            <button type="button" class="btn btn-outline" disabled={busy} onclick={() => act('lock')}>{t('Simulate lock')}</button>
            <button type="button" class="btn btn-ghost" disabled={busy} onclick={() => act('denied')}>{t('Simulate denied access')}</button>
          {/if}
        </div>
        <p class="text-sm text-base-content/70">{t('Events appear in the shared Logs section. Demo settings reset when the page is reloaded.')}</p>
      </div>
    {/if}
  {:else if !error}<p role="status">{t('Loading...')}</p>{/if}
</section>
