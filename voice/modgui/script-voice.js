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
 *   - lets a USER slot be given a LONG name, which lives in this browser
 *     because no port carries text. The short one, picked from a list on
 *     the NAME control, lives in the slot and reaches the pedal.
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
            d = { program: 0, ports: {} };
            icon.data('voiceState', d);
        }
        return d;
    }

    function cleNom(slot) {
        return 'voice.userName.' + slot;
    }

    function cleMot(slot) {
        return 'voice.userWord.' + slot;
    }

    function lireMot(slot) {
        try {
            var t = window.localStorage.getItem(cleMot(slot));
            return t === null ? -1 : Number(t);
        } catch (e) {
            return -1;
        }
    }

    function ecrireMot(slot, mot) {
        try { window.localStorage.setItem(cleMot(slot), String(mot)); }
        catch (e) { /* the plugin has the word anyway, in the slot */ }
    }

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
        } catch (e) { /* private mode: the sound still saves, the name does not */ }
    }

    /* The name box only means anything on a USER slot. */
    function majProgramme(icon, valeur, slotDest) {
        var p = Math.round(borner(valeur, 0, DERNIER));
        var estUser = p >= PREMIER_USER;
        var champ = icon.find('.voice-prog-rename');
        /* The box names the slot being LOOKED at when a USER program is
           selected, and otherwise the slot SAVE would write to - which is
           the one the player is about to name. */
        var slot = estUser ? (p - PREMIER_USER + 1)
                           : Math.round(borner(slotDest || 1, 1, N_SLOT));

        champ.prop('disabled', false);
        champ.val(lireNom(slot));
        /* say WHICH slot the box is naming: with a factory sound
           selected it names the one SAVE would write to, which is not
           obvious from a box that just says "name this slot" */
        champ.attr('placeholder', 'name for USER ' + slot);
        if (estUser) {
            icon.find('.voice-prog-value').text(lireNom(slot) || ('USER ' + slot));
        }
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
            ecrireMot(slot, Math.round(nombre(d.ports['slot_name'])));
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

        /* Typing in the pedal must not drag it around the board. */
        icon.find('.voice-prog-rename').on('mousedown', function (e) {
            if (e && e.stopPropagation) { e.stopPropagation(); }
        });
        icon.find('.voice-prog-rename').on('change', function (e) {
            var d = etat(icon);
            var p = Math.round(d.program);
            var nom = (e && e.target && typeof e.target.value === 'string')
                    ? e.target.value : '';
            var slot = (p >= PREMIER_USER) ? (p - PREMIER_USER + 1)
                     : Math.round(borner(d.ports['user_slot'] || 1, 1, N_SLOT));
            ecrireNom(slot, nom);
            if (p >= PREMIER_USER) {
                icon.find('.voice-prog-value').text(nom || ('USER ' + slot));
            }
        });
    }

    function changement(icon, symbol, valeur) {
        var d = etat(icon);
        d.ports[symbol] = valeur;
        if (symbol === 'program_now') {
            /* The plugin cannot write its own PROGRAM port, so A/B - and
               anything else that moves the program in force - is only
               visible here. Follow it: write the port back, which puts
               the list, the name and every knob where the sound is. */
            var p = Math.round(borner(valeur, 0, DERNIER));
            if (d.demarre && p !== Math.round(d.program)) {
                d.program = p;
                funcs.set_port_value('program', p);
                majProgramme(icon, p, d.ports['user_slot']);
            }
            return;
        }
        if (symbol === 'program') {
            var avant = d.program;
            d.program = nombre(valeur);
            majProgramme(icon, valeur, d.ports['user_slot']);
        } else if (symbol === 'user_slot') {
            majProgramme(icon, d.program, valeur);
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
        majProgramme(event.icon, etat(event.icon).program,
                     etat(event.icon).ports['user_slot']);
        etat(event.icon).demarre = true;
    } else if (event.type === 'change') {
        changement(event.icon, event.symbol, event.value);
    }
}
