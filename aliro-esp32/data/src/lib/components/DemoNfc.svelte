<script lang="ts">
  // Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
  import ws from '$lib/services/ws.js';
  import { demoNfc, type DemoScenario } from '$lib/stores/demo-nfc.svelte';
  let scenario = $state<DemoScenario>('valid');
  const ru = new URLSearchParams(location.search).get('lang') === 'ru';
  const logsHref = __WEB_DEMO__ ? `${location.pathname}${location.search}#/logs` : '/logs';
  const result = $derived(demoNfc.result === 'granted'
    ? (ru ? 'Доступ разрешён' : 'Access granted')
    : demoNfc.result === 'idle' ? (ru ? 'Ожидание касания' : 'Waiting for a tap')
    : (ru ? 'Доступ запрещён' : 'Access denied'));
</script>

<section class="card shrink-0 border border-base-content/15 mt-4 bg-base-200" aria-label={ru ? 'Симуляция PN532' : 'PN532 simulation'}>
  <div class="card-body p-4 gap-3">
    <div class="flex flex-wrap justify-between items-center gap-3">
      <div>
        <h2 class="font-semibold">PN532 NFC Reader <span class="badge badge-outline badge-sm ml-1">{ru ? 'симуляция' : 'simulation'}</span></h2>
        <p class="text-xs opacity-65 mt-1">{ru ? 'Только события в браузере. NFC, криптография и команды устройству не выполняются.' : 'Browser events only. No NFC, cryptographic validation or device commands.'}</p>
      </div>
      <div class="flex flex-wrap items-center gap-2">
        <select class="select select-bordered select-sm" aria-label={ru ? 'Пример ключа' : 'Example key'} bind:value={scenario}>
          <option value="valid">{ru ? 'Действующий ключ' : 'Valid key'}</option>
          <option value="revoked">{ru ? 'Отозванный ключ' : 'Revoked key'}</option>
          <option value="unknown">{ru ? 'Неизвестный ключ' : 'Unknown key'}</option>
        </select>
        <button class="btn btn-sm btn-primary" onclick={() => ws.send({ type: 'demo_nfc_tap', scenario })}>{ru ? 'Приложить телефон' : 'Tap the phone'}</button>
        <a class="link text-sm" href={logsHref}>{ru ? 'Открыть журнал' : 'Open logs'} →</a>
      </div>
    </div>
    <div class="flex flex-wrap gap-x-4 gap-y-1 text-sm" role="status" aria-live="polite">
      <strong class:text-success={demoNfc.result === 'granted'} class:text-warning={['revoked', 'unknown'].includes(demoNfc.result)}>{result}</strong>
      <span>{ru ? 'Замок' : 'Lock'}: {demoNfc.locked ? (ru ? 'закрыт' : 'locked') : (ru ? 'открыт' : 'unlocked')}</span>
    </div>

  </div>
</section>
