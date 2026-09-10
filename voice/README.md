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

There is a **pitch shifter** in here, and it is not a contradiction:
shifting is not detecting. The signal is read out of a delay line at
another rate, which *is* a transposition, and nothing has to know what
note it was. Baritone, tenor, helium and an octave down all come out of
that one trick. What you give up is the thing that genuinely needs
detection: harmonies in a key, and correction. What you get back is
everything else a VoiceLive does to a voice, running on hardware you
already own, with the effects fed by a switch that lets the tails ring
out.

## The chain

```
        [on] [on]  [on]    [on]                    [on]
IN GAIN → LOW CUT → NO HOWL → GATE → COMP → DE-ESS → EQ → DRIVE → PITCH
                                                     + [on] HARMONY x2
     ┬─→ (dry) ────────────────────────────────────────────────┬─→ MUTE → OUTPUT
     ├─→ [on] DOUBLE ──────────────────────────────────────────┤
     ├─→ [on] MOD ─────────────────────────────────────────────┤
     └─→ [on] DELAY ─┬────────────────────────────────────────┤
                     └─→ [on] REVERB ─────────────────────────┘
                          all four also under one master FX switch
```

PITCH is *in* the chain, because a baritone is the voice and not something
added to it. HARMONY is *beside* it, because a harmony is somebody else
singing.

**Every effect has its own switch**, made for a footswitch, and each one
sits immediately in front of the controls it switches. On top of them, the
**FX** master feeds all four effects or stops feeding them at once.

Switching off stops the *send*, never the return: the delay and the reverb
ring out instead of being chopped. That is the same idea the Fade plugin
in this repository exists for. Every switch is a 40 ms ramp, so a foot on
any of them fades rather than clicks.

## The program list

**PROGRAM** is a list, not a knob: MANUAL, then seventy-two built-in sounds,
then six USER slots of your own. Put it on an encoder and you can walk
through them from the device. The list is grouped by family — spoken
voices first, then singing, doubling and choirs, pitched voices, grilles
and loudspeakers, echoes and rooms, and last the instrument sounds — so
walking it goes somewhere rather than everywhere.

What a program takes over, and what stays yours, is a deliberate split:

| | |
|---|---|
| The program owns | low cut, gate threshold, comp, de-ess, the three tone bands, drive, double, voices, mod, delay and reverb — everything that makes up *the sound* |
| You always own | **IN GAIN** and **OUTPUT** (rig levels — a master volume that stops responding is a broken master volume), the **FX** master, **TAP**, and every effect switch |

**A program is a starting point, not a cage.** Turn any control it owns and
that one control comes back to you on the spot, while the rest of the
program stays in force. Choosing another program hands everything back to
it. The switches work the same way: selecting a program *adopts* its switch
positions — pick Ballad and the doubler, delay and reverb come in with it —
and then lets go, because a footswitch that stopped working when a program
was chosen would be worse than having no programs at all.

That is what makes the USER slots useful rather than decorative: pick
Cathedral, decide it wants less reverb and a shorter delay, turn those two,
point **USER SLOT** at slot 3 and press **SAVE**. What gets stored is what
you were hearing — the program *plus* your changes — and Cathedral itself is
untouched.

Two consequences worth knowing. A knob you have not touched under a program
reads whatever it last read in the web UI, though the device screen always
shows what is really in force — the same honest problem as the tapped tempo.
And reloading a pedalboard takes the switch positions from the saved ports,
not from the program, so a board comes back exactly as you left it.

## One page of the pedal, and the whole loop

Three encoders and two footswitches is what a Dwarf page holds, and that is
exactly what this needs:

| | |
|---|---|
| **ENC 1 · FAVOURITE** | Walks the six USER slots, by name. The sound follows as you turn. |
| **ENC 2 · PARAMETER** | Walks the sound's parameters — **NAME** first, then everything a program owns — showing the one it points at with its value. |
| **ENC 3 · VALUE** | Changes it. On NAME it opens the name editor. |
| **USER ▶** (footswitch) | The next slot with something in it. |
| **SAVE** (footswitch) | Store what you are hearing into the slot. |

In a hall that turns out deader than the soundcheck: turn ENC 2 to
PRESENCE, ENC 3 up two clicks, ENC 2 back to NAME, ENC 3 to open the
editor, type `SALLE B`, turn ENC 3 right to keep it, press SAVE. Three
songs later, come back with the footswitch and change the reverb. No
computer, no browser, one page.

**Typing a name.** ENC 3 on NAME opens the editor, whichever way you turn
it — a knob you have to turn the *right* way to be let in is a knob that
looks broken. Then all three change together, and say so: **ENC 1** moves
the cursor along the word, with the letter being typed **blinking**;
**ENC 2** is the letter (space, A–Z, 0–9, dash, dot, and it wraps); **ENC
3** is the answer — *left is no, right is yes*. Yes writes the name onto
the slot and onto the disc there and then. No puts back the name that was
there. A mode nobody touches for fifteen seconds lets go on its own and
puts the name back too: a silence is not a decision. What you had typed
stays in the buffer, so opening the editor again resumes it.

