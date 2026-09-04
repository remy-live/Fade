# Voice

A vocal channel strip and effects rack for the [MOD Dwarf](https://mod.audio).
Two plugins in one bundle: **Voice** (1 in, 1 out) and **Voice Stereo**
(2 in, 2 out).

## Why

A VoiceLive does two jobs at once. It follows the *pitch* of the voice to
build harmonies, and around that it runs a channel strip and an effects
rack. The pitch half is the half that fails on stage: it wants a guide
chord or a key, it smears on consonants, and it goes wrong most where the
stage is loudest.

This is the other half, on purpose. There is no pitch detection anywhere
in it — nothing to track, so nothing to mistrack. Every block works on
level and time alone, which is why none of it needs to know what note is
being sung, and why it behaves the same on a spoken word, a scream, two
people on one microphone, or a saxophone.

What you give up is real: no harmonies, no correction, no octave.
What you get back is everything else a VoiceLive does to a voice, running
on hardware you already own, with the effects fed by a switch that lets
the tails ring out.

## The chain

```
IN GAIN → LOW CUT → GATE → COMP → DE-ESS → BODY/PRESENCE/AIR
        → DRIVE ─┬─→ (dry) ──────────────────────────────┬─→ OUTPUT
                 ├─→ DOUBLE ─────────────────────────────┤
                 ├─→ MOD ────────────────────────────────┤
                 └─→ DELAY ─┬────────────────────────────┤
                            └─→ REVERB ──────────────────┘
```

The four blocks after the split are the **FX**. One switch feeds them or
stops feeding them, and it stops the *send*, not the return: switch off
and the delay and the reverb ring out instead of being chopped. That is
the same idea the Fade plugin in this repository exists for.

## Controls

| Control | What it does |
|---|---|
| **IN GAIN** | −20 to +40 dB. A dynamic microphone straight into the Dwarf usually wants +20 to +30. |
| **LOW CUT** | 0–400 Hz, 6 dB/octave. Rumble, handling noise and plosives, before they reach the gate. At 0 it is off. |
| **GATE** | Threshold, −80 to −20 dB. 6 dB of hysteresis and an 80 ms hold, so a held note does not chatter. At −80 dB it is off. |
| **COMP** | 0–100 %. One control: it lowers the threshold and raises the ratio together, from off to −40 dB at 6:1, and adds back most of what it takes off. |
| **DE-ESS** | 0–100 %. Compresses the band above 5.5 kHz alone: an S loses its edge, the word does not go dull. |
| **BODY** | ±12 dB below ~240 Hz. |
| **PRESENCE** | ±12 dB between ~1 and 4.5 kHz. Where a voice cuts through a band. |
| **AIR** | ±12 dB above ~6 kHz. |
| **DRIVE** | 0–100 %. Soft saturation, level-matched at −12 dBFS: the colour changes, how loud you are does not. |
| **DOUBLE** | 0–100 %. Two copies a few tens of milliseconds late, each drifting on its own slow LFO. In the stereo build one goes left and the other right. |
| **MOD** / **MOD SPEED** | Chorus depth and rate, 0.05–8 Hz. The two sides move a quarter cycle apart in the stereo build. |
| **DELAY** / **REPEATS** / **DELAY MIX** | 20–2000 ms, up to 95 % feedback. The repeats lose their top and their bottom each time round, so a long tail sits behind the voice. |
| **REVERB** / **REVERB MIX** | Tail length and how much is heard. At 100 % mix the tail sits at the same level as the dry voice — measured, not guessed. |
| **FX** | On: the four effects are fed. Off: the send is cut over 40 ms and the tails ring out. Meant for a footswitch. |
| **FX TRIGGER** | One pulse flips the same state. Meant for MIDI. |
| **TAP** | Two presses set the delay time. Meant for a footswitch. |
| **OUTPUT** | −60 to +12 dB. At −60 the plugin is silent. |

And five outputs, for the screen, the web UI and anything else that
watches: **GR** (compressor reduction in dB), **LEVEL** (peak out),
**GATE OPEN**, **FX STATE**, and **TIME** (the delay time actually in
force).

Those last two exist for the same reason Fade's STATE does: TOGGLE and
TRIGGER drive one internal state, and TAP overrides a knob, but an LV2
plugin must not write into a control *input* port. So the widget can go
stale and the output tells the truth.

## On the device screen

Address any of these to a knob or a footswitch and the plugin takes over
the readout:

| Addressed control | What the screen shows |
|---|---|
| **OUTPUT** | A level meter: peak dB, a bar, and a red LED past −1 dB, where the ceiling starts working. |
| **COMP** | `COMP GR` and how many dB it is taking off *right now*, with a bar. |
| **GATE** | `OPEN` or `SHUT`, with a bar following the gate's own fade. |
| **DELAY** | The time in force, in ms — and the label reads `TAP` instead of `DELAY` when the tap owns it, rather than showing a knob position that is no longer true. |
| **TAP** | The tempo in BPM, and the LED blinks it back at you. |
| **FX** / **FX TRIGGER** | `ON` or `OFF`, green or dark. |

## Presets

Six, on both variants: Speech, Stage Dry, Ballad, Rock, Wide, Cathedral.
Each one writes every control it does not name at its default, so loading
a preset lands somewhere known instead of on top of half of whatever was
there before. Neither trigger is ever written — a preset that pressed TAP
would set a tempo as it loaded.

## Install

### With the web page

`install-voice.html` carries the whole bundle. Upload it to User Files and
open it from the device's own File Manager: one button, no terminal. The
page must be served *by the Dwarf* — opened from your own disk, the
browser blocks the request.

### With the terminal

Check what you downloaded before sending it:

```sh
rm -rf /tmp/voice-inst && mkdir -p /tmp/voice-inst
tar xf voice-aarch64.tar.gz -C /tmp/voice-inst
od -An -tx1 -N1 -j4 /tmp/voice-inst/voice.lv2/voice.so   # must print 02
grep -ao 'VOICE_BUILD[A-Za-z0-9_]*' /tmp/voice-inst/voice.lv2/voice.so
```

`02` is the ELF class byte: 64-bit. The Dwarf is aarch64 and refuses a
32-bit binary with `wrong ELF class: ELFCLASS32`.

```sh
cd /tmp/voice-inst
COPYFILE_DISABLE=1 tar czf - --exclude='._*' voice.lv2 \
  | ssh root@192.168.51.1 'rm -rf /root/.lv2/voice.lv2 \
      && tar xzf - -C /root/.lv2 \
      && systemctl restart mod-ui'
```

`COPYFILE_DISABLE` is not optional on macOS: the `._*` files it otherwise
adds are mistaken by lilv for bundle directories, and the plugin fails to
load. Remove the block from your pedalboard before installing and add it
back afterwards — mod-ui caches a failed load.

## Build

Needs an aarch64 cross-compiler, `rapper`, `python3`, and a native `gcc`
with the LV2 headers for the test bench:

```sh
./build.sh
```

It validates the descriptors, cross-checks them against the C source, runs
the whole test bench, compiles, packages, verifies the binary *extracted
from the archive* (64-bit, libc only, nothing newer than GLIBC 2.17), and
regenerates the installer page around the tarball it just built. Any one
of those failing stops the build before an archive exists.

The descriptors are generated:

```sh
python3 make_ttl.py     # voice.ttl, voice_stereo.ttl, presets.ttl, manifest.ttl
```

The two variants differ only in their audio ports and share all twenty-six
controls, so the list lives in one place. `check_descriptor.py` then reads
the generated files back and compares them, entry by entry, against the
table in `voice.c` — which is written by hand. A default that is right in
one and wrong in the other is invisible until a singer plugs in and the
gate is shut.

Tests on their own:

```sh
gcc -std=c99 -O1 -g -fsanitize=address,undefined -I.. -I. -o test_voice test_voice.c -lm
./test_voice
```

105 checks: the approximations against libm, every block of the chain
against what it claims to do, the delay against a clock at three sample
rates, and a simulated HMI screen. Without a simulated screen none of the
display code ever runs, and that is where the bugs live.

## Notes on the implementation

- **No libm.** The bundle must link against libc alone, so `log2` and
  `exp2` are degree-5 polynomials over the float's own exponent bits
  (0.0002 dB and 0.000002 dB of error, measured), the LFOs are a parabola
  with one refinement pass (0.06 %), and one-pole cutoffs come from
  `w/(1+w)` rather than a tangent. Every one of those is measured against
  the real function in the bench.
- **One allocation.** Delay lines, doubler line and reverb — 900 kB of it
  at 48 kHz in stereo — come out of a single `calloc` in `instantiate()`,
  sliced into pointers. `cleanup()` frees two things and cannot leave a
  piece behind.
- **The de-esser's band split has to be complementary.** Two high passes
  in series measured 0.2 dB of ducking on an 8 kHz tone where the
  arithmetic promised 26: the band and the signal it is subtracted from
  were out of phase. A two-pole low pass, with the band taken as
  `input − lowpass`, gives 12 dB, because there the two halves really do
  sum back to the input.
- **The drive is level-matched at −12 dBFS**, which is about where a voice
  sits after the compressor. Before that it was compensated by a formula
  rather than by measurement, and turning it up cost a loud passage 10 dB.
- **Gate, compressor and de-esser share one detector across both
  channels.** Independent detectors pull the stereo image sideways every
  time one side is louder, which on a voice is every sibilant. The bench
  checks the two channels are bit-identical through the whole strip.
- **DRIVE at zero is exactly unity.** The saturated signal is blended in,
  not switched in: a stage bypassed by a branch steps the level of a loud
  passage by nearly 2 dB the moment the control leaves zero.
- **There is an output ceiling**, transparent below 0.85 and asymptotic at
  1.0. Four wet effects and a drive stage can sum past full scale, and the
  converter should not be the thing that finds out.
- **Denormals are flushed** in every feedback path. They cost tens of
  cycles each on this CPU and are inaudible by definition.
- **Screen writes are capped at 25 passes per second**, with a full cache
  flush once a second so the display survives the firmware's repaints.
- The delay time *glides* to a new value rather than jumping, so turning
  the knob bends the pitch of what is in the line, like tape. A tap is
  resolved to one audio block — under 3 ms, finer than a foot.
- Cost: about 1.5 % of one core of an x86-64 build machine for the stereo
  variant with every effect on. Not measured on the device.

## What it does not do

- No pitch detection, and therefore no harmony, no correction, no octave.
  That is the point, not an omission.
- No MIDI input: the FX switch and the tap take a control port each, which
  is what the Dwarf addresses to a footswitch or to a MIDI CC.
- No custom web UI. mod-ui draws its own from the descriptor, which shows
  every control and cannot be broken by a template bug. The device screen
  is where the work went instead.
- The tone controls are three broad parallel bands, not a surgical EQ, and
  the reverb is a Freeverb — a good room, not a convolution.

## Licence

ISC, same as the rest of the repository. See [../LICENSE](../LICENSE).
