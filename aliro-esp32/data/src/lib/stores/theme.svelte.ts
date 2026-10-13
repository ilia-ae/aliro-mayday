type ThemeName = 'dracula' | 'autumn';

let currentTheme : ThemeName = $state('dracula');

function getCurrentTheme() : ThemeName {
	return currentTheme;
}

function toggleTheme() {
	currentTheme = currentTheme === 'dracula' ? 'autumn' : 'dracula';
	localStorage.setItem('theme', currentTheme);
	document.documentElement.setAttribute('data-theme', currentTheme);
}

function initTheme() {
	currentTheme = (localStorage.getItem('theme') || 'dracula') as ThemeName;
	document.documentElement.setAttribute('data-theme', currentTheme);
}

// Logo matches aliro-site/frontend/public/favicon.svg (green rounded
// square + stroke-only "A" monogram). Inlined as a data URL so the
// admin bundle has no external asset dependency. Brand colour stays
// #00723a regardless of dracula/autumn theme.
const logo = `data:image/svg+xml;charset=utf8,${encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#00723a"/><path d="M18 48 L32 16 L46 48 M23.5 38 L40.5 38" fill="none" stroke="#ffffff" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/></svg>`)}`;
let logoSrc = () => logo;
export { getCurrentTheme, toggleTheme, initTheme, logoSrc};