**Why an encoder can change meaning at all.** Because it is read in
**detents** and never as a position: the plugin counts the clicks of the
knob. There is no position to inherit, so pointing ENC 3 at another
parameter cannot make anything jump — which is the whole reason forty
parameters can live under one knob instead of on six pages. The size of a
detent is *learned* (the player sets it per addressing; the Dwarf's
default is 201 steps over the course), two clicks inside 80 ms count five
steps where a list is long, and a move bigger than a hand — a snapshot
recall — moves nothing at all. Each knob is asked back to the middle when
it strays: with no room left, a knob stops answering, and on a decision
that means "yes" becomes impossible to say.

## Controls

| Control | What it does |
|---|---|
| **PROGRAM** | The list: MANUAL, seventy-two built-in sounds grouped by family, then six USER slots of your own. MANUAL means the controls below are yours; anything else overrides them while it is selected. Address it to an encoder and walk the list from the device. |
| **SAVE** | Stores **what you are hearing** into the slot USER SLOT points at — the program you picked plus every change you made to it. |
| **USER SLOT** | Which of the six USER slots SAVE writes to. A list of its own, so a built-in sound can be changed and kept somewhere else without the original being touched. |
| **IN GAIN** | −20 to +40 dB. A dynamic microphone straight into the Dwarf usually wants +20 to +30. No preset and no program ever touches it. |
| **LOW CUT** | 0–400 Hz, 6 dB/octave. Rumble, handling noise and plosives, before they reach the gate. At 0 it is off. |
| **GATE** | Threshold, −80 to −20 dB. 6 dB of hysteresis and an 80 ms hold, so a held note does not chatter. At −80 dB it is off. |
| **COMP** | 0–100 %. One control: it lowers the threshold and raises the ratio together, from off to −40 dB at 6:1. What it gives back is what it takes off a voice at −12 dBFS, so turning it up changes the sound, not how loud you are. |
| **DE-ESS** | 0–100 %. Compresses the band above DE-ESS FREQ alone: an S loses its edge, the word does not go dull. |
| **DE-ESS FREQ** | 2–12 kHz. Where the sibilance actually is, which is not the same for every voice or every microphone. Around 5–7 kHz for most singers; up if the de-esser starts eating the word rather than the S. |
| **BODY** | ±12 dB below ~240 Hz. |
| **PRESENCE** | ±12 dB between ~1 and 4.5 kHz. Where a voice cuts through a band. |
| **AIR** | ±12 dB above ~6 kHz. |
| **DRIVE** | 0–100 %. Soft saturation that measures itself either side of the saturator, twice a second, and corrects the difference: the colour changes, how loud you are does not — at any input level, which a fixed reference could not do. |
| **… ON** | One switch per effect — GATE, COMP, DE-ESS, DRIVE, DOUBLE, MOD, DELAY, REVERB — each sitting immediately in front of the controls it switches. Made for footswitches. DELAY and REVERB cut what goes *in*, so their tails ring out. |
| **HARMONY** / **VOICE 1** / **VOICE 2** | Two more singers, at fixed intervals in semitones from what *you* sing — +4 a major third above, −5 a fourth below, ±12 an octave. Nothing detects your note, so they follow the song wherever it stays in one key, which is what a chorus usually does. A voice at 0 is silent and costs nothing. Both are counted from the sung note, not from where PITCH has put you: PITCH −12 with a third above gives you both. |
| **DOUBLE** | 0–100 %. How much of the doubled voices is heard. They arrive 26 to 52 ms late, each held a constant few cents off the lead, each with its own drift, its own vibrato — which swells and relaxes on a cycle of its own — and its own throat, brighter or darker than the lead. |
| **VOICES** | 2, 3 or 4. Two is a straight double, three is thicker, four is a small choir. The level is held steady as the count changes, so this picks a texture and not a volume. In the stereo build they alternate left and right, with the odd one up the middle. |
| **SPREAD** | How far apart the voices stand: their detune, their drift and how staggered their entries are. Low is one singer twice; high is a group who have never met. |
| **NO HOWL** / **HUNT** | The anti-Larsen hunter. Sixteen filters listen for a band that rises and then just *sits* there — which is what feedback does and singing does not — and drop a narrow notch on it. At 0 it is off and costs nothing. NOTCHES says how many are in place. |
| **MOD** / **MOD SPEED** | Chorus depth and rate, 0.05–8 Hz. The two sides move a quarter cycle apart in the stereo build. |
| **DELAY** / **REPEATS** / **DELAY MIX** | 20–2000 ms, up to 95 % feedback. The repeats lose their top and their bottom each time round, so a long tail sits behind the voice. |
| **REVERB** / **REVERB MIX** | Tail length and how much is heard. At 100 % mix the tail sits at the same level as the dry voice — measured, not guessed. |
| **FX** | The master switch: on, all four effects are fed; off, their send is cut over 40 ms and the tails ring out. It sits on top of the individual switches, not instead of them. Meant for a footswitch. |
| **FX 2** | A second switch on the same state, for a second footswitch or a MIDI controller — a port can only take one addressing. Either switch moving flips the state. |
| **FX TRIGGER** | One pulse flips the same state. Meant for MIDI. |
| **MUTE** | Cuts the output over 20 ms and lets it back the same way. Not the gate, which listens, and not FX, which shapes: this one stops the sound, which is what you want between two songs. The tails go on decaying behind it, so letting go does not release a frozen reverb. |
| **WORD** | A word from a list of thirty-two. Turning it names the slot USER SLOT points at, there and then, and puts it on the disc — it does not wait for a SAVE, and SAVE does not read it. The name itself is seven characters of free text — the width of a footswitch label — and it travels with the slot. Nothing is named during the settle window, or opening a pedalboard would rename a slot every time. |
| **WEB SLOT** / **WEB CHAR** / **WEB STROBE** | How a name typed in the web page reaches the plugin: the favourite, the character, and one *change* of the strobe per character. Written by the interface, never by hand. See *Naming* below. |
| **ENC 1 / ENC 2 / ENC 3** | The three encoders of a pedal page. See *One page of the pedal* above. |
| **USER ▶** | Steps to the next USER slot with something in it, and round again. One footswitch for your own sounds, each under its name on the screen. Empty slots are skipped. |
| **FAV BROWSE** | The second footswitch. One press walks the filled USER slots **without changing the sound** and names the one it reaches; holding it half a second goes there. A plain toggle, not a trigger, so that mod-ui offers **Momentary** under Advanced — address it that way. Twelve seconds untouched and it forgets. |
| **GO 1**…**GO 6** | One switch per favourite: a press goes to that one, always, whatever was pressed before. Each carries its own favourite's name. |
| **A/B** | Back to the program you were on before this one; press again to return. For comparing two sounds at a soundcheck without walking the list. A plugin may not write its own PROGRAM port, so **PROGRAM NOW** publishes which one is really in force — and the web UI follows it. |
| **TAP** | Two presses set the delay time. Meant for a footswitch. |
| **OUTPUT** | −60 to +12 dB. At −60 the plugin is silent. |

