// Copyright (c) 2026 Mayday / Ilia Arestov. See ../LICENSE.
import { createDemoData, disabledMessage } from './controller-ui-fixtures.mjs';

// Installed before importing the real UI, only in the dedicated web-demo build.
// There is deliberately no native fetch/WebSocket fallback.
export function installBrowserDemo(target = window) {
  const requestedId = new URL(target.location.href).searchParams.get('controller');
  const demo = createDemoData({ controllerId: requestedId || 'DEMO-01' });
  const sockets = new Set();
  const parentState = data => {
    if (data.type === 'demo_nfc' && target.parent && target.parent !== target) {
      target.parent.postMessage({ ...data, type: 'aliro-demo-state' }, target.location.origin);
    }
  };
  const publish = data => {
    for (const socket of sockets) socket.receive(data);
    parentState(data);
  };
  const onParentMessage = event => {
    const data = event.data;
    if (event.origin !== target.location.origin || event.source !== target.parent
        || !target.parent || target.parent === target || data?.type !== 'aliro-demo-command'
        || data.controller_id !== demo.controllerId) return;
    if (data.command === 'reset') publish(demo.message('{"type":"demo_nfc_reset"}'));
    else if (data.command === 'tap' && ['valid', 'revoked', 'unknown'].includes(data.scenario)) {
      publish(demo.message(JSON.stringify({ type: 'demo_nfc_tap', scenario: data.scenario })));
    }
  };
  target.addEventListener('message', onParentMessage);
  target.fetch = async (input, init = {}) => {
    const url = new URL(input instanceof Request ? input.url : String(input), target.location.href);
    const method = (init.method || (input instanceof Request ? input.method : 'GET')).toUpperCase();
    const reply = url.origin === target.location.origin
      ? demo.request(url, method)
      : { status: 403, body: { success: false, error: disabledMessage } };
    return Response.json(reply.body, { status: reply.status });
  };

  class DemoWebSocket extends EventTarget {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    readyState = 0;
    bufferedAmount = 0;
    protocol = '';
    extensions = '';
    binaryType = 'blob';
    onopen = null;
    onmessage = null;
    onerror = null;
    onclose = null;
    timer;

    constructor(address) {
      super();
      const url = new URL(address, target.location.href);
      if (!['ws:', 'wss:', 'http:', 'https:'].includes(url.protocol)
          || url.host !== target.location.host || url.pathname !== '/ws') {
        throw new Error(disabledMessage);
      }
      this.url = url.href;
      sockets.add(this);
      this.openTimer = setTimeout(() => {
        if (this.readyState !== 0) return;
        this.readyState = 1;
        this.emit('open', new Event('open'));
        for (const data of demo.initial()) { this.receive(data); parentState(data); }
        this.timer = setInterval(() => {
          for (const data of demo.tick()) this.receive(data);
        }, 5000);
      }, 0);
    }

    emit(type, event) {
      this.dispatchEvent(event);
      this[`on${type}`]?.call(this, event);
    }

    receive(data) {
      if (this.readyState === 1) this.emit('message', new MessageEvent('message', { data: JSON.stringify(data) }));
    }

    send(message) {
      if (this.readyState !== 1) throw new Error('Demo socket is not open');
      queueMicrotask(() => {
        if (this.readyState !== 1) return;
        const reply = demo.message(message);
        if (reply.type === 'demo_nfc') publish(reply);
        else this.receive(reply);
      });
    }

    close() {
      if (this.readyState === 3) return;
      clearTimeout(this.openTimer);
      clearInterval(this.timer);
      this.readyState = 3;
      sockets.delete(this);
      this.emit('close', new Event('close'));
    }
  }
  target.WebSocket = DemoWebSocket;
  const stop = () => {
    for (const socket of sockets) socket.close();
    target.removeEventListener('message', onParentMessage);
  };
  target.addEventListener('pagehide', stop, { once: true });
  return { stop };
}
