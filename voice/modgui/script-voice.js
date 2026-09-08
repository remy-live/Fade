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
 *     on web_char/web_strobe, and come back UP on name_slot + n1..n7, one
 *     slot per second, which is how this page learns what the pedal was
 *     told to call them.
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

    function etat(icon) {
        var d = icon.data('voiceState');
        if (!d) {
            d = { program: 0, ports: {},
                  /* the six names as this page last heard them, the queue
                     of codes still to go down, and the strobe that makes
                     the plugin look at each one */
                  noms: {}, file: [], strobe: 0, tape: 0,
                  echo: 0, echoVu: -1, lettres: [] };
            icon.data('voiceState', d);
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
       11..16 chooses the slot, 32..126 is a letter, 2 stores what has
       been spelt. Nothing here is sent twice in one tick: the plugin
       counts CHANGES of the strobe, and two changes inside one audio
       block are one change as far as it is concerned. */
    function envoyerNom(icon, slot, nom) {
        var d = etat(icon);
        var t = propre(nom);
        var i;
        d.file.push(10 + slot);
        for (i = 0; i < t.length; i++) { d.file.push(t.charCodeAt(i)); }
        d.file.push(2);
        /* draw it straight away rather than waiting for the echo: the
           plugin has it a fifth of a second from now, the page a further
           six at worst, and a box that seems to forget what was typed
           into it is a box nobody types into twice */
        d.noms[slot] = t;
        ecrireNom(slot, t);
        majFavoris(icon);
    }

    function pomper(icon) {
        var d = etat(icon);
        if (!d.file.length) { return; }
        var c = d.file.shift();
        d.strobe = (d.strobe % 250) + 1;
        /* the character first, then the strobe: the plugin reads the
           character it finds when the strobe has moved */
        funcs.set_port_value('web_char', c);
        funcs.set_port_value('web_strobe', d.strobe);
        icon.find('.voice-fav-name').toggleClass('envoi', d.file.length > 0);
    }

    /* ---------------- and the six coming back ----------------
       name_slot says whose name is on n1..n7, and it moves once a second.
       Only ports that CHANGE are delivered, so the letters of a name that
       happens to match the last one never arrive: read the lot on a tick
       instead, once the slot has stayed still for one. */
    function ecouterEcho(icon) {
        var d = etat(icon);
        var slot = Math.round(borner(d.echo, 0, N_SLOT));
        if (slot < 1) { return; }
        if (slot !== d.echoVu) { d.echoVu = slot; return; }
        var t = '', k;
        for (k = 0; k < LONG_NOM; k++) {
            var c = Math.round(nombre(d.lettres[k]));
            t += (c >= 32 && c <= 126) ? String.fromCharCode(c) : ' ';
        }
        t = propre(t);
        /* a name still on its way down would be overwritten by the old
           one coming back up */
        if (d.file.length) { return; }
        if (d.noms[slot] !== t) {
            d.noms[slot] = t;
            ecrireNom(slot, t);
            majFavoris(icon);
        }
    }

    /* The list, and the button that opens it, both say the names. */
    function majFavoris(icon) {
        var d = etat(icon);
        var p = Math.round(borner(d.program, 0, DERNIER));
        var courant = (p >= PREMIER_USER) ? (p - PREMIER_USER + 1) : 0;
        for (var slot = 1; slot <= N_SLOT; slot++) {
            var nom = nomDe(d, slot);
            var pick = icon.find('.voice-fav-pick[data-slot="' + slot + '"]');
            pick.text(nom || ('USER ' + slot))
                .toggleClass('vide', !nom)
                .toggleClass('actif', slot === courant);
            /* never while it is being typed into: a box that rewrites
               itself under the cursor cannot be used */
            if (d.tape === slot) { continue; }
            icon.find('.voice-fav-name[data-slot="' + slot + '"]').val(nom);
        }
        icon.find('.voice-fav-btn').text(
            courant ? (nomDe(d, courant) || ('USER ' + courant)) : 'FAVORIS \u25BE');
    }

    /* mod-ui writes USER 1 to USER 6 into the readout, because that is
       what the descriptor says. Say the name instead, wherever there is
       one, and light the row of the list the sound came from. */
    function majProgramme(icon, valeur) {
        var d = etat(icon);
        var p = Math.round(borner(valeur, 0, DERNIER));
        if (p >= PREMIER_USER) {
            var slot = p - PREMIER_USER + 1;
            icon.find('.voice-prog-value').text(nomDe(d, slot) || ('USER ' + slot));
        }
        majFavoris(icon);
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
        var d0 = etat(icon);

        icon.find('.voice-prev').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var d = etat(icon);
            d.program = borner(Math.round(d.program) - 1, 0, DERNIER);
            funcs.set_port_value('program', d.program);
            majProgramme(icon, d.program);
        });

        icon.find('.voice-next').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var d = etat(icon);
            d.program = borner(Math.round(d.program) + 1, 0, DERNIER);
            funcs.set_port_value('program', d.program);
            majProgramme(icon, d.program);
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
                d.program = p;
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

        /* ---------------- the favourites ---------------- */

        icon.find('.voice-fav-btn').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var d = etat(icon);
            d.ouvert = !d.ouvert;
            icon.find('.voice-fav-panel').toggleClass('ouvert', d.ouvert);
            icon.find('.voice-fav-btn').toggleClass('ouvert', d.ouvert);
            if (d.ouvert) { majFavoris(icon); }
        });

        /* A row of the list is the sound: click it and go there. */
        icon.find('.voice-fav-pick').on('click', function (e) {
            if (e && e.preventDefault) { e.preventDefault(); e.stopPropagation(); }
            var cible = e && e.target ? e.target : null;
            var slot = cible && cible.getAttribute
                     ? Math.round(borner(Number(cible.getAttribute('data-slot')), 1, N_SLOT))
                     : 1;
            var d = etat(icon);
            d.program = PREMIER_USER + slot - 1;
            funcs.set_port_value('program', d.program);
            majProgramme(icon, d.program);
        });

        /* Typing in the pedal must not drag it around the board. */
        icon.find('.voice-fav-name').on('mousedown', function (e) {
            if (e && e.stopPropagation) { e.stopPropagation(); }
        });
        icon.find('.voice-fav-name').on('focus', function (e) {
            var d = etat(icon);
            var c = e && e.target ? e.target : null;
            d.tape = c && c.getAttribute ? Number(c.getAttribute('data-slot')) : 0;
        });
        icon.find('.voice-fav-name').on('blur', function () {
            etat(icon).tape = 0;
        });
        /* ENTER, or leaving the box, sends the name down. Not every
           keystroke: seven characters and a store, per letter typed,
           would be a hundred writes for one word. */
        icon.find('.voice-fav-name').on('change', function (e) {
            var c = e && e.target ? e.target : null;
            if (!c || !c.getAttribute) { return; }
            var slot = Math.round(borner(Number(c.getAttribute('data-slot')), 1, N_SLOT));
            var nom = propre(typeof c.value === 'string' ? c.value : '');
            c.value = nom;
            envoyerNom(icon, slot, nom);
        });

        /* The one clock this page runs: it walks the queue of characters
           down to the plugin, and reads the six names coming back. */
        d0.horloge = window.setInterval(function () {
            pomper(icon);
            ecouterEcho(icon);
        }, PAS_MS);
    }

    function changement(icon, symbol, valeur) {
        var d = etat(icon);
        d.ports[symbol] = valeur;
        /* the six names coming back: whose, and its letters */
        if (symbol === 'name_slot') {
            d.echo = nombre(valeur);
            return;
        }
        if (symbol.length === 2 && symbol.charAt(0) === 'n'
            && symbol >= 'n1' && symbol <= 'n7') {
            d.lettres[symbol.charCodeAt(1) - 49] = nombre(valeur);
            return;
        }
        /* Pick the strobe up where the pedalboard left it: starting again
           from zero after a reload could land on the value the port
           already holds, and a strobe that does not change types nothing. */
        if (symbol === 'web_strobe') {
            d.strobe = Math.round(borner(valeur, 0, 250));
            return;
        }
        if (symbol === 'web_char') { return; }
        if (symbol === 'program_now') {
            /* The plugin cannot write its own PROGRAM port, so A/B - and
               anything else that moves the program in force - is only
               visible here. Follow it: write the port back, which puts
               the list, the name and every knob where the sound is. */
            var p = Math.round(borner(valeur, 0, DERNIER));
            if (d.demarre && p !== Math.round(d.program)) {
                d.program = p;
                funcs.set_port_value('program', p);
                majProgramme(icon, p);
            }
            return;
        }
        if (symbol === 'program') {
            d.program = nombre(valeur);
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
        majProgramme(event.icon, etat(event.icon).program);
        majFavoris(event.icon);
        etat(event.icon).demarre = true;
    } else if (event.type === 'change') {
        changement(event.icon, event.symbol, event.value);
    }
}
