import { Routes } from '@angular/router';

export const appRoutes: Routes = [
    {
        path: '',
        loadComponent: () => import('@/app/pages/landing/landing').then((c) => c.LandingPage)
    },
    {
        path: 'privacy',
        loadComponent: () =>
            import('@/app/pages/privacy/privacy').then((c) => c.PrivacyPage)
    },
    {
        path: 'qr',
        loadComponent: () =>
            import('@/app/pages/qr/qr').then((c) => c.QrPage)
    },
    { path: '**', redirectTo: '' }
];
