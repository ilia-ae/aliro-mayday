<script>
  import { t } from '$lib/stores/i18n.svelte';
  import { notifications } from "../stores/notifications.svelte.js";

  /**
   * @param {import('../types/api').NotificationType} type
   * @returns {string}
   */
  function getAlertClass(type) {
    switch (type) {
      case "success":
        return "alert-success";
      case "error":
        return "alert-error";
      case "warning":
        return "alert-warning";
      case "info":
        return "alert-info";
      default:
        return "alert-info";
    }
  }
</script>

<div
  class="toast toast-bottom"
  role="region"
  aria-live="polite"
  aria-label={t("Notifications")}
>
  {#each notifications.notifications as notification (notification.id)}
    <div
      class="alert {getAlertClass(notification.type)} shadow-lg"
      role="alert"
      aria-atomic="true"
      aria-live="assertive"
    >
      <span>{t(notification.message)}</span>
      <button
        class="btn btn-sm btn-circle btn-ghost"
        onclick={() => notifications.remove(notification.id)}
        aria-label={t("Close notification")}
      >
        ✕
      </button>
    </div>
  {/each}
</div>
