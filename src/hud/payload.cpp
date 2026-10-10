#include "hud/payload.h"

#include "cdp/json.h"
#include "hud/assets.h"
#include "hud/images.h"
#include "core/layout.h"
#include "core/logger.h"
#include "core/settings.h"
#include "core/util.h"
#include "core/version.h"

#include <cstdio>
#include <string>
#include <vector>

namespace hud {

namespace {

// ---------------------------------------------------------------------------------------------
// Number formatting
//
// These numbers are pasted straight into a stylesheet. The C runtime's decimal separator follows
// the process locale, and this DLL is a guest inside a game that may have set one where the
// separator is a comma. "0,5" is not an opacity, so the separator is normalised after formatting
// rather than hoped about.
// ---------------------------------------------------------------------------------------------
std::string num(double v, int decimals = 3)
{
    char buf[64];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.*f", decimals, v);

    std::string out(buf);
    for (char& c : out)
        if (c == ',') c = '.';
    return out;
}

// The panel's layers, encoded for the stylesheet the first time the panel is installed and kept
// for every reinstall after.
std::vector<std::string> g_panelBase64;
bool g_assetsLoaded = false;

void loadAssetsOnce()
{
    if (g_assetsLoaded) return;
    g_assetsLoaded = true;

    std::string bytes;
    for (int i = 0; kPanelLayers[i]; ++i) {
        std::wstring name = L"KB_";
        for (const char* c = kPanelLayers[i]; *c; ++c)
            name.push_back(wchar_t(*c >= 'a' && *c <= 'z' ? *c - 'a' + 'A' : *c));
        if (loadAsset(name.c_str(), bytes)) g_panelBase64.push_back(util::base64(bytes));
        else {
            g_panelBase64.push_back(std::string());
            LOG_WARN("Panel layer %s is missing from the plugin", kPanelLayers[i]);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The stylesheet
//
// Every rule is prefixed with the root id. The reset has to be able to beat a stray element rule
// in the page's own stylesheet, so it carries the id; the rules after it carry it too so they
// are not beaten by their own reset.
// ---------------------------------------------------------------------------------------------
const char* kBaseCss = R"CSS(
#gtaw-siren-panel-root{
  position:fixed; left:0; top:0; right:0; bottom:0;
  margin:0; padding:0; border:0;
  pointer-events:none;
  z-index:4550;
  opacity:1;
  transition:opacity 200ms linear;
  contain:layout style;
}
#gtaw-siren-panel-root *{
  margin:0; padding:0; border:0; box-sizing:border-box; background:none;
}

/* The Siren Master panel. Every layer is a full-size picture of the whole panel with only its own
   part drawn in, stretched to the panel's width from the top-left corner, so they stack into one
   image. The clip keeps a rounding error in the height from showing a sliver past the edge. The
   fade is the hold: with hold_ms set, the panel fades out that long after its last change. */
#gtaw-siren-panel-root .smp-panel{
  position:absolute;
  opacity:var(--smp-alpha);
  transition:opacity 300ms linear;
}
#gtaw-siren-panel-root .smp-panel.smp-faded{ opacity:0; }
#gtaw-siren-panel-root .smp-clip{
  position:absolute; left:0; top:0; width:100%; height:100%;
  overflow:hidden;
}
#gtaw-siren-panel-root .smp-ss{
  position:absolute; left:0; top:0; width:100%; height:100%;
  background-repeat:no-repeat; background-position:0 0; background-size:100% auto;
}

/* While the game's layout editor is open the panel can be taken hold of the way its own widgets
   can: outlined, dragged, scaled with the wheel, hidden with the cross. */
#gtaw-siren-panel-root.smp-editing .smp-panel{
  pointer-events:auto; cursor:move; opacity:var(--smp-alpha);
  outline:1px dashed rgba(255,172,0,0.8); outline-offset:2px;
}
#gtaw-siren-panel-root.smp-editing .smp-panel.smp-off{ opacity:0.3; }
#gtaw-siren-panel-root .smp-x{ display:none; }
#gtaw-siren-panel-root.smp-editing .smp-x{
  display:flex; position:absolute; top:-10px; right:-10px; width:20px; height:20px;
  align-items:center; justify-content:center; border-radius:50%;
  background:#c0392b; color:#fff; border:1px solid rgba(255,255,255,0.85);
  font:15px/1 sans-serif; cursor:pointer; pointer-events:auto;
}
#gtaw-siren-panel-root.smp-editing .smp-x::before{ content:"\00d7"; }
)CSS";

