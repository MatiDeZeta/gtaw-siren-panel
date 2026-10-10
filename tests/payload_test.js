// Drives the injected payload against the stub DOM with the messages the HUD receives in game.
// Checks what the panel lights, where it sits, when it shows, and that the game's HUD is left
// alone.
//
// Usage: node tests/payload_test.js <extracted-script.js>
'use strict';
const fs = require('fs');
const dom = require('./dom');
const { section, check, done } = require('./assert');

const { window, document, body, flush, postLikeGtaw: post, widget, advanceClock, mutate, fire } = dom;

const W = window.innerWidth, H = window.innerHeight;   // 1600 x 900

// ---- the current HUD's speedometer, bottom right ---------------------------------------------
const speedo = widget('spd-root');
speedo._rect = { left: 1300, top: 640, width: 260, height: 180 };

// ---- run the payload -------------------------------------------------------------------------
const js = fs.readFileSync(process.argv[2], 'utf8');
const CFG = {
  version: '1.0.0', css: '/* css */', layout: '',
  holdMs: 0, lightsStage: 2,
  toneKeys: ['wail', 'yelp', 'sbs', 'wig', 'bumr', 'sub'],
  keys: { lrco: 'handbrake', warn: 'hazards', dim: 'headlights', flood: 'none', radio: 'none',
          takedown: 'highbeams', leftalley: 'leftsignal', rightalley: 'rightsignal',
          crimeject: 'none', gunlock: 'locked' },
  backlight: 'engine',
};
const install = eval('(' + js + ')');
const installResult = install(CFG);
flush();

const LAYERS = ['base', 'backlight', 'lrco', 'warn', 'dim', 'flood', 'stby', 'radio', 'sub',
                'sbs', 'wail', 'yelp', 'wig', 'bumr', 'leftalley', 'takedown', 'rightalley',
                'crimeject', 'gunlock', 'led1', 'led2', 'led3', 'knob0', 'knob1', 'knob2', 'knob3'];

const root  = () => document.getElementById('gtaw-siren-panel-root');
const panel = () => root().childNodes[0];
const cross = () => panel().childNodes[1];
const shown = () => panel().style.display !== 'none';
const faded = () => panel().className.includes('smp-faded');
// The layers that are drawn, in stacking order.
const lit = () => panel().childNodes[0].childNodes
    .map((n, i) => n.style.display === 'none' ? null : LAYERS[i]).filter(Boolean).join(' ');
const px = (v) => Math.round(parseFloat(v) * 100) / 100;
const state = (data) => { post({ action: 'NUI::Speedometer::State', data }); flush(); };
// Pushes a render through without changing any state, so a test can see what the panel draws
// after moving the clock.
const tick = () => { post({ action: 'SET_HUD_VISIBLE', data: true }); flush(); };

section('install');
check('return value', installResult, 'installed 1.0.0');
check('one root, holding the panel', root().childNodes.length, 1);
check('every layer is there', panel().childNodes[0].childNodes.length, LAYERS.length);

section('on foot, nothing');
check('the panel is not drawn', shown(), false);

section('in a car that is not an emergency vehicle, nothing');
post({ action: 'NUI::Speedometer::Show', data: { elsVisible: false } });
flush();
check('still not drawn', shown(), false);

section('in an emergency vehicle, the panel at standby');
state({ elsVisible: true, elsCode: 1, engineOn: true });
check('drawn', shown(), true);
check('standby, knob at OFF, legends backlit', lit(), 'base backlight stby knob0');
check('not faded: it stays up by default', faded(), false);

section('lights');
state({ elsCode: 2 });
check('knob at 2, two LEDs, no standby', lit(), 'base backlight led1 led2 knob2');

