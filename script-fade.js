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

    } else if (event.type === 'change') {
        if (event.symbol === 'position') {
            dessiner(event.icon, event.value);
        }
    }
}
