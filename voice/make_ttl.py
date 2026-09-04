#!/usr/bin/env python3
"""Writes voice.ttl, voice_stereo.ttl, presets.ttl and manifest.ttl.

The two variants differ ONLY in their audio ports - 1 in / 1 out against
2 in / 2 out - and share all twenty-six controls. Maintaining that list
twice by hand is how a port ends up with a different default in one file
than the other, which shows up as a plugin that sounds different in mono
and nobody can say why. So it is written once, here.

This is a generator, not a checker: check_descriptor.py reads the .ttl
files back and compares them against the table in voice.c, which is a
separate source. If this file and voice.c disagree, the check fails.
"""
import sys

HEAD = """@prefix doap:   <http://usefulinc.com/ns/doap#> .
@prefix foaf:   <http://xmlns.com/foaf/0.1/> .
@prefix hmi:    <http://moddevices.com/ns/hmi#> .
@prefix lv2:    <http://lv2plug.in/ns/lv2core#> .
@prefix mod:    <http://moddevices.com/ns/mod#> .
@prefix rdfs:   <http://www.w3.org/2000/01/rdf-schema#> .
@prefix pprops: <http://lv2plug.in/ns/ext/port-props#> .
@prefix units:  <http://lv2plug.in/ns/extensions/units#> .

<%(uri)s>
    a lv2:Plugin , lv2:DynamicsPlugin ;

    doap:name "%(name)s" ;
    doap:license <http://opensource.org/licenses/isc> ;
    doap:maintainer [ foaf:name "Remy" ] ;

    mod:brand "REMY" ;
    mod:label "%(label)s" ;

    rdfs:comment "%(comment)s" ;

    lv2:minorVersion 1 ;
    lv2:microVersion 0 ;

    lv2:optionalFeature lv2:hardRTCapable , hmi:WidgetControl ;
    lv2:extensionData hmi:PluginNotification ;

    lv2:port """

COMMENT = ("A vocal channel strip and effects rack, with no pitch detection "
           "anywhere in it: gate, compressor, de-esser, low cut, three tone "
           "bands, drive, doubler, modulation, tap delay and reverb. One "
           "switch feeds the effects or stops feeding them, and it stops the "
           "send, not the return, so the delay and the reverb ring out "
           "instead of being chopped.")