// One rule per panel layer, carrying its picture. They are written after the reset, which
// clears every background, and are as specific as it so they win by coming later.
std::string buildPanelCss()
{
    std::string css;
    for (size_t i = 0; kPanelLayers[i] && i < g_panelBase64.size(); ++i) {
        if (g_panelBase64[i].empty()) continue;
        css += "#gtaw-siren-panel-root .smp-ss-";
        css += kPanelLayers[i];
        css += "{background-image:url(data:image/png;base64,";
        css += g_panelBase64[i];
        css += ");}\n";
    }
    return css;
}

std::string buildCss()
{
    std::string css;
    css += "#gtaw-siren-panel-root{ --smp-alpha:" + num(g_set.alpha, 3) + "; }\n";
    css += kBaseCss;
    css += buildPanelCss();
    return css;
}

// ---------------------------------------------------------------------------------------------
// The injected script
//
// It only listens. It never posts a message, never calls an NUI callback and never touches the
// game's own state or its HUD: it reads the messages the interface is already receiving and
// draws the panel from them, on top of everything else.
// ---------------------------------------------------------------------------------------------
const char* kScript = R"JS(function (CFG) {
  'use strict';

  var NS = '__gtawSirenPanel', ROOT_ID = 'gtaw-siren-panel-root', STYLE_ID = 'gtaw-siren-panel-style';

  // A previous install may still be running in an older private world, or may have left its
  // nodes behind when that world was torn down with the document. Clear both before rebuilding.
  if (window[NS] && typeof window[NS].destroy === 'function') {
    try { window[NS].destroy(); } catch (e) {}
  }
  var stale = [document.getElementById(ROOT_ID), document.getElementById(STYLE_ID)];
  for (var s = 0; s < stale.length; s++)
    if (stale[s] && stale[s].parentNode) stale[s].parentNode.removeChild(stale[s]);

  if (!document.body) return 'no-body';

  // ---- where the panel goes -------------------------------------------------------------
  // It sits on top of the speedometer, right edges lined up, GAP of the screen height above it,
  // so it follows the speedometer wherever the game's own layout put it. Its height is PANEL_H
  // of the screen height and its width the layers' own 1200 by 580.
  //
  // Before the speedometer is on the page, the panel goes where it would be at the game's
  // default layout: SPEEDO_RIGHT in from the right edge, with its bottom SPEEDO_TOP up from the
  // bottom of the screen.
  var PANEL_H = 0.14, PANEL_ASPECT = 1200 / 580, GAP = 0.012;
  var SPEEDO_RIGHT = 0.025, SPEEDO_TOP = 0.24;

  // ---- state ---------------------------------------------------------------------------
  var spd = { visible: false, elsVisible: false, elsCode: 1, elsTone: 0,
              engineOn: null, lightOn: false, fullLightOn: false, turnLeft: false,
              turnRight: false, isLocked: false, cruiseOn: false, handbrake: false };

  // With a hold set, the panel goes up whenever what it shows changes and fades out that long
  // after. changedAt is when it last changed, litKey what it showed then.
  var changedAt = 0, litKey = '', holdTimer = null;

  var offset = { x: 0, y: 0 };
  var hudVisible = true, pauseMenu = false, fadeOpaque = true, fadeMs = 200;

  // ---- nodes ---------------------------------------------------------------------------
  var style = document.createElement('style');
  style.id = STYLE_ID;
  style.textContent = CFG.css;
  (document.head || document.documentElement).appendChild(style);

  var root = document.createElement('div');
  root.id = ROOT_ID;

  // The panel, bottom layer first. Each layer is shown or hidden as a whole, and each key's
  // layer is named after the key.
  var PANEL_LAYERS = ['base', 'backlight',
                      'lrco', 'warn', 'dim', 'flood',
                      'stby', 'radio', 'sub', 'sbs', 'wail', 'yelp', 'wig', 'bumr',
                      'leftalley', 'takedown', 'rightalley', 'crimeject', 'gunlock',
                      'led1', 'led2', 'led3', 'knob0', 'knob1', 'knob2', 'knob3'];

  // The keys, and the ones a siren tone can light: the siren row, right of STBY and RADIO.
  var KEYS = PANEL_LAYERS.slice(2, 19);
  var TONE_KEYS = ['sub', 'sbs', 'wail', 'yelp', 'wig', 'bumr'];

  // The vehicle state a key can follow, as the speedometer reports it. Which key follows which is
  // the keys setting; every key but STBY, which is the siren's own, can be given one.
  var KEY_SOURCE = {
    cruise:      function () { return !!spd.cruiseOn; },
    handbrake:   function () { return !!spd.handbrake; },
    headlights:  function () { return !!spd.lightOn || !!spd.fullLightOn; },
    highbeams:   function () { return !!spd.fullLightOn; },
    leftsignal:  function () { return !!spd.turnLeft; },
    hazards:     function () { return !!spd.turnLeft && !!spd.turnRight; },
    rightsignal: function () { return !!spd.turnRight; },
    locked:      function () { return !!spd.isLocked; },
    engine:      function () { return spd.engineOn === true; },
    none:        function () { return false; }
  };

  var panelEl = document.createElement('div');
  panelEl.className = 'smp-panel';
  root.appendChild(panelEl);
  var panelClip = document.createElement('div');
  panelClip.className = 'smp-clip';
  panelEl.appendChild(panelClip);
  var panelLayer = {};
  for (var pl = 0; pl < PANEL_LAYERS.length; pl++) {
    var layer = document.createElement('div');
    layer.className = 'smp-ss smp-ss-' + PANEL_LAYERS[pl];
    panelClip.appendChild(layer);
    panelLayer[PANEL_LAYERS[pl]] = layer;
  }
  var crossEl = document.createElement('span');
  crossEl.className = 'smp-x';
  panelEl.appendChild(crossEl);

  // ---- placing with the game's layout editor (/hudlayout) --------------------------------
  // The editor is the page's own and its widgets are fixed; the panel cannot be registered in it
  // and this never sends anything, so what the game saves stays the game's. But its session is
  // all on the page: a toolbar while it is open, a grid while snapping is on, its buttons in a
  // fixed order. So the panel is edited inside that session the same way -- outlined, dragged,
  // scaled with the wheel, hidden with a cross -- and the result is handed to the plugin through
  // its heartbeat, which keeps it next to the .asi.
  //
  // x and y are fractions of the screen, s multiplies the panel's size.
  var SCALE_STEP = 0.05, SCALE_MIN = 0.4, SCALE_MAX = 3, SNAP = 10;
  var REST = { x: 0, y: 0, s: 1, hidden: false };

  var lay = REST, laySaved = REST, layPending = null;
  var editing = false, snapping = false, cancelled = false, drag = null, editTimer = null;

  function copy(l) { return { x: l.x, y: l.y, s: l.s, hidden: l.hidden }; }

  // The file's shape: "panel = <x> <y> <scale> <hidden>", or nothing at all when at rest.
  function formatLayout(l) {
    if (!l.x && !l.y && l.s === 1 && !l.hidden) return '';
    return 'panel = ' + l.x.toFixed(5) + ' ' + l.y.toFixed(5) + ' ' + l.s.toFixed(2) + ' ' +
           (l.hidden ? '1' : '0');
  }

  function parseLayout(text) {
    var lines = String(text || '').split(/\r?\n/);
    for (var i = 0; i < lines.length; i++) {
      var m = /^\s*panel\s*=\s*(-?[0-9.]+)\s+(-?[0-9.]+)\s+([0-9.]+)\s+([01])\s*$/.exec(lines[i]);
      if (!m) continue;
      var x = Number(m[1]), y = Number(m[2]), sc = Number(m[3]);
      if (!isFinite(x) || !isFinite(y) || !isFinite(sc)) continue;
      return { x: Math.max(-1, Math.min(1, x)), y: Math.max(-1, Math.min(1, y)),
               s: Math.max(SCALE_MIN, Math.min(SCALE_MAX, sc)), hidden: m[4] === '1' };
    }
    return copy(REST);
  }

  function onPanel(node) {
    for (var n = node; n && n !== root; n = n.parentNode) if (n === panelEl) return true;
    return false;
  }

  function snap(px) { return snapping ? Math.round(px / SNAP) * SNAP : px; }

  // Matches the editor's own session: its toolbar is on the page while it is open, and its
  // grid while snapping is on. Opening takes a copy to go back to; closing is a save unless
  // Cancel was pressed, since Save, Escape and the game's own close all keep what was done.
  //
  // While it is open the speedometer can be dragged too, and the panel sits on it, so the panel
  // is re-placed a few times a second until it closes.
  function syncEditor() {
    var open = !!document.querySelector('.hle-root');
    snapping = !!document.querySelector('.hle-grid');
    if (open === editing) return;
    editing = open;
    if (editing) {
      laySaved = copy(lay);
      cancelled = false;
      if (!editTimer) editTimer = setInterval(schedule, 250);
    } else {
      endDrag();
      if (editTimer) { clearInterval(editTimer); editTimer = null; }
      if (cancelled) lay = copy(laySaved);
      else if (formatLayout(lay) !== formatLayout(laySaved)) layPending = formatLayout(lay);
    }
    root.className = editing ? 'smp-editing' : '';
    schedule();
  }

  // The toolbar's buttons, in the order the editor lays them out.
  var BTN_RESET = 2, BTN_CANCEL = 3;

  function onClickCapture(ev) {
    if (!editing) return;
    var t = ev.target;
    if (!t || !t.parentNode) return;
    var bar = null;
    for (var n = t; n; n = n.parentNode)
      if (n.className && String(n.className).indexOf('hle-buttons') >= 0) { bar = n; break; }
    if (!bar) return;
    var buttons = [], i;
    for (i = 0; i < bar.childNodes.length; i++)
      if (bar.childNodes[i].tagName === 'BUTTON') buttons.push(bar.childNodes[i]);
    var which = -1;
    for (i = 0; i < buttons.length; i++)
      for (var m = t; m; m = m.parentNode) if (m === buttons[i]) { which = i; break; }
    if (which === BTN_RESET)  { lay = copy(REST); schedule(); }
    if (which === BTN_CANCEL) { cancelled = true; }
  }

  function onMouseDown(ev) {
    if (!editing || ev.button !== 0 || !onPanel(ev.target)) return;
    ev.preventDefault();
    ev.stopPropagation();
    lay = copy(lay);
    if (ev.target === crossEl) {
      lay.hidden = !lay.hidden;
      schedule();
      return;
    }
    drag = { x0: ev.clientX, y0: ev.clientY, sx: lay.x, sy: lay.y };
    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', endDrag);
  }

  function onMouseMove(ev) {
    if (!drag) return;
    var W = window.innerWidth || 1, H = window.innerHeight || 1;
    lay.x = snap(drag.sx * W + (ev.clientX - drag.x0)) / W;
    lay.y = snap(drag.sy * H + (ev.clientY - drag.y0)) / H;
    schedule();
  }

  function endDrag() {
    if (!drag) return;
    drag = null;
    window.removeEventListener('mousemove', onMouseMove);
    window.removeEventListener('mouseup', endDrag);
  }

  function onWheel(ev) {
    if (!editing || !onPanel(ev.target)) return;
    ev.preventDefault();
    ev.stopPropagation();
    var next = lay.s + (ev.deltaY < 0 ? SCALE_STEP : -SCALE_STEP);
    next = Math.max(SCALE_MIN, Math.min(SCALE_MAX, Math.round(next * 100) / 100));
    if (next === lay.s) return;
    lay = copy(lay);
    lay.s = next;
    schedule();
  }

  root.addEventListener('mousedown', onMouseDown);
  root.addEventListener('wheel', onWheel);
  document.addEventListener('click', onClickCapture, true);

  lay = parseLayout(CFG.layout);

  document.body.appendChild(root);

  // ---- helpers -------------------------------------------------------------------------
  function show(node, on) { node.style.display = on ? '' : 'none'; }

  function toNum(v) {
    if (typeof v === 'number') return isFinite(v) ? v : null;
    if (typeof v === 'string') {
      var cleaned = v.replace(/[^0-9.\-]/g, '');
      if (!cleaned) return null;
      var n = Number(cleaned);
      return isFinite(n) ? n : null;
    }
    return null;
  }

  // The bottom-right corner the panel hangs from: the top-right of the speedometer, or where it
  // would be at the default layout. A speedometer that is not drawn reports an empty rectangle
  // and is not used.
  function corner(W, H) {
    var speedo = document.querySelector('.spd-root');
    if (speedo && speedo.style.display !== 'none') {
      var b = speedo.getBoundingClientRect();
      if (b.width > 0 && b.height > 0) return { x: b.left + b.width, y: b.top - GAP * H };
    }
    // The game's nudge for the whole HUD. The speedometer already carries it; the default spot
    // has to be given it.
    return { x: (1 - SPEEDO_RIGHT + offset.x) * W, y: (1 - SPEEDO_TOP + offset.y) * H };
  }

  // ---- drawing -------------------------------------------------------------------------
  var pending = false;
  function schedule() {
    if (pending) return;
    pending = true;
    window.requestAnimationFrame(render);
  }

  function render() {
    pending = false;

    var W = window.innerWidth || 0, H = window.innerHeight || 0;

    root.style.transitionDuration = fadeMs + 'ms';
    root.style.opacity = (hudVisible && !pauseMenu && fadeOpaque) ? '1' : '0';

    var c = corner(W, H);
    var ph = PANEL_H * H * lay.s, pw = ph * PANEL_ASPECT;
    panelEl.style.width  = pw + 'px';
    panelEl.style.height = ph + 'px';
    panelEl.style.left   = (c.x - pw + lay.x * W) + 'px';
    panelEl.style.top    = (c.y - ph + lay.y * H) + 'px';

    var lit = panelState();
    for (var li = 0; li < PANEL_LAYERS.length; li++)
      show(panelLayer[PANEL_LAYERS[li]], !!lit[PANEL_LAYERS[li]]);

    // Up in an emergency vehicle; while the editor is open, up so it can be placed. A panel
    // hidden in the editor is drawn faded there, so it can be found and brought back.
    var inCar = !!spd.visible && !!spd.elsVisible;
    show(panelEl, editing || (inCar && !lay.hidden));
    var cls = 'smp-panel';
    if (editing && lay.hidden) cls += ' smp-off';
    if (!editing && CFG.holdMs > 0 && Date.now() - changedAt >= CFG.holdMs) cls += ' smp-faded';
    if (panelEl.className !== cls) panelEl.className = cls;
  }

  // Which of the panel's layers are lit. The game reports three ELS states -- 1 off, 2 lights,
  // 3 lights and siren -- and, with the siren on, which tone it is sounding, counted from 1.
  // Off is the panel at standby with the knob at OFF; lights put the knob where the settings say;
  // the siren puts it at 3 and lights the key for the tone. The LEDs above it count the stage.
  //
  // The key legends are backlit while the engine runs, the way a control head on the ignition
  // is, unless the settings say always. An engine state that has not arrived counts as running,
  // so a panel is never left dark for want of a field. The rest of the keys follow the vehicle,
  // as the keys setting maps them; the editor shows the panel at rest.
  function panelState() {
    if (editing && !spd.visible) return { base: true, backlight: true, stby: true, knob0: true };
    var code = toNum(spd.elsCode), tone = toNum(spd.elsTone);
    var siren = code === 3, lights = code === 2;
    var stage = siren ? 3 : lights ? CFG.lightsStage : 0;
    var key = (siren && tone !== null && tone >= 1) ? CFG.toneKeys[Math.round(tone) - 1] : null;
    var powered = CFG.backlight === 'always' || spd.engineOn !== false;
    var lit = { base: true, backlight: powered, stby: powered && !siren && !lights };
    lit['knob' + stage] = true;
    for (var led = 1; led <= stage; led++) lit['led' + led] = true;
    if (key && TONE_KEYS.indexOf(key) >= 0) lit[key] = true;
    for (var k in CFG.keys) {
      var src = CFG.keys[k];
      if (k !== 'stby' && KEYS.indexOf(k) >= 0 && KEY_SOURCE.hasOwnProperty(src) && KEY_SOURCE[src]())
        lit[k] = true;
    }
    return lit;
  }

  // Restarts the hold whenever what the panel shows changes -- the knob, a tone, any key, the
  // backlight -- or it comes up in a new vehicle, and wakes the panel to fade it when it runs out.
  function noteChange() {
    var lit = panelState(), on = [];
    for (var i = 0; i < PANEL_LAYERS.length; i++) if (lit[PANEL_LAYERS[i]]) on.push(PANEL_LAYERS[i]);
    var key = (spd.visible && spd.elsVisible ? '1:' : '0:') + on.join(',');
    if (key === litKey) return;
    litKey = key;
    changedAt = Date.now();
    if (holdTimer) { clearTimeout(holdTimer); holdTimer = null; }
    if (CFG.holdMs > 0)
      holdTimer = setTimeout(function () { holdTimer = null; schedule(); }, CFG.holdMs + 50);
  }

  // ---- incoming messages ----------------------------------------------------------------
  var SPD_KEYS = ['visible', 'elsVisible', 'elsCode', 'elsTone', 'engineOn', 'lightOn',
                  'fullLightOn', 'turnLeft', 'turnRight', 'isLocked', 'cruiseOn', 'handbrake'];

  // Merge, never replace: an update carries only the fields that changed, so a field left out
  // has to keep its previous value.
  function merge(target, keys, data) {
    if (!data || typeof data !== 'object') return;
    for (var i = 0; i < keys.length; i++)
      if (keys[i] in data) target[keys[i]] = data[keys[i]];
  }

  // The speedometer slides in when it is shown, so the panel is placed against it again once it
  // has had time to settle.
  function settle() {
    setTimeout(schedule, 150);
    setTimeout(schedule, 400);
    setTimeout(schedule, 800);
  }

  function handle(msg) {
    var action = msg.action, data = msg.data;

    // A batch carries its updates in an array. Each child is also delivered again as a message
    // of its own, so every child is seen twice; the handlers only ever merge fields, so
    // applying one twice lands on the same state.
    if (action === 'NUI::Global::Batch') {
      if (!Array.isArray(data)) return;
      for (var i = 0; i < data.length; i++) {
        var child = data[i];
        if (child && typeof child === 'object' && child.action) handle(child);
      }
      return;
    }

    switch (action) {
      case 'NUI::Speedometer::State': merge(spd, SPD_KEYS, data); noteChange(); break;
      case 'NUI::Speedometer::Show':
        // Entering a vehicle starts from a clean reading: the last vehicle's lights, siren,
        // signals, locks, brake and cruise control do not carry over into this one.
        spd.visible = true;
        spd.elsVisible = false; spd.elsCode = 1; spd.elsTone = 0;
        spd.engineOn = null; spd.lightOn = false; spd.fullLightOn = false; spd.turnLeft = false;
        spd.turnRight = false; spd.isLocked = false; spd.cruiseOn = false; spd.handbrake = false;
        merge(spd, SPD_KEYS, data);
        noteChange();
        settle();
        break;
      case 'NUI::Speedometer::Hide':  spd.visible = false; noteChange(); break;
      case 'NUI::HudLayout::Edit':    syncEditor(); break;
      case 'NUI::Hud::Offset':
        offset = { x: (data && Number(data.x)) || 0, y: (data && Number(data.y)) || 0 };
        break;
      case 'SET_HUD_VISIBLE':         hudVisible = !!data; break;
      case 'NUI::Hud::PauseMenu':     pauseMenu = !!(data && data.active); break;
      case 'NUI::Hud::InactiveFade':
        fadeOpaque = !data || data.opaque !== false;
        var ms = data ? Number(data.ms) : NaN;
        fadeMs = isFinite(ms) && ms > 0 ? ms : 200;
        break;
      default: return;
    }
    schedule();
  }

  function onMessage(ev) {
    var data = ev.data;
    if (typeof data === 'string') {
      try { data = JSON.parse(data); } catch (e) { return; }
    }
    if (!data || typeof data !== 'object' || !data.action) return;
    handle(data);
  }

  function onResize() { schedule(); }

  // ---- lifetime ---------------------------------------------------------------------------
  window.addEventListener('message', onMessage);
  window.addEventListener('resize', onResize);

  // The editor's toolbar and grid come and go on the page, so the page is watched for nodes
  // appearing and going. The observer is batched by the browser and the check is two lookups.
  var observer = null;
  if (typeof MutationObserver === 'function') {
    try {
      observer = new MutationObserver(function (records) {
        for (var i = 0; i < records.length; i++) {
          if (root.contains(records[i].target)) continue;
          syncEditor();
          return;
        }
      });
      observer.observe(document.documentElement, { subtree: true, childList: true });
    } catch (e) { observer = null; }
  }

  syncEditor();
  render();

  window[NS] = {
    version: CFG.version,

    // Also repairs the panel: if something cleared the page's nodes, put them back.
    alive: function () {
      if (!document.getElementById(STYLE_ID))
        (document.head || document.documentElement).appendChild(style);
      if (!document.getElementById(ROOT_ID)) {
        if (!document.body) return false;
        document.body.appendChild(root);
        schedule();
      }
      // Without a MutationObserver this heartbeat is what notices the editor.
      if (!observer) syncEditor();
      return true;
    },

    // The layout as the editor left it since the last call, in the file's shape (empty after
    // a Reset), or null when it has not changed.
    pull: function () {
      var p = layPending;
      layPending = null;
      return p;
    },

    destroy: function () {
      window.removeEventListener('message', onMessage);
      window.removeEventListener('resize', onResize);
      document.removeEventListener('click', onClickCapture, true);
      endDrag();
      if (observer) { try { observer.disconnect(); } catch (e) {} observer = null; }
      if (holdTimer) { clearTimeout(holdTimer); holdTimer = null; }
      if (editTimer) { clearInterval(editTimer); editTimer = null; }
      if (root.parentNode)  root.parentNode.removeChild(root);
      if (style.parentNode) style.parentNode.removeChild(style);
      try { delete window[NS]; } catch (e) { window[NS] = undefined; }
    }
  };

  return 'installed ' + CFG.version;
})JS";

}  // namespace

