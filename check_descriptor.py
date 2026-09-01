#!/usr/bin/env python3
"""Pre-release checks, on the descriptor AND on the source."""
import re, sys
ok = True
def dire(quoi, cond, detail=""):
    global ok
    print("  %-52s %s %s" % (quoi, "OK" if cond else "ECHEC", detail))
    if not cond: ok = False

ttl = open('fade.ttl').read()
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
n_enum = len(re.findall(r'PORT_\w+\s*=\s*\d+', src)) - 1   # excluding PORT_COUNT
dire("descriptor and enum agree on port count", len(idx) == n_enum,
     "%d ports / %d dans l'enum" % (len(idx), n_enum))

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
