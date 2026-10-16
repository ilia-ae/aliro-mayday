// Copyright (c) 2026 Mayday / Ilia Arestov. See ../../../../../LICENSE.
export type DemoScenario = 'valid' | 'revoked' | 'unknown';
export const demoNfc = $state({
  locked: true, result: 'idle', tap_count: 0,
  events: [] as { tag: string; msg: string; level: string; ts: number }[],
});
