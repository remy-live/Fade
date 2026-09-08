/* Voice — web UI behaviour.
 *
 * mod-ui evaluates this file with  eval('method = ' + code)  so it must
 * hold ONE function expression and nothing else: no trailing semicolon,
 * no declaration around it.
 *
 * It does four things the markup cannot:
 *   - lights the section a switch belongs to, so ON is legible at a glance
 *   - drives the two meters from the monitored outputs
 *   - walks the program list, and pulses SAVE and TAP, which are ports
 *     that only respond to being written
 *   - names the six favourites, and lists them BY NAME. mod-ui's own
 *     PROGRAM menu reads the descriptor, which says USER 1 to USER 6 and
 *     can never say anything else, because the names are typed long after
 *     the plugin was built. So the names go DOWN one character at a time
 *     on web_slot/web_char/web_strobe, and come back UP on name_slot +
 *     n1..n7, one slot per second, which is how this page learns what the
 *     pedal was told to call them.
 *
 * THE RULE THAT SHAPES ALL OF THAT: in the pedalboard, mod-ui COPIES the
 * interface after building it, and every event binding the script made
 * goes with the copy. Only mod-ui's own widgets keep working. So nothing
 * here binds an event to anything. The naming reads plain form controls -
 * a text box keeps what is typed into it, a checkbox and a radio keep what
 * is clicked - and a timer looks at them ten times a second. For the same
 * reason the state lives on `window` rather than on the icon, and the
 * boxes are looked for in the WHOLE PAGE: the settings panel is not a
 * descendant of the icon, and that is where most of them are.
 *
 * What it deliberately does NOT do any more: move the knobs. The plugin
 * asks the host to do that through the kx change-request feature, so the
 * pedal's encoders and this page follow the same one truth. A copy kept
 * here could only disagree with it.
 *
 * PREMIER_USER must match N_PROGRAM in programs.h and DERNIER the end of
 * the PROGRAM range in voice.ttl. check_modgui.js compares all three.
 */
