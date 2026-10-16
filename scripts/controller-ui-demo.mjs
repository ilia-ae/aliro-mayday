// Copyright (c) 2026 Mayday / Ilia Arestov. See ../LICENSE.
// Synthetic, read-only API for the controller UI. Never forwards to hardware.
import { createDemoData } from './controller-ui-fixtures.mjs';

export function startDemoServer({ port = 0 } = {}) {
  const demo = createDemoData();
  const timers = new Set();
  const server = Bun.serve({
    hostname: '127.0.0.1', port,
    fetch(request, server) {
      const url = new URL(request.url);
      if (url.pathname === '/ws' && request.method === 'GET') {
        if (server.upgrade(request, { data: { timer: undefined } })) return;
        return Response.json({ success: false, error: 'WebSocket upgrade required' }, { status: 426 });
      }
      const reply = demo.request(url, request.method);
      return Response.json(reply.body, { status: reply.status });
    },
    websocket: {
      maxPayloadLength: 4096,
      open(ws) {
        for (const data of demo.initial()) ws.send(JSON.stringify(data));
        ws.data.timer = setInterval(() => {
          for (const data of demo.tick()) ws.send(JSON.stringify(data));
        }, 5000);
        timers.add(ws.data.timer);
      },
      message(ws, message) { ws.send(JSON.stringify(demo.message(message))); },
      close(ws) { clearInterval(ws.data.timer); timers.delete(ws.data.timer); },
    },
  });
  let stopped;
  return {
    url: server.url,
    stop() {
      if (!stopped) {
        for (const timer of timers) clearInterval(timer);
        timers.clear();
        stopped = server.stop(true);
      }
      return stopped;
    },
  };
}
