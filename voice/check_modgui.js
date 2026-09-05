/* Renders the web UI with the SAME engine mod-ui uses (mustache), then
 * inspects the DOM that comes out, runs the script against it, and checks
 * the numbers the script has to keep in step with the plugin.
 *
 * The point of rendering rather than reading: a template is not what the
 * browser sees, and the previous version of this plugin shipped with no
 * web UI at all partly because there was no way to be sure one worked.
 *
 * Checks:
 *   - the template renders with no mustache braces left behind
 *   - EVERY control input port has somewhere to be turned
 *   - every mod-port-symbol targets a port that exists in the descriptor
 *   - every audio port has its jack, and the mandatory roles are present
 *   - the script is a function expression, it runs, and switching a
 *     switch really does light its section
 *   - the constants in the script match programs.h and the descriptor
 *   - the stylesheet substitutes, and styles nothing that is not there
 *   - the bank images exist and the thumbnail is wide, not a sliver
 */
const fs = require('fs');
const Mustache = require('/tmp/node_modules/mustache');
const { JSDOM } = require('/tmp/node_modules/jsdom');

let failed = 0;
function say(what, ok, detail) {
    console.log('  ' + what.padEnd(52) + (ok ? 'OK' : 'ECHEC') + (detail ? '  ' + detail : ''));
    if (!ok) failed = 1;
}

const stereo = process.argv[2] === 'stereo';
const which = stereo ? 'voice_stereo.ttl' : 'voice.ttl';
const ttl = fs.readFileSync(which, 'utf8');
console.log('  rendering with ' + which);

const ports = [];
for (const b of ttl.split('] , [')) {
    const s = /lv2:symbol\s+"([^"]+)"/.exec(b);
    if (!s) continue;
    const n = /lv2:name\s+"([^"]+)"/.exec(b);
    ports.push({
        symbol: s[1], name: n ? n[1] : s[1],
        audio: b.includes('lv2:AudioPort'),
        input: b.includes('lv2:InputPort'),
        control: b.includes('lv2:ControlPort'),
    });
}

const data = {
    cns: '_http___remy_live_github_io_lv2_voice',
    brand: 'REMY', label: stereo ? 'VOICE ST' : 'VOICE',
    effect: { ports: {
        audio: { input: ports.filter(p => p.audio && p.input),
                 output: ports.filter(p => p.audio && !p.input) },
        midi: { input: [], output: [] }, cv: { input: [], output: [] },
        control: { input: ports.filter(p => p.control && p.input),
                   output: ports.filter(p => p.control && !p.input) } } },
};

const template = fs.readFileSync('modgui/icon-voice.html', 'utf8');
let rendered;
try {
    rendered = Mustache.render(template, data);
    say('template renders without error', true);
} catch (e) {
    say('template renders without error', false, e.message);
    process.exit(1);
}
say('no mustache braces left', !/\{\{|\}\}/.test(rendered));

/* A URL, so the document has a real origin: without one jsdom refuses
   localStorage, and the USER slots the script keeps there would look
   broken here while working in a browser. */
const dom = new JSDOM('<body>' + rendered + '</body>',
                      { url: 'https://mod.local/voice' });
global.document = dom.window.document;
global.window = dom.window;
const doc = dom.window.document;
say('a .mod-pedal root exists', doc.querySelector('.mod-pedal') !== null);

/* --- every symbol targeted must exist --- */
const known = new Set(ports.map(p => p.symbol));
let inconnus = [];
for (const el of doc.querySelectorAll('[mod-port-symbol]')) {
    const s = el.getAttribute('mod-port-symbol');
    if (!known.has(s)) inconnus.push(s);
}
say('every mod-port-symbol exists in the descriptor', inconnus.length === 0,
    inconnus.join(' '));