And the outputs, for the screen, the web UI and anything else that
watches: **GR** (compressor reduction in dB), **LEVEL** (peak out),
**GATE OPEN**, **FX STATE**, **PROGRAM NOW**, **PARAM NOW**, **NOTCHES**,
**TIME** (the delay time actually in force), and **NAME SLOT** with
**N1**–**N7**, which carry the six slot names back up to the page one per
second.

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
| any effect switch | Its own name — `DELAY`, `REVERB`, `DOUBLE`… — with `ON` or `OFF` and the LED to match. It shows what is actually in force, which after a program change is not always what the knob says. |
| **PROGRAM** | The name of the sound in force: `MANUAL`, `BALLAD`, `CATHEDRL`… |
| **VOICES** | How many voices the doubler is running. |

## Levels

Every program lands within about a decibel of a transparent plugin, measured
on a *sung phrase* — loud lines, quiet lines, breaths between them — and the
bench fails the build if one drifts outside ±2.5 dB. That measurement is the
point: on steady noise every preset here already looked fine, because noise
gives a compressor nothing to work on. Density is loudness, and only material
with dynamics shows it.

Three rules get them there, and they apply while you turn knobs too:

- **Nothing but you writes IN GAIN.** How loud the microphone is belongs to
  the rig, not to the sound.
- **COMP gives back what it takes off a voice at −12 dBFS.** Turning it up
  compresses harder; it does not make you louder.
- **DRIVE is matched at the same level**, so it changes the colour and not
  the volume.

## Presets, and your own sounds

Seventy-two, on both variants, and they exist twice over: as entries in the
PROGRAM list, and as LV2 presets in the plugin's own preset menu. Both come
from one table in `make_ttl.py`, and the bench runs a phrase through both
routes and subtracts — picking Ballad from the menu and selecting the
program of the same name give the same samples, or the build stops.

The list is in this order, which is also the order of the families:

| | |
|---|---|
| **One voice on its own** | Speech · Podcast · Audiobook · Voice-Over · Desk Mic · Radio Announcer · Stage Dry |
| **Singing in front of a band** | Ballad · Power Ballad · Warm Crooner · Modern Pop · Pop Lead · Rock · Rock Lead · Hard Rock Shout · Country · Cut Through · Whisper |
| **Doubling yourself, and the choir** | Tight Double · Stage Double · Wide · Backing Vocals · Stacked Backing · Choir · Wide Choir · Gospel Choir · Gospel Stack · Angel Choir · Seraphim |
| **Somebody else's voice** | Baritone · Tenor · Helium · Octave · Octave Below · Fifth Below · Monster · Robot · Alien |
| **Somebody else singing with you** | **Third Up** · **Trio** · **Power Fifths** · **Octaves** |
| **Out of a grille** | **Hygiaphone** · Telephone · Megaphone · Walkie Talkie · Radio |
| **Echoes and rooms** | Slapback · Tape Slap · Eighth Notes · Dub · Dub Echo · Ambient · Ambient Wash · Arena · Stadium · Cathedral · Church · Basilica · Shimmer |
| **An instrument instead of the microphone** | **Guitar Solo** · Lead Solo · **Fuzz Lead** · **High Gain** · Guitar Crunch · Guitar Clean · Clean Chime · Acoustic Piezo · Bass DI · Harmonica · Saxophone · Rotary Keys |

Two of them answer questions that were asked out loud. **Hygiaphone** is
the speaking grille at a bank counter: nothing below 320 Hz or above
6 kHz, a hard bell at 1.8 kHz, and enough drive to make it buzz.
**Guitar Solo** is an overdriven lead that does not howl — drive at 42,
the noise gate tight at −38 dB for the space between phrases, and the
anti-Larsen hunter at 70 for the part a gate can do nothing about, which
is the howl that happens *while* you are playing.