# symbol, name, min, max, default, unit, properties, comment
CONTROLS = [
 ("in_gain", "IN GAIN", -20.0, 40.0, 0.0, "db", [],
  "Gain applied to the input, before everything else. A dynamic microphone "
  "straight into the Dwarf usually wants +20 to +30 dB here."),
 ("low_cut", "LOW CUT", 0.0, 400.0, 90.0, "hz", [],
  "High-pass filter on the way in, 6 dB per octave. Takes out stage rumble, "
  "handling noise and plosives before they reach the gate. At 0 Hz it is off."),
 ("gate", "GATE", -80.0, -20.0, -80.0, "db", [],
  "Gate threshold. Below it the channel closes over about 120 ms, with 6 dB "
  "of hysteresis and an 80 ms hold so a held note does not chatter. At -80 dB "
  "the gate is off."),
 ("comp", "COMP", 0.0, 100.0, 30.0, "pc", [],
  "Compression amount. One control: it lowers the threshold and raises the "
  "ratio together, from off to -40 dB at 6:1, and adds back most of what it "
  "takes off. The GR output says how hard it is working."),
 ("de_ess", "DE-ESS", 0.0, 100.0, 0.0, "pc", [],
  "Tames sibilance by compressing the band above 5.5 kHz alone, so an S loses "
  "its edge without the whole word going dull."),
 ("body", "BODY", -12.0, 12.0, 0.0, "db", [],
  "Low band, below about 240 Hz. Up for weight, down when the microphone is "
  "close and the proximity effect has already added it."),
 ("presence", "PRESENCE", -12.0, 12.0, 0.0, "db", [],
  "Middle band, roughly 1 to 4.5 kHz. This is where a voice cuts through a "
  "band, and where it gets honky if pushed too far."),
 ("air", "AIR", -12.0, 12.0, 0.0, "db", [],
  "Top band, above about 6 kHz. Breath and detail. Use the de-esser if adding "
  "air also brings the sibilance up."),
 ("drive", "DRIVE", 0.0, 100.0, 0.0, "pc", [],
  "Soft saturation. Adds harmonics and levels out what the compressor left, "
  "from clean through to an overdriven-preamp sound."),
 ("doubler", "DOUBLE", 0.0, 100.0, 0.0, "pc", [],
  "Two copies of the voice a few tens of milliseconds late, each drifting on "
  "its own slow LFO. The drift is what makes it a second take rather than a "
  "copy. In the stereo build one copy goes left and the other right."),
 ("modulation", "MOD", 0.0, 100.0, 0.0, "pc", [],
  "Chorus. Sets the depth and the amount together. In the stereo build the "
  "two sides move a quarter cycle apart, which is where the width comes from."),
 ("mod_speed", "MOD SPEED", 0.05, 8.0, 0.6, "hz", ["pprops:logarithmic"],
  "Speed of the chorus. Slow is a drift, fast is a vibrato."),
 ("delay_time", "DELAY", 20.0, 2000.0, 400.0, "ms", ["pprops:logarithmic"],
  "Delay time. Two presses of TAP override it; moving this control takes it "
  "back. TIME publishes the value actually in force."),
 ("delay_repeats", "REPEATS", 0.0, 95.0, 30.0, "pc", [],
  "Delay feedback. The repeats lose their top and their bottom each time "
  "round, so a long tail sits behind the voice instead of fighting it."),
 ("delay_mix", "DELAY MIX", 0.0, 100.0, 0.0, "pc", [],
  "How much delay is heard. The delay also feeds the reverb, so repeats are "
  "in the room rather than in front of it."),
 ("reverb", "REVERB", 0.0, 100.0, 40.0, "pc", [],
  "Tail length. Moves the size of the room and its damping together, from a "
  "small dry box to a long hall."),
 ("reverb_mix", "REVERB MIX", 0.0, 100.0, 0.0, "pc", [],
  "How much reverb is heard."),
 ("fx", "FX", 0.0, 1.0, 1.0, None, ["lv2:toggled"],
  "On: the doubler, chorus, delay and reverb are fed. Off: the send is cut "
  "over 40 ms and the tails ring out instead of being chopped. Meant for a "
  "footswitch."),
 ("fx_trigger", "FX TRIGGER", 0.0, 1.0, 0.0, None,
  ["lv2:toggled", "pprops:trigger"],
  "Each pulse flips the FX state. Meant for MIDI: a port can only take one "
  "addressing, so this doubles the toggle rather than replacing it."),
 ("tap", "TAP", 0.0, 1.0, 0.0, None, ["lv2:toggled", "pprops:trigger"],
  "Tap tempo for the delay. Two presses set the time, from 20 to 2000 ms. "
  "Longer than that is treated as a fresh start, not a tempo."),
 ("output", "OUTPUT", -60.0, 12.0, 0.0, "db", [],
  "Output level, after everything. At -60 dB the plugin is silent."),
]

OUTPUTS = [
 ("gr", "GR", -24.0, 0.0, 0.0, "db", [],
  "Output. Compressor gain reduction in dB, at its worst over the last "
  "block. 0 means the compressor is doing nothing."),
 ("level", "LEVEL", 0.0, 1.0, 0.0, None, [],
  "Output. Peak level leaving the plugin, 0 to 1, with a 300 ms fall so a "
  "peak stays visible."),
 ("gate_open", "GATE OPEN", 0.0, 1.0, 0.0, None, ["lv2:toggled"],
  "Output. 1 while the gate is open."),
 ("fx_state", "FX STATE", 0.0, 1.0, 1.0, None, ["lv2:toggled"],
  "Output. The FX state actually in force. It exists because the trigger and "
  "the toggle drive one state, and a plugin must not write back into either."),
 ("time_out", "TIME", 20.0, 2000.0, 400.0, "ms", [],
  "Output. The delay time in force, in milliseconds, whether it came from "
  "the knob or from the tap."),
]


