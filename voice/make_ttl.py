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
@prefix rdf:    <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix state:  <http://lv2plug.in/ns/ext/state#> .
@prefix units:  <http://lv2plug.in/ns/extensions/units#> .
@prefix urid:   <http://lv2plug.in/ns/ext/urid#> .

<%(uri)s>
    a lv2:Plugin , lv2:DynamicsPlugin ;

    doap:name "%(name)s" ;
    doap:license <http://opensource.org/licenses/isc> ;
    doap:maintainer [ foaf:name "Remy" ] ;

    mod:brand "REMY" ;
    mod:label "%(label)s" ;

    rdfs:comment "%(comment)s" ;

    lv2:minorVersion 5 ;
    lv2:microVersion 0 ;

    lv2:optionalFeature lv2:hardRTCapable , hmi:WidgetControl , urid:map ;
    lv2:extensionData hmi:PluginNotification , state:interface ;

    lv2:port """

COMMENT = ("A vocal channel strip and effects rack, with no pitch detection "
           "anywhere in it: gate, compressor, de-esser, low cut, three tone "
           "bands, drive, three-voice doubler, chorus, tap delay and reverb. "
           "Every effect has its own switch for a footswitch, and one master "
           "switch feeds them all or stops feeding them. Switching off stops "
           "the send, not the return, so the delay and the reverb ring out "
           "instead of being chopped.")

# symbol, name, min, max, default, unit, properties, comment
# The order IS the layout: mod-ui lists ports by index, so the patch
# selector comes first and each effect's switch sits immediately in front
# of the controls it switches.
SWITCH = ["lv2:toggled"]
LIST = ["lv2:integer", "lv2:enumeration", "pprops:hasStrictBounds"]
CONTROLS = [
 ("program", "PROGRAM", 0.0, 72.0, 0.0, None, LIST,
  "Picks a sound from the list: MANUAL, the built-in programs, then four "
  "slots of your own. MANUAL means the controls below are yours; anything "
  "else overrides them for as long as it is selected, and IN GAIN, OUTPUT "
  "and every switch stay yours either way. Address it to an encoder to walk "
  "the list from the device."),
 ("user_slot", "USER SLOT", 1.0, 6.0, 1.0, None, LIST,
  "Which of the six USER slots SAVE writes to. It is a separate list from "
  "PROGRAM on purpose: it lets you pick a built-in sound, change what you "
  "want, and store the result somewhere else without losing the original."),
 ("save", "SAVE", 0.0, 1.0, 0.0, None, ["lv2:toggled", "pprops:trigger"],
  "Stores WHAT YOU ARE HEARING into the slot USER SLOT points at - the "
  "program you picked, plus every change you made to it. Turning any control "
  "while a program is selected hands that control back to you, so a built-in "
  "sound is a starting point rather than a cage. The slots are saved with the "
  "pedalboard."),
 ("in_gain", "IN GAIN", -20.0, 40.0, 0.0, "db", [],
  "Gain applied to the input, before everything else. This is a rig "
  "setting, not a sound: no preset and no program touches it, so what you "
  "set here survives everything."),
 ("low_cut", "LOW CUT", 0.0, 400.0, 90.0, "hz", [],
  "High-pass filter on the way in, 6 dB per octave. Takes out stage rumble, "
  "handling noise and plosives before they reach the gate. At 0 Hz it is off."),
 ("gate_on", "GATE ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the gate in and out. Made for a footswitch."),
 ("gate", "GATE", -80.0, -20.0, -80.0, "db", [],
  "Gate threshold. Below it the channel closes over about 120 ms, with 6 dB "
  "of hysteresis and an 80 ms hold so a held note does not chatter. At -80 dB "
  "the gate does nothing whatever its switch says."),
 ("comp_on", "COMP ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the compressor in and out. Made for a footswitch."),
 ("comp", "COMP", 0.0, 100.0, 30.0, "pc", [],
  "Compression amount. One control: it lowers the threshold and raises the "
  "ratio together, from off to -40 dB at 6:1. What it gives back is most of "
  "what it takes off a voice at -12 dBFS, so turning it up changes the sound "
  "rather than how loud you are. The GR output says how hard it is working."),
 ("de_ess_on", "DE-ESS ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the de-esser in and out. Made for a footswitch."),
 ("de_ess", "DE-ESS", 0.0, 100.0, 0.0, "pc", [],
  "Tames sibilance by compressing the band above 5.5 kHz alone, so an S loses "
  "its edge without the whole word going dull."),
 ("eq_on", "EQ ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the three tone bands in and out together. Made for a footswitch."),
 ("body", "BODY", -12.0, 12.0, 0.0, "db", [],
  "Low band, a shelf below about 240 Hz. Up for weight, down when the "
  "microphone is close and the proximity effect has already added it."),
 ("mid_freq", "MID FREQ", 300.0, 5000.0, 2200.0, "hz", ["pprops:logarithmic"],
  "Where the middle band sits. Low for the chest of a voice, high for the "
  "edge that cuts through a band."),
 ("presence", "PRESENCE", -12.0, 12.0, 0.0, "db", [],
  "The middle band, a wide bell around MID FREQ. This is where a voice cuts "
  "through, and where it gets honky if pushed too far."),
 ("air", "AIR", -12.0, 12.0, 0.0, "db", [],
  "Top band, a shelf above about 6 kHz. Breath and detail. Use the de-esser "
  "if adding air also brings the sibilance up."),
 ("drive_on", "DRIVE ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the saturation in and out. Made for a footswitch."),
 ("drive", "DRIVE", 0.0, 100.0, 0.0, "pc", [],
  "Soft saturation, level-matched at -12 dBFS like the compressor: it adds "
  "harmonics and holds down what the compressor left, without making you "
  "louder."),
 ("pitch_on", "PITCH ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the pitch shifter in and out. Made for a footswitch."),
 ("pitch", "PITCH", -12.0, 12.0, 0.0, None,
  ["lv2:integer", "pprops:hasStrictBounds"],
  "Shifts the whole voice, in semitones, WITHOUT following its pitch: the "
  "signal is read out of a delay line at a different rate, so there is "
  "nothing to detect and nothing to mistrack. It moves the formants with the "
  "note, which is why down sounds like a bigger singer and up sounds like "
  "helium rather than like a harmony. At 0 it is out of the way entirely."),
 ("pitch_mix", "PITCH MIX", 0.0, 100.0, 100.0, "pc", [],
  "How much of the shifted voice replaces the original. At 100 you hear only "
  "the new voice; lower down the two sing together, which at an octave is an "
  "octaver and at three or four semitones is a fixed harmony."),
 ("doubler_on", "DOUBLE ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the doubler in and out. Made for a footswitch."),
 ("doubler", "DOUBLE", 0.0, 100.0, 0.0, "pc", [],
  "How much of the doubled voices is heard. They arrive twenty to forty-six "
  "milliseconds late, each drifting a few cents on its own slow LFO and "
  "sitting slightly darker than the lead."),
 ("spread", "SPREAD", 0.0, 100.0, 50.0, "pc", [],
  "How far apart the doubled voices are: their detune, their drift and how "
  "much their entries differ. Low is one singer twice, high is a group of "
  "them who have never met."),
 ("voices", "VOICES", 2.0, 4.0, 3.0, None, LIST,
  "How many doubled voices: two for a straight double, three for a thicker "
  "one, four for a small choir. The level is held steady as the count "
  "changes, so this picks a texture and not a volume. In the stereo build "
  "they alternate left and right, with the odd one up the middle."),
 ("mod_on", "MOD ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the chorus in and out. Made for a footswitch."),
 ("modulation", "MOD", 0.0, 100.0, 0.0, "pc", [],
  "Chorus. Sets the depth and the amount together. In the stereo build the "
  "two sides move a quarter cycle apart, which is where the width comes from."),
 ("mod_speed", "MOD SPEED", 0.05, 8.0, 0.6, "hz", ["pprops:logarithmic"],
  "Speed of the chorus. Slow is a drift, fast is a vibrato."),
 ("feedback_on", "FEEDBACK ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the anti-Larsen block in and out. Made for a footswitch."),
 ("feedback", "FEEDBACK", 0.0, 100.0, 0.0, "pc", [],
  "Hunts acoustic feedback and notches it out. A bank of filters watches for "
  "a band that rises and then just sits there - which is what howling does "
  "and singing does not - and drops a narrow notch on it. At 0 it is off; "
  "turn it up for a loud stage or a distorted guitar in front of its own "
  "monitor. NOTCHES says how many are in place."),
 ("delay_on", "DELAY ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the delay in and out. It cuts what goes IN, so switching off "
  "lets the repeats ring out instead of chopping them. Made for a footswitch."),
 ("delay_time", "DELAY", 20.0, 2000.0, 400.0, "ms", ["pprops:logarithmic"],
  "Delay time. Two presses of TAP override it; moving this control takes it "
  "back. TIME publishes the value actually in force."),
 ("delay_repeats", "REPEATS", 0.0, 95.0, 30.0, "pc", [],
  "Delay feedback. The repeats lose their top and their bottom each time "
  "round, so a long tail sits behind the voice instead of fighting it."),
 ("delay_mix", "DELAY MIX", 0.0, 100.0, 0.0, "pc", [],
  "How much delay is heard. The delay also feeds the reverb, so repeats are "
  "in the room rather than in front of it."),
 ("reverb_on", "REVERB ON", 0.0, 1.0, 1.0, None, SWITCH,
  "Switches the reverb in and out. Like the delay it cuts the send, so the "
  "tail rings out. Made for a footswitch."),
 ("reverb", "REVERB", 0.0, 100.0, 40.0, "pc", [],
  "Tail length. Moves the size of the room and its damping together, from a "
  "small dry box to a long hall."),
 ("reverb_mix", "REVERB MIX", 0.0, 100.0, 0.0, "pc", [],
  "How much reverb is heard. At 100 the tail sits at the same level as the "
  "voice."),
 ("fx", "FX", 0.0, 1.0, 1.0, None, ["lv2:toggled"],
  "The master switch for all four effects at once. Off: their send is cut "
  "over 40 ms and the tails ring out instead of being chopped. Each effect "
  "also has its own switch; this one is on top of them."),
 ("fx_2", "FX 2", 0.0, 1.0, 1.0, None, ["lv2:toggled"],
  "A second switch on the same FX state, for a second footswitch or a MIDI "
  "controller: a port can only take one addressing, so this doubles FX "
  "rather than replacing it. Either one flips the state; FX STATE publishes "
  "which way it actually is."),
 ("tap", "TAP", 0.0, 1.0, 0.0, None, ["lv2:toggled", "pprops:trigger"],
  "Tap tempo for the delay. Two presses set the time, from 20 to 2000 ms. "
  "Longer than that is treated as a fresh start, not a tempo."),
 ("output", "OUTPUT", -60.0, 12.0, 0.0, "db", [],
  "Output level, after everything. Yours, always: no program moves it. At "
  "-60 dB the plugin is silent."),
]

# What a program overrides, and what stays the player's whatever is
# selected. IN GAIN and OUTPUT are rig levels, the switches are feet.
LIVE = ("program", "user_slot", "save", "in_gain", "output", "fx", "fx_2", "tap")
SWITCHES = ("gate_on", "comp_on", "de_ess_on", "eq_on", "drive_on", "pitch_on",
            "doubler_on", "mod_on", "feedback_on", "delay_on", "reverb_on")
N_USER = 6

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
 ("notches", "NOTCHES", 0.0, 4.0, 0.0, None, [],
  "Output. How many anti-Larsen notches are in place right now. If this sits "
  "at its maximum the stage is fighting you, not the plugin."),
 ("time_out", "TIME", 20.0, 2000.0, 400.0, "ms", [],
  "Output. The delay time in force, in milliseconds, whether it came from "
  "the knob or from the tap."),
]


# Presets. A VoiceLive is used by picking a sound and then adjusting it, and
# a rack of controls at their defaults is not a sound. Each entry below only
# names what it changes; everything else is written out at its default, so
# loading a preset always lands somewhere known rather than on top of half of
# whatever was there before.
#
# Two rules learned the hard way. IN GAIN is never written: it is how loud
# the microphone is, not what the preset sounds like, and a preset that
# moved it added twenty decibels on top of everything else. And every
# effect gets a usable amount even when its switch starts OFF, so the
# footswitch has something to bring in rather than turning on silence.
PRESETS = [
    # key, label (for the preset list), name on the device screen (8 max),
    # and what it changes. Everything else is written at its default.

    # One voice on its own: speaking, reading, presenting.
    ("speech", "Speech", "SPEECH", {
        "low_cut": 120.0, "gate": -45.0, "comp": 30.0, "de_ess": 40.0,
        "mid_freq": 2500.0, "presence": 3.0, "air": 1.0, "drive": 15.0,
        "drive_on": 0.0, "doubler": 20.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 25.0, "mod_on": 0.0, "delay_time": 300.0,
        "delay_repeats": 20.0, "delay_mix": 10.0, "delay_on": 0.0,
        "reverb": 25.0, "reverb_mix": 10.0, "reverb_on": 0.0}),
    ("podcast", "Podcast", "PODCAST", {
        "low_cut": 110.0, "gate": -44.0, "comp": 38.0, "de_ess": 45.0,
        "body": 1.0, "mid_freq": 2400.0, "presence": 2.0, "air": 2.0,
        "drive": 12.0, "drive_on": 0.0, "doubler": 15.0, "doubler_on": 0.0,
        "modulation": 20.0, "mod_on": 0.0, "delay_time": 250.0,
        "delay_repeats": 15.0, "delay_mix": 6.0, "delay_on": 0.0,
        "reverb": 20.0, "reverb_mix": 6.0, "reverb_on": 0.0}),
    ("audiobook", "Audiobook", "AUDIOBK", {
        "low_cut": 85.0, "gate": -50.0, "comp": 30.0, "de_ess": 55.0,
        "body": 1.0, "mid_freq": 1800.0, "presence": 1.0, "air": -1.0,
        "drive": 10.0, "drive_on": 0.0, "doubler": 15.0, "spread": 25.0,
        "voices": 2.0, "doubler_on": 0.0, "modulation": 18.0,
        "mod_on": 0.0, "feedback": 25.0, "feedback_on": 0.0,
        "delay_time": 220.0, "delay_repeats": 12.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 18.0, "reverb_mix": 7.0,
        "reverb_on": 0.0}),
    ("voiceover", "Voice-Over", "VOICEOVR", {
        "low_cut": 120.0, "gate": -42.0, "comp": 38.0, "de_ess": 50.0,
        "body": -2.0, "mid_freq": 3000.0, "presence": 5.0, "air": 3.0,
        "drive": 20.0, "doubler": 18.0, "spread": 30.0, "voices": 2.0,
        "doubler_on": 0.0, "modulation": 20.0, "mod_on": 0.0,
        "feedback": 30.0, "feedback_on": 0.0, "delay_time": 180.0,
        "delay_repeats": 12.0, "delay_mix": 8.0, "delay_on": 0.0,
        "reverb": 22.0, "reverb_mix": 8.0, "reverb_on": 0.0}),
    ("desk", "Desk Mic", "DESK", {
        "low_cut": 150.0, "gate": -38.0, "comp": 34.0, "de_ess": 45.0,
        "body": -3.0, "mid_freq": 2600.0, "presence": 3.0, "air": 1.0,
        "drive": 10.0, "drive_on": 0.0, "doubler": 15.0, "spread": 30.0,
        "voices": 2.0, "doubler_on": 0.0, "modulation": 15.0,
        "mod_on": 0.0, "feedback_on": 1.0, "feedback": 50.0,
        "delay_time": 200.0, "delay_repeats": 12.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 18.0, "reverb_mix": 8.0,
        "reverb_on": 0.0}),
    ("announcer", "Radio Announcer", "ANNOUNCE", {
        "low_cut": 70.0, "gate": -44.0, "comp": 28.0, "de_ess": 55.0,
        "body": 3.0, "mid_freq": 1600.0, "presence": 2.0, "air": 2.0,
        "drive": 14.0, "drive_on": 0.0, "feedback": 30.0,
        "feedback_on": 0.0, "pitch_on": 1.0, "pitch": -3.0,
        "pitch_mix": 100.0, "doubler": 15.0, "voices": 2.0, "spread": 20.0,
        "doubler_on": 0.0, "modulation": 20.0, "mod_speed": 0.5,
        "mod_on": 0.0, "delay_time": 250.0, "delay_repeats": 15.0,
        "delay_mix": 8.0, "delay_on": 0.0, "reverb": 20.0,
        "reverb_mix": 8.0}),
    ("stage", "Stage Dry", "STAGE", {
        "low_cut": 100.0, "gate": -42.0, "comp": 32.0, "de_ess": 30.0,
        "body": -2.0, "presence": 2.0, "air": 2.0, "drive": 20.0,
        "drive_on": 0.0, "doubler": 25.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 30.0, "mod_on": 0.0, "delay_time": 350.0,
        "delay_repeats": 25.0, "delay_mix": 12.0, "delay_on": 0.0,
        "reverb": 35.0, "reverb_mix": 10.0}),

    # Singing in front of a band.
    ("ballad", "Ballad", "BALLAD", {
        "low_cut": 90.0, "gate": -48.0, "comp": 30.0, "de_ess": 30.0,
        "body": 2.0, "air": 2.0, "drive": 15.0, "drive_on": 0.0,
        "doubler": 25.0, "voices": 3.0, "modulation": 30.0, "mod_on": 0.0,
        "delay_time": 420.0, "delay_repeats": 25.0, "delay_mix": 12.0,
        "reverb": 55.0, "reverb_mix": 16.0}),
    ("powerballad", "Power Ballad", "PWRBLLAD", {
        "low_cut": 80.0, "gate": -50.0, "comp": 26.0, "de_ess": 30.0,
        "body": 2.0, "mid_freq": 2000.0, "presence": 1.0, "air": 3.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 28.0, "voices": 3.0,
        "spread": 55.0, "modulation": 28.0, "mod_speed": 0.35,
        "feedback": 25.0, "feedback_on": 0.0, "delay_time": 500.0,
        "delay_repeats": 30.0, "delay_mix": 13.0, "reverb": 70.0,
        "reverb_mix": 18.0}),
    ("crooner", "Warm Crooner", "CROONER", {
        "low_cut": 70.0, "gate": -50.0, "comp": 28.0, "de_ess": 30.0,
        "body": 2.0, "mid_freq": 900.0, "presence": -2.0, "air": 2.0,
        "drive": 18.0, "doubler": 20.0, "spread": 30.0, "voices": 2.0,
        "doubler_on": 0.0, "modulation": 20.0, "mod_on": 0.0,
        "feedback": 25.0, "feedback_on": 0.0, "delay_time": 380.0,
        "delay_repeats": 22.0, "delay_mix": 10.0, "delay_on": 0.0,
        "reverb": 45.0, "reverb_mix": 16.0}),
    ("pop", "Modern Pop", "POP", {
        "low_cut": 110.0, "gate": -44.0, "comp": 36.0, "de_ess": 55.0,
        "body": -1.0, "mid_freq": 3200.0, "presence": 4.0, "air": 5.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 26.0, "spread": 30.0,
        "voices": 2.0, "modulation": 20.0, "mod_on": 0.0, "feedback": 25.0,
        "feedback_on": 0.0, "delay_time": 300.0, "delay_repeats": 20.0,
        "delay_mix": 10.0, "reverb": 35.0, "reverb_mix": 14.0}),
    ("poplead", "Pop Lead", "POPLEAD", {
        "low_cut": 100.0, "gate": -44.0, "comp": 32.0, "de_ess": 55.0,
        "body": 1.0, "mid_freq": 2400.0, "presence": 2.0, "air": 3.0,
        "drive": 18.0, "drive_on": 0.0, "doubler": 30.0, "voices": 2.0,
        "spread": 30.0, "modulation": 25.0, "mod_on": 0.0,
        "feedback": 20.0, "feedback_on": 0.0, "delay_time": 375.0,
        "delay_repeats": 22.0, "delay_mix": 12.0, "reverb": 40.0,
        "reverb_mix": 14.0}),
    ("rock", "Rock", "ROCK", {
        "low_cut": 130.0, "gate": -40.0, "comp": 34.0, "de_ess": 45.0,
        "mid_freq": 3000.0, "presence": 4.0, "air": 1.0, "drive": 22.0,
        "doubler": 25.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 25.0, "mod_on": 0.0, "delay_time": 120.0,
        "delay_repeats": 20.0, "delay_mix": 8.0, "reverb": 30.0,
        "reverb_mix": 10.0}),
    ("rocklead", "Rock Lead", "ROCKLEAD", {
        "low_cut": 120.0, "gate": -40.0, "comp": 34.0, "de_ess": 45.0,
        "body": -1.0, "mid_freq": 3000.0, "presence": 4.0, "air": 1.5,
        "drive": 28.0, "drive_on": 1.0, "doubler": 22.0, "doubler_on": 0.0,
        "voices": 2.0, "spread": 35.0, "modulation": 22.0, "mod_on": 0.0,
        "feedback_on": 1.0, "feedback": 35.0, "delay_time": 110.0,
        "delay_repeats": 18.0, "delay_mix": 10.0, "reverb": 28.0,
        "reverb_mix": 9.0}),
    ("shout", "Hard Rock Shout", "SHOUT", {
        "low_cut": 150.0, "gate": -36.0, "comp": 38.0, "de_ess": 60.0,
        "body": -3.0, "mid_freq": 3200.0, "presence": 5.0, "drive": 45.0,
        "drive_on": 1.0, "doubler": 22.0, "doubler_on": 0.0, "voices": 2.0,
        "spread": 25.0, "modulation": 20.0, "mod_on": 0.0,
        "feedback": 60.0, "feedback_on": 1.0, "delay_time": 90.0,
        "delay_repeats": 10.0, "delay_mix": 10.0, "delay_on": 0.0,
        "reverb": 18.0, "reverb_mix": 6.0}),
    ("country", "Country", "COUNTRY", {
        "low_cut": 110.0, "gate": -42.0, "comp": 32.0, "de_ess": 40.0,
        "body": -1.0, "mid_freq": 2800.0, "presence": 3.0, "air": 3.0,
        "drive": 20.0, "drive_on": 1.0, "doubler": 20.0, "doubler_on": 0.0,
        "voices": 2.0, "spread": 40.0, "modulation": 20.0, "mod_on": 0.0,
        "feedback": 25.0, "feedback_on": 1.0, "delay_time": 130.0,
        "delay_repeats": 6.0, "delay_mix": 20.0, "reverb": 30.0,
        "reverb_mix": 10.0}),
    ("cutthru", "Cut Through", "CUTTHRU", {
        "low_cut": 160.0, "gate": -38.0, "comp": 40.0, "de_ess": 50.0,
        "body": -5.0, "mid_freq": 3500.0, "presence": 6.0, "air": -1.0,
        "drive": 25.0, "drive_on": 0.0, "doubler": 22.0, "doubler_on": 0.0,
        "voices": 2.0, "spread": 30.0, "modulation": 18.0, "mod_on": 0.0,
        "feedback": 70.0, "feedback_on": 1.0, "delay_time": 100.0,
        "delay_repeats": 12.0, "delay_mix": 10.0, "delay_on": 0.0,
        "reverb": 25.0, "reverb_mix": 10.0, "reverb_on": 0.0}),
    ("whisper", "Whisper", "WHISPER", {
        "low_cut": 140.0, "gate": -52.0, "comp": 45.0, "de_ess": 45.0,
        "body": -3.0, "mid_freq": 3200.0, "presence": 2.0, "air": 5.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 30.0, "voices": 2.0,
        "spread": 40.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 320.0, "delay_repeats": 20.0, "delay_mix": 10.0,
        "delay_on": 0.0, "reverb": 45.0, "reverb_mix": 20.0}),

    # Doubling yourself, and the choir.
    ("tight", "Tight Double", "TIGHT", {
        "low_cut": 95.0, "gate": -46.0, "comp": 32.0, "de_ess": 35.0,
        "mid_freq": 2600.0, "presence": 2.0, "air": 2.0, "drive": 15.0,
        "drive_on": 0.0, "doubler": 45.0, "voices": 2.0, "spread": 15.0,
        "modulation": 20.0, "mod_on": 0.0, "delay_time": 300.0,
        "delay_repeats": 20.0, "delay_mix": 8.0, "delay_on": 0.0,
        "reverb": 30.0, "reverb_mix": 10.0}),
    ("stagedouble", "Stage Double", "STAGEDBL", {
        "low_cut": 110.0, "gate": -38.0, "comp": 32.0, "de_ess": 35.0,
        "mid_freq": 2800.0, "presence": 3.0, "air": 1.0, "drive": 16.0,
        "drive_on": 0.0, "feedback_on": 1.0, "feedback": 55.0,
        "doubler": 40.0, "voices": 2.0, "spread": 30.0, "modulation": 20.0,
        "mod_on": 0.0, "delay_time": 300.0, "delay_repeats": 20.0,
        "delay_mix": 8.0, "delay_on": 0.0, "reverb": 30.0,
        "reverb_mix": 10.0}),
    ("wide", "Wide", "WIDE", {
        "low_cut": 90.0, "gate": -48.0, "comp": 30.0, "de_ess": 30.0,
        "air": 3.0, "drive": 15.0, "drive_on": 0.0, "doubler": 55.0,
        "voices": 3.0, "modulation": 40.0, "mod_speed": 0.4,
        "delay_time": 400.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 60.0, "reverb_mix": 20.0}),
    ("backing", "Backing Vocals", "BACKING", {
        "low_cut": 140.0, "gate": -40.0, "comp": 34.0, "de_ess": 45.0,
        "body": -3.0, "mid_freq": 1500.0, "presence": -2.0, "air": 2.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 55.0, "voices": 4.0,
        "spread": 75.0, "modulation": 35.0, "mod_speed": 0.5,
        "feedback": 30.0, "feedback_on": 1.0, "delay_time": 280.0,
        "delay_repeats": 18.0, "delay_mix": 10.0, "delay_on": 0.0,
        "reverb": 55.0, "reverb_mix": 18.0}),
    ("stack", "Stacked Backing", "STACK", {
        "low_cut": 120.0, "gate": -44.0, "comp": 30.0, "de_ess": 45.0,
        "body": -3.0, "mid_freq": 3000.0, "presence": 2.0, "air": 4.0,
        "drive": 12.0, "drive_on": 0.0, "doubler": 55.0, "spread": 75.0,
        "voices": 4.0, "modulation": 30.0, "mod_speed": 0.45,
        "feedback": 25.0, "feedback_on": 0.0, "delay_time": 320.0,
        "delay_repeats": 18.0, "delay_mix": 8.0, "delay_on": 0.0,
        "reverb": 50.0, "reverb_mix": 16.0}),
    ("choir", "Choir", "CHOIR", {
        "low_cut": 100.0, "gate": -46.0, "comp": 28.0, "de_ess": 30.0,
        "body": 1.0, "air": 2.0, "drive": 15.0, "drive_on": 0.0,
        "doubler": 60.0, "voices": 4.0, "modulation": 45.0,
        "mod_speed": 0.35, "delay_time": 350.0, "delay_repeats": 20.0,
        "delay_mix": 8.0, "delay_on": 0.0, "reverb": 70.0,
        "reverb_mix": 18.0}),
    ("widechoir", "Wide Choir", "WIDECHOR", {
        "low_cut": 100.0, "gate": -48.0, "comp": 28.0, "de_ess": 35.0,
        "mid_freq": 2400.0, "presence": 1.0, "air": 3.0, "drive": 15.0,
        "drive_on": 0.0, "doubler": 65.0, "voices": 4.0, "spread": 100.0,
        "modulation": 35.0, "mod_speed": 0.3, "delay_time": 450.0,
        "delay_repeats": 25.0, "delay_mix": 10.0, "delay_on": 0.0,
        "reverb": 72.0, "reverb_mix": 22.0}),
    ("gospel", "Gospel Choir", "GOSPEL", {
        "low_cut": 95.0, "gate": -46.0, "comp": 24.0, "de_ess": 30.0,
        "body": 2.0, "mid_freq": 2000.0, "presence": 2.0, "air": 3.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 60.0, "voices": 4.0,
        "spread": 85.0, "modulation": 30.0, "mod_speed": 0.35,
        "delay_time": 400.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 65.0, "reverb_mix": 16.0}),
    ("gospelstack", "Gospel Stack", "STACK", {
        "low_cut": 110.0, "gate": -44.0, "comp": 24.0, "de_ess": 30.0,
        "body": 1.0, "mid_freq": 2000.0, "presence": 3.0, "air": 2.0,
        "drive": 20.0, "feedback": 40.0, "doubler": 58.0, "voices": 4.0,
        "spread": 70.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 330.0, "delay_repeats": 22.0, "delay_mix": 10.0,
        "reverb": 55.0, "reverb_mix": 14.0}),
    ("angel", "Angel Choir", "ANGEL", {
        "low_cut": 110.0, "gate": -48.0, "comp": 26.0, "de_ess": 35.0,
        "air": 4.0, "mid_freq": 3000.0, "presence": 1.0, "drive": 15.0,
        "drive_on": 0.0, "pitch": 12.0, "pitch_mix": 22.0, "doubler": 65.0,
        "voices": 4.0, "spread": 95.0, "modulation": 35.0,
        "mod_speed": 0.3, "delay_time": 550.0, "delay_repeats": 35.0,
        "delay_mix": 12.0, "reverb": 90.0, "reverb_mix": 24.0}),
    ("seraphim", "Seraphim", "SERAPHIM", {
        "low_cut": 115.0, "gate": -48.0, "comp": 26.0, "de_ess": 40.0,
        "body": -1.0, "mid_freq": 3200.0, "air": 5.0, "drive": 14.0,
        "drive_on": 0.0, "doubler": 70.0, "voices": 4.0, "spread": 90.0,
        "modulation": 30.0, "mod_speed": 0.25, "delay_time": 600.0,
        "delay_repeats": 35.0, "delay_mix": 11.0, "reverb": 95.0,
        "reverb_mix": 28.0}),

    # Somebody else's voice: the pitch, with no detection anywhere.
    ("baritone", "Baritone", "BARITONE", {
        "low_cut": 80.0, "gate": -46.0, "comp": 30.0, "de_ess": 25.0,
        "body": 3.0, "mid_freq": 1200.0, "presence": 1.0, "pitch": -4.0,
        "pitch_mix": 100.0, "doubler": 20.0, "voices": 2.0,
        "doubler_on": 0.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 400.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 35.0, "reverb_mix": 10.0}),
    ("tenor", "Tenor", "TENOR", {
        "low_cut": 100.0, "gate": -46.0, "comp": 30.0, "de_ess": 35.0,
        "mid_freq": 2600.0, "presence": 2.0, "air": 2.0, "pitch": 3.0,
        "pitch_mix": 100.0, "doubler": 20.0, "voices": 2.0,
        "doubler_on": 0.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 350.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 35.0, "reverb_mix": 10.0}),
    ("helium", "Helium", "HELIUM", {
        "low_cut": 150.0, "gate": -44.0, "comp": 36.0, "de_ess": 30.0,
        "mid_freq": 3000.0, "presence": 3.0, "pitch": 9.0,
        "pitch_mix": 100.0, "doubler": 20.0, "voices": 2.0,
        "doubler_on": 0.0, "modulation": 30.0, "mod_on": 0.0,
        "delay_time": 250.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 20.0, "reverb_mix": 8.0}),
    ("octave", "Octave", "OCTAVE", {
        "low_cut": 90.0, "gate": -46.0, "comp": 36.0, "de_ess": 30.0,
        "body": 2.0, "mid_freq": 2200.0, "pitch": -12.0, "pitch_mix": 35.0,
        "doubler": 20.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 25.0, "mod_on": 0.0, "delay_time": 400.0,
        "delay_repeats": 20.0, "delay_mix": 8.0, "delay_on": 0.0,
        "reverb": 30.0, "reverb_mix": 12.0}),
    ("octavebelow", "Octave Below", "OCTBELOW", {
        "low_cut": 75.0, "gate": -46.0, "comp": 32.0, "de_ess": 25.0,
        "body": 4.0, "mid_freq": 1400.0, "presence": 1.0, "pitch": -12.0,
        "pitch_mix": 40.0, "drive": 15.0, "drive_on": 0.0, "doubler": 32.0,
        "voices": 2.0, "spread": 25.0, "modulation": 20.0, "mod_on": 0.0,
        "delay_time": 380.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 35.0, "reverb_mix": 10.0}),
    ("fifthbelow", "Fifth Below", "FIFTH", {
        "low_cut": 70.0, "gate": -42.0, "comp": 30.0, "de_ess": 15.0,
        "body": 3.0, "mid_freq": 900.0, "presence": -2.0, "air": -4.0,
        "pitch": -7.0, "pitch_mix": 65.0, "drive": 35.0, "doubler": 40.0,
        "voices": 3.0, "spread": 70.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 400.0, "delay_repeats": 25.0, "delay_mix": 9.0,
        "delay_on": 0.0, "reverb": 50.0, "reverb_mix": 14.0}),
    ("monster", "Monster", "MONSTER", {
        "low_cut": 70.0, "gate": -40.0, "comp": 28.0, "de_ess": 20.0,
        "body": 2.0, "mid_freq": 900.0, "presence": -3.0, "air": -6.0,
        "pitch": -8.0, "pitch_mix": 100.0, "drive": 20.0, "doubler": 25.0,
        "voices": 2.0, "spread": 70.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 450.0, "delay_repeats": 25.0, "delay_mix": 10.0,
        "delay_on": 0.0, "reverb": 55.0, "reverb_mix": 18.0}),
    ("robot", "Robot", "ROBOT", {
        "low_cut": 200.0, "gate": -38.0, "comp": 40.0, "de_ess": 20.0,
        "body": -6.0, "mid_freq": 1600.0, "presence": 6.0, "air": -6.0,
        "drive": 55.0, "pitch": -5.0, "pitch_mix": 60.0, "doubler": 40.0,
        "voices": 3.0, "spread": 10.0, "modulation": 55.0,
        "mod_speed": 6.0, "delay_time": 90.0, "delay_repeats": 30.0,
        "delay_mix": 10.0, "reverb": 20.0, "reverb_mix": 8.0}),
    ("alien", "Alien", "ALIEN", {
        "low_cut": 180.0, "gate": -42.0, "comp": 52.0, "de_ess": 25.0,
        "body": -6.0, "mid_freq": 3200.0, "presence": 4.0, "air": 4.0,
        "drive": 25.0, "pitch_on": 1.0, "pitch": 7.0, "pitch_mix": 55.0,
        "feedback": 30.0, "feedback_on": 0.0, "doubler": 40.0,
        "voices": 4.0, "spread": 100.0, "modulation": 65.0,
        "mod_speed": 3.5, "mod_on": 1.0, "delay_time": 40.0,
        "delay_repeats": 40.0, "delay_mix": 15.0, "reverb": 70.0,
        "reverb_mix": 22.0}),

    # Coming out of a grille or a loudspeaker.
    ("hygiaphone", "Hygiaphone", "HYGIAPH", {
        "low_cut": 320.0, "gate": -42.0, "comp": 45.0, "de_ess": 20.0,
        "body": -12.0, "mid_freq": 1800.0, "presence": 9.0, "air": -12.0,
        "drive": 40.0, "doubler": 15.0, "doubler_on": 0.0,
        "modulation": 20.0, "mod_on": 0.0, "delay_time": 180.0,
        "delay_repeats": 15.0, "delay_mix": 8.0, "delay_on": 0.0,
        "reverb": 15.0, "reverb_mix": 10.0}),
    ("telephone", "Telephone", "PHONE", {
        "low_cut": 400.0, "gate": -40.0, "comp": 58.0, "de_ess": 25.0,
        "body": -12.0, "mid_freq": 2400.0, "presence": 6.0, "air": -12.0,
        "drive": 20.0, "doubler": 15.0, "doubler_on": 0.0,
        "modulation": 20.0, "mod_on": 0.0, "delay_time": 200.0,
        "delay_repeats": 15.0, "delay_mix": 8.0, "delay_on": 0.0,
        "reverb": 15.0, "reverb_mix": 8.0, "reverb_on": 0.0}),
    ("megaphone", "Megaphone", "MEGAPHON", {
        "low_cut": 380.0, "gate": -38.0, "comp": 34.0, "de_ess": 20.0,
        "body": -10.0, "mid_freq": 1500.0, "presence": 10.0, "air": -10.0,
        "drive": 40.0, "doubler": 15.0, "doubler_on": 0.0,
        "modulation": 25.0, "mod_on": 0.0, "delay_time": 120.0,
        "delay_repeats": 20.0, "delay_mix": 10.0, "reverb": 25.0,
        "reverb_mix": 12.0}),
    ("walkie", "Walkie Talkie", "WALKIE", {
        "low_cut": 400.0, "gate": -34.0, "comp": 34.0, "de_ess": 20.0,
        "body": -12.0, "mid_freq": 2000.0, "presence": 8.0, "air": -12.0,
        "drive": 44.0, "doubler": 15.0, "doubler_on": 0.0,
        "modulation": 20.0, "mod_on": 0.0, "delay_time": 100.0,
        "delay_repeats": 10.0, "delay_mix": 6.0, "delay_on": 0.0,
        "reverb": 12.0, "reverb_mix": 6.0, "reverb_on": 0.0}),
    ("radio", "Radio", "RADIO", {
        "low_cut": 250.0, "gate": -42.0, "comp": 40.0, "de_ess": 40.0,
        "body": -8.0, "mid_freq": 1500.0, "presence": 6.0, "air": -6.0,
        "drive": 50.0, "doubler": 20.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 25.0, "mod_on": 0.0, "delay_time": 200.0,
        "delay_repeats": 20.0, "delay_mix": 8.0, "delay_on": 0.0,
        "reverb": 20.0, "reverb_mix": 8.0, "reverb_on": 0.0}),

    # Time: echoes and rooms.
    ("slap", "Slapback", "SLAP", {
        "low_cut": 120.0, "gate": -42.0, "comp": 32.0, "de_ess": 35.0,
        "mid_freq": 2800.0, "presence": 3.0, "drive": 25.0,
        "doubler": 20.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 25.0, "mod_on": 0.0, "delay_time": 95.0,
        "delay_repeats": 8.0, "delay_mix": 18.0, "reverb": 20.0,
        "reverb_mix": 8.0}),
    ("tapeslap", "Tape Slap", "TAPESLAP", {
        "low_cut": 120.0, "gate": -42.0, "comp": 32.0, "de_ess": 35.0,
        "body": -1.0, "mid_freq": 2800.0, "presence": 3.0, "air": 1.0,
        "drive": 28.0, "doubler": 20.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 20.0, "mod_speed": 0.5, "mod_on": 0.0,
        "feedback_on": 0.0, "feedback": 40.0, "delay_time": 110.0,
        "delay_repeats": 12.0, "delay_mix": 22.0, "reverb": 20.0,
        "reverb_mix": 8.0}),
    ("eighths", "Eighth Notes", "EIGHTHS", {
        "low_cut": 110.0, "gate": -44.0, "comp": 32.0, "de_ess": 35.0,
        "mid_freq": 2800.0, "presence": 3.0, "air": 2.0, "drive": 16.0,
        "drive_on": 0.0, "doubler": 20.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 20.0, "mod_on": 0.0, "feedback_on": 0.0,
        "feedback": 40.0, "delay_time": 250.0, "delay_repeats": 45.0,
        "delay_mix": 20.0, "reverb": 30.0, "reverb_mix": 10.0}),
    ("dub", "Dub", "DUB", {
        "low_cut": 100.0, "gate": -44.0, "comp": 30.0, "de_ess": 30.0,
        "body": 2.0, "mid_freq": 1800.0, "air": 1.0, "drive": 16.0,
        "doubler": 20.0, "doubler_on": 0.0, "modulation": 30.0,
        "mod_speed": 0.3, "delay_time": 480.0, "delay_repeats": 75.0,
        "delay_mix": 14.0, "reverb": 60.0, "reverb_mix": 13.0}),
    ("dubecho", "Dub Echo", "DUBECHO", {
        "low_cut": 110.0, "gate": -44.0, "comp": 26.0, "de_ess": 30.0,
        "body": 2.0, "mid_freq": 1600.0, "presence": -1.0, "air": -2.0,
        "drive": 26.0, "doubler": 20.0, "voices": 2.0, "doubler_on": 0.0,
        "modulation": 30.0, "mod_speed": 0.25, "feedback_on": 0.0,
        "feedback": 35.0, "delay_time": 520.0, "delay_repeats": 78.0,
        "delay_mix": 20.0, "reverb": 60.0, "reverb_mix": 14.0}),
    ("ambient", "Ambient", "AMBIENT", {
        "low_cut": 90.0, "gate": -50.0, "comp": 24.0, "de_ess": 30.0,
        "air": 3.0, "drive": 15.0, "drive_on": 0.0, "doubler": 30.0,
        "voices": 3.0, "modulation": 30.0, "mod_speed": 0.3,
        "delay_time": 700.0, "delay_repeats": 45.0, "delay_mix": 13.0,
        "reverb": 85.0, "reverb_mix": 24.0}),
    ("wash", "Ambient Wash", "WASH", {
        "low_cut": 120.0, "gate": -52.0, "comp": 22.0, "de_ess": 30.0,
        "body": -2.0, "mid_freq": 3000.0, "presence": -1.0, "air": 4.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 35.0, "voices": 3.0,
        "spread": 75.0, "modulation": 40.0, "mod_speed": 0.2,
        "feedback_on": 0.0, "feedback": 30.0, "delay_time": 900.0,
        "delay_repeats": 60.0, "delay_mix": 16.0, "reverb": 95.0,
        "reverb_mix": 32.0}),
    ("arena", "Arena", "ARENA", {
        "low_cut": 105.0, "gate": -44.0, "comp": 34.0, "de_ess": 35.0,
        "mid_freq": 2800.0, "presence": 3.0, "air": 2.0, "drive": 18.0,
        "drive_on": 0.0, "doubler": 30.0, "voices": 3.0, "spread": 60.0,
        "modulation": 25.0, "mod_on": 0.0, "delay_time": 500.0,
        "delay_repeats": 30.0, "delay_mix": 12.0, "reverb": 75.0,
        "reverb_mix": 20.0}),
    ("stadium", "Stadium", "STADIUM", {
        "low_cut": 110.0, "gate": -40.0, "comp": 32.0, "de_ess": 35.0,
        "mid_freq": 2800.0, "presence": 3.0, "air": 2.0, "drive": 18.0,
        "drive_on": 0.0, "doubler": 25.0, "voices": 3.0, "spread": 55.0,
        "doubler_on": 0.0, "modulation": 25.0, "mod_on": 0.0,
        "feedback_on": 1.0, "feedback": 60.0, "delay_time": 450.0,
        "delay_repeats": 28.0, "delay_mix": 12.0, "reverb": 80.0,
        "reverb_mix": 20.0}),
    ("cathedral", "Cathedral", "CATHEDRL", {
        "low_cut": 110.0, "gate": -46.0, "comp": 26.0, "de_ess": 35.0,
        "air": 2.0, "drive": 15.0, "drive_on": 0.0, "doubler": 25.0,
        "voices": 3.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 600.0, "delay_repeats": 45.0, "delay_mix": 11.0,
        "reverb": 100.0, "reverb_mix": 22.0}),
    ("church", "Church", "CHURCH", {
        "low_cut": 100.0, "gate": -50.0, "comp": 26.0, "de_ess": 30.0,
        "body": 1.0, "mid_freq": 2400.0, "presence": 1.0, "air": 3.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 20.0, "voices": 3.0,
        "doubler_on": 0.0, "modulation": 20.0, "mod_on": 0.0,
        "feedback_on": 1.0, "feedback": 45.0, "delay_time": 380.0,
        "delay_repeats": 25.0, "delay_mix": 10.0, "delay_on": 0.0,
        "reverb": 70.0, "reverb_mix": 26.0}),
    ("basilica", "Basilica", "BASILICA", {
        "low_cut": 120.0, "gate": -46.0, "comp": 24.0, "de_ess": 35.0,
        "body": -1.0, "mid_freq": 2600.0, "presence": 1.0, "air": 2.0,
        "drive": 15.0, "drive_on": 0.0, "doubler": 25.0, "voices": 3.0,
        "spread": 60.0, "doubler_on": 0.0, "modulation": 25.0,
        "mod_on": 0.0, "feedback_on": 1.0, "feedback": 40.0,
        "delay_time": 750.0, "delay_repeats": 55.0, "delay_mix": 12.0,
        "reverb": 100.0, "reverb_mix": 26.0}),
    ("shimmer", "Shimmer", "SHIMMER", {
        "low_cut": 130.0, "gate": -50.0, "comp": 24.0, "de_ess": 35.0,
        "mid_freq": 3000.0, "presence": 1.0, "air": 4.0, "drive": 15.0,
        "drive_on": 0.0, "pitch": 12.0, "pitch_mix": 18.0, "doubler": 25.0,
        "voices": 2.0, "spread": 50.0, "doubler_on": 0.0,
        "modulation": 30.0, "mod_speed": 0.2, "feedback_on": 0.0,
        "feedback": 30.0, "delay_time": 800.0, "delay_repeats": 55.0,
        "delay_mix": 14.0, "reverb": 100.0, "reverb_mix": 30.0}),

    # An instrument in place of the microphone.
    ("solo", "Guitar Solo", "SOLO", {
        "low_cut": 95.0, "gate": -38.0, "comp": 8.0, "de_ess": 15.0,
        "mid_freq": 2600.0, "presence": 4.0, "air": 1.0, "drive": 42.0,
        "feedback": 70.0, "doubler": 25.0, "voices": 2.0, "spread": 35.0,
        "doubler_on": 0.0, "modulation": 25.0, "mod_on": 0.0,
        "delay_time": 380.0, "delay_repeats": 30.0, "delay_mix": 14.0,
        "reverb": 45.0, "reverb_mix": 16.0}),
    ("lead_solo", "Lead Solo", "SOLO", {
        "low_cut": 150.0, "gate": -34.0, "comp": 18.0, "de_ess_on": 0.0,
        "de_ess": 30.0, "body": -5.0, "mid_freq": 2400.0, "presence": 5.0,
        "air": -4.0, "drive": 60.0, "feedback_on": 1.0, "feedback": 70.0,
        "doubler_on": 0.0, "doubler": 25.0, "voices": 2.0, "spread": 30.0,
        "mod_on": 0.0, "modulation": 25.0, "mod_speed": 0.5,
        "delay_time": 420.0, "delay_repeats": 32.0, "delay_mix": 16.0,
        "reverb": 45.0, "reverb_mix": 12.0}),
    ("crunch", "Guitar Crunch", "CRUNCH", {
        "low_cut": 110.0, "gate": -36.0, "comp": 18.0, "de_ess": 10.0,
        "body": -2.0, "mid_freq": 1400.0, "presence": 2.0, "drive": 45.0,
        "feedback": 45.0, "doubler": 20.0, "voices": 2.0,
        "doubler_on": 0.0, "modulation": 20.0, "mod_on": 0.0,
        "delay_time": 300.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "delay_on": 0.0, "reverb": 25.0, "reverb_mix": 10.0}),
    ("clean", "Guitar Clean", "CLEAN", {
        "low_cut": 90.0, "gate": -44.0, "comp": 25.0, "de_ess": 10.0,
        "mid_freq": 1200.0, "presence": -2.0, "air": 3.0, "drive": 12.0,
        "drive_on": 0.0, "feedback": 35.0, "doubler": 30.0, "voices": 2.0,
        "spread": 60.0, "modulation": 35.0, "mod_speed": 0.5,
        "delay_time": 420.0, "delay_repeats": 25.0, "delay_mix": 12.0,
        "reverb": 50.0, "reverb_mix": 18.0}),
    ("chime", "Clean Chime", "CHIME", {
        "gate": -48.0, "comp": 32.0, "de_ess_on": 0.0, "de_ess": 25.0,
        "body": 1.0, "mid_freq": 3200.0, "presence": -2.0, "air": 5.0,
        "drive_on": 0.0, "drive": 18.0, "pitch_on": 0.0, "pitch": 12.0,
        "pitch_mix": 30.0, "feedback_on": 0.0, "feedback": 30.0,
        "doubler_on": 0.0, "doubler": 30.0, "voices": 2.0, "spread": 45.0,
        "modulation": 35.0, "mod_speed": 0.45, "delay_time": 480.0,
        "delay_mix": 14.0, "reverb": 55.0, "reverb_mix": 18.0}),
    ("acoustic", "Acoustic Piezo", "ACOUSTC", {
        "low_cut": 100.0, "gate": -50.0, "comp": 34.0, "de_ess_on": 0.0,
        "de_ess": 25.0, "body": -3.0, "mid_freq": 3000.0, "presence": -2.0,
        "air": 4.0, "drive_on": 0.0, "drive": 12.0, "feedback_on": 1.0,
        "feedback": 60.0, "doubler_on": 0.0, "doubler": 25.0,
        "voices": 2.0, "mod_on": 0.0, "modulation": 25.0, "mod_speed": 0.5,
        "delay_on": 0.0, "delay_repeats": 20.0, "delay_mix": 10.0,
        "reverb": 45.0, "reverb_mix": 16.0}),
    ("bass", "Bass DI", "BASS", {
        "low_cut": 30.0, "gate": -46.0, "comp": 28.0, "de_ess_on": 0.0,
        "de_ess": 20.0, "body": 1.0, "mid_freq": 1000.0, "presence": 1.0,
        "air": -4.0, "drive": 18.0, "pitch_on": 0.0, "pitch": -12.0,
        "pitch_mix": 30.0, "feedback_on": 0.0, "feedback": 25.0,
        "doubler_on": 0.0, "doubler": 20.0, "voices": 2.0, "spread": 20.0,
        "mod_on": 0.0, "modulation": 20.0, "mod_speed": 0.5,
        "delay_on": 0.0, "delay_time": 300.0, "delay_repeats": 15.0,
        "delay_mix": 8.0, "reverb_on": 0.0, "reverb": 20.0,
        "reverb_mix": 8.0}),
    ("harp", "Harmonica", "HARP", {
        "low_cut": 160.0, "gate": -34.0, "comp": 34.0, "de_ess_on": 0.0,
        "de_ess": 30.0, "body": -4.0, "mid_freq": 1600.0, "presence": 4.0,
        "air": -5.0, "drive": 50.0, "feedback_on": 1.0, "feedback": 75.0,
        "doubler_on": 0.0, "doubler": 20.0, "voices": 2.0, "spread": 30.0,
        "mod_on": 0.0, "modulation": 25.0, "mod_speed": 0.5,
        "delay_time": 110.0, "delay_repeats": 18.0, "delay_mix": 15.0,
        "reverb": 30.0, "reverb_mix": 10.0}),
    ("sax", "Saxophone", "SAX", {
        "low_cut": 80.0, "gate": -46.0, "comp": 28.0, "de_ess": 35.0,
        "body": 2.0, "mid_freq": 2600.0, "presence": -2.0, "air": 2.0,
        "drive_on": 0.0, "drive": 18.0, "feedback_on": 1.0,
        "feedback": 35.0, "doubler_on": 0.0, "doubler": 25.0,
        "voices": 2.0, "spread": 40.0, "mod_on": 0.0, "modulation": 25.0,
        "mod_speed": 0.5, "delay_on": 0.0, "delay_time": 380.0,
        "delay_repeats": 25.0, "delay_mix": 12.0, "reverb": 60.0,
        "reverb_mix": 20.0}),
    ("rotary", "Rotary Keys", "ROTARY", {
        "low_cut": 70.0, "gate": -50.0, "comp": 28.0, "de_ess_on": 0.0,
        "de_ess": 20.0, "mid_freq": 1200.0, "presence": 1.0, "air": 2.0,
        "drive": 20.0, "feedback_on": 0.0, "feedback": 20.0,
        "doubler_on": 0.0, "doubler": 30.0, "spread": 60.0,
        "modulation": 58.0, "mod_speed": 5.5, "delay_on": 0.0,
        "delay_time": 350.0, "delay_repeats": 20.0, "delay_mix": 8.0,
        "reverb_mix": 14.0}),
]

# Scale points: the lists a knob walks through on the device.
SCALE = {
    "voices": [("2 voices", 2.0), ("3 voices", 3.0), ("4 voices", 4.0)],
    "user_slot": [("User %d" % (i + 1), float(i + 1)) for i in range(N_USER)],
    "program": ([("Manual", 0.0)]
                + [(p[1], float(i + 1)) for i, p in enumerate(PRESETS)]
                + [("User %d" % (i + 1), float(len(PRESETS) + 1 + i))
                   for i in range(N_USER)]),
}

# Triggers are left out on purpose: a preset that presses TAP would set a
# tempo the moment it loads.
PRESET_SKIP = ("save", "tap")

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
        for key, label, _short, values in PRESETS:
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
    rdfs:seeAlso <voice.ttl> ,
                 <modgui.ttl> .

<http://remy-live.github.io/lv2/voice#stereo>
    a lv2:Plugin ;
    lv2:binary <voice.so> ;
    rdfs:seeAlso <voice_stereo.ttl> ,
                 <modgui.ttl> .
"""