All seventy-two are measured. The bench sings a phrase through a transparent
plugin, then through each preset, and any that lands more than 2 dB above
or 2.5 dB below the plain voice fails the build — which is how *Monster*
and *Dub*, both nearly 3 dB hot, were caught and trimmed before they ever
reached a stage.

Each writes every control it does not name at its default, so loading one
lands somewhere known instead of on top of half of whatever was there
before. Neither trigger is ever written — a preset that pressed TAP would
set a tempo as it loaded — and neither is IN GAIN.

Every effect gets a usable amount even where its switch starts *off*, so the
footswitch has something to bring in rather than turning on silence.

### Your own, on the list

Six **USER** slots sit on the end of the PROGRAM list, and they are the
answer to "custom sounds without pedalboard snapshots". Two ways in:

**From nothing.** PROGRAM on MANUAL, dial the sound, point USER SLOT at a
slot, press SAVE.

**From a built-in sound.** Pick one, change what you do not like about it —
every control you touch comes back to you — point USER SLOT somewhere and
press SAVE. What is stored is what you heard, and the sound you started
from is untouched.

Selecting a slot recalls it, and it behaves like any other program: turn a
knob and that knob is yours again, so a saved sound can be edited and saved
back.

**What you should see when it works.** Press SAVE and the list jumps to the
slot you wrote — the web UI goes there on its own, and on the pedal the
screen says `SAVED 3` for a second. That feedback exists because the honest
answer to "did it save?" used to be "yes, but nothing on the screen said
so". Walk away to another sound and come back to the slot: the knobs move
to what you stored.

**One bug worth knowing about, now fixed.** Pressing SAVE in the web UI
also selects the slot it wrote to — and both port writes could land
between the same two `run()` calls. Applied in that order, entering the
slot made SAVE read *the slot* rather than the knobs, and store the
slot's old contents back into itself. The sound stayed right until you
came back to it, which is the worst way for a save to fail. SAVE is now
handled before the program list, whatever order the writes arrive in, and
the web UI waits for the pulse to finish before jumping. Slots saved with
an earlier build kept the sound they had *before* the edit; save them
again.

**One trigger is ignored for two seconds after loading.** A pedalboard
restores the saved value of *every* port, and a trigger restored to 1
reads as a press — a save fired at every opening, over whatever slot
happened to be selected. SAVE, A/B, USER ▶ and TAP therefore count
nothing for the first two seconds of the plugin's life. Measured, from
the sources of a plugin of Rémy's that had already been bitten by it.

**The slots reach the disc when you press SAVE, not when the board is
saved.** They travel with the pedalboard too — the plugin implements the
LV2 State extension — but state only reaches the disc when the *board* is
saved, so pressing SAVE and walking away used to save nothing at all.
There is now a file beside the bundle, `voice-slots.bin`, written from the
worker thread the moment SAVE is pressed: never a disc from `run()`, and
written to a temporary and renamed, so a Dwarf switched off mid-write
cannot leave half a file. It is read back at startup, and a pedalboard
that carries its own copy overwrites it a moment later, which is right —
the board is the more specific answer.

**The knobs move themselves now.** An LV2 plugin may not write its own
control input ports — but on a MOD it may *ask*. `kx.studio`'s
[control-input-port-change-request][kx] feature is the host-blessed way,
mod-host implements it, and picking a sound now moves the encoders and the
web page to what that sound holds. That is the difference between a preset
list that works and one that changes the sound while every control on the
screen still shows the sound before it — which is what this did until
build 11, and why the USER slots looked as though they were not saving
anything.

[kx]: http://kx.studio/ns/lv2ext/control-input-port-change-request

A request the host declines changes nothing: the program table is still
what is heard. And a knob turned afterwards still wins — the plugin knows
its own request by the *value* it asked for, not by a window of time, so a
hand landing on a control a millisecond later is a hand and not an echo.

There is one truth now instead of two. The web UI used to keep its own
copy of every slot in the browser to move the knobs with; it does not, and
cannot disagree with the plugin any more. The device screen never
had this problem: it always showed what was in force.

**Naming.** A slot's name is seven characters of free text — the exact
width of a footswitch label on this machine, measured — stored in the slot,
saved with the pedalboard and written to the disc, and shown wherever that
slot appears on the screen. There are three ways to fill it, and they all
write the same one:

| | |
|---|---|
| **The pedal** | ENC 3 on NAME opens the editor and you type it, letter by letter. Nothing else needed: no browser, no computer. |
| **The FAVORIS list**, in the web UI | Six boxes, one per slot. Type a name, press ENTER, and it is in the slot, on the disc and on the footswitch a fifth of a second later — no SAVE needed, because a name is not a sound. |
| **WORD**, the list | Thirty-two ready-made words — INTRO, VERSE, CHORUS, SOLO, SONG 1, BALLAD… For whoever would rather turn one knob than spell anything. It names the slot **USER SLOT** points at the moment it is turned, and writes it to the disc; USER, at the top, means leave the name alone. |

