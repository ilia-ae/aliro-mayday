<script>
	import HKInfo from '$lib/components/HKInfo.svelte';
	import { route } from 'sv-router/generated';
  import NfcActivity from '$lib/components/NfcActivity.svelte';
</script>

<HKInfo hkInfo={route.meta.homeData?.hkInfo} error={route.meta.homeData?.error} />
<NfcActivity />