section('siren, and each tone lights its key');
state({ elsCode: 3, elsTone: 1 });
check('tone 1 is WAIL, knob at 3, three LEDs', lit(), 'base backlight wail led1 led2 led3 knob3');
state({ elsTone: 2 });
check('tone 2 is YELP', lit(), 'base backlight yelp led1 led2 led3 knob3');
state({ elsTone: 6 });
check('tone 6 is SUB', lit(), 'base backlight sub led1 led2 led3 knob3');
state({ elsTone: 9 });
check('a tone past the list lights no key', lit(), 'base backlight led1 led2 led3 knob3');
state({ elsCode: 1, elsTone: 0 });

section('GUN LOCK follows the door locks, as the speedometer reports them');
state({ isLocked: true });
check('locked lights GUN LOCK', lit(), 'base backlight stby gunlock knob0');
state({ isLocked: false });
check('unlocked puts it out', lit(), 'base backlight stby knob0');

section('the other keys follow the vehicle');
state({ handbrake: true, lightOn: true, turnLeft: true, turnRight: true });
check('LRCO, WARN, DIM and both alleys', lit(),
      'base backlight lrco warn dim stby leftalley rightalley knob0');
state({ fullLightOn: true, turnRight: false, handbrake: false });
check('high beams are TAKE DOWN, and still DIM', lit(),
      'base backlight dim stby leftalley takedown knob0');
state({ lightOn: false, fullLightOn: false, turnLeft: false });

section('the backlight follows the engine');
state({ engineOn: false });
check('engine off: dark, and no standby light', lit(), 'base knob0');
state({ engineOn: true });

section('it sits on top of the speedometer');
const ph = 0.14 * H, pw = ph * 1200 / 580;
check('as tall as 0.14 of the screen', px(panel().style.height), px(ph));
check('in the layers\' own proportions', px(panel().style.width), px(pw));
check('right edges lined up', px(parseFloat(panel().style.left) + pw), 1560);
check('bottom just above the speedometer', px(parseFloat(panel().style.top) + ph), px(640 - 0.012 * H));

speedo._rect = { left: 900, top: 300, width: 200, height: 100 };
tick();
check('and follows it where the layout put it', px(parseFloat(panel().style.left) + pw), 1100);
check('up as well as across', px(parseFloat(panel().style.top) + ph), px(300 - 0.012 * H));

speedo.style.display = 'none';
tick();
check('a speedometer that is not drawn is not used', px(parseFloat(panel().style.left) + pw), px(0.975 * W));
check('the panel goes where it would sit by default', px(parseFloat(panel().style.top) + ph), px(0.76 * H));
post({ action: 'NUI::Hud::Offset', data: { x: -0.01, y: 0.02 } });
flush();
check('and that spot takes the game\'s HUD nudge', px(parseFloat(panel().style.left) + pw), px(0.965 * W));
post({ action: 'NUI::Hud::Offset', data: { x: 0, y: 0 } });
speedo.style.display = '';
speedo._rect = { left: 1300, top: 640, width: 260, height: 180 };
tick();

section('the game\'s HUD is left alone');
check('the speedometer has no style of ours', speedo.style.opacity === undefined, true);
check('one stylesheet, and only ours', document.head.childNodes.map(n => n.id).join(','),
      'gtaw-siren-panel-style');
check('nothing on the page but the root', body.childNodes.filter(n => n.id).map(n => n.id).join(','),
      'gtaw-siren-panel-root');

section('it fades with the HUD');
post({ action: 'NUI::Hud::PauseMenu', data: { active: true } });
flush();
check('in the pause menu', root().style.opacity, '0');
post({ action: 'NUI::Hud::PauseMenu', data: { active: false } });
flush();
check('and back', root().style.opacity, '1');

section('leaving the vehicle');
post({ action: 'NUI::Speedometer::Hide' });
flush();
check('the panel goes', shown(), false);

section('a new vehicle starts from a clean reading');
post({ action: 'NUI::Speedometer::Show', data: { elsVisible: true } });
flush();
check('no lock, no siren carried over', lit(), 'base backlight stby knob0');