**SAVE never renames.** It stores the sound into the slot and nothing else.
That was a real bug, and this is what it looked like: the WORD list and the
pedal's editor shared one buffer, and SAVE stamped that buffer onto whatever
slot it wrote. So a word picked once — or restored with a pedalboard, or
merely passed over while browsing the favourites on the pedal — sat there for
the rest of the session and renamed every slot saved afterwards. You typed
CHORUS and it came back VERSE, hours later, with no visible cause. Saving a
sound is not renaming it; the two editors write the slot themselves, when you
tell them to.

**The FAVORIS list.** mod-ui's own PROGRAM menu reads the descriptor, and
the descriptor was written before you named anything: it can only ever say
USER 1 to USER 6. So the web UI keeps its own list, and it says the names.
**FAVORIS** in the pedal opens the six and goes to one when you click it.
The boxes you *type* in are in the plugin's settings panel, where there is
room for them — and where the PROGRAM list, in the same panel, says the
names too. Type, and the name is in the slot, on the disc and on the
footswitch: there is nothing to press.

**Why not an atom port.** That is what LV2 has them for: one message
carrying the whole string, and none of what follows would be needed. It
arrives nowhere on this firmware — measured, on the machine, by the plugin
this was learnt from. So:

A name has no port to travel on: a control port carries a number, not a
word. So it goes down one character at a time — **WEB SLOT** says which
favourite, **WEB CHAR** holds the character, **WEB STROBE** changes to say
"look at it now", and the plugin counts the *changes*, never the value, or
a pedalboard putting its ports back would type a character of its own every
time you opened it. Which is also why nothing is typed during the first two
seconds. The slot travels *with* the character rather than being chosen by
an order of its own, so a code lost on the way costs one letter instead of
landing a whole name on the wrong favourite. Coming back the other way, the
plugin publishes the six names on **NAME SLOT** and **N1**–**N7**, one slot
per second, and that is how the page learns what the pedal was told to call
them — whoever typed them.

**Why none of that is bound to an event.** In the pedalboard, mod-ui copies
the interface after building it, and every event binding the script made
goes with the copy: only mod-ui's own widgets keep working. That is why the
first version of this — a box with a `change` handler and a button with a
`click` handler — worked on the bench and not on the machine. So nothing in
the naming binds anything. The panel is opened by a checkbox and a CSS rule,
a favourite is chosen with a radio, a name is typed into a plain box: three
form controls the browser keeps for us whether or not the script ever ran.
A clock reads them back eight times a second, looking through the *whole
page* rather than inside the icon — the settings panel is not a descendant
of the icon, and that is where most of the boxes live. For the same reason
the state it keeps lives on `window` and not on the icon, which may be a
copy about to be thrown away with a name half sent.

**Type at whatever speed you like.** The page waits for the keys to stop —
about a third of a second — before sending anything, because it sends the
whole word each time and not the difference: on every keystroke, `SOLO`
would go down as S, then SO, then SOL, then SOLO, fourteen codes for a four
letter name. Waiting makes it five. And the echo for a slot is disbelieved
from the instant a key is pressed in its box, not from the instant the word
goes down, or the old name coming back up in between would land in the box
and be sent as if it had been typed.

The disc is written half a second after the last letter, so a seven-letter
word is one write of the file and not seven; closing the plugin before that
half second is up writes it anyway, on the way out.

**USER ▶** — the cycle switch — steps to the next USER slot that has
something in it, skipping the empty ones, and the screen says where you
landed *by your name for it*, on the switch itself. One footswitch, your
own sounds, in order.

There **was** a popup here, and it is gone. It existed because the labels
were not working — every screen write was gated on capabilities this host
announces as zero, so the popup, the one send not gated, was the only thing
that ever appeared. Now that the switches carry the name themselves a popup
is a second copy of what is already on them, and it costs two seconds
during which nothing else can be written: it hid the very labels it was
standing in for.

**GO TO ▷** — the browse switch — is the second footswitch, and it is the
one for reaching the third of five in the middle of a song. What USER ▶
cannot do is get there without playing the second on the way; this one
moves a **cursor** instead of the sound:

| | |
|---|---|
| **One press** | walks to the next favourite that has something in it. **Nothing is heard.** The switch shows its name and its LED **blinks** — chosen, not entered. |
| **Held half a second** | goes there. |
| **Twelve seconds untouched** | the cursor gives up and returns to the sound in force, so a walk left half done cannot fire ten minutes later. Four seconds was the first figure and it was not long enough to tap, read the switch, decide and go. |

One switch, one gesture in two lengths, and nothing that depends on
another switch. **USER ▶ is the cycle and nothing else** — it used to also
finish a walk begun on GO TO, and a switch that does two things depending
on what was pressed before it is a switch nobody can read on a stage.

The step happens on the **release**, not on the press. Otherwise the press
that becomes the hold would step first, and the favourite entered would be
the one after the one aimed at.

**Walking fast is the whole reason this switch exists** — three quick
presses to get from the first favourite to the fourth. There was a version
that read two quick presses as "go there", and it made exactly that
impossible: every fast walk was a series of go-theres. A release is
instant, so a fast walk is one step per press with nothing to wait for.

**FAV BROWSE is a plain `lv2:toggled` port, and NOT a `pprops:trigger`.**
That one line of the descriptor is the whole of why a hold works, and four
builds were spent finding it out. A trigger is, by MOD's own convention, a
momentary *pulse*: the host writes one and then zero whatever the foot
does, so the length of a press cannot be seen from inside the plugin — and
mod-ui, knowing it is a trigger, offers **no Momentary option** in the
addressing dialog, there being nothing to choose. That missing option was
the symptom. A plain toggle does offer it, under **Advanced**, and
addressed that way the port stays high while the foot is down.

