<script lang="ts">
  import { t } from '$lib/stores/i18n.svelte';
  import PanelPreferences from '$lib/components/PanelPreferences.svelte';
	import '@/app.css';
	let { children } = $props();
</script>

<svelte:head>
	<title>{t("Device Configuration - aliro.mayday")}</title>
</svelte:head>

<div class="min-h-dvh bg-base-100 flex flex-col">
	<!-- Header -->
	<header class="bg-base-200 shadow-sm px-4 py-3">
		<div class="flex flex-col items-center gap-1">
			<div class="flex items-center gap-2">
				<span class="font-bold text-lg">aliro.mayday</span>
				<span class="badge badge-sm badge-info">{t("Setup")}</span>
			</div>
			<span class="text-sm text-base-content/70">{t("Device Configuration")}</span>
		</div>
    <PanelPreferences />
	</header>

	<!-- Main Content -->
	<main class="flex-1 flex items-center justify-center p-4">
		<div class="w-full max-w-md">
			{@render children()}
		</div>
	</main>

	<!-- Footer -->
	<footer class="footer footer-center p-4 bg-base-200 text-base-content">
		<div>
			<p class="text-sm text-base-content/70">{t("Configure your device to connect to your network")}</p>
		</div>
	</footer>
</div>
