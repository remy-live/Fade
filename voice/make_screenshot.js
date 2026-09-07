/* Photographs the web UI, through the same engine that renders it in the
 * browser, and writes the result into modgui/ as the plugin's screenshot.
 *
 * A screenshot drawn by hand is a drawing of what the author hoped the
 * interface looked like. This one cannot be, which is the point: it also
 * fails loudly if the layout overflows the pedal.
 *
 *   node make_screenshot.js            # mono
 *   node make_screenshot.js stereo
 *
 * Needs mustache and playwright-core in /tmp/node_modules and a Chromium
 * at CHROMIUM (default: the one shipped with playwright). It is NOT part
 * of build.sh: the images it writes are tracked in git, because a build
 * should not need a browser.
 */
const fs = require('fs');
const Mustache = require('/tmp/node_modules/mustache');
const { chromium } = require(process.env.PW || '/tmp/node_modules/playwright-core');

const stereo = process.argv[2] === 'stereo';
const which = stereo ? 'voice_stereo.ttl' : 'voice.ttl';
const ttl = fs.readFileSync(which, 'utf8');
const ports = [];
for (const b of ttl.split('] , [')) {
    const s = /lv2:symbol\s+"([^"]+)"/.exec(b);
    if (!s) continue;
    const n = /lv2:name\s+"([^"]+)"/.exec(b);
    const d = /lv2:default\s+(-?[\d.]+)/.exec(b);
    const u = /units:unit\s+units:(\w+)/.exec(b);
    ports.push({ symbol: s[1], name: n ? n[1] : s[1],
        audio: b.includes('lv2:AudioPort'), input: b.includes('lv2:InputPort'),
        control: b.includes('lv2:ControlPort'),
        def: d ? parseFloat(d[1]) : null, unit: u ? u[1] : null,
        toggled: b.includes('lv2:toggled') });
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
const html = Mustache.render(fs.readFileSync('modgui/icon-voice.html', 'utf8'), data);
const css  = Mustache.render(fs.readFileSync('modgui/style-voice.css', 'utf8'), data);

/* mod-ui's own classes that the pedal sits inside, faked just enough */
const page = `<!doctype html><meta charset="utf-8"><style>
body { margin:0; padding:24px; background:#20242b; }
.mod-pedal { position: relative; }
.mod-input, .mod-output { position:absolute; width:24px; height:24px; }
.mod-input { left:-12px; } .mod-output { right:-12px; }
.mod-pedal-input-image, .mod-pedal-output-image {
  width:22px; height:22px; border-radius:50%;
  background:#111; border:2px solid #6a7788; }
.mod-footswitch { width:46px; height:46px; border-radius:50%;
  background:radial-gradient(circle at 50% 35%, #6d7b8d, #39424f); border:1px solid #10151b; }
${css}
</style>${html}
<script>
/* what mod-ui's switchWidget does: .on / .off on toggled ports */
const on = ${JSON.stringify(ports
    .filter(p => p.control && p.input && p.toggled && p.def >= 0.5)
    .map(p => p.symbol))};
for (const s of on) {
  const el = document.querySelector('[mod-port-symbol="'+s+'"]');
  if (el) { el.classList.add('on'); el.closest('.voice-section${'_http___remy_live_github_io_lv2_voice'}')?.classList.add('actif'); }
}
/* Every readout, from the descriptor's own defaults rather than a list
   kept by hand - a photograph with three empty boxes in it says the
   plugin has three empty boxes. */
const defauts = ${JSON.stringify(Object.fromEntries(ports
    .filter(p => p.control && p.input && p.def !== null)
    .map(p => {
        const u = { db: ' dB', hz: ' Hz', pc: ' %', ms: ' ms', s: ' s' }[p.unit] || '';
        const dec = (p.def !== Math.round(p.def)) ? 2 : 0;
        return [p.symbol, p.def.toFixed(dec) + u];
    })))};
document.querySelectorAll('[mod-role="input-control-value"]').forEach(function (el) {
  const s = el.getAttribute('mod-port-symbol');
  const v = (s === 'program') ? 'MANUAL' : defauts[s];
  if (v !== undefined) el.textContent = v;
});
document.querySelector('.voice-gr-fill').style.width = '38%';
document.querySelector('.voice-level-fill').style.width = '62%';
document.querySelector('.voice-state').textContent = 'FX ON';
document.querySelector('.voice-state').classList.add('actif');
document.querySelector('.voice-light${'_http___remy_live_github_io_lv2_voice'}').classList.add('on');
</script>`;


(async () => {
    const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || undefined });
    const p = await browser.newPage({ viewport: { width: 780, height: 700 } });
    await p.setContent(page);
    const debord = await p.evaluate(() => {
        const pedal = document.querySelector('.mod-pedal');
        const box = pedal.getBoundingClientRect();
        let bas = 0, droite = 0;
        for (const el of pedal.querySelectorAll('*')) {
            /* the jacks stick out on purpose: that is where a cable goes */
            if (el.closest('.mod-pedal-input, .mod-pedal-output')) continue;
            const r = el.getBoundingClientRect();
            if (r.height === 0) continue;
            bas = Math.max(bas, r.bottom - box.bottom);
            droite = Math.max(droite, r.right - box.right);
        }
        let fond = 0;
        for (const el of pedal.children) {
            if (el.className && String(el.className).indexOf('mod-pedal-') === 0) continue;
            const r = el.getBoundingClientRect();
            if (r.height > 0) fond = Math.max(fond, r.bottom - box.top);
        }
        return { bas: Math.round(bas), droite: Math.round(droite),
                 w: Math.round(box.width), h: Math.round(box.height),
                 contenu: Math.round(fond) };   /* CONTENT_HEIGHT */
    });
    if (debord.bas > 0 || debord.droite > 0) {
        console.error('the layout overflows the pedal by ' + debord.bas +
                      'px at the bottom and ' + debord.droite + 'px on the right');
        await browser.close();
        process.exit(1);
    }
    const cible = 'modgui/screenshot-voice' + (stereo ? '-stereo' : '') + '.png';
    /* a margin either side, so the jacks - which live OUTSIDE the panel -
       are in the picture: the interface is also where somebody looks to
       find out where the cable goes */
    const box = await p.locator('.mod-pedal').boundingBox();
    await p.screenshot({ path: cible, clip: {
        x: Math.max(0, box.x - 34), y: box.y,
        width: box.width + 68, height: box.height } });
    await browser.close();
    console.log(cible + ': ' + debord.w + 'x' + debord.h
                + ' (content ends at ' + debord.contenu + ')');
})();