# Presets. A VoiceLive is used by picking a sound and then adjusting it, and
# twenty-one controls at their defaults is not a sound. Each entry below only
# names what it changes; everything else is written out at its default, so
# loading a preset always lands somewhere known rather than on top of half of
# whatever was there before.
PRESETS = [
    ("speech", "Speech", {
        "in_gain": 12.0, "low_cut": 120.0, "gate": -45.0, "comp": 35.0,
        "de_ess": 40.0, "presence": 4.0, "air": 2.0}),
    ("stage", "Stage Dry", {
        "in_gain": 18.0, "low_cut": 100.0, "gate": -42.0, "comp": 45.0,
        "de_ess": 30.0, "presence": 3.0, "air": 2.0, "body": -2.0}),
    ("ballad", "Ballad", {
        "in_gain": 18.0, "low_cut": 90.0, "gate": -48.0, "comp": 45.0,
        "de_ess": 30.0, "body": 2.0, "air": 3.0, "doubler": 20.0,
        "delay_time": 420.0, "delay_repeats": 25.0, "delay_mix": 18.0,
        "reverb": 55.0, "reverb_mix": 35.0}),
    ("rock", "Rock", {
        "in_gain": 20.0, "low_cut": 130.0, "gate": -40.0, "comp": 65.0,
        "de_ess": 45.0, "presence": 5.0, "air": 2.0, "drive": 35.0,
        "delay_time": 120.0, "delay_repeats": 20.0, "delay_mix": 12.0,
        "reverb": 30.0, "reverb_mix": 16.0}),
    ("wide", "Wide", {
        "in_gain": 18.0, "low_cut": 90.0, "gate": -48.0, "comp": 40.0,
        "de_ess": 30.0, "air": 4.0, "doubler": 55.0, "modulation": 40.0,
        "mod_speed": 0.4, "reverb": 60.0, "reverb_mix": 40.0}),
    ("cathedral", "Cathedral", {
        "in_gain": 18.0, "low_cut": 110.0, "gate": -46.0, "comp": 40.0,
        "de_ess": 35.0, "air": 3.0, "doubler": 25.0,
        "delay_time": 600.0, "delay_repeats": 45.0, "delay_mix": 25.0,
        "reverb": 100.0, "reverb_mix": 55.0}),
]

# Triggers are left out on purpose: a preset that presses TAP would set a
# tempo the moment it loads.
PRESET_SKIP = ("fx_trigger", "tap")

PRESET_HEAD = """@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .
@prefix pset:  <http://lv2plug.in/ns/ext/presets#> .
@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .
"""

PLUGINS = (("http://remy-live.github.io/lv2/voice", ""),
           ("http://remy-live.github.io/lv2/voice#stereo", "_stereo"))


def preset_uri(plugin_uri, suffix, key):
    """One preset per plugin: a single node with two lv2:appliesTo works in
    lilv but leaves hosts guessing which plugin a saved state belongs to."""
    return "http://remy-live.github.io/lv2/voice#preset_%s%s" % (key, suffix)


def write_presets(path):
    out = [PRESET_HEAD]
    for uri, suffix in PLUGINS:
        for key, label, values in PRESETS:
            unknown = [k for k in values if k not in [c[0] for c in CONTROLS]]
            if unknown:
                raise SystemExit("preset %s names no such port: %s" % (key, unknown))
            ports = []
            for symbol, _n, mn, mx, default, _u, _p, _c in CONTROLS:
                if symbol in PRESET_SKIP:
                    continue
                v = values.get(symbol, default)
                if not mn <= v <= mx:
                    raise SystemExit("preset %s: %s = %s is outside %s..%s"
                                     % (key, symbol, v, mn, mx))
                ports.append('        lv2:symbol "%s" ;\n        pset:value %s'
                             % (symbol, fmt(v)))
            out.append("<%s>\n    a pset:Preset ;\n    lv2:appliesTo <%s> ;\n"
                       '    rdfs:label "%s" ;\n    lv2:port [\n%s\n    ] .\n'
                       % (preset_uri(uri, suffix, key), uri, label,
                          "\n    ] , [\n".join(ports)))
    open(path, "w").write("\n".join(out))
    return len(PRESETS) * len(PLUGINS)


