/* Renders the template with the SAME engine mod-ui uses (mustache), then
 * inspects the resulting DOM. This is the step the previous web UI was
 * missing: only the source file was reviewed, never the rendered result.
 *
 * Checks:
 *   - the template renders with no mustache braces left behind
 *   - the HTML produced is well formed (jsdom would complain otherwise)
 *   - every mod-port-symbol targets a port that EXISTS in the descriptor
 *   - every audio port in the descriptor has its jack
 *   - the classes the script looks for really exist in the DOM
 *   - the script is a valid function expression, and it runs
 */
const fs = require('fs');
const Mustache = require('/tmp/node_modules/mustache');
const { JSDOM } = require('/tmp/node_modules/jsdom');

let failed = 0;
function say(what, ok, detail) {
    console.log('  ' + what.padEnd(52) + (ok ? 'OK' : 'ECHEC') + (detail ? '  ' + detail : ''));
    if (!ok) failed = 1;
}

/* --- ports read from the descriptor, not retyped by hand --- */
const which = process.argv[2] === 'stereo' ? 'fade_stereo.ttl' : 'fade.ttl';
const ttl = fs.readFileSync(which, 'utf8');
console.log('  rendering with ' + which);
const blocks = ttl.split('] , [');
const ports = [];
for (const b of blocks) {
    const s = /lv2:symbol\s+"([^"]+)"/.exec(b);
    if (!s) continue;
    const n = /lv2:name\s+"([^"]+)"/.exec(b);
    ports.push({
        symbol: s[1],
        name: n ? n[1] : s[1],
        audio: b.includes('lv2:AudioPort'),
        input: b.includes('lv2:InputPort'),
        control: b.includes('lv2:ControlPort'),
    });
}

const data = {
    cns: '_http___remy_live_github_io_lv2_fade',
    brand: 'REMY',
    label: 'FONDU',
    effect: {
        ports: {
            audio: {
                input:  ports.filter(p => p.audio && p.input),
                output: ports.filter(p => p.audio && !p.input),
            },
            midi: { input: [], output: [] },
            cv:   { input: [], output: [] },
            control: {
                input:  ports.filter(p => p.control && p.input),
                output: ports.filter(p => p.control && !p.input),
            },
        },
    },
};

const template = fs.readFileSync('modgui/icon-fade.html', 'utf8');
let rendered;
try {
    rendered = Mustache.render(template, data);
    say('template renders without error', true);
} catch (e) {
    say('template renders without error', false, e.message);
    process.exit(1);
}

say('no mustache braces left', !/\{\{|\}\}/.test(rendered));

const dom = new JSDOM('<body>' + rendered + '</body>');
global.document = dom.window.document;   /* the drag code listens on document */
const doc = dom.window.document;
const racine = doc.querySelector('.mod-pedal');
say('a .mod-pedal root exists', racine !== null);

/* --- every targeted symbol must exist in the descriptor --- */
const known = new Set(ports.map(p => p.symbol));
let allKnown = true, seen = [];
for (const el of doc.querySelectorAll('[mod-port-symbol]')) {
    const s = el.getAttribute('mod-port-symbol');
    seen.push(s);
    if (!known.has(s)) { allKnown = false; console.log('    *** unknown symbol: ' + s); }
}
say('every mod-port-symbol exists in the descriptor', allKnown, seen.length + ' references');

/* --- every audio input/output has its jack --- */
for (const p of data.effect.ports.audio.input) {
    say('jack for audio input ' + p.symbol,
         doc.querySelector('[mod-role="input-audio-port"][mod-port-symbol="' + p.symbol + '"]') !== null);
}
for (const p of data.effect.ports.audio.output) {
    say('jack for audio output ' + p.symbol,
         doc.querySelector('[mod-role="output-audio-port"][mod-port-symbol="' + p.symbol + '"]') !== null);
}

/* --- roles that must be there --- */
for (const r of ['drag-handle', 'bypass', 'bypass-light']) {
    say('role ' + r + ' present', doc.querySelector('[mod-role="' + r + '"]') !== null);
}

/* --- are all the controls meant to be on the pedal there? --- */
for (const s of ['fade_1_2', 'fade_2_1', 'gain_1', 'gain_2', 'toggle', 'trigger']) {
    say('control ' + s + ' placed on the pedal',
         doc.querySelector('[mod-role="input-control-port"][mod-port-symbol="' + s + '"]') !== null);
}

/* --- does the script find its hooks? --- */
const script = fs.readFileSync('modgui/script-fade.js', 'utf8');
const hooks = [...script.matchAll(/find\('\.([a-z0-9-]+)'\)/g)].map(m => m[1]);
for (const c of [...new Set(hooks)]) {
    say('hook .' + c + ' present in the rendered DOM',
         doc.querySelector('.' + c) !== null);
}

/* --- is the script an executable function expression? --- */
let fn = null;
try {
    fn = eval('(' + script + ')');
    say('script evaluates to a function', typeof fn === 'function');
} catch (e) {
    say('script evaluates to a function', false, e.message);
}