So: **address FAV BROWSE as momentary**. Every other switch here stays a
trigger, because a pulse is exactly right for them.

`check_descriptor.py` fails the build if that property ever comes back.

**DIAG says whether the hold arrives**: *maintien vu jusqu'à 0.5 s*, or
*AUCUN MAINTIEN VU — le switch envoie des impulsions*, which is the
difference between a hold that does not work and one that cannot.

## One switch per favourite

Six ports, **GO 1** to **GO 6**, so six footswitches can each own one. A
press goes to that favourite, always, whatever was pressed before — nothing
to enchain, nothing to remember, and the switch carries that favourite's
name whether or not it is the one being played. Three footswitches to a
page of the Dwarf, more with the pages, and three favourites under the foot
is already a concert.

An empty slot is not refused: going there leaves the knobs in charge, which
is how a sound is dialled before being saved into it.

If you want a sound named everywhere and in your own words, add it to
`PRESETS` in `make_ttl.py` and rebuild — it becomes a program *and* an LV2
preset, with its name on the screen and in the list.

mod-ui's own **Save** on the plugin block is the other route: it writes a
plugin preset — your settings, under your name, in the same menu as the
built-in ones, not a pedalboard snapshot. Use that when you want a long
name and a long list; use the USER slots when you want it under your foot.

## The web UI

![the web interface](modgui/screenshot-voice.png)

Every effect is a box with its switch in the corner, and **ON is a lit
green track with a white knob and a glow**, next to a plain grey OFF — the
first version of this plugin shipped without a custom interface at all, and
mod-ui's default one drew switch states in a violet you could not see. The
section a switch belongs to lights its border too.

The bar across the top is the program list: arrows to walk it, the name of
what is selected, and **FAVORIS**, which drops the six USER slots by name —
click one to go to it, type in the box beside it to rename it. The second bar
is where a sound is stored, compared and cut: SAVE TO, WORD, SAVE, A/B,
USER ▶, the three encoder dials of the pedal page, and MUTE. The
compressor box carries a gain-reduction meter and the levels box an output
meter, both fed by the plugin's own outputs. TAP is a button as well as a
port.

The plugin also ships its own **settings panel**, which mod-ui would
otherwise build from the descriptor — and a panel built from the descriptor
is every port in index order: a wall of sixty knobs with no shape, and
nowhere at all to type a name, since a control port carries a number. Ours
is the same panel **in sections** — FAVORIS, IN AND OUT, then the strip in
the order of the pedal, then PEDAL PAGE and the three ports that are not
for hands — with the six name boxes under the first one.

It is **generated by `make_ttl.py`** from the port list and a `PANEL` table
that says which section each control belongs to. A port added to the
descriptor and forgotten in that table stops the build rather than quietly
vanishing from the interface.

And the **PROGRAM list in that panel says the names**. mod-ui builds the
list from the descriptor's scale points, which were written before anything
was named and can only say USER 1 to USER 6; the script gives those six
entries their real names on every tick, which also puts them back whenever
mod-ui rebuilds the list. So the sound you saved as CULCUL is in the list as
CULCUL.

**One editor per name, and no more.** The six boxes are in the settings
panel only; the FAVORIS list in the pedal shows the names and goes to one
when clicked, but does not rename. Two boxes for the same name would each
read the other as a change and fight over it — the clock cannot tell which
of two disagreeing boxes the player meant.

The jacks sit *outside* the panel, on a socket rail down each edge — which
is why the stylesheet must never put `overflow: hidden` on the pedal. The
first version of this interface did, and it looked immaculate right up
until somebody went to plug a cable in and found there was nowhere to plug
it: the sockets had been clipped away along with the corners. There is now
a check for exactly that, and the shipped screenshot is framed wide enough
to show them.

The interface is checked the way the rest of the plugin is:
`check_modgui.js` renders the template with mustache — the engine mod-ui
itself uses — then walks the DOM: every control port must be reachable, every
audio port must have its jack, the script must evaluate *and run*, throwing a
switch must really light its section, and the two constants the script cannot
work out for itself must match `programs.h`. `make_screenshot.js` then
photographs the result through Chromium, and refuses to write the image if
anything overflows the pedal — which is also where the screenshot in this
README comes from.

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

## When a switch does nothing: DIAG

From inside a plugin a footswitch is a black box. Whether its port is even
connected, how many favourites the walk has to step over, what the screen
announced — every one of those was answered by a supposition here, and one
of the suppositions was wrong for three builds running. So the plugin now
publishes what it sees, on **DIAG**, and the web interface reads it out in
words at the top left of the pedal:

```
GO TO connecte · 5 favoris enregistres · curseur sur 3 · ecran annonce 6
```

Five digits, from the left:

| Digit | Says |
|---|---|
| **1-2** | the **longest press ever seen** on FAV BROWSE, in tenths of a second. Going there is a held switch, and a held switch is only visible if the footswitch was addressed as momentary. **Zero** means the host sends one pulse however long the foot stays down — a hold that *cannot* work here, rather than one that does not. |
| **3** | 1 once that switch has been seen to move at all. **0 means the port is not connected** — remove the block from the pedalboard and add it back. |
| **4** | how many USER slots have something **SAVED** in them. This is what the walk steps over, and a slot that was *named* but never saved does not count. With fewer than two, walking has nowhere to go. |
| **5** | the favourite the cursor is on, 0 for none. |
| **6-7** | the screen capabilities the host announced for that switch, 99 if it is not addressed at all. |

