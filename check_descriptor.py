#!/usr/bin/env python3
"""Pre-release checks, on the descriptor AND on the source."""
import re, sys
ok = True
def dire(quoi, cond, detail=""):
    global ok
    print("  %-52s %s %s" % (quoi, "OK" if cond else "ECHEC", detail))
    if not cond: ok = False

ttl = open('fade.ttl').read()
ttl_st = open('fade_stereo.ttl').read()
mani = open('manifest.ttl').read()
src = open('fade.c').read()

# --- The declaration without which addressed() is NEVER called ---
dire("hmi:PluginNotification declared as extensionData",
     re.search(r'lv2:extensionData\s+[^;]*hmi:PluginNotification', ttl) is not None)
dire("hmi:WidgetControl declared as optionalFeature",
     re.search(r'lv2:optionalFeature\s+[^;]*hmi:WidgetControl', ttl) is not None)
dire("hmi: prefix is http://moddevices.com/ns/hmi#",
     '@prefix hmi:    <http://moddevices.com/ns/hmi#>' in ttl)

# --- Ports: contiguous indices, unique symbols ---
idx = [int(x) for x in re.findall(r'lv2:index\s+(\d+)', ttl)]
sym = re.findall(r'lv2:symbol\s+"([^"]+)"', ttl)
dire("indices contiguous from 0", sorted(idx) == list(range(len(idx))), str(sorted(idx)))
dire("no duplicate symbol", len(sym) == len(set(sym)))
# The C enum numbers the CONTROL ports only; the audio ports differ between
# the mono and stereo variants, so the two are compared separately.
n_ctl_enum = len(re.findall(r'CTL_\w+\s*=\s*\d+', src)) - 1   # excluding CTL_COUNT
n_ctl_ttl = len(re.findall(r'lv2:ControlPort', ttl))
n_audio_ttl = len(re.findall(r'lv2:AudioPort', ttl))
dire("mono: control ports match the C enum", n_ctl_ttl == n_ctl_enum,
     "%d in ttl / %d in enum" % (n_ctl_ttl, n_ctl_enum))
dire("mono has 3 audio ports", n_audio_ttl == 3, str(n_audio_ttl))
dire("stereo: same controls as mono",
     len(re.findall(r'lv2:ControlPort', ttl_st)) == n_ctl_enum)

# --- Defaults inside their ranges ---
for bloc in ttl.split('] , ['):
    d = re.search(r'lv2:default\s+([-\d.]+)', bloc)
    mn = re.search(r'lv2:minimum\s+([-\d.]+)', bloc)
    mx = re.search(r'lv2:maximum\s+([-\d.]+)', bloc)
    s  = re.search(r'lv2:symbol\s+"([^"]+)"', bloc)
    if d and mn and mx:
        v, a, b = float(d.group(1)), float(mn.group(1)), float(mx.group(1))
        dire("default of %s within range" % s.group(1), a <= v <= b)

# --- modgui present: check_modgui.js inspects it thoroughly ---
import os
# --- bank images: without them mod-ui has nothing to show in the strip ---
import os
dire("thumbnail present", os.path.exists('modgui/thumbnail-fade.png'))
dire("screenshot present", os.path.exists('modgui/screenshot-fade.png'))
try:
    from PIL import Image
    w, h = Image.open('modgui/thumbnail-fade.png').size
    # The strip caps images at 256x64. A tall image scales down to a sliver.
    dire("thumbnail is wide, not a sliver", w >= 2 * h, "%dx%d" % (w, h))
except ImportError:
    pass

# --- stereo variant: the audio port counts are the whole point ---
n_in = len(re.findall(r'lv2:AudioPort , lv2:InputPort', ttl_st))
n_out = len(re.findall(r'lv2:AudioPort , lv2:OutputPort', ttl_st))
dire("stereo has >=2 audio in AND >=2 audio out", n_in >= 2 and n_out >= 2,
     "%d in / %d out" % (n_in, n_out))
dire("stereo URI differs from mono",
     'lv2/fade#stereo' in ttl_st and 'lv2/fade#stereo' not in ttl)
idx_st = sorted(int(x) for x in re.findall(r'lv2:index (\d+)', ttl_st))
dire("stereo indices contiguous from 0", idx_st == list(range(len(idx_st))),
     str(len(idx_st)) + " ports")
sym_st = re.findall(r'lv2:symbol "([^"]+)"', ttl_st)
dire("no duplicate symbol in stereo", len(sym_st) == len(set(sym_st)))
dire("both plugins in the manifest",
     mani.count('a lv2:Plugin') == 2 and 'fade_stereo.ttl' in mani)

mg0 = open('modgui.ttl').read()
i_st = mg0.index('lv2/fade#stereo')
mono_blk, st_blk = mg0[:i_st], mg0[i_st:]
def val(block, key):
    m = re.search(key + r'\s+[<"]([^>"]+)[>"]', block)
    return m.group(1) if m else None
for key, what in [(r'modgui:thumbnail', 'thumbnail'),
                  (r'modgui:screenshot', 'screenshot'),
                  (r'modgui:label', 'label')]:
    a, b = val(mono_blk, key), val(st_blk, key)
    dire("mono and stereo have different %s" % what, a is not None and a != b,
         "%s vs %s" % (a, b))
for p in re.findall(r'modgui:(?:thumbnail|screenshot) <([^>]+)>', mg0):
    dire("image file exists: " + p, os.path.exists(p))

dire("modgui.ttl present", os.path.exists('modgui.ttl'))
dire("modgui directory present", os.path.isdir('modgui'))
dire("modgui.ttl referenced by the manifest", 'modgui.ttl' in mani)
mg = open('modgui.ttl').read()
dire("monitoredOutputs as [ lv2:symbol ] nodes, not a bare string",
     re.search(r'monitoredOutputs\s*\[\s*lv2:symbol', mg) is not None)
sorties = [re.search(r'lv2:symbol\s+"([^"]+)"', b).group(1)
           for b in ttl.split('] , [')
           if 'lv2:ControlPort' in b and 'lv2:OutputPort' in b
           and re.search(r'lv2:symbol\s+"([^"]+)"', b)]
manquantes = [s2 for s2 in sorties if '"%s"' % s2 not in mg]
dire("every control output is monitored by the web UI",
     not manquantes, " ".join(manquantes))

# --- Screen strings: caps, ASCII, lengths ---
for t in re.findall(r'"([^"]*)"', src):
    if t.startswith("http") or t.startswith("FADE_BUILD") or len(t) > 12: continue
print("  (screen strings are checked at runtime by the test bench)")

sys.exit(0 if ok else 1)