def write_manifest(path):
    out = [MANIFEST_HEAD]
    for uri, suffix in PLUGINS:
        for key, _label, _short, _values in PRESETS:
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


def scale_block(symbol):
    """The list a knob walks through, if this control has one."""
    pts = SCALE.get(symbol)
    if not pts:
        return []
    out = []
    for label, value in pts:
        out.append('            [ rdfs:label "%s" ; rdf:value %s ]'
                   % (label, fmt(value)))
    return ["        lv2:scalePoint\n" + " ,\n".join(out) + " ;"]


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
    out += scale_block(symbol)
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


PROGRAM_H = """/* Generated by make_ttl.py - DO NOT EDIT.
 *
 * The built-in programs, as the plugin reads them. The same numbers go
 * into presets.ttl, where they are written to ports instead; a program
 * selected on the PROGRAM control and the preset of the same name loaded
 * from the list must sound identical, and the test bench checks exactly
 * that by running both and subtracting.
 *
 * Regenerate with:  python3 make_ttl.py
 */

#define N_PROGRAM     %(n_program)d
#define N_PROGRAM_COL %(n_col)d
/* Four more slots the player fills in, saved with the pedalboard. They sit
   after the built-in programs on the same list. */
#define N_USER        %(n_user)d
#define N_SWITCH_SAVED %(n_switch)d

/* Eight characters at most: the device truncates silently. */
static const char* const program_name[N_PROGRAM] = {
%(names)s};

/* Which column of program_value holds a control, -1 for the ones a
   program never touches: the rig levels and the switches. */
static const int8_t program_col[CTL_COUNT] = {
%(cols)s};

static const float program_value[N_PROGRAM][N_PROGRAM_COL] = {
%(values)s};

/* Switch positions, in the order of SwitchIndex. A program ADOPTS these
   when it is selected and then lets go: a foot on a switch must always
   win, or a footswitch stops working the moment a program is chosen. */
static const uint8_t program_switch[N_PROGRAM][%(n_switch)d] = {
%(switches)s};
"""