/* --- and every control the player needs must be reachable --- */
/* A control is reachable if ANY element names it. The two triggers are
   buttons the script pulses rather than knobs that hold a value, so they
   carry mod-port-symbol without a mod-role: that attribute is inert to
   mod-ui and says here which port the button drives. */
const places = new Set([...doc.querySelectorAll('[mod-port-symbol]')]
    .map(el => el.getAttribute('mod-port-symbol')));
const manquants = ports.filter(p => p.control && p.input && !places.has(p.symbol))
                       .map(p => p.symbol);
say('every control input is reachable from the interface', manquants.length === 0,
    manquants.join(' '));

for (const p of data.effect.ports.audio.input) {
    say('jack for audio input ' + p.symbol,
        doc.querySelector('[mod-role="input-audio-port"][mod-port-symbol="' + p.symbol + '"]') !== null);
}
for (const p of data.effect.ports.audio.output) {
    say('jack for audio output ' + p.symbol,
        doc.querySelector('[mod-role="output-audio-port"][mod-port-symbol="' + p.symbol + '"]') !== null);
}
for (const r of ['drag-handle', 'bypass', 'bypass-light']) {
    say('role ' + r + ' present', doc.querySelector('[mod-role="' + r + '"]') !== null);
}

/* --- the script: does it evaluate, run, and light what it should? --- */
const script = fs.readFileSync('modgui/script-voice.js', 'utf8');
let fn = null;
try {
    fn = eval('(' + script + ')');
    say('script evaluates to a function', typeof fn === 'function');
} catch (e) {
    say('script evaluates to a function', false, e.message);
}

/* jQuery, faked down to what the script uses - but acting on the REAL
   DOM, so "did switching this switch light its section" is a question
   about the document rather than about the fake. */
const store = {};
function jq(sel) {
    const els = [...doc.querySelectorAll(sel)];
    const api = {
        length: els.length,
        css: () => api,
        text: (t) => { if (t !== undefined) els.forEach(e => { e.textContent = t; }); return api; },
        val: (v) => { if (v !== undefined) els.forEach(e => { e.value = v; }); return api; },
        prop: (k, v) => { els.forEach(e => { e[k] = v; }); return api; },
        attr: (k) => (els[0] ? els[0].getAttribute(k) : null),
        toggleClass: (c, on) => { els.forEach(e => e.classList.toggle(c, !!on)); return api; },
        data: (k, v) => (v === undefined ? store[k] : (store[k] = v, api)),
        on: (ev, f) => { els.forEach(e => e.addEventListener(ev, f)); return api; },
        parent: () => {
            const p = els[0] ? els[0].parentElement : null;
            return p ? jqOf(p) : jq('nothing-at-all');
        },
    };
    return api;
}
function jqOf(el) {
    const api = jq('#none');
    api.length = 1;
    api.toggleClass = (c, on) => { el.classList.toggle(c, !!on); return api; };
    api.parent = () => (el.parentElement ? jqOf(el.parentElement) : api);
    api.text = () => api;
    return api;
}

