import type { Hooks } from 'sv-router';
import type { HKInfo } from '$lib/types/api';
import { setLoadingState } from '$lib/stores/system.svelte';
import { loadReaderInfo } from '$lib/stores/controller.svelte';

declare module 'sv-router' {
  interface RouteMeta {
    homeData?: { hkInfo: HKInfo | null; error: string | null };
  }
}

export default {
  async beforeLoad({ meta, pathname }) {
    try {
      if (pathname !== '/') {
        return;
      }
      setLoadingState(true);
      meta.homeData = { hkInfo: await loadReaderInfo(true), error: null };
    } catch (error) {
      console.error('Failed to load hkinfo:', error);
      meta.homeData = { hkInfo: null, error: error instanceof Error ? error.message : 'Unknown error' };
    } finally {
      setLoadingState(false);
    }
  },
} satisfies Hooks;