function (event, funcs) {

    var PREMIER_USER = 73;
    var DERNIER = 78;
    var N_SLOT = 6;
    /* seven letters: the width of a footswitch label on the Dwarf, and
       so the width of a name everywhere else */
    var LONG_NOM = 7;
    /* one code per tick down the wire. Slow on purpose: a control port
       is a value the host resends when it feels like it, and a name typed
       faster than the host relays it is a name that arrives in pieces. */
    var PAS_MS = 120;


    function nombre(v) {
        return (typeof v === 'number' && v === v) ? v : 0;
    }

    function borner(v, bas, haut) {
        v = nombre(v);
        return v < bas ? bas : (v > haut ? haut : v);
    }

    /* What is drawn on THIS copy of the interface. mod-ui makes more than
       one, and each keeps its own meters and its own lit sections. */
    function etat(icon) {
        var d = icon.data('voiceState');
        if (!d) {
            d = { program: 0, ports: {} };
            icon.data('voiceState', d);
        }
        return d;
    }

    /* And what belongs to the PLUGIN rather than to a copy of its
       interface: the six names, the queue of characters still to go down,
       the strobe that makes the plugin look at each one. On `window`,
       because the icon this runs against may be a copy that is about to
       be thrown away, and a name half sent from a discarded copy is a
       name that never arrives. */
    function fav() {
        var d = window.__voiceFav;
        if (!d) {
            d = { noms: {}, vu: {}, file: [], strobe: 0, coche: 0,
                  tic: 0, garde: {}, program: 0,
                  echo: 0, echoVu: -1, lettres: [],
                  horloge: null, envoi: function () {} };
            window.__voiceFav = d;
        }
        return d;
    }

    /* The names live in the plugin, on the disc beside the bundle. What
       is kept here is only a copy to draw with before the first echo has
       come round - six seconds is a long time to look at an empty list. */
    function cleNom(slot) {
        return 'voice.userName.' + slot;
    }

    function lireNom(slot) {
        try {
            return window.localStorage.getItem(cleNom(slot)) || '';
        } catch (e) {
            return '';
        }
    }

    function ecrireNom(slot, nom) {
        try {
            window.localStorage.setItem(cleNom(slot), nom);
        } catch (e) { /* private mode: the plugin still has the name */ }
    }

    /* Seven upper-case letters, spaces trimmed: the same shape the plugin
       stores, so what is typed here and what comes back are comparable. */
    function propre(nom) {
        var t = String(nom === undefined || nom === null ? '' : nom)
                .toUpperCase().replace(/[^ -~]/g, ' ');
        if (t.length > LONG_NOM) { t = t.substring(0, LONG_NOM); }
        return t.replace(/\s+$/, '');
    }

    function nomDe(d, slot) {
        var n = d.noms[slot];
        if (n === undefined) { n = lireNom(slot); }
        return propre(n);
    }

    /* ---------------- a name, one character at a time ----------------
       The slot goes down WITH each character, on web_slot, so nothing
       depends on an order that arrived earlier: a code lost on the way
       is one lost letter and not a name landing on the wrong favourite.
       1 clears, 32..126 append. The word is sent whole every time - clear,
       then spell - because comparing two strings is the only thing this
       page can know for certain about what was typed. */
    function envoyerNom(slot, nom) {
        var d = fav();
        var t = propre(nom);
        var i;
        d.file.push({ slot: slot, code: 1 });
        for (i = 0; i < t.length; i++) {
            d.file.push({ slot: slot, code: t.charCodeAt(i) });
        }
        d.noms[slot] = t;
        ecrireNom(slot, t);
        /* And do not believe the echo about THIS slot for a moment: it
           is a snapshot the host relays when it feels like it, so the
           name on its way UP may have been read before the one going
           DOWN arrived, and the box would rewrite itself with what it
           said a second ago. Two seconds is longer than that round trip.
           Per slot, so naming one does not deafen the page to the rest. */
        d.garde[slot] = d.tic + Math.ceil(2000 / PAS_MS);
    }

    function pomper() {
        var d = fav();
        if (!d.file.length) { return; }
        var o = d.file.shift();
        d.strobe = (d.strobe % 250) + 1;
        /* the slot and the character first, then the strobe: the plugin
           reads what it finds when the strobe has moved */
        d.envoi('web_slot', o.slot);
        d.envoi('web_char', o.code);
        d.envoi('web_strobe', d.strobe);
    }

    /* ---------------- and the six coming back ----------------
       name_slot says whose name is on n1..n7, and it moves once a second.
       Only ports that CHANGE are delivered, so the letters of a name that
       happens to match the last one never arrive: read the lot on a tick
       instead, once the slot has stayed still for one. */
    function ecouterEcho() {
        var d = fav();
        var slot = Math.round(borner(d.echo, 0, N_SLOT));
        if (slot < 1) { return; }
        if (slot !== d.echoVu) { d.echoVu = slot; return; }
        var t = '', k;
        for (k = 0; k < LONG_NOM; k++) {
            var c = Math.round(nombre(d.lettres[k]));
            t += (c >= 32 && c <= 126) ? String.fromCharCode(c) : ' ';
        }
        t = propre(t);
        /* a name still on its way down, or only just arrived, would be
           overwritten by the old one coming back up */
        if (d.file.length) { return; }
        if ((d.garde[slot] || 0) > d.tic) { return; }
        if (d.noms[slot] !== t) {
            d.noms[slot] = t;
            ecrireNom(slot, t);
        }
    }

    /* ---------------- the clock ----------------
       Everything the naming does happens here, ten times a second, on
       plain form controls read across the WHOLE page. No binding survives
       mod-ui copying the interface; a box that has been typed into does.

       jQuery is what mod-ui gives the script; there is no document here
       other than the one it is running in. */
    function tourJq(sel) {
        return (typeof jQuery === 'function') ? jQuery(sel) : null;
    }

    function battre() {
        var d = fav();
        d.tic++;

        /* 1. what has been typed, box by box, wherever it lives */
        var boites = tourJq('.voice-fav-name');
        if (boites) {
            boites.each(function () {
                var champ = jQuery(this);
                var slot = Math.round(borner(Number(champ.attr('data-slot')),
                                             1, N_SLOT));
                var texte = propre(champ.val() || '');
                if (d.vu[slot] === undefined) { d.vu[slot] = texte; return; }
                if (texte === d.vu[slot]) { return; }
                d.vu[slot] = texte;
                envoyerNom(slot, texte);
            });
        }

        /* 2. one code down the wire */
        pomper();

        /* 3. and one name up it */
        ecouterEcho();

        /* 4. a favourite ticked in the list is a favourite to go to */
        var choisi = 0;
        var ronds = tourJq('.voice-fav-pick');
        if (ronds) {
            ronds.each(function () {
                if (this.checked) {
                    choisi = Math.round(borner(
                        Number(jQuery(this).attr('data-slot')), 1, N_SLOT));
                }
            });
        }
        if (choisi && choisi !== d.coche) {
            d.coche = choisi;
            d.envoi('program', PREMIER_USER + choisi - 1);
        }

        /* 5. and the names on show */
        peindreFavoris();
    }

    /* The list, its labels and the button that opens it all say the names.
       Writing into a box is the one thing done carefully: the clock reads
       those boxes back, so what is written has to be recorded as seen or
       the page would send it round again for ever. */
    function peindreFavoris() {
        var d = fav();
        var p = Math.round(borner(d.program, 0, DERNIER));
        var courant = (p >= PREMIER_USER) ? (p - PREMIER_USER + 1) : 0;
        for (var slot = 1; slot <= N_SLOT; slot++) {
            var nom = nomDe(d, slot);
            var etiq = tourJq('.voice-fav-label[data-slot="' + slot + '"]');
            if (etiq) {
                etiq.text(nom || ('USER ' + slot))
                    .toggleClass('vide', !nom)
                    .toggleClass('actif', slot === courant);
            }
            /* The boxes are only ever filled from a name the PLUGIN has
               said out loud. Filling one from a guess - from what this
               browser remembers, or from nothing at all - would wipe a
               name typed a tenth of a second ago and not yet echoed. */
            if (d.noms[slot] === undefined) { continue; }
            var boites = tourJq('.voice-fav-name[data-slot="' + slot + '"]');
            if (boites) {
                boites.each(function () {
                    var champ = jQuery(this);
                    if (this === document.activeElement) { return; }
                    /* compared as typed, not cleaned up: a box left
                       holding "chorus" is put into the capitals the
                       plugin stores as soon as the cursor leaves it */
                    if ((champ.val() || '') === nom) { return; }
                    champ.val(nom);
                    d.vu[slot] = nom;
                });
            }
        }
        var bouton = tourJq('.voice-fav-btn');
        if (bouton) {
            bouton.text(courant ? (nomDe(d, courant) || ('USER ' + courant))
                                : 'FAVORIS ▾');
        }

        /* And the PROGRAM list itself. mod-ui builds it from the
           descriptor, which was written before anything was named and can
           only say USER 1 to USER 6 - so the six entries at the end of it
           are given the names here, every tick, which also puts them back
           whenever mod-ui rebuilds the list. */
        for (var u = 1; u <= N_SLOT; u++) {
            var nomU = nomDe(d, u);
            var entree = tourJq('.voice-prog-user[data-slot="' + u + '"]');
            if (entree && entree.length) {
                var voulu = nomU || ('USER ' + u);
                if (entree.text() !== voulu) { entree.text(voulu); }
            }
        }
        /* the line that shows which one is chosen is written by mod-ui
           when it is clicked, so it needs the same treatment */
        if (courant) {
            var choisi = tourJq('.mod-enumerated-selected');
            if (choisi && choisi.length) {
                var dit = nomDe(d, courant) || ('USER ' + courant);
                if (/^USER \d$/.test(choisi.text() || '')
                    || choisi.text() === 'User ' + courant) {
                    choisi.text(dit);
                }
            }
        }
    }

    /* mod-ui writes USER 1 to USER 6 into the readout, because that is
       what the descriptor says. Say the name instead, wherever there is
       one, and light the row of the list the sound came from. */
    function majProgramme(icon, valeur) {
        var d = fav();
        var p = Math.round(borner(valeur, 0, DERNIER));
        d.program = p;
        if (p >= PREMIER_USER) {
            var slot = p - PREMIER_USER + 1;
            icon.find('.voice-prog-value').text(nomDe(d, slot) || ('USER ' + slot));
        }
        peindreFavoris();
    }

    function majMetres(icon, symbol, valeur) {
        if (symbol === 'gr') {
            /* 0 dB is nothing, -24 dB is everything the meter shows */
            icon.find('.voice-gr-fill').css('width',
                (borner(-nombre(valeur) / 24, 0, 1) * 100).toFixed(1) + '%');
        } else if (symbol === 'level') {
            icon.find('.voice-level-fill').css('width',
                (borner(valeur, 0, 1) * 100).toFixed(1) + '%');
        } else if (symbol === 'fx_state') {
            var actif = nombre(valeur) > 0.5;
            icon.find('.voice-state').text(actif ? 'FX ON' : 'FX OFF')
                                     .toggleClass('actif', actif);
        } else if (symbol === 'notches') {
            var n = Math.round(nombre(valeur));
            for (var i = 0; i < 4; i++) {
                icon.find('.voice-notch-' + i).toggleClass('actif', i < n);
            }
        } else if (symbol === 'time_out') {
            icon.find('.voice-time-value').text(Math.round(nombre(valeur)) + ' ms');
        }
    }

    /* A switch lights the whole section it sits in, which is the thing the
       default interface could not do. */
    var SECTIONS = ['gate_on', 'comp_on', 'de_ess_on', 'eq_on', 'drive_on',
                    'pitch_on', 'doubler_on', 'mod_on', 'feedback_on',
                    'delay_on', 'reverb_on'];

    function majSection(icon, symbol, valeur) {
        if (SECTIONS.indexOf(symbol) < 0) { return; }
        var sw = icon.find('[mod-port-symbol="' + symbol + '"]');
        if (!sw.length) { return; }
        var on = nombre(valeur) > 0.5;
        sw.toggleClass('on', on).toggleClass('off', !on);
        if (sw.parent && sw.parent().parent) {
            sw.parent().parent().toggleClass('actif', on);
        }
    }

    function pulse(icon, symbol, classe, cible, apres) {
        funcs.set_port_value(symbol, 1);
        cible.toggleClass('flash', true);
        window.setTimeout(function () {
            funcs.set_port_value(symbol, 0);
            cible.toggleClass('flash', false);
            if (apres) { apres(); }
        }, 120);
    }

    function brancher(icon) {
        if (icon.data('voiceBound')) { return; }
        icon.data('voiceBound', true);

        icon.find('.voice-prev').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var p = borner(Math.round(fav().program) - 1, 0, DERNIER);
            funcs.set_port_value('program', p);
            majProgramme(icon, p);
        });

        icon.find('.voice-next').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var p = borner(Math.round(fav().program) + 1, 0, DERNIER);
            funcs.set_port_value('program', p);
            majProgramme(icon, p);
        });

        icon.find('.voice-save').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            /* SAVE always has a destination: USER SLOT says which. */
            var d = etat(icon);
            var slot = Math.round(borner(d.ports['user_slot'] || 1, 1, N_SLOT));
            /* Keep the same values here for the knobs, then GO to the
               slot - a save you cannot see is a save you do not believe
               in. The jump waits for the pulse to finish: the plugin
               takes SAVE before the program list on purpose, but two
               port writes in one audio block are still two things
               happening at once, and this one costs nothing to order. */
            pulse(icon, 'save', 'flash', icon.find('.voice-save'), function () {
                var p = PREMIER_USER + slot - 1;
                funcs.set_port_value('program', p);
                majProgramme(icon, p);
            });
        });

        icon.find('.voice-cycle').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            pulse(icon, 'next_user', 'flash', icon.find('.voice-cycle'));
        });

        icon.find('.voice-ab').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            pulse(icon, 'ab', 'flash', icon.find('.voice-ab'));
        });

        icon.find('.voice-tap').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            pulse(icon, 'tap', 'flash', icon.find('.voice-tap'));
        });

        /* ---------------- the favourites ----------------
           Nothing is bound here. The panel is opened by a checkbox and
           CSS, a favourite is chosen with a radio, and a name is typed
           into a plain box: three form controls that keep their own state
           whether or not this script ever ran. The clock below reads
           them. See the note at the top for why.

           It is started here rather than at the top so it exists once,
           and re-made on every start so it always writes through the
           funcs of the interface that is actually on screen. */
        var d0 = fav();
        d0.envoi = function (symbole, valeur) {
            funcs.set_port_value(symbole, valeur);
        };
        /* What the boxes hold BEFORE anyone has typed. Taken here rather
           than on the first tick of the clock: a name typed in the tenth
           of a second between the two would otherwise be mistaken for
           what was already there, and never sent. */
        var depart = tourJq('.voice-fav-name');
        if (depart) {
            depart.each(function () {
                var champ = jQuery(this);
                var slot = Math.round(borner(Number(champ.attr('data-slot')),
                                             1, N_SLOT));
                if (d0.vu[slot] === undefined) {
                    d0.vu[slot] = propre(champ.val() || '');
                }
            });
        }
        if (d0.horloge) { window.clearInterval(d0.horloge); }
        d0.horloge = window.setInterval(battre, PAS_MS);
    }

    function changement(icon, symbol, valeur) {
        var d = etat(icon);
        var g = fav();
        d.ports[symbol] = valeur;
        /* the six names coming back: whose, and its letters. They belong
           to the plugin, not to this copy of its interface. */
        if (symbol === 'name_slot') {
            g.echo = nombre(valeur);
            return;
        }
        if (symbol.length === 2 && symbol.charAt(0) === 'n'
            && symbol >= 'n1' && symbol <= 'n7') {
            g.lettres[symbol.charCodeAt(1) - 49] = nombre(valeur);
            return;
        }
        /* Pick the strobe up where the pedalboard left it: starting again
           from zero after a reload could land on the value the port
           already holds, and a strobe that does not change types nothing. */
        if (symbol === 'web_strobe') {
            g.strobe = Math.round(borner(valeur, 0, 250));
            return;
        }
        if (symbol === 'web_char' || symbol === 'web_slot') { return; }
        if (symbol === 'program_now') {
            /* The plugin cannot write its own PROGRAM port, so A/B - and
               anything else that moves the program in force - is only
               visible here. Follow it: write the port back, which puts
               the list, the name and every knob where the sound is. */
            var p = Math.round(borner(valeur, 0, DERNIER));
            if (d.demarre && p !== Math.round(g.program)) {
                funcs.set_port_value('program', p);
                majProgramme(icon, p);
            }
            return;
        }
        if (symbol === 'program') {
            majProgramme(icon, valeur);
        }
        majMetres(icon, symbol, valeur);
        majSection(icon, symbol, valeur);
    }

    if (event.type === 'start') {
        brancher(event.icon);
        var ports = event.ports || [];
        for (var i = 0; i < ports.length; i++) {
            changement(event.icon, ports[i].symbol, ports[i].value);
        }
        majProgramme(event.icon, fav().program);
        etat(event.icon).demarre = true;
    } else if (event.type === 'change') {
        changement(event.icon, event.symbol, event.value);
    }
}