The readout turns amber in the two states where the switch cannot do what
is asked of it and it is not the switch's fault: port not connected, or
fewer than two favourites saved.

## The screen, and what it announces

An addressing arrives with `info->caps`, a set of bits saying which of
label, value, unit, indicator and LED the plugin may write. On this machine
**it arrives empty** — an `info` that is not null and is entirely zero — and
a plugin that takes that at face value writes nothing at all, for ever.

That is what happened: the footswitches showed the label mod-ui had put on
them from the port name, not one word from the plugin, and the **popup** —
the one send in `paint()` that is not gated on caps — was the only thing
that ever appeared. It looked like a display problem in one place and was a
closed door in front of everything.

An empty announcement is therefore read as **everything permitted**. At
worst the firmware ignores a send it cannot use; at best, which is what
happens, the screen works.

**And the name goes in the LABEL.** A footswitch shows its label — that is
the field the host itself fills — so USER ▶ carries the name of the sound in
force and GO TO ▷ the name of the one it has walked to. The value carries
the same text, for the screens that show one.

## Adding a port

Never in the middle. A port **index** is what a pedalboard remembers, so a
new port inserted where it looks tidy hands every later value to its
neighbour — a knob you never touched moves, and the sound that comes back
is not the one you saved. Everything added since build 12 goes at the END
of the list, whatever its direction, which is why the descriptor has a
`TAIL` and the inputs and outputs are no longer two clean blocks. The
plugin knows which is which from `ctl_is_out`, a table written from the
same descriptor.

After installing a build that adds ports: **remove the VOICE block from
the pedalboard and add it again**, or the new ports are not connected at
all — and a control the host never connected reads its own default for
ever, which is exactly what a save that stores nothing looks like.

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

426 checks: the approximations against libm, every block of the chain
against what it claims to do, every switch for what it removes and for the
click it must not make, all seventy-two presets for the level they land on,
the delay against a clock at three sample rates, and a simulated HMI
screen. Without a simulated screen none of the
display code ever runs, and that is where the bugs live.

A doubler is judged by ear, though, and no test can do that:

```sh
./test_voice --demo        # writes voice-demo.wav
```

One phrase sung on "ah" - a glottal pulse train through three formants,
with vibrato, a little jitter and a breath, which is not a recording but
is close enough for the question - played dry, then through Tight Double,
Choir, Wide Choir, Angel Choir and Gospel Choir, each with its tail
ringing into the gap that follows it.

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
- **DRIVE is level-matched by measurement, not by formula.** Compensating
  the peak gain at one reference level - which is what this did, at
  −12 dBFS - is not enough, because saturation is compression: the peaks
  stay where they were and the average comes up. Measured on a sung
  phrase, the top of the knob was **+7.3 dB** louder, and **+11.9 dB** on
  a quiet singer, which is most of the reason a preset with any drive in
  it arrived shouting. The stage now keeps a slow average of what goes
  into the saturator and of what comes out of it, and scales the wet
  signal by the ratio - two fifths of a second, so it follows the passage
  rather than the syllable. The same measurement now reads **−0.6 to
  −2.0 dB** across a fifteen-decibel range of input level. The average is
  taken of what the saturator produces rather than of what leaves the
  stage: measure after the correction and the correction becomes its own
  input, and it flips about instead of settling.
- **The compressor is level-matched at −12 dBFS**,
  which is about where a voice sits mid-chain. It was compensated by a
  formula first, and the formula *gave* nearly 12 dB at COMP 65 — which,
  on top of a preset that also moved IN GAIN, is why the first presets
  came out shouting. What is given back is now the reduction the same
  curve applies at the reference level: measured, not derived.
- **The doubled voices are micro-shifters, not delay taps.** A delay whose
  length is wobbled by a sine has an average pitch offset of exactly zero
  and passes back through unison twice a cycle — at which moments it is a
  plain delayed copy, which is a comb filter. That is why the first version
  of this doubler sounded like an effect and not like people. Each voice now
  runs its own grain pair at a *constant* detune, seven to twenty-three
  cents at the middle of SPREAD, so it beats against the lead at a steady
  rate and never returns to unison. No two detunes are symmetric or in a
  small-integer ratio, or their beats lock into one pulsation and you hear a
  tremolo instead of a group. On top of that each voice has its own slow
  drift, its own small vibrato, its own entry time, its own window length
  and its own filtering, top and bottom — identical spectra fuse back into
  one object however far apart they are tuned.
- **Harmony is the same shifter, twice more, in parallel.** Two grain
  pairs reading the line the pitch shifter already writes, at fixed
  intervals, mixed beside the voice rather than into it — and both
  counted from what you *sing*, not from where PITCH has put you, so
  PITCH −12 with a third above gives an octave below *and* a third above
  rather than a surprise. They cost two grain reads each and nothing at
  all at 0 semitones, where a grain that does not move is a comb filter
  and the block steps aside. Formants move with the note, as they do for
  PITCH: a third up is a smaller singer than you, a fourth down a bigger
  one, which is most of what makes three voices sound like three people.
