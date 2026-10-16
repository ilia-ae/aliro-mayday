// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
import { Component, DestroyRef, effect, inject, input, output, signal } from '@angular/core';
import { TranslatePipe } from '@ngx-translate/core';

/** Browser-only NFC visualisation linked to the isolated demo iframe, never to hardware. */
@Component({
    selector: 'app-access-demo',
    standalone: true,
    imports: [TranslatePipe],
    template: `
        <div class="access-demo" [attr.aria-busy]="!ready()" [class.is-tapping]="step() >= 1 && step() <= 3" [class.is-unlocked]="step() === 3 || visualOpen()" [class.is-open]="visualOpen()" [class.is-denied]="step() === 5">
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
                <div class="demo-inputs">
                <select [attr.aria-label]="'access_demo.key_label' | translate" [value]="scenario()" (change)="scenario.set($any($event.target).value)">
                    <option value="valid">{{ 'access_demo.keys.valid' | translate }}</option>
                    <option value="revoked">{{ 'access_demo.keys.revoked' | translate }}</option>
                    <option value="unknown">{{ 'access_demo.keys.unknown' | translate }}</option>
                </select>
                <button type="button" (click)="play()" [disabled]="!ready()">
                    {{ (running() ? 'access_demo.reset' : 'access_demo.play') | translate }}
                    <span aria-hidden="true">{{ running() ? '↺' : '→' }}</span>
                </button>
                </div>
            </div>
            <p class="note">{{ 'access_demo.note' | translate }}</p>
            <div class="underhood">
                <div>
                    <h3>{{ 'access_demo.log_title' | translate }}</h3>
                    @if (trace().length) {
                        <ol class="trace" aria-live="polite" aria-relevant="additions">
                            @for (entry of trace().slice(-6); track $index) {
                                <li [class.warn]="entry.level === 'WARN'"><span>{{ formatTime(entry.ts) }} · {{ entry.tag }}</span>{{ entry.msg }}</li>
                            }
                        </ol>
                    } @else {
                        <p class="empty-trace">{{ (ready() ? 'access_demo.log_empty' : 'access_demo.loading') | translate }}</p>
                    }
                </div>
                <div>
                    <h3>{{ 'access_demo.tech_title' | translate }}</h3>
                    <dl class="tech-data">
                        <div><dt>{{ 'access_demo.controller' | translate }}</dt><dd>{{ reader().controller_id }}</dd></div>
                        <div><dt>{{ 'access_demo.hardware_label' | translate }}</dt><dd>ESP32-S3 → PN532 (SPI)</dd></div>
                        <div><dt>{{ 'access_demo.protocol_label' | translate }}</dt><dd>Aliro NFC · ISO 7816 APDU</dd></div>
                        <div><dt>{{ 'access_demo.checks_label' | translate }}</dt><dd>{{ 'access_demo.checks' | translate }}</dd></div>
                        <div><dt>{{ 'access_demo.result_label' | translate }}</dt><dd>Matter DoorLock · {{ reader().locked ? 'LOCKED' : 'UNLOCKED' }}</dd></div>
                    </dl>
                    <a class="implementation" href="https://github.com/ilia-ae/aliro-mayday/blob/main/aliro-esp32/main/NfcManager.cpp" target="_blank" rel="noopener noreferrer">{{ 'access_demo.source' | translate }} ↗</a>
                </div>
            </div>
        </div>
    `,
    styles: [`
        :host { display: block; margin: 0 0 1.5rem; }
        .access-demo { padding: clamp(1.25rem, 3vw, 2rem); border: 1px solid var(--border); border-radius: 1rem; background: var(--bg-elevated); }
        .scene { max-width: 44rem; margin: 0 auto; }
        svg { display: block; width: 100%; height: auto; overflow: visible; }
        .floor, .doorway { stroke: var(--border-strong); stroke-width: 3; }
        .door-shadow { fill: color-mix(in srgb, var(--fg) 16%, var(--bg)); stroke: var(--fg-muted); stroke-width: 2; }
        .door { transform-origin: 386px 148px; transition: transform 900ms ease; }
        .door-leaf { fill: color-mix(in srgb, var(--bg-elevated) 88%, var(--fg)); stroke: var(--fg); stroke-width: 2.5; }
        .door-detail { stroke: var(--fg-dim); stroke-width: 2; }
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
        button:disabled { opacity: 0.5; cursor: wait; }
        .demo-inputs { display: flex; flex-wrap: wrap; align-items: center; gap: 0.75rem; }
        select { max-width: 100%; min-height: 48px; padding: 0.5rem 0.75rem; border: 1px solid var(--border-strong); border-radius: 0.5rem; background: var(--bg); color: var(--fg); font: inherit; }
        .is-denied .reader-led { fill: #c95737; }
        .is-denied .check { opacity: 0; }
        .underhood { display: grid; grid-template-columns: minmax(0, 1.25fr) minmax(0, 1fr); gap: 1.5rem; margin-top: 1.5rem; padding-top: 1.5rem; border-top: 1px solid var(--border); }
        .underhood h3 { font-size: 0.9375rem; margin: 0 0 1rem; }
        .trace { margin: 0; padding: 0; list-style: none; max-height: 20rem; overflow: auto; }
        .trace li { padding: 0.75rem 0; border-bottom: 1px solid var(--border); font: 0.75rem/1.65 ui-monospace, monospace; overflow-wrap: anywhere; }
        .trace li span { display: block; color: var(--fg-dim); font-size: 0.6875rem; }
        .trace .warn { color: var(--fg); border-left: 2px solid #b86a36; padding-left: 0.75rem; }
        .empty-trace { font-size: 0.875rem; color: var(--fg-muted); }
        .tech-data { margin: 0; display: grid; gap: 0.75rem; font-size: 0.8125rem; }
        .tech-data dt { color: var(--fg-dim); font-size: 0.75rem; margin-bottom: 0.125rem; }
        .tech-data dd { margin: 0; line-height: 1.6; }
        .implementation { display: inline-flex; align-items: center; min-height: 44px; margin-top: 0.75rem; font-size: 0.8125rem; }
        @media (max-width: 48rem) { .underhood { grid-template-columns: 1fr; } }

        .note { margin-top: 1rem; max-width: 70ch; font-size: 0.875rem; line-height: 1.6; color: var(--fg-muted); }
        @media (prefers-reduced-motion: reduce) { .phone, .door, .waves, .check, .reader-led { transition: none; } }
    `]
})
export class AccessDemo {
    readonly reader = input({ locked: true, result: 'idle', tap_count: 0, controller_id: 'DEMO-01' });
    readonly ready = input(false);
    readonly trace = input<{ tag: string; msg: string; level: string; ts: number }[]>([]);
    readonly tap = output<string>();
    readonly reset = output<void>();
    readonly scenario = signal('valid');
    readonly step = signal(0);
    readonly running = signal(false);
    readonly visualOpen = signal(false);
    private readonly pending = signal(false);
    private readonly elapsed = signal(false);
    private startedTap = 0;
    private timers: ReturnType<typeof setTimeout>[] = [];

