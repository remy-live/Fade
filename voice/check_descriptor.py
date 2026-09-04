#!/usr/bin/env python3
"""Pre-release checks on the descriptors AND on the source.

The point of this file is that voice.c and the two .ttl files each hold
the same table - symbol, minimum, maximum, default, for twenty-six
controls - and they are written by different hands. A default that is
right in one and wrong in the other is invisible until a singer plugs in
and the gate is shut. So the two are compared here, entry by entry.
"""
import os
import re
import sys

ok = True


def dire(quoi, cond, detail=""):
    global ok
    print("  %-52s %s %s" % (quoi, "OK" if cond else "ECHEC", detail))
    if not cond:
        ok = False


src = open('voice.c').read()


def code_only(text):
    """The source with its comments and string literals taken out.

    Without this, a check for a libm call trips over a comment that
    explains which libm call is NOT being made - which is exactly the
    kind of false alarm that teaches people to ignore the checker.
    """
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    return re.sub(r'"(\\.|[^"\\])*"', '""', text)


code = code_only(src)
ttl = open('voice.ttl').read()
ttl_st = open('voice_stereo.ttl').read()
mani = open('manifest.ttl').read()

# --- the declaration without which addressed() is NEVER called ---
dire("hmi:PluginNotification declared as extensionData",
     re.search(r'lv2:extensionData\s+[^;]*hmi:PluginNotification', ttl) is not None)
dire("hmi:WidgetControl declared as optionalFeature",
     re.search(r'lv2:optionalFeature\s+[^;]*hmi:WidgetControl', ttl) is not None)
dire("hmi: prefix is http://moddevices.com/ns/hmi#",
     '@prefix hmi:    <http://moddevices.com/ns/hmi#>' in ttl)


def ports(text):
    """Every port block, in file order."""
    out = []
    for b in text.split('] , ['):
        s = re.search(r'lv2:symbol\s+"([^"]+)"', b)
        i = re.search(r'lv2:index\s+(\d+)', b)
        if not s or not i:
            continue

        def num(key):
            m = re.search(r'lv2:%s\s+(-?[\d.]+)' % key, b)
            return float(m.group(1)) if m else None

        out.append({
            'symbol': s.group(1),
            'index': int(i.group(1)),
            'audio': 'lv2:AudioPort' in b,
            'control': 'lv2:ControlPort' in b,
            'input': 'lv2:InputPort' in b,
            'min': num('minimum'), 'max': num('maximum'), 'def': num('default'),
            'props': re.findall(r'lv2:portProperty\s+([^;]+);', b),
            'unit': (re.search(r'units:unit\s+units:(\w+)', b) or [None, None])[1]
            if re.search(r'units:unit\s+units:(\w+)', b) else None,
            'text': b,
        })
    return out


mono, stereo = ports(ttl), ports(ttl_st)

# --- the C table, read out of the source ---
bloc = re.search(r'static const CtlSpec ctl_spec\[CTL_COUNT\] = \{(.*?)\n\};', src, re.S)
dire("ctl_spec table found in voice.c", bloc is not None)
c_table = []
if bloc:
    for line in bloc.group(1).splitlines():
        m = re.match(r'\s*\{\s*"([^"]+)"\s*,\s*(-?[\d.]+)f\s*,\s*(-?[\d.]+)f\s*,'
                     r'\s*(-?[\d.]+)f\s*\}', line)
        if m:
            c_table.append((m.group(1), float(m.group(2)),
                            float(m.group(3)), float(m.group(4))))

n_enum = len(re.findall(r'CTL_\w+\s*=\s*\d+', src)) - 1     # CTL_COUNT excluded
dire("the C enum and the C table have the same length",
     len(c_table) == n_enum, "%d table / %d enum" % (len(c_table), n_enum))

first_out = re.search(r'#define CTL_FIRST_OUTPUT (CTL_\w+)', src)
dire("CTL_FIRST_OUTPUT defined", first_out is not None)
enum_index = dict((m.group(1), int(m.group(2)))
                  for m in re.finditer(r'(CTL_\w+)\s*=\s*(\d+)', src))
n_in_ctl = enum_index.get(first_out.group(1), -1) if first_out else -1

for name, blocks, n_audio_expected, in_out in (("mono", mono, 2, (1, 1)),
                                               ("stereo", stereo, 4, (2, 2))):
    idx = sorted(p['index'] for p in blocks)
    dire("%s: indices contiguous from 0" % name, idx == list(range(len(idx))),
         "%d ports" % len(idx))
    syms = [p['symbol'] for p in blocks]
    dire("%s: no duplicate symbol" % name, len(syms) == len(set(syms)))

    audio = [p for p in blocks if p['audio']]
    dire("%s: %d audio ports" % (name, n_audio_expected),
         len(audio) == n_audio_expected, str(len(audio)))
    dire("%s: %d audio in / %d audio out" % ((name,) + in_out),
         (len([p for p in audio if p['input']]),
          len([p for p in audio if not p['input']])) == in_out)
    dire("%s: audio ports come first" % name,
         all(p['index'] < n_audio_expected for p in audio))

    ctl = [p for p in blocks if p['control']]
    dire("%s: control ports match the C table" % name,
         len(ctl) == len(c_table), "%d ttl / %d C" % (len(ctl), len(c_table)))

    # --- the comparison this file exists for ---
    bad = []
    for spec, port in zip(c_table, ctl):
        if (spec[0] != port['symbol'] or spec[1] != port['min']
                or spec[2] != port['max'] or spec[3] != port['def']):
            bad.append("%s(ttl %s %s..%s d=%s / C %s %s..%s d=%s)" % (
                port['symbol'], port['symbol'], port['min'], port['max'],
                port['def'], spec[0], spec[1], spec[2], spec[3]))
    dire("%s: every control matches voice.c, entry by entry" % name,
         not bad, " ".join(bad))

    dire("%s: inputs and outputs split where the C enum splits" % name,
         all(p['input'] == (i < n_in_ctl) for i, p in enumerate(ctl)))
    dire("%s: every default is inside its own range" % name,
         all(p['min'] <= p['def'] <= p['max'] for p in ctl if p['def'] is not None))
    dire("%s: no logarithmic port reaches zero" % name,
         all(not any('logarithmic' in x for x in p['props']) or p['min'] > 0
             for p in ctl))

