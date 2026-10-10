// A stub DOM, just deep enough to run the injected payload outside a browser. It models node
// trees, class and id lookup, inline styles, element rectangles and the message and animation
// callbacks the payload uses. It does not do layout or CSS.
'use strict';

class El {
  constructor(tag) {
    this.tagName = String(tag).toUpperCase();
    this.childNodes = []; this.parentNode = null;
    this.className = ''; this.id = '';
    this._text = ''; this._rect = null;
    // Custom properties live beside the inline styles, the way they do on a real element.
    const props = {};
    this.style = {
      setProperty: (k, v) => { props[k] = String(v); },
      getPropertyValue: (k) => props[k] || '',
    };
  }
  appendChild(c) { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.childNodes.push(c); return c; }
  removeChild(c) { const i = this.childNodes.indexOf(c); if (i >= 0) this.childNodes.splice(i, 1); c.parentNode = null; return c; }
  set textContent(v) { this._text = String(v); this.childNodes = []; }
  get textContent() { return this.childNodes.length ? this.childNodes.map(c => c.textContent).join('') : this._text; }
  get classList() { return this.className.split(/\s+/).filter(Boolean); }
  getBoundingClientRect() {
    const r = this._rect || { left: 0, top: 0, width: 0, height: 0 };
    return { left: r.left, top: r.top, width: r.width, height: r.height,
             right: r.left + r.width, bottom: r.top + r.height };
  }
  descendants() { let o = []; for (const c of this.childNodes) { o.push(c); o = o.concat(c.descendants()); } return o; }
  contains(n) { for (let x = n; x; x = x.parentNode) if (x === this) return true; return false; }
  addEventListener(type, fn) { (this._listeners = this._listeners || {}); (this._listeners[type] = this._listeners[type] || []).push(fn); }
  removeEventListener(type, fn) {
    const a = (this._listeners && this._listeners[type]) || [];
    const i = a.indexOf(fn);
    if (i >= 0) a.splice(i, 1);
  }

  // Canvas text metrics, which the payload uses to find the font size that draws capitals a
  // wanted number of pixels tall. The ratios are Roboto Condensed's, one of the faces the
  // payload asks for. A test can set fontMetrics to null to exercise the fallback path.
  getContext(kind) {
    if (this.tagName !== 'CANVAS' || kind !== '2d' || !fontMetrics) return null;
    const ctx = { font: '10px sans-serif' };
    ctx.measureText = (text) => {
      if (fontProbe) fontProbe(ctx.font);
      // A font shorthand may carry a weight before the size; the size is the px value.
      const size = parseFloat(/(\d+(?:\.\d+)?)px/.exec(ctx.font)?.[1] || '0');
      const cap = size * fontMetrics.cap;
      // Enough of a width model to drive the horizontal calibration: a flat advance per
      // character, which makes the squeeze a fixed number the test can predict.
      const ink = String(text).length * size * fontMetrics.advance;
      return {
        actualBoundingBoxAscent: cap,
        actualBoundingBoxLeft:   0,
        actualBoundingBoxRight:  ink,
        width:                   ink,
        fontBoundingBoxAscent:   size * fontMetrics.ascent,
        fontBoundingBoxDescent:  size * fontMetrics.descent,
      };
    };
    return ctx;
  }
}

let fontMetrics = { cap: 0.711, ascent: 0.927, descent: 0.244, advance: 0.45 };
const setFontMetrics = (m) => { fontMetrics = m && { advance: 0.45, ...m }; };

// Lets a test see the canvas font shorthand the payload measured with.
let fontProbe = null;
const setFontProbe = (fn) => { fontProbe = fn; };

const matches = (el, sel) =>
    sel[0] === '.' ? el.classList.includes(sel.slice(1))
  : sel[0] === '#' ? el.id === sel.slice(1)
  : el.tagName === sel.toUpperCase();

const documentRoot = new El('html');
const head = new El('head');
const body = new El('body');
documentRoot.appendChild(head);
documentRoot.appendChild(body);