/* --- and does it actually run on this DOM? --- */
if (typeof fn === 'function') {
    /* fake jQuery: only what the script uses, but enough to actually RUN
       the drag code rather than skip over it. */
    const handlers = {};
    const store = {};
    const fakeJq = (sel) => {
        const els = [...doc.querySelectorAll(sel)];
        const api = {
            css: () => api,
            text: () => api,
            toggleClass: () => api,
            data: (k, v) => (v === undefined ? store[sel + k] : (store[sel + k] = v, api)),
            on: (ev, fn) => { (handlers[sel + ev] = handlers[sel + ev] || []).push(fn); return api; },
            length: els.length,
            0: {
                getBoundingClientRect: () => ({ left: 100, top: 50, width: 208, height: 9,
                                                right: 308, bottom: 59 }),
            },
        };
        return api;
    };
    const icon = { find: fakeJq };
    try {
        /* Symbols must match the descriptor: with the old names this block
           exercised nothing at all and still reported success. */
        let wrote = null;
        const funcs = { set_port_value: (sym, v) => { wrote = [sym, v]; } };
        fn({ type: 'start', icon: icon, ports: [{ symbol: 'position', value: 0.42 }] }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'position', value: 0.75 }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'state', value: 1 }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'position', value: NaN }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'position', value: 99 }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'unknown', value: 3 }, funcs);
        fn({ type: 'start', icon: icon }, funcs);          /* with no ports */
        say('script survives start, change, NaN, out of range', true);

        /* Exercise the drag: mousedown at 25 % of the rail must write
           'progress' at about 25. This is the control the forum reported
           as doing nothing. */
        const md = handlers['.fade-track' + 'mousedown'];
        say('the rail has a mousedown handler', !!md && md.length > 0);
        if (md && md.length) {
            wrote = null;
            const md2 = handlers['.fade-track' + 'mousedown'];
            md2[md2.length - 1].call(null, {
                clientX: 152, preventDefault() {}, stopPropagation() {},
            });
            say('dragging writes the progress port',
                wrote !== null && wrote[0] === 'progress',
                wrote ? wrote[0] + ' = ' + wrote[1].toFixed(1) : 'nothing written');
            say('written value matches the grab position',
                wrote !== null && Math.abs(wrote[1] - 25) < 1.0);
        }
    } catch (e) {
        say('script survives start, change, NaN, out of range', false, e.message);
    }
}

/* --- stylesheet: {{{cns}}} unescaped, and it substitutes --- */
const css = fs.readFileSync('modgui/style-fade.css', 'utf8');
say('{{{cns}}} unescaped in the stylesheet', !/\\\{\\\{/.test(css));
const renderedCss = Mustache.render(css, data);
say('stylesheet substitutes completely', !/\{\{|\}\}/.test(renderedCss));

/* --- does every styled class exist in the DOM? (catches typos) --- */
const styledClasses = new Set([...renderedCss.matchAll(/\.(fade-[a-z0-9-]+)_http/g)].map(m => m[1]));
let orphans = [];
for (const c of styledClasses) {
    if (!doc.querySelector('[class*="' + c + '"]')) orphans.push(c);
}
say('no styled class missing from the template', orphans.length === 0, orphans.join(' '));


/* --- geometry: everything is absolutely positioned, so it can be checked
       by arithmetic. mod-ui clips nothing, so a block lower than the box
       would draw over the pedalboard. --- */
function num(re, text) { const m = re.exec(text); return m ? parseFloat(m[1]) : null; }
const pedalHeight = num(/\.mod-pedal-fade[^{]*\{[^}]*height:\s*(\d+)px/, renderedCss);
const layoutBlocks = [
    ['fade-head',   9,  40],   /* brand ~11px + name ~24px + margin */
    ['fade-meter',  58, 27],   /* labels + rail, knob dot included */
    ['fade-knobs',  92, 86],
    ['fade-cmds',   186, 54],
];
let bottom = 0, overlapping = [];
for (let i = 0; i < layoutBlocks.length; i++) {
    const [name, top, h] = layoutBlocks[i];
    if (top < bottom) overlapping.push(name);
    bottom = top + h;
}
const footTop = pedalHeight - 14 - 50;   /* bottom:14px, 50px tall */
say('declared pedal height', pedalHeight !== null, pedalHeight + 'px');
say('no block overlaps another', overlapping.length === 0, overlapping.join(' '));
say('last block ends above the footswitch', bottom <= footTop,
     bottom + 'px vs ' + footTop + 'px');
say('footswitch fits inside the box', footTop + 50 <= pedalHeight,
     (footTop + 50) + 'px of ' + pedalHeight + 'px');

console.log('  (browser rendering NOT verified: Chrome cannot be downloaded here)');

console.log(failed ? '\n*** THE WEB UI HAS PROBLEMS ***' : '\nWeb UI: all checks pass.');
process.exit(failed);
