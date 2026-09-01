#!/bin/sh
# Builds fade.lv2 for the MOD Dwarf. aarch64 ONLY: the device is 64-bit,
# and shipping a 32-bit bundle alongside it once got the wrong one installed.
set -e
CFLAGS="-std=c99 -O3 -ffp-contract=fast -fPIC -shared -DNDEBUG -fvisibility=hidden \
 -fno-math-errno -fno-stack-protector -Wall -Wextra"
LDFLAGS="-Wl,--no-undefined -Wl,-O1 -s"

rapper -i turtle -c manifest.ttl >/dev/null 2>&1 || { echo "manifest.ttl is invalid"; exit 1; }
rapper -i turtle -c fade.ttl    >/dev/null 2>&1 || { echo "fade.ttl is invalid";    exit 1; }
rapper -i turtle -c modgui.ttl   >/dev/null 2>&1 || { echo "modgui.ttl is invalid";   exit 1; }
node check_modgui.js || { echo "web UI checks failed"; exit 1; }
python3 check_descriptor.py || { echo "descriptor checks failed"; exit 1; }

rm -rf build-aarch64 fade-aarch64.tar.gz
mkdir -p build-aarch64/fade.lv2/modgui
cp fade.ttl manifest.ttl modgui.ttl build-aarch64/fade.lv2/
cp modgui/* build-aarch64/fade.lv2/modgui/
aarch64-linux-gnu-gcc $CFLAGS $LDFLAGS -I. -o build-aarch64/fade.lv2/fade.so fade.c
( cd build-aarch64 && tar czf ../fade-aarch64.tar.gz fade.lv2 )

# Checks run on the binary EXTRACTED FROM THE TARBALL, not the build tree:
# what ships is what gets verified.
rm -rf /tmp/verif-fade; mkdir -p /tmp/verif-fade
tar xzf fade-aarch64.tar.gz -C /tmp/verif-fade
SO=/tmp/verif-fade/fade.lv2/fade.so
CLASSE=$(od -An -tx1 -N1 -j4 "$SO" | tr -d ' ')
[ "$CLASSE" = "02" ] || { echo "NOT a 64-bit binary (ELF class $CLASSE)"; exit 1; }
aarch64-linux-gnu-objdump -p "$SO" | grep -q "NEEDED.*libm" && { echo "links against libm"; exit 1; }
aarch64-linux-gnu-objdump -T "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -u | grep -v '^GLIBC_2\.17$' \
  && { echo "symbol newer than GLIBC_2.17"; exit 1; }
echo "aarch64 OK - ELF class $CLASSE - $(grep -ao 'FADE_BUILD[A-Za-z0-9_]*' "$SO")"
aarch64-linux-gnu-objdump -p "$SO" | grep NEEDED
