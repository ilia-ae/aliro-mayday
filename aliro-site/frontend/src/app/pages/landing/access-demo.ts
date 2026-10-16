// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
import { Component, DestroyRef, inject, signal } from '@angular/core';
import { TranslatePipe } from '@ngx-translate/core';

/** A schematic animation only. It has no controller or demo-panel connection. */
@Component({
    selector: 'app-access-demo',
    standalone: true,
    imports: [TranslatePipe],
    template: `
        <div class="access-demo" [class.is-tapping]="step() >= 1" [class.is-unlocked]="step() >= 3" [class.is-open]="step() >= 4">
            <div class="scene" aria-hidden="true">
                <svg viewBox="0 0 640 290" fill="none">
                    <path class="floor" d="M36 260H604" />
                    <path class="doorway" d="M378 260V28H546V260" />
                    <rect class="door-shadow" x="386" y="36" width="152" height="224" rx="3" />
                    <g class="door">
                        <rect class="door-leaf" x="386" y="36" width="152" height="224" rx="3" />
                        <path class="door-detail" d="M405 54H518V238H405Z" />
                        <path class="handle" d="M505 151H521V170" />
                    </g>
                    <rect class="reader" x="311" y="108" width="42" height="74" rx="10" />
                    <rect class="reader-led" x="323" y="119" width="18" height="4" rx="2" />
                    <path class="reader-mark" d="M326 141Q338 149 326 157M332 136Q351 149 332 162" />
                    <g class="waves">
                        <path d="M287 132Q303 149 287 166M277 124Q300 149 277 174" />
                    </g>
                    <g class="phone">
                        <rect class="phone-body" x="100" y="93" width="78" height="140" rx="15" />
                        <rect class="phone-screen" x="107" y="101" width="64" height="123" rx="10" />
                        <path class="notch" d="M126 106H152" />
                        <rect class="key-card" x="115" y="138" width="48" height="48" rx="9" />
                        <circle class="key-icon" cx="133" cy="158" r="7" />
                        <path class="key-icon" d="M139 164L150 175M146 171L151 166" />
                    </g>
                    <g class="check">
                        <circle cx="332" cy="73" r="18" />
                        <path d="M323 73L330 80L342 66" />
                    </g>
                </svg>
            </div>
            <div class="controls">
                <div>
                    <p class="label">{{ 'access_demo.label' | translate }}</p>
                    <p class="state" role="status" aria-live="polite" aria-atomic="true">{{ 'access_demo.states.' + step() | translate }}</p>
                </div>
                <button type="button" (click)="play()">
                    {{ (running() ? 'access_demo.reset' : 'access_demo.play') | translate }}
                    <span aria-hidden="true">{{ running() ? '↺' : '→' }}</span>
                </button>
            </div>
            <p class="note">{{ 'access_demo.note' | translate }}</p>
        </div>
    `,
    styles: [`
        :host { display: block; margin: 0 0 1.5rem; }
        .access-demo { padding: clamp(1.25rem, 3vw, 2rem); border: 1px solid var(--border); border-radius: 1rem; background: var(--bg-elevated); }
        .scene { max-width: 44rem; margin: 0 auto; }
        svg { display: block; width: 100%; height: auto; overflow: visible; }
        .floor, .doorway { stroke: var(--border-strong); stroke-width: 3; }
        .door-shadow { fill: var(--bg); }
        .door { transform-origin: 386px 148px; transition: transform 900ms ease; }
        .door-leaf { fill: var(--bg); stroke: var(--fg-muted); stroke-width: 2; }
        .door-detail { stroke: var(--border); stroke-width: 2; }
        .handle { stroke: var(--fg); stroke-width: 5; stroke-linecap: round; }
        .reader { fill: var(--fg); }
        .reader-led { fill: var(--fg-dim); transition: fill 200ms ease; }
        .reader-mark { stroke: var(--bg); stroke-width: 2; stroke-linecap: round; }
        .phone { transition: transform 900ms ease; }
        .phone-body { fill: var(--fg); }
        .phone-screen { fill: var(--bg); }
        .notch { stroke: var(--fg-muted); stroke-width: 3; stroke-linecap: round; }
        .key-card { fill: var(--accent); }
        .key-icon { stroke: var(--accent-ink); stroke-width: 3; stroke-linecap: round; }
        .waves { stroke: var(--accent); stroke-width: 3; stroke-linecap: round; opacity: 0; transition: opacity 200ms ease; }
        .check { opacity: 0; transition: opacity 200ms ease; }
        .check circle { fill: var(--accent); }
        .check path { stroke: var(--accent-ink); stroke-width: 3; stroke-linecap: round; stroke-linejoin: round; }
        .is-tapping .phone { transform: translate(120px, -20px) rotate(-5deg); transform-origin: 139px 163px; }
        .is-tapping .waves { opacity: 1; }
        .is-unlocked .reader-led { fill: var(--accent); }
        .is-unlocked .check { opacity: 1; }
        .is-open .door { transform: perspective(600px) rotateY(-64deg); }
        .is-open .phone { transform: translate(15px, 0); }
        .is-open .waves { opacity: 0; }
        .controls { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 1rem; margin-top: 0.5rem; }
        p { margin: 0; }
        .label { color: var(--fg-dim); font-size: 0.8125rem; margin-bottom: 0.375rem; }
        .state { font-size: 1.125rem; font-weight: 600; color: var(--fg); }
        button { display: inline-flex; align-items: center; justify-content: center; gap: 1rem; min-height: 48px; padding: 0.75rem 1.25rem; border: 1px solid var(--border-strong); border-radius: 0.5rem; background: var(--bg); color: var(--fg); font: inherit; font-weight: 600; cursor: pointer; }
        button:hover { border-color: var(--fg-muted); }
        .note { margin-top: 1rem; max-width: 70ch; font-size: 0.875rem; line-height: 1.6; color: var(--fg-muted); }
        @media (prefers-reduced-motion: reduce) { .phone, .door, .waves, .check, .reader-led { transition: none; } }
    `]
})
export class AccessDemo {
    readonly step = signal(0);
    readonly running = signal(false);
    private timers: ReturnType<typeof setTimeout>[] = [];

    constructor() {
        inject(DestroyRef).onDestroy(() => this.clearTimers());
    }

    play(): void {
        this.clearTimers();
        if (this.running()) {
            this.step.set(0);
            this.running.set(false);
            return;
        }
        if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
            this.step.set(4);
            this.running.set(true);
            return;
        }
        this.step.set(1);
        this.running.set(true);
        for (const [delay, step] of [[1200, 2], [2400, 3], [3200, 4]]) {
            this.timers.push(setTimeout(() => this.step.set(step), delay));
        }
    }

    private clearTimers(): void {
        for (const timer of this.timers) clearTimeout(timer);
        this.timers = [];
    }
}