const docListeners = {};
const document = {
  documentElement: documentRoot, head, body,
  addEventListener(type, fn) { (docListeners[type] = docListeners[type] || []).push(fn); },
  removeEventListener(type, fn) {
    const a = docListeners[type] || [];
    const i = a.indexOf(fn);
    if (i >= 0) a.splice(i, 1);
  },
  createElement: (tag) => new El(tag),
  getElementById(id) { return documentRoot.descendants().find(e => e.id === id) || null; },
  // Supports one class/tag/id selector, or two separated by a descendant combinator, which is
  // all the payload asks for.
  querySelector(sel) {
    const parts = sel.trim().split(/\s+/);
    const all = documentRoot.descendants();
    if (parts.length === 1) return all.find(e => matches(e, parts[0])) || null;
    for (const ancestor of all) {
      if (!matches(ancestor, parts[0])) continue;
      const hit = ancestor.descendants().find(e => matches(e, parts[1]));
      if (hit) return hit;
    }
    return null;
  },
};

let rafQueue = [];
const listeners = {};

const window = {
  innerWidth: 1600, innerHeight: 900,
  addEventListener(type, fn) { (listeners[type] = listeners[type] || []).push(fn); },
  removeEventListener(type, fn) {
    const a = listeners[type] || [];
    const i = a.indexOf(fn);
    if (i >= 0) a.splice(i, 1);
  },
  requestAnimationFrame(fn) { rafQueue.push(fn); return rafQueue.length; },
};

function flush() { const q = rafQueue; rafQueue = []; q.forEach(fn => fn(0)); }

// Delivers a DOM event the way a browser would: capture listeners on the document first, then
// the target and its ancestors, then the window. Enough for the layout editor's mouse work.
function fire(target, type, init) {
  const ev = Object.assign({ type, target, button: 0, clientX: 0, clientY: 0, deltaY: 0,
                             preventDefault() {}, stopPropagation() {} }, init || {});
  for (const fn of (docListeners[type] || []).slice()) fn(ev);
  for (let n = target; n; n = n.parentNode)
    for (const fn of ((n._listeners && n._listeners[type]) || []).slice()) fn(ev);
  for (const fn of (listeners[type] || []).slice()) fn(ev);
  return ev;
}

// A movable clock, so a test can look at what the HUD draws five seconds later without waiting
// five seconds. Only Date.now moves; the payload re-decides what to show on every render, and a
// test pushes a render through by posting a message.
const realNow = Date.now;
let clockOffset = 0;
Date.now = () => realNow() + clockOffset;
function advanceClock(ms) { clockOffset += ms; }
function resetClock() { clockOffset = 0; }

function post(msg) {
  for (const fn of (listeners.message || []).slice()) fn({ data: msg });
}

// Every child of a batch is also delivered again as its own message. Tests use this to prove
// the payload stays correct while each batched update is delivered twice.
function postLikeGtaw(msg) {
  post(msg);
  if (msg && msg.action === 'NUI::Global::Batch' && Array.isArray(msg.data))
    for (const child of msg.data) if (child && child.action) postLikeGtaw(child);
}

function widget(className, text, parent) {
  const n = new El('div');
  n.className = className;
  if (text != null) n.textContent = text;
  (parent || body).appendChild(n);
  return n;
}

// A MutationObserver that fires when a test says the page changed, with a record naming the
// node that did, so the payload's own rendering can be told apart from the editor's.
const observers = [];
class MutationObserver {
  constructor(fn) { this.fn = fn; }
  observe() { observers.push(this); }
  disconnect() { const i = observers.indexOf(this); if (i >= 0) observers.splice(i, 1); }
}
function mutate(target) {
  for (const o of observers.slice()) o.fn([{ target: target || body }]);
}

global.window = window;
global.document = document;
global.MutationObserver = MutationObserver;

module.exports = { El, window, document, body, head, flush, post, postLikeGtaw, widget, listeners,
                   setFontMetrics, setFontProbe, advanceClock, resetClock, mutate, observers,
                   fire, docListeners };