if (typeof fn === 'function') {
    const icon = { find: jq, data: (k, v) => (v === undefined ? store['icon' + k]
                                                              : (store['icon' + k] = v, icon)) };
    let ecrits = [];
    const funcs = { set_port_value: (s, v) => ecrits.push([s, v]) };
    try {
        fn({ type: 'start', icon: icon, ports: [
            { symbol: 'program', value: 0 }, { symbol: 'gr', value: -6 },
            { symbol: 'level', value: 0.5 }, { symbol: 'fx_state', value: 1 },
            { symbol: 'gate_on', value: 1 }, { symbol: 'delay_on', value: 0 },
        ] }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'level', value: NaN }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'gr', value: -999 }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'inconnu', value: 3 }, funcs);
        fn({ type: 'start', icon: icon }, funcs);
        say('script survives start, change, NaN and out of range', true);
    } catch (e) {
        say('script survives start, change, NaN and out of range', false, e.message);
    }

    const sw = doc.querySelector('[mod-port-symbol="gate_on"]');
    say('a switch turned on lights up', sw !== null && sw.classList.contains('on'));
    /* the switch sits in the section HEAD, and it is the section itself
       that lights up - so climb two, rather than asking closest() for a
       class prefix that both of them share */
    const section = sw ? sw.parentElement.parentElement : null;
    say('and lights the section it belongs to',
        section !== null && section.classList.contains('actif'));

    /* the buttons that write ports */
    try {
        ecrits = [];
        doc.querySelector('.voice-next').dispatchEvent(new dom.window.Event('click'));
        /* It writes the program AND the sound: an LV2 plugin may not
           move its own knobs, so if the web UI does not write them,
           picking a sound leaves every control showing the one before. */
        const rangee = /var PROGRAMMES = \[\s*\n\s*null,[^\n]*\n\s*\[([^\]]*)\]/
                       .exec(script);
        const attendu = rangee ? rangee[1].split(',').map(Number) : [];
        const symboles = /var SYMBOLES = \[([^\]]*)\]/.exec(script)[1]
                         .split(',').map(t => t.trim().replace(/"/g, ''));
        const ecritsApres = ecrits.slice(1);
        say('the next button walks the program list',
            ecrits.length === 1 + symboles.length
            && ecrits[0][0] === 'program' && ecrits[0][1] === 1,
            JSON.stringify(ecrits[0]) + ' + ' + ecritsApres.length + ' controls');
        say('and moves every knob the program owns',
            attendu.length === symboles.length
            && ecritsApres.every((e, i) => e[0] === symboles[i]
                                        && Math.abs(e[1] - attendu[i]) < 1e-6),
            JSON.stringify(ecritsApres.slice(0, 3)));
        ecrits = [];
        doc.querySelector('.voice-tap').dispatchEvent(new dom.window.Event('click'));
        say('the tap button pulses the tap port',
            ecrits.length === 1 && ecrits[0][0] === 'tap' && ecrits[0][1] === 1);
        ecrits = [];
        doc.querySelector('.voice-save').dispatchEvent(new dom.window.Event('click'));
        say('SAVE pulses its port whatever program is selected',
            ecrits.length >= 1 && ecrits[0][0] === 'save' && ecrits[0][1] === 1,
            JSON.stringify(ecrits));
        /* and then goes to the slot it just wrote, so the save is
           something you can see rather than something you hope for */
        const premierUser = parseInt(/var PREMIER_USER = (\d+)/.exec(script)[1], 10);
        const versSlot = ecrits.filter(e => e[0] === 'program');
        say('and then selects the slot it wrote to',
            versSlot.length === 1 && versSlot[0][1] === premierUser,
            JSON.stringify(versSlot));
        /* The round trip the whole thing exists for: dial a sound, SAVE
           it, go somewhere else, come back - and find the knobs where
           you left them. The plugin keeps its own copy for the pedal;
           this copy is what moves the screen. */
        ecrits = [];
        fn({ type: 'change', icon: icon, symbol: 'user_slot', value: 2 }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'low_cut', value: 133 }, funcs);
        fn({ type: 'change', icon: icon, symbol: 'reverb_mix', value: 44 }, funcs);
        ecrits = [];
        doc.querySelector('.voice-save').dispatchEvent(new dom.window.Event('click'));
        const slotDeux = parseInt(/var PREMIER_USER = (\d+)/.exec(script)[1], 10) + 1;
        ecrits = [];
        fn({ type: 'change', icon: icon, symbol: 'program', value: 3 }, funcs);
        ecrits = [];
        fn({ type: 'change', icon: icon, symbol: 'program', value: slotDeux }, funcs);
        const rendu = {};
        ecrits.forEach(e => { rendu[e[0]] = e[1]; });
        say('a saved USER slot puts the knobs back where they were',
            rendu['low_cut'] === 133 && rendu['reverb_mix'] === 44,
            JSON.stringify([rendu['low_cut'], rendu['reverb_mix']]));
    } catch (e) {
        say('the buttons that write ports work', false, e.message);
    }
}

