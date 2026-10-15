import { SwUpdate } from '@angular/service-worker';

/** Refresh the informational landing page once the complete next version is ready. */
export function watchForSiteUpdates(updates: SwUpdate): void {
    if (!updates.isEnabled) return;

    let reloading = false;
    updates.versionUpdates.subscribe((event) => {
        if (event.type === 'VERSION_READY' && !reloading) {
            reloading = true;
            // Reload instead of activating in place, keeping HTML and chunks together.
            window.location.reload();
        }
    });

    const check = () => {
        if (document.visibilityState === 'visible') {
            void updates.checkForUpdate().catch(() => {
                // Offline visitors keep their current, complete cached version.
            });
        }
    };
    check();
    window.addEventListener('focus', check);
    window.addEventListener('online', check);
    document.addEventListener('visibilitychange', check);
    window.setInterval(check, 60_000);
}
