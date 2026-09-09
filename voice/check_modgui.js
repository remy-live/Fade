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

/* The settings panel is rendered into the SAME document as the icon,
   because that is where it lives in mod-ui - and because the script looks
   for its boxes across the whole page rather than inside the icon. */
const reglages = fs.readFileSync('modgui/settings-voice.html', 'utf8');
let rendu2 = '';
try {
    rendu2 = Mustache.render(reglages, data);
    say('the settings template renders without error', true);
} catch (e) {
    say('the settings template renders without error', false, e.message);
}
say('no mustache braces left in the settings panel', !/\{\{|\}\}/.test(rendu2));

/* A URL, so the document has a real origin: without one jsdom refuses
   localStorage, and the USER slots the script keeps there would look
   broken here while working in a browser. */
const dom = new JSDOM('<body>' + rendered + rendu2 + '</body>',
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
const pedale = doc.querySelector('.mod-pedal');
const places = new Set([...pedale.querySelectorAll('[mod-port-symbol]')]
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
        attr: (k, v) => {
            if (v === undefined) { return els[0] ? els[0].getAttribute(k) : null; }
            els.forEach(e => e.setAttribute(k, v));
            return api;
        },
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

/* The script looks for its boxes in the WHOLE page with jQuery, not
   inside the icon: the settings panel is not a descendant of the icon.
   So give it one, backed by the real document. */
function jqAll(sel) {
    const els = (typeof sel === 'string') ? [...doc.querySelectorAll(sel)] : [sel];
    const api = {
        length: els.length,
        each: (f) => { els.forEach((e, i) => f.call(e, i, e)); return api; },
        attr: (k, v) => {
            if (v === undefined) { return els[0] ? els[0].getAttribute(k) : null; }
            els.forEach(e => e.setAttribute(k, v)); return api;
        },
        val: (v) => {
            if (v === undefined) { return els[0] ? els[0].value : undefined; }
            els.forEach(e => { e.value = v; }); return api;
        },
        text: (t) => {
            if (t === undefined) { return els[0] ? els[0].textContent : ''; }
            els.forEach(e => { e.textContent = t; }); return api;
        },
        toggleClass: (c, on) => { els.forEach(e => e.classList.toggle(c, !!on)); return api; },
        css: () => api,
    };
    return api;
}
global.jQuery = jqAll;
dom.window.jQuery = jqAll;

const differe = [];
if (typeof fn === 'function') {
    const icon = { find: jq, data: (k, v) => (v === undefined ? store['icon' + k]
                                                              : (store['icon' + k] = v, icon)) };
    let ecrits = [];
    /* what has to be looked at once the button pulses have finished */
    const apres = differe;
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
        /* It writes the program and NOTHING else. Moving the knobs to
           match is the plugin's job now: it asks the host through the kx
           change-request feature, so the pedal's encoders and this page
           follow one truth rather than two copies that can disagree. */
        say('the next button walks the program list',
            ecrits.length === 1 && ecrits[0][0] === 'program'
            && ecrits[0][1] === 1, JSON.stringify(ecrits));
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
           something you can see rather than something you hope for -
           AFTER the pulse, so the plugin never sees the save and the
           program change in one audio block */
        const premierUser = parseInt(/var PREMIER_USER = (\d+)/.exec(script)[1], 10);
        say('and does not select it in the same breath',
            ecrits.filter(e => e[0] === 'program').length === 0,
            JSON.stringify(ecrits));
        apres.push(() => {
            /* PROGRAM NOW was pushed through further down and wrote the
               port too, so look for the one this SAVE asked for rather
               than assuming it is the only one */
            const versSlot = ecrits.filter(e => e[0] === 'program');
            say('and then selects the slot it wrote to',
                versSlot.some(e => e[1] === premierUser),
                JSON.stringify(versSlot));
        });
        /* PROGRAM NOW is how A/B and the cycle switch reach the page:
           the plugin cannot write its own PROGRAM port, so it publishes
           what is in force and the page follows it. Moving the knobs to
           match is the plugin's job now, through the host. */
        ecrits = [];
        const premierUser2 = parseInt(/var PREMIER_USER = (\d+)/.exec(script)[1], 10);
        fn({ type: 'change', icon: icon, symbol: 'program_now',
             value: premierUser2 + 2 }, funcs);
        say('PROGRAM NOW moves the list to where the pedal went',
            ecrits.length === 1 && ecrits[0][0] === 'program'
            && ecrits[0][1] === premierUser2 + 2, JSON.stringify(ecrits));

        /* --- the favourites: the list mod-ui cannot draw ---
           Nothing here is clicked with a dispatched event, on purpose:
           in the pedalboard mod-ui copies the interface after building
           it and every binding the script made goes with the copy. What
           is tested is what actually survives that - form controls the
           browser keeps for us, read back by the script's own clock. */

        /* the panel opens with a checkbox and CSS, no script at all */
        const coche = doc.querySelector('input.voice-fav-open' + data.cns);
        const etiqOuvre = doc.querySelector('label.voice-fav-btn');
        say('the FAVORIS button is a label, not a handler',
            coche !== null && etiqOuvre !== null
            && etiqOuvre.getAttribute('for') === coche.id);
        say('and its checkbox stands beside the panel, so CSS can open it',
            coche !== null && coche.nextElementSibling !== null
            && coche.nextElementSibling.className.indexOf('voice-fav-panel') >= 0);
        /* the rule itself is checked with the stylesheet, further down */

        /* a favourite is chosen with a radio, read back by the clock */
        ecrits = [];
        doc.querySelector('.voice-fav-pick[data-slot="4"]').checked = true;
        apres.push(() => {
            const vers = ecrits.filter(e => e[0] === 'program');
            say('ticking the fourth favourite goes to the fourth USER slot',
                vers.some(e => e[1] === premierUser2 + 3), JSON.stringify(vers));
        });

        /* A name typed here goes down web_slot / web_char, one character
           per change of web_strobe - clear first, then the letters. */
        const boite = doc.querySelector('.voice-fav-name[data-slot="2"]');
        boite.value = 'chorus';
        say('nothing goes down in the same breath as the typing',
            ecrits.filter(e => e[0] === 'web_char').length === 0,
            JSON.stringify(ecrits));

        /* The echo is a snapshot the host relays when it feels like it,
           so straight after typing it can still be carrying the name
           from before - which is how a box rewrites itself under the
           player. The slot just named ignores it for two seconds. */
        const echo = (slot, mot) => {
            fn({ type: 'change', icon: icon, symbol: 'name_slot',
                 value: slot }, funcs);
            const sept = (mot + '       ').substring(0, 7);
            for (let k = 0; k < 7; k++) {
                fn({ type: 'change', icon: icon, symbol: 'n' + (k + 1),
                     value: sept.charCodeAt(k) }, funcs);
            }
        };
        echo(2, 'VERSE');

        /* what the plugin sees, read out in words */
        fn({ type: 'change', icon: icon, symbol: 'diag', value: 815306 }, funcs);
        const lu = doc.querySelector('.voice-diag').textContent;
        say('the diagnostic reads out in words, not in digits',
            lu.indexOf('connecte') >= 0 && lu.indexOf('5 favoris') >= 0
            && lu.indexOf('curseur sur 3') >= 0 && lu.indexOf('annonce 6') >= 0, lu);
        say('and how close together two presses have ever landed',
            lu.indexOf('0.08 s') >= 0, lu);
        fn({ type: 'change', icon: icon, symbol: 'diag', value: 9900099 }, funcs);
        const lu2 = doc.querySelector('.voice-diag').textContent;
        say('and says so plainly when the switch has never moved',
            lu2.indexOf('JAMAIS VU BOUGER') >= 0
            && lu2.indexOf('non adresse') >= 0
            && lu2.indexOf('jamais deux appuis') >= 0, lu2);

        /* one button per favourite, and each writes its own port */
        for (const n of [1, 3, 6]) {
            ecrits = [];
            doc.querySelector('.voice-go-btn[data-slot="' + n + '"]')
               .dispatchEvent(new dom.window.Event('click'));
            say('the button for favourite ' + n + ' pulses its own port',
                ecrits.length === 1 && ecrits[0][0] === 'fav_' + n
                && ecrits[0][1] === 1, JSON.stringify(ecrits));
        }
        say('and marks it as something to look at',
            doc.querySelector('.voice-diag').classList.contains('alerte'));

        /* Looked at once the codes of CHORUS have all gone down - until
           then the queue itself holds the echo off - but while the two
           seconds are still running. */
        setTimeout(() => {
            say('a stale echo does not undo what was just typed',
                doc.querySelector('.voice-fav-label[data-slot="2"]')
                   .textContent === 'CHORUS',
                doc.querySelector('.voice-fav-label[data-slot="2"]').textContent);
            /* and then the six coming back, one slot per second */
            echo(5, 'GROWL');
        }, 1250);

        apres.push(() => {
            const slots   = ecrits.filter(e => e[0] === 'web_slot').map(e => e[1]);
            const lettres = ecrits.filter(e => e[0] === 'web_char').map(e => e[1]);
            const tops    = ecrits.filter(e => e[0] === 'web_strobe').map(e => e[1]);
            say('a name typed into a box goes down on its own, unprompted',
                JSON.stringify(lettres) ===
                JSON.stringify([1, 67, 72, 79, 82, 85, 83]),
                JSON.stringify(lettres));
            say('with the slot beside every character of it',
                slots.length === lettres.length && slots.every(v => v === 2),
                JSON.stringify(slots));
            say('and a strobe that changes for every one of them',
                tops.length === lettres.length
                && tops.every((v, i) => i === 0 || v !== tops[i - 1]),
                JSON.stringify(tops));
            say('the box is put in the shape the plugin stores',
                boite.value === 'CHORUS', boite.value);
            say('and the list says the name rather than USER 2',
                doc.querySelector('.voice-fav-label[data-slot="2"]')
                   .textContent === 'CHORUS',
                doc.querySelector('.voice-fav-label[data-slot="2"]').textContent);
            say('a name coming back up lands in its row',
                doc.querySelector('.voice-fav-label[data-slot="5"]')
                   .textContent === 'GROWL',
                doc.querySelector('.voice-fav-label[data-slot="5"]').textContent);
            say('and in its box, ready to be edited',
                doc.querySelector('.voice-fav-name[data-slot="5"]')
                   .value === 'GROWL');
            /* The PROGRAM list is built from the descriptor, which was
               written before anything was named: it can only say USER 1
               to USER 6. The script gives the six entries their names. */
            say('the PROGRAM list says the names, not USER 2',
                doc.querySelector('.voice-prog-user[data-slot="2"]')
                   .textContent === 'CHORUS',
                doc.querySelector('.voice-prog-user[data-slot="2"]').textContent);
            say('and USER n where there is no name yet',
                doc.querySelector('.voice-prog-user[data-slot="3"]')
                   .textContent === 'USER 3',
                doc.querySelector('.voice-prog-user[data-slot="3"]').textContent);
        });

    } catch (e) {
        say('the buttons that write ports work', false,
            e.message + ' | ' + (e.stack || '').split('\n')[1]);
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
say('the panel is opened by its checkbox, with no script at all',
    /voice-fav-open[^{]*:checked\s*~\s*\.voice-fav-panel/.test(renderedCss));

/* --- the settings panel: mod-ui's default one has nowhere to type --- */
const doc2 = doc.querySelector('.mod-pedal-settings');
say('a .mod-pedal-settings root exists', doc2 !== null);
say('the six name boxes are in the settings panel',
    doc2.querySelectorAll('.voice-fav-name').length === 6);
const boitesSlots = [...doc2.querySelectorAll('.voice-fav-name')]
    .map(e => e.getAttribute('data-slot')).join(',');
say('one per USER slot, in order', boitesSlots === '1,2,3,4,5,6', boitesSlots);
say('and one editor per slot in the whole page, never two',
    doc.querySelectorAll('.voice-fav-name').length === 6,
    String(doc.querySelectorAll('.voice-fav-name').length));
say('the PROGRAM list has an entry per USER slot to be renamed',
    doc2.querySelectorAll('.voice-prog-user').length === 6);
say('the panel is in sections, not a wall of knobs in port order',
    doc2.querySelectorAll('.voice-set-sec' + data.cns).length >= 10);
const placesSet = new Set([...doc2.querySelectorAll('[mod-port-symbol]')]
    .map(el => el.getAttribute('mod-port-symbol')));
const manquants2 = ports.filter(p => p.control && p.input && !placesSet.has(p.symbol))
                        .map(p => p.symbol);
say('every control input is reachable from the settings panel',
    manquants2.length === 0, manquants2.join(' '));
for (const r of ['bypass', 'bypass-light']) {
    say('settings role ' + r + ' present',
        doc2.querySelector('[mod-role="' + r + '"]') !== null);
}

const styled = new Set([...renderedCss.matchAll(/\.(voice-[a-z0-9-]+)_http/g)].map(m => m[1]));
const orphelines = [...styled].filter(
    c => !doc.querySelector('[class*="' + c + '"]')
      && !doc2.querySelector('[class*="' + c + '"]'));
say('no styled class missing from the templates', orphelines.length === 0,
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

/* The buttons pulse for 120 ms and only then move the program, and a
   name is sent one character every 120 ms, so the last few checks cannot
   be made before all of that has happened. */
setTimeout(() => {
    for (const f of differe) { f(); }
    console.log('  (the LOOK is checked by make_screenshot.js, which photographs it)');
    console.log(failed ? '\n*** THE WEB UI HAS PROBLEMS ***'
                       : '\nWeb UI: all checks pass.');
    process.exit(failed);
}, 2300);