section('with hold_ms set it pops up on a change and fades');
install(Object.assign({}, CFG, { holdMs: 5000 }));
post({ action: 'NUI::Speedometer::Show', data: { elsVisible: true, engineOn: true } });
flush();
check('up on entering', faded(), false);
advanceClock(4900); tick();
check('still up just before five seconds', faded(), false);
advanceClock(200); tick();
check('faded after five seconds', faded(), true);
check('faded, not removed, so it can come back smoothly', shown(), true);
state({ isLocked: true });
check('locking the doors brings it back', faded(), false);
advanceClock(5100); tick();
check('and it fades again', faded(), true);
state({ elsCode: 3, elsTone: 2 });
check('the siren brings it back', faded(), false);
advanceClock(5100); tick();
state({ elsCode: 3, elsTone: 2 });
check('a repeat of the same state does not', faded(), true);

section('/hudlayout: the panel is placed in the editor\'s session');
install(CFG);
post({ action: 'NUI::Speedometer::Hide' });
flush();
check('hidden on foot', shown(), false);
const toolbar = widget('hle-root');
const buttons = widget('hle-buttons', null, toolbar);
const btn = [];
for (let i = 0; i < 4; i++) { const b = new dom.El('button'); buttons.appendChild(b); btn.push(b); }
mutate();
flush();
check('the root knows the editor is open', root().className, 'smp-editing');
check('shown so it can be placed', shown(), true);
check('at rest', lit(), 'base backlight stby knob0');

const left0 = parseFloat(panel().style.left), top0 = parseFloat(panel().style.top);
fire(panel(), 'mousedown', { clientX: 100, clientY: 100 });
fire(panel(), 'mousemove', { clientX: 60, clientY: 80 });
fire(panel(), 'mouseup');
flush();
check('dragged left', px(parseFloat(panel().style.left)), px(left0 - 40));
check('and up', px(parseFloat(panel().style.top)), px(top0 - 20));
fire(panel(), 'wheel', { deltaY: -1 });
flush();
check('the wheel scales it', px(panel().style.height), px(ph * 1.05));
fire(cross(), 'mousedown');
flush();
check('the cross hides it, drawn faded while editing', panel().className, 'smp-panel smp-off');
fire(cross(), 'mousedown');
flush();

body.removeChild(toolbar);
mutate();
flush();
check('closing the editor saves', window.__gtawSirenPanel.pull(), 'panel = -0.02500 -0.02222 1.05 0');
check('once', window.__gtawSirenPanel.pull(), null);

section('a saved layout is applied');
install(Object.assign({}, CFG, { layout: 'panel = -0.02500 -0.02222 1.05 0\r\n' }));
post({ action: 'NUI::Speedometer::Show', data: { elsVisible: true } });
flush();
const ph2 = ph * 1.05, pw2 = ph2 * 1200 / 580;
check('scaled', px(panel().style.height), px(ph2));
check('moved', px(parseFloat(panel().style.left)), px(1560 - pw2 - 40));
install(Object.assign({}, CFG, { layout: 'panel = 0 0 1.00 1\n' }));
post({ action: 'NUI::Speedometer::Show', data: { elsVisible: true } });
flush();
check('hidden in the editor stays hidden', shown(), false);

section('Cancel puts it back');
install(CFG);
post({ action: 'NUI::Speedometer::Show', data: { elsVisible: true } });
const toolbar2 = widget('hle-root');
const buttons2 = widget('hle-buttons', null, toolbar2);
const btn2 = [];
for (let i = 0; i < 4; i++) { const b = new dom.El('button'); buttons2.appendChild(b); btn2.push(b); }
mutate();
flush();
fire(panel(), 'wheel', { deltaY: -1 });
fire(btn2[3], 'click');
body.removeChild(toolbar2);
mutate();
flush();
check('nothing to save', window.__gtawSirenPanel.pull(), null);
check('the size is back', px(panel().style.height), px(ph));

section('it cleans up after itself');
window.__gtawSirenPanel.destroy();
check('root gone', document.getElementById('gtaw-siren-panel-root'), null);
check('style gone', document.getElementById('gtaw-siren-panel-style'), null);
check('no listener left', (dom.listeners.message || []).length, 0);

done();
