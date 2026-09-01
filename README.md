# Fade

An LV2 crossfade plugin for the [MOD Dwarf](https://mod.audio).

## Why

Switching off an echo or a delay with a normal bypass cuts the sound dead:
the tail you were counting on disappears mid-air. Fade fixes that. Send the
dry signal to **IN 1** and the delay or reverb chain to **IN 2**, and one
switch crossfades between them over a time you choose. The tail rings out
instead of being chopped.

The two directions have separate times, so you can leave the effect quickly
and come back slowly, or the reverse.

## Controls

| Control | What it does |
|---|---|
| **TOGGLE** | Off: IN 1 is heard. On: IN 2 is heard. Meant for a footswitch. |
| **TRIGGER** | One pulse starts the fade the other way. Meant for MIDI. |
| **FADE 1>2** | Fade time from IN 1 to IN 2, 0–10000 ms. At 0 the switch is instant. |
| **FADE 2>1** | Fade time back, 0–10000 ms. |
| **GAIN 1** / **GAIN 2** | Input gain, −60 to +12 dB, applied before the crossfade. At −60 dB the input is muted. |
| **PROGRESS** | Display port. Address it to an encoder to get the fade bar on the device screen. |
| **STATE** | Output. 0 = IN 1, 1 = IN 2. |
| **POSITION** | Output. Fade position 0 to 1, drives the bar in the web UI. |

TOGGLE and TRIGGER drive the same internal state, so either one flips it.
They exist as two ports because a port can only take one addressing: this
way you can have a footswitch *and* a MIDI control on the same function.

One consequence worth knowing: after a TRIGGER pulse, the TOGGLE widget may
show the opposite of reality. The STATE output always tells the truth. An
LV2 plugin is not allowed to write into a control input port, so the two
widgets cannot be forced into agreement — publishing the real state on an
output is the honest way round it.

## Install

### With the web page

Open `install.html` from the device's own File Manager (upload it to
User Files first). One button, no terminal.

The page must be served *by the Dwarf* — opening it from your own disk will
not work, the browser blocks the request.

### With the terminal

Check what you actually downloaded before sending it. The browser may have
renamed or unpacked the archive, and a silent failure here looks exactly
like a broken plugin:

```sh
rm -rf /tmp/fade-inst && mkdir -p /tmp/fade-inst
tar xf fade-aarch64.tar.gz -C /tmp/fade-inst
od -An -tx1 -N1 -j4 /tmp/fade-inst/fade.lv2/fade.so     # must print 02
grep -ao 'FADE_BUILD[A-Za-z0-9_]*' /tmp/fade-inst/fade.lv2/fade.so
```

`02` is the ELF class byte: 64-bit. The Dwarf is aarch64 and will refuse a
32-bit binary with `wrong ELF class: ELFCLASS32` in the log.

Then send it, on macOS with `COPYFILE_DISABLE` so the resource-fork files
macOS adds to archives do not end up in the bundle — lilv mistakes them for
bundle directories and the plugin fails to load:

```sh
cd /tmp/fade-inst
COPYFILE_DISABLE=1 tar czf - --exclude='._*' fade.lv2 \
  | ssh root@192.168.51.1 'rm -rf /root/.lv2/fade.lv2 \
      && tar xzf - -C /root/.lv2 \
      && systemctl restart mod-ui'
```

Confirm what landed:

```sh
ssh root@192.168.51.1 "grep -ao 'FADE_BUILD[A-Za-z0-9_]*' /root/.lv2/fade.lv2/fade.so"
```

Remove the block from your pedalboard before installing and add it back
afterwards — mod-ui caches a failed load.

### Removing the web UI without touching the plugin

If the web UI misbehaves, this one command strips it and leaves a working
plugin. It rewrites the manifest too, otherwise lilv looks for a file that
is no longer there:

```sh
ssh root@192.168.51.1 'cd /root/.lv2/fade.lv2 && rm -rf modgui modgui.ttl \
  && printf "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n<http://remy-live.github.io/lv2/fade>\n    a lv2:Plugin ;\n    lv2:binary <fade.so> ;\n    rdfs:seeAlso <fade.ttl> .\n" > manifest.ttl \
  && systemctl restart mod-ui'
```

## Build

Needs an aarch64 cross-compiler, `rapper`, `node` and `python3`:

```sh
./build.sh
```

The script refuses to produce an archive unless the descriptor checks, the
web UI checks, the ELF class and the ABI all pass — and it runs those checks
on the binary *extracted from the archive*, not on the build tree, so what
ships is what gets verified.

Tests:

```sh
gcc -std=c99 -O1 -g -fsanitize=address,undefined -I. -o test_fade fade.c test_fade.c -lm
./test_fade
```

The bench includes a simulated HMI screen. Without one, none of the display
code ever runs, and that is where the bugs live.

## Notes on the implementation

- No libm. dB to linear gain goes through a table in 0.5 dB steps, measured
  against real `pow` to within 0.0036 dB.
- The fade position is kept in `double`. In `float`, accumulating a 1e-6
  step drifted by more than 1 % over a ten second fade.
- The crossfade is linear, not equal-power. The two inputs are usually the
  same source, correlated, so linear keeps the sum at exactly 1. Equal-power
  would put a bump in the middle.
- Screen writes are capped at 25 passes per second, with a full cache flush
  once a second so the display survives the firmware's repaints.

## Licence

ISC. See [LICENSE](LICENSE).