def write_programs(path):
    cont = [c for c in CONTROLS if c[0] not in LIVE and c[0] not in SWITCHES]
    col = {}
    for i, c in enumerate(cont):
        col[c[0]] = i

    names = '    "MANUAL",\n'
    for _k, _label, short, _v in PRESETS:
        if len(short) > 8 or short != short.upper():
            raise SystemExit("program name %r must be 8 upper-case characters "
                             "or fewer" % short)
        names += '    "%s",\n' % short

    cols = ""
    for symbol, _n, _mn, _mx, _d, _u, _p, _c in CONTROLS:
        cols += "    %d,   /* %s */\n" % (col.get(symbol, -1), symbol)
    for symbol, _n, _mn, _mx, _d, _u, _p, _c in OUTPUTS:
        cols += "    -1,  /* %s */\n" % symbol

    def row(values):
        out = []
        for c in cont:
            symbol, _n, mn, mx, default, _u, _p, _cm = c
            v = values.get(symbol, default)
            if not mn <= v <= mx:
                raise SystemExit("%s = %s is outside %s..%s" % (symbol, v, mn, mx))
            out.append("%sf" % fmt(v))
        return "    { " + ", ".join(out) + " },\n"

    values = row({})                      # MANUAL, never read
    for _k, _label, _s, v in PRESETS:
        values += row(v)

    def srow(v):
        return ("    { " + ", ".join("%d" % int(v.get(s, 1.0)) for s in SWITCHES)
                + " },\n")

    switches = srow({})
    for _k, _label, _s, v in PRESETS:
        switches += srow(v)

    open(path, "w").write(PROGRAM_H % {
        "n_program": len(PRESETS) + 1, "n_col": len(cont),
        "n_user": N_USER,
        "n_switch": len(SWITCHES), "names": names, "cols": cols,
        "values": values, "switches": switches})
    return len(cont)


if __name__ == "__main__":
    n = write("voice.ttl", "http://remy-live.github.io/lv2/voice",
              "Voice", "VOICE", MONO_AUDIO)
    m = write("voice_stereo.ttl", "http://remy-live.github.io/lv2/voice#stereo",
              "Voice Stereo", "VOICE ST", STEREO_AUDIO)
    p = write_presets("presets.ttl")
    write_manifest("manifest.ttl")
    c = write_programs("programs.h")
    print("voice.ttl: %d ports, voice_stereo.ttl: %d ports, presets.ttl: %d,"
          " programs.h: %d programs x %d controls"
          % (n, m, p, len(PRESETS) + 1, c))
    print("PROGRAM must run 0..%d in voice.c's ctl_spec"
          % (len(PRESETS) + N_USER))
    sys.exit(0)