std::string buildInstallScript()
{
    loadAssetsOnce();

    std::string cfg = "{";
    cfg += "\"version\":"     + json::quote(GTAW_SIREN_PANEL_VERSION);
    cfg += ",\"css\":"        + json::quote(buildCss());
    cfg += ",\"layout\":"     + json::quote(layout::load());
    cfg += ",\"holdMs\":"     + std::to_string(g_set.holdMs);
    cfg += ",\"lightsStage\":" + std::to_string(g_set.lightsStage);
    cfg += ",\"toneKeys\":[";
    {
        bool first = true;
        for (const std::string& key : util::split(g_set.toneKeys, ',')) {
            if (!first) cfg += ",";
            cfg += json::quote(key);
            first = false;
        }
    }
    cfg += "]";
    cfg += ",\"keys\":{";
    {
        bool first = true;
        for (const std::string& pair : util::split(g_set.keys, ',')) {
            size_t colon = pair.find(':');
            if (colon == std::string::npos) continue;
            if (!first) cfg += ",";
            cfg += json::quote(pair.substr(0, colon)) + ":" + json::quote(pair.substr(colon + 1));
            first = false;
        }
    }
    cfg += "}";
    cfg += ",\"backlight\":" + json::quote(g_set.backlight);
    cfg += "}";

    return std::string("(") + kScript + ")(" + cfg + ")";
}

// Answers "" when the panel is gone, "ok" when it is there, and "ok\n<layout>" when the editor
// changed its layout since the last heartbeat, so the plugin can keep it.
std::string buildProbeScript()
{
    return "(function(){var a=window.__gtawSirenPanel;"
           "if(!(a && typeof a.alive === 'function' && a.alive())) return '';"
           "var p = typeof a.pull === 'function' ? a.pull() : null;"
           "return 'ok' + (p === null ? '' : '\\n' + p);})()";
}

}  // namespace hud
