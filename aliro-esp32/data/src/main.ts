import { mount } from 'svelte';
import App from './App.svelte';
import './app.css';
import { initLanguage } from '$lib/stores/i18n.svelte';
import { initTheme } from '$lib/stores/theme.svelte';

initLanguage();
initTheme();

const app = mount(App, {
  target: document.getElementById('app')!,
});

export default app;