/* --- the constants the script cannot work out for itself --- */
const hdr = fs.readFileSync('programs.h', 'utf8');
const nProgram = parseInt(/#define N_PROGRAM\s+(\d+)/.exec(hdr)[1], 10);
const nUser = parseInt(/#define N_USER\s+(\d+)/.exec(hdr)[1], 10);
const premier = parseInt(/var PREMIER_USER = (\d+)/.exec(script)[1], 10);
const dernier = parseInt(/var DERNIER = (\d+)/.exec(script)[1], 10);
say('PREMIER_USER matches N_PROGRAM in programs.h', premier === nProgram,
    premier + ' / ' + nProgram);
say('DERNIER matches the end of the PROGRAM range',
    dernier === nProgram - 1 + nUser, dernier + ' / ' + (nProgram - 1 + nUser));

/* --- monitored outputs: without these the meters never move --- */
const mg = fs.readFileSync('modgui.ttl', 'utf8');
say('monitoredOutputs as [ lv2:symbol ] nodes, not a bare string',
    /monitoredOutputs\s*\[\s*lv2:symbol/.test(mg));
const sorties = ports.filter(p => p.control && !p.input).map(p => p.symbol);
const oubliees = sorties.filter(s => !mg.includes('"' + s + '"'));
say('every control output is monitored', oubliees.length === 0, oubliees.join(' '));

/* --- the stylesheet --- */
const css = fs.readFileSync('modgui/style-voice.css', 'utf8');
say('{{{cns}}} unescaped in the stylesheet', !/\\\{\\\{/.test(css));
const renderedCss = Mustache.render(css, data);
say('stylesheet substitutes completely', !/\{\{|\}\}/.test(renderedCss));
const styled = new Set([...renderedCss.matchAll(/\.(voice-[a-z0-9-]+)_http/g)].map(m => m[1]));
const orphelines = [...styled].filter(c => !doc.querySelector('[class*="' + c + '"]'));
say('no styled class missing from the template', orphelines.length === 0,
    orphelines.join(' '));

/* --- the jacks live outside the panel, so the panel must not clip ---
       This is the check that would have caught the interface shipping with
       no visible sockets at all: overflow: hidden on the pedal deletes
       them, and with them any way of patching the plugin in. */
/* comments stripped first: the rule carries a comment saying not to put
   overflow: hidden there, and a checker that reads its own warning as a
   violation is a checker nobody trusts */
const cssNu = renderedCss.replace(/\/\*[\s\S]*?\*\//g, ' ');
const regleP = /\.mod-pedal-voice[^{]*\{([^}]*)\}/.exec(cssNu);
say('the pedal does not clip what sticks out of it',
    regleP !== null && !/overflow\s*:\s*hidden/.test(regleP[1]));
for (const cote of ['mod-input', 'mod-output']) {
    say('the ' + cote + ' jacks are placed by the stylesheet',
        new RegExp('\\.' + cote + '[^{]*\\{[^}]*top\\s*:').test(cssNu));
}

/* --- bank images --- */
for (const f of ['thumbnail-voice.png', 'screenshot-voice.png',
                 'thumbnail-voice-stereo.png', 'screenshot-voice-stereo.png']) {
    say('image present: ' + f, fs.existsSync('modgui/' + f));
}
const png = fs.readFileSync('modgui/thumbnail-voice.png');
const w = png.readUInt32BE(16), h = png.readUInt32BE(20);
say('thumbnail is wide, not a sliver', w >= 2 * h, w + 'x' + h);

console.log('  (the LOOK is checked by make_screenshot.js, which photographs it)');
console.log(failed ? '\n*** THE WEB UI HAS PROBLEMS ***' : '\nWeb UI: all checks pass.');
process.exit(failed);