    constructor() {
        inject(DestroyRef).onDestroy(() => this.clearTimers());
        effect(() => {
            const state = this.reader();
            if (this.pending() && (!this.elapsed() || state.tap_count <= this.startedTap)) return;
            this.visualOpen.set(!state.locked);
            this.step.set(state.result === 'idle' ? 0 : state.result !== 'granted' ? 5 : state.locked ? 6 : 4);
            this.pending.set(false);
        });
    }

    play(): void {
        this.clearTimers();
        if (this.running()) {
            this.running.set(false);
            this.pending.set(false);
            this.step.set(0);
            this.visualOpen.set(false);
            this.reset.emit();
            return;
        }
        this.startedTap = this.reader().tap_count;
        this.elapsed.set(false);
        this.pending.set(true);
        this.running.set(true);
        this.step.set(1);
        this.tap.emit(this.scenario());
        if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
            this.elapsed.set(true);
            return;
        }
        this.timers.push(setTimeout(() => this.step.set(2), 1200));
        this.timers.push(setTimeout(() => this.step.set(this.reader().result === 'granted' ? 3 : 5), 2400));
        this.timers.push(setTimeout(() => this.elapsed.set(true), 3200));
    }

    formatTime(ts: number): string { return new Date(ts).toLocaleTimeString(); }

    private clearTimers(): void {
        for (const timer of this.timers) clearTimeout(timer);
        this.timers = [];
    }
}