- **And the vibrato itself breathes.** A vibrato of fixed depth is the one
  thing no singer does, and it is what made four copies read as four
  oscillators rather than as four people. Each voice's depth now rides a
  very slow sine of its own — twelve to thirty-three seconds a cycle — so
  it swells to its full width and relaxes back to a little over half of it.
  The bench measures it the only way it can be measured: it sings a steady
  note for three whole swell cycles, reads the vibrato sidebands 4.7 Hz
  either side of the first voice's carrier in three-second windows, and
  sorts them into the ones near a peak and the ones near a trough. The peak
  group comes out 1.9 times louder. If it ever comes out flat, the depth
  has stopped moving.
- **Nobody holds a level either.** Each doubled voice leans in and backs
  off by about a decibel over ten to thirty seconds, on a cycle of its
  own. Four copies at a fixed level are four faders; the movement is what
  makes them people.
- **The old doubler ran two, three or four taps**, at 21, 29, 38 and 46 ms,
  drifting a few cents each on LFOs at 0.13, 0.19, 0.27 and 0.09 Hz — rates that share no
  common period, so they never line up into one wobble. Decorrelated copies
  add in power, so the level is divided by the root of the count — written
  out as a table, since there is no square root in this binary — and
  changing VOICES changes the texture without changing the volume. They are
  filtered a shade darker than the lead: three bright copies sound like a
  phaser, three darker ones sound like people. At full mix the copies sit
  about 2.5 dB under the lead, which the bench measures by running the
  same noise through the plugin twice and subtracting.
- **A program and its preset are one table.** `make_ttl.py` writes both
  `presets.ttl`, which sets ports, and `programs.h`, which the plugin reads
  directly; `check_descriptor.py` compares the two files entry by entry and
  the bench compares the *audio* they produce, sample for sample. That test
  found a real bug on its first run: `activate()` worked out which program
  was in force halfway down, after the smoothed values had already been
  initialised from the knobs.
- **The pitch shifter has no detector in it.** Two read pointers walk a
  delay line half a window apart at the rate the shift asks for, each
  crossfaded with a raised cosine that reaches zero exactly where that
  pointer wraps, so the join is never heard. Formants move with the note,
  which is why down sounds like a bigger singer and up sounds like helium
  rather than like a harmony. Measured: a 220 Hz tone comes out at 440,
  330, 165 or 110 Hz with forty decibels between the new note and the old,
  and the level holds to a tenth of a decibel. At 0 semitones the block
  steps aside rather than sitting there combing the signal with two static
  taps.
- **The USER slots go out through LV2 State as plain floats**, not as the
  struct they live in: a struct has padding, and padding is not something
  to write into somebody's saved session. A state of the wrong size is
  refused rather than believed, and a slot that was never filled stays
  empty rather than coming back full of zeros.
- **The anti-Larsen hunter tells a howl from a note by three tests, and the
  third one is the one that matters.** Sixteen band-pass filters listen to
  the mono sum *after* the notches, so a notch that is working makes its own
  band go quiet and the hunter learns it can eventually let go. A band has
  to be **loud** (dominating the broadband peak, which a chord or a strum
  never does), **steady** (within a couple of decibels of its own 700 ms
  average, which a plucked or bowed note never is)... and **harmonic-free**.
  That last one is the answer to the hard question. A held, vibrato-ed sung
  note passes the first two tests easily: vibrato moves the *pitch*, and
  inside a third of an octave the *level* does not move at all. But a note
  has an octave, and a room mode ringing on its own does not — so if the
  band three up (an octave and a bit) has anything in it, the band is
  vetoed. The bench holds it to that: a howling room is silenced, a sung
  note with harmonics and vibrato collects no notches at all. The price is
  written down honestly — a genuinely pure sustained tone with no harmonics,
  a whistle or a sine pad, looks exactly like feedback and will be notched.
- **Where the howl actually is** comes from fitting a parabola through the
  three neighbouring band levels in the log domain, which places it inside a
  third-octave band to a few percent and buys a notch narrow enough not to
  be heard. Measured, not assumed: at a Q past about five the notch starts
  missing and the howl simply walks to the next peak of the room, costing a
  second notch.
- **Every switch is a 40 ms ramp**, never a branch. The bench throws all
  eight while a note is playing and fails if the biggest sample-to-sample
  step during the throw is more than half again the biggest step while
  nothing is moving.
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

- No pitch *detection*, and therefore no harmony in a key and no
  correction. That is the point, not an omission. Pitch *shifting* by a
  fixed interval needs no detection and is in here.
- No MIDI input: every switch, the two lists and the tap take a control
  port each, which
  is what the Dwarf addresses to a footswitch or to a MIDI CC.
- LOW CUT and the three tone bands have no switch of their own. They have
  neutral positions — 0 Hz and 0 dB — and a switch that only duplicates a
  knob position is a control that can disagree with itself.
- The name of a USER slot lives in the browser that typed it, because a
  control port carries a number and not a string. The sound travels; the
  name does not follow it to another machine.
- The tone controls are three broad parallel bands, not a surgical EQ, and
  the reverb is a Freeverb — a good room, not a convolution.

## Licence

ISC, same as the rest of the repository. See [../LICENSE](../LICENSE).