MANIFEST_HEAD = """@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix pset: <http://lv2plug.in/ns/ext/presets#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

<http://remy-live.github.io/lv2/voice>
    a lv2:Plugin ;
    lv2:binary <voice.so> ;
    rdfs:seeAlso <voice.ttl> .

<http://remy-live.github.io/lv2/voice#stereo>
    a lv2:Plugin ;
    lv2:binary <voice.so> ;
    rdfs:seeAlso <voice_stereo.ttl> .
"""


def write_manifest(path):
    out = [MANIFEST_HEAD]
    for uri, suffix in PLUGINS:
        for key, _label, _values in PRESETS:
            out.append("<%s>\n    a pset:Preset ;\n    lv2:appliesTo <%s> ;\n"
                       "    rdfs:seeAlso <presets.ttl> .\n"
                       % (preset_uri(uri, suffix, key), uri))
    open(path, "w").write("\n".join(out))


MONO_AUDIO = [("in", "IN", "InputPort", "The microphone, or whatever else is being sung through."),
              ("out", "OUT", "OutputPort", None)]
STEREO_AUDIO = [("in_l", "IN L", "InputPort", "Left input."),
                ("in_r", "IN R", "InputPort", "Right input."),
                ("out_l", "OUT L", "OutputPort", None),
                ("out_r", "OUT R", "OutputPort", None)]


def fmt(v):
    """Turtle numbers: keep them decimal so the parser sees a float."""
    s = "%.6f" % v
    s = s.rstrip("0")
    if s.endswith("."):
        s += "0"
    return s


def port_block(index, kind, direction, symbol, name, comment,
               mn=None, mx=None, default=None, unit=None, props=()):
    out = ["        a lv2:%s , lv2:%s ;" % (kind, direction),
           "        lv2:index %d ;" % index,
           '        lv2:symbol "%s" ;' % symbol,
           '        lv2:name "%s" ;' % name]
    if props:
        out.append("        lv2:portProperty %s ;" % " , ".join(props))
    if default is not None:
        out.append("        lv2:default %s ;" % fmt(default))
        out.append("        lv2:minimum %s ;" % fmt(mn))
        out.append("        lv2:maximum %s ;" % fmt(mx))
    if unit:
        out.append("        units:unit units:%s ;" % unit)
    if comment:
        out.append('        rdfs:comment "%s"' % comment)
    else:                      # nothing may end with a semicolon
        out[-1] = out[-1].rstrip(" ;")
    return "\n".join(out)


def write(path, uri, name, label, audio):
    blocks = []
    index = 0
    for symbol, pname, direction, comment in audio:
        blocks.append(port_block(index, "AudioPort", direction, symbol, pname, comment))
        index += 1
    for symbol, pname, mn, mx, default, unit, props, comment in CONTROLS:
        blocks.append(port_block(index, "ControlPort", "InputPort", symbol, pname,
                                 comment, mn, mx, default, unit, props))
        index += 1
    for symbol, pname, mn, mx, default, unit, props, comment in OUTPUTS:
        blocks.append(port_block(index, "ControlPort", "OutputPort", symbol, pname,
                                 comment, mn, mx, default, unit, props))
        index += 1

    body = HEAD % {"uri": uri, "name": name, "label": label, "comment": COMMENT}
    body += "[\n" + "\n    ] , [\n".join(blocks) + "\n    ] .\n"
    open(path, "w").write(body)
    return index


if __name__ == "__main__":
    n = write("voice.ttl", "http://remy-live.github.io/lv2/voice",
              "Voice", "VOICE", MONO_AUDIO)
    m = write("voice_stereo.ttl", "http://remy-live.github.io/lv2/voice#stereo",
              "Voice Stereo", "VOICE ST", STEREO_AUDIO)
    p = write_presets("presets.ttl")
    write_manifest("manifest.ttl")
    print("voice.ttl: %d ports, voice_stereo.ttl: %d ports, presets.ttl: %d"
          % (n, m, p))
    sys.exit(0)
