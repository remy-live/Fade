#!/bin/sh
# Builds voice.lv2 for the MOD Dwarf. aarch64 ONLY: the device is 64-bit,
# and shipping a 32-bit bundle alongside it once got the wrong one
# installed - see the note in the sibling build.sh.
set -e
cd "$(dirname "$0")"

CFLAGS="-std=c99 -O3 -ffp-contract=fast -fPIC -shared -DNDEBUG -fvisibility=hidden \
 -fno-math-errno -fno-stack-protector -Wall -Wextra"
LDFLAGS="-Wl,--no-undefined -Wl,-O1 -s"

rapper -i turtle -c manifest.ttl      >/dev/null 2>&1 || { echo "manifest.ttl is invalid";      exit 1; }
rapper -i turtle -c voice.ttl         >/dev/null 2>&1 || { echo "voice.ttl is invalid";         exit 1; }
rapper -i turtle -c voice_stereo.ttl  >/dev/null 2>&1 || { echo "voice_stereo.ttl is invalid";  exit 1; }
rapper -i turtle -c presets.ttl       >/dev/null 2>&1 || { echo "presets.ttl is invalid";       exit 1; }
rapper -i turtle -c modgui.ttl        >/dev/null 2>&1 || { echo "modgui.ttl is invalid";        exit 1; }
python3 check_descriptor.py || { echo "descriptor checks failed"; exit 1; }
python3 make_images.py > /dev/null || { echo "bank images failed"; exit 1; }
node check_modgui.js        || { echo "web UI checks failed (mono)"; exit 1; }
node check_modgui.js stereo || { echo "web UI checks failed (stereo)"; exit 1; }

# The bench runs on the SAME source that is about to be cross-compiled, and
# the build stops if it fails. Every approximation that stands in for a libm
# call is measured against libm in there; nothing else can catch a bad
# polynomial before a singer does.
gcc -std=c99 -O1 -g -I.. -I. -o test_voice test_voice.c -lm \
    || { echo "the test bench does not build"; exit 1; }
./test_voice > test_voice.log || { echo "TESTS FAILED - see voice/test_voice.log"; exit 1; }
echo "tests: $(grep -c OK test_voice.log) checks pass"

rm -rf build-aarch64 voice-aarch64.tar.gz
mkdir -p build-aarch64/voice.lv2/modgui
cp voice.ttl voice_stereo.ttl presets.ttl manifest.ttl modgui.ttl build-aarch64/voice.lv2/
cp modgui/* build-aarch64/voice.lv2/modgui/
aarch64-linux-gnu-gcc $CFLAGS $LDFLAGS -I.. -I. -o build-aarch64/voice.lv2/voice.so voice.c
( cd build-aarch64 && tar czf ../voice-aarch64.tar.gz voice.lv2 )

# Checks run on the binary EXTRACTED FROM THE TARBALL, not the build tree:
# what ships is what gets verified.
rm -rf /tmp/verif-voice; mkdir -p /tmp/verif-voice
tar xzf voice-aarch64.tar.gz -C /tmp/verif-voice
SO=/tmp/verif-voice/voice.lv2/voice.so
CLASSE=$(od -An -tx1 -N1 -j4 "$SO" | tr -d ' ')
[ "$CLASSE" = "02" ] || { echo "NOT a 64-bit binary (ELF class $CLASSE)"; exit 1; }
aarch64-linux-gnu-objdump -p "$SO" | grep -q "NEEDED.*libm" && { echo "links against libm"; exit 1; }
aarch64-linux-gnu-objdump -T "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -u | grep -v '^GLIBC_2\.17$' \
  && { echo "symbol newer than GLIBC_2.17"; exit 1; }
echo "aarch64 OK - ELF class $CLASSE - $(grep -ao 'VOICE_BUILD[A-Za-z0-9_]*' "$SO")"
aarch64-linux-gnu-objdump -p "$SO" | grep NEEDED

# The installer page carries the bundle, so it is regenerated from the
# tarball that was just built and verified - never edited by hand.
python3 make_installer.py || { echo "installer page failed"; exit 1; }