# --- mono and stereo must not drift apart ---
dire("mono and stereo declare the same controls",
     [(p['symbol'], p['min'], p['max'], p['def'])
      for p in mono if p['control']] ==
     [(p['symbol'], p['min'], p['max'], p['def'])
      for p in stereo if p['control']])
dire("stereo URI differs from mono",
     'lv2/voice#stereo' in ttl_st and 'lv2/voice#stereo' not in ttl)

# --- the switches have to be switches ---
by_symbol = dict((p['symbol'], p) for p in mono)
for sym in ('fx', 'fx_state', 'gate_open'):
    dire("%s is toggled" % sym,
         any('lv2:toggled' in x for x in by_symbol[sym]['props']))
for sym in ('fx_trigger', 'tap'):
    dire("%s is a trigger" % sym,
         any('pprops:trigger' in x for x in by_symbol[sym]['props']))

# --- manifest ---
dire("both plugins in the manifest",
     mani.count('a lv2:Plugin') == 2 and 'voice_stereo.ttl' in mani
     and 'voice.ttl' in mani)
dire("the manifest names the binary that gets built",
     mani.count('<voice.so>') == 2)

# --- presets: the shipped file, not the generator's intentions ---
pres = open('presets.ttl').read()
blocs = re.findall(r'<([^>]+)>\s*\n\s*a pset:Preset ;\s*\n\s*lv2:appliesTo <([^>]+)> ;'
                   r'\s*\n\s*rdfs:label "([^"]+)" ;(.*?)\n    \] \.', pres, re.S)
dire("presets.ttl parses into presets", len(blocs) > 0, "%d found" % len(blocs))

connus = dict((p['symbol'], p) for p in mono if p['control'] and p['input'])
hors_plage, inconnus, non_listes, mauvais_uri = [], [], [], []
uris = ('http://remy-live.github.io/lv2/voice',
        'http://remy-live.github.io/lv2/voice#stereo')
for uri, applique, label, corps in blocs:
    if applique not in uris:
        mauvais_uri.append(applique)
    if '<%s>' % uri not in mani:
        non_listes.append(label)
    for sym, val in re.findall(r'lv2:symbol "([^"]+)" ;\s*\n\s*pset:value ([-\d.]+)', corps):
        if sym not in connus:
            inconnus.append(sym)
        elif not (connus[sym]['min'] <= float(val) <= connus[sym]['max']):
            hors_plage.append("%s/%s=%s" % (label, sym, val))
dire("every preset names an input port that exists", not inconnus,
     " ".join(sorted(set(inconnus))))
dire("every preset value is inside its port's range", not hors_plage,
     " ".join(hors_plage))
dire("every preset applies to a plugin that exists", not mauvais_uri,
     " ".join(sorted(set(mauvais_uri))))
dire("every preset is listed in the manifest", not non_listes,
     " ".join(sorted(set(non_listes))))
dire("both variants have the same presets",
     len([b for b in blocs if b[1].endswith('#stereo')]) ==
     len([b for b in blocs if not b[1].endswith('#stereo')]))
dire("no preset presses a trigger",
     not re.search(r'lv2:symbol "(tap|fx_trigger)"', pres))

# --- source rules that the compiler cannot check ---
libm = re.findall(r'\b(sinf?|cosf?|tanf?|powf?|expf?|logf?|log2f?|log10f?|sqrtf?|'
                  r'fabsf?|tanhf?|floorf?|ceilf?|roundf?|fmodf?)\s*\(', code)
dire("no libm call in the code", not libm, " ".join(sorted(set(libm))))
dire("no allocation inside run()",
     'alloc' not in code.split('run(LV2_Handle instance')[1])
dire("no printf family in the code",
     not re.search(r'\b\w*printf\s*\(', code))
dire("cleanup frees the pool as well as the instance",
     re.search(r'free\(self->pool\)', code) is not None)
dire("every buffer comes from the one pool",
     len(re.findall(r'\bcalloc\s*\(', code)) == 2, "one for the instance, one for the pool")
dire("the build stamp names the architecture",
     re.search(r'VOICE_BUILD\d+_AARCH64_\d+', src) is not None)

print("  (screen strings and DSP behaviour are checked by test_voice.c)")
sys.exit(0 if ok else 1)
