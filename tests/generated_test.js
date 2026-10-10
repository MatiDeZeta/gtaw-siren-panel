// Runs the exact expression the C++ builds, rather than a hand-written copy of it, so the real
// generated stylesheet and config are what gets exercised.
//
// Usage: node tests/generated_test.js <install-script.js>
'use strict';
const fs = require('fs');
const dom = require('./dom');
const { section, check, done } = require('./assert');

const { document, flush, post, widget } = dom;
dom.window.innerWidth = 1920;
dom.window.innerHeight = 1080;

const speedo = widget('spd-root');
speedo._rect = { left: 1580, top: 760, width: 292, height: 228 };

const script = fs.readFileSync(process.argv[2], 'utf8');
const result = eval(script);
flush();

const root  = () => document.getElementById('gtaw-siren-panel-root');
const panel = () => root().childNodes[0];

section('the generated expression runs');
check('install', result, 'installed 1.0.0');

const css = document.getElementById('gtaw-siren-panel-style').textContent;

section('the generated stylesheet');
check('braces balance', (css.match(/{/g) || []).length === (css.match(/}/g) || []).length, 'true');
check('root rule present', css.includes('#gtaw-siren-panel-root{'), 'true');
for (const layer of ['base', 'backlight', 'lrco', 'warn', 'dim', 'flood', 'stby', 'radio', 'sub',
                     'sbs', 'wail', 'yelp', 'wig', 'bumr', 'leftalley', 'takedown', 'rightalley',
                     'crimeject', 'gunlock', 'led1', 'led2', 'led3', 'knob0', 'knob1', 'knob2',
                     'knob3'])
  check('the panel carries its ' + layer + ' layer as an inline PNG',
        css.includes('#gtaw-siren-panel-root .smp-ss-' + layer +
                     '{background-image:url(data:image/png;base64,iVBORw0KGgo'), 'true');
check('the panel is fully opaque by default', /--smp-alpha:1\.000;/.test(css), 'true');
// The decimal separator follows the process locale in C, and a comma here would silently break
// every number in the sheet.
check('decimal separator is a dot', /:\s*\d+,\d+(vh|vw|px|;)/.test(css) === false, 'true');

const used = new Set([...css.matchAll(/var\((--[a-z0-9-]+)/g)].map(m => m[1]));
const defined = new Set([...css.matchAll(/(--[a-z0-9-]+)\s*:/g)].map(m => m[1]));
const missing = [...used].filter(v => !defined.has(v));
check('no undefined custom properties', missing.length ? missing.join(',') : 'none', 'none');

section('the game\'s HUD is not touched');
// Every rule is the panel's own. Nothing hides, moves or restyles a widget of the game's.
const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
const selectors = [...bare.matchAll(/(^|})\s*([^{}@]+){/g)].map(m => m[2].trim());
const unscoped = selectors.filter(sel =>
  !sel.split(',').every(part => part.trim().startsWith('#gtaw-siren-panel-root')));
check('no rule outside the panel', unscoped.length ? unscoped.join(' | ') : 'none', 'none');
check('no !important anywhere', /!important/.test(css), 'false');
check('no second stylesheet', document.head.childNodes.length, 1);
check('no font is embedded: the panel has no text', /@font-face/.test(css), 'false');

section('nothing is fetched, at build time or at run time');
// The plugin opens one socket, to loopback. A stylesheet or a script that reaches out for an
// image or anything else would be an outbound connection from inside the game's browser, which
// this must never make.
const urls = [...script.matchAll(/url\(\s*['"]?([^'")]+)/g)].map(m => m[1].trim());
const external = urls.filter(u => !u.startsWith('data:'));
check('every url() is a data: URI', external.length ? external.join(' | ') : 'none', 'none');
check('no @import', /@import/.test(script), 'false');
check('no http(s) anywhere in what gets injected', /https?:\/\//.test(script), 'false');
check('no protocol-relative URL either', /['"(]\/\/[a-z0-9.-]+\//i.test(script), 'false');
check('nothing is loaded at run time', /\b(fetch|XMLHttpRequest|WebSocket|EventSource|importScripts)\b/.test(script), 'false');
check('nothing is sent', /\b(postMessage|fetchNui|\$\.post)\b/.test(script), 'false');

section('it draws the panel from real messages');
post({ action: 'NUI::Speedometer::Show', data: { elsVisible: true, engineOn: true } });
post({ action: 'NUI::Speedometer::State', data: { elsCode: 3, elsTone: 1, isLocked: true } });
flush();
const on = panel().childNodes[0].childNodes.filter(n => n.style.display !== 'none')
    .map(n => n.className.replace('smp-ss smp-ss-', '')).join(' ');
check('siren on WAIL with the doors locked', on, 'base backlight wail gunlock led1 led2 led3 knob3');
check('stays up with the default hold', panel().className, 'smp-panel');

section('and places it on the speedometer');
const ph = 0.14 * 1080, pw = ph * 1200 / 580;
check('right edges lined up', Math.round(parseFloat(panel().style.left) + pw), 1872);
check('bottom just above it', Math.round(parseFloat(panel().style.top) + ph), Math.round(760 - 0.012 * 1080));

done();
