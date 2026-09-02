/* Fade — web UI animation.
 *
 * mod-ui evaluates this file with  eval('method = ' + code)  so it must
 * hold ONE function expression and nothing else: no trailing semicolon,
 * no declaration around it.
 *
 * We get 'start' once, then 'change' for every value. Outputs declared in
 * modgui:monitoredOutputs arrive through the same path.
 */
function (event, funcs) {

    function borner(v) {
        if (typeof v !== 'number' || v !== v) { return 0; }
        if (v < 0) { return 0; }
        if (v > 1) { return 1; }
        return v;
    }

    function dessiner(icon, v) {
        v = borner(v);
        var pct = v * 100;

        icon.find('.fade-fill').css('width', pct.toFixed(1) + '%');
        icon.find('.fade-dot').css('left', pct.toFixed(1) + '%');
        icon.find('.fade-pct').text(Math.round(pct) + '%');

        /* The end we reach lights up, the one we leave goes dark. */
        icon.find('.fade-end-1').toggleClass('vif', v < 0.01);
        icon.find('.fade-end-2').toggleClass('vif', v > 0.99);

        /* The TRIGGER button glows while the fade is running. */
        icon.find('.fade-push').toggleClass('actif', v > 0.001 && v < 0.999);
    }

    /* Dragging the rail crossfades by hand. mod-ui hands us
       funcs.set_port_value, which is the only supported way for icon code
       to move a control. */
    function attachDrag(icon) {
        var track = icon.find('.fade-track');
        if (!track.length || track.data('fadeDrag')) { return; }
        track.data('fadeDrag', true);

        function positionFromEvent(e) {
            var el = track[0];
            var box = el.getBoundingClientRect();
            var x = (e.clientX !== undefined ? e.clientX
                                             : (e.originalEvent || {}).clientX);
            if (x === undefined || !box.width) { return null; }
            var v = (x - box.left) / box.width;
            if (v < 0) { v = 0; }
            if (v > 1) { v = 1; }
            return v;
        }

        function send(e) {
            var v = positionFromEvent(e);
            if (v === null) { return; }
            dessiner(icon, v);
            if (funcs && funcs.set_port_value) {
                funcs.set_port_value('progress', v * 100);
            }
        }

        track.on('mousedown', function (e) {
            e.preventDefault();
            e.stopPropagation();      /* or the pedal gets dragged instead */
            send(e);

            function move(ev) { send(ev); }
            function up() {
                document.removeEventListener('mousemove', move, true);
                document.removeEventListener('mouseup', up, true);
            }
            document.addEventListener('mousemove', move, true);
            document.addEventListener('mouseup', up, true);
        });
    }

    if (event.type === 'start') {
        var depart = 0;
        if (event.ports) {
            for (var i = 0; i < event.ports.length; i++) {
                if (event.ports[i].symbol === 'position') {
                    depart = event.ports[i].value;
                }
            }
        }
        dessiner(event.icon, depart);
        attachDrag(event.icon);

    } else if (event.type === 'change') {
        if (event.symbol === 'position') {
            dessiner(event.icon, event.value);
        }
    }
}
