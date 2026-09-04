#!/usr/bin/env python3
"""Builds install-voice.html from installer-template.html and the tarball.

The page carries the whole bundle base64-encoded, so it can install itself
when opened from the device's own File Manager. That makes it a build
artifact, not source: it is regenerated here rather than hand-edited, and
it is kept out of git along with the tarball (see .gitignore).

After writing it, the payload is decoded back out of the page and
inspected. A page carrying the wrong binary looks perfectly fine and
installs the wrong plugin, so the check is not optional.
"""
import base64
import io
import re
import sys
import tarfile

TARBALL = 'voice-aarch64.tar.gz'
TEMPLATE = 'installer-template.html'
OUTPUT = 'install-voice.html'
BINARY = 'voice.lv2/voice.so'
REQUIRED = ('voice.lv2/voice.ttl', 'voice.lv2/voice_stereo.ttl',
            'voice.lv2/presets.ttl', 'voice.lv2/manifest.ttl', BINARY)


def build():
    data = open(TARBALL, 'rb').read()
    payload = base64.b64encode(data).decode()

    so = tarfile.open(fileobj=io.BytesIO(data)).extractfile(BINARY).read()
    stamp = re.search(rb'VOICE_BUILD[A-Za-z0-9_]*', so)
    if not stamp:
        sys.exit('no build stamp in the binary')
    stamp = stamp.group().decode()

    page = open(TEMPLATE).read()
    for marker in ('__PAYLOAD_BASE64__', '__BUILD_STAMP__', '__SIZE__'):
        if marker not in page:
            sys.exit('marker %s missing from %s' % (marker, TEMPLATE))
    page = (page.replace('__PAYLOAD_BASE64__', payload)
                .replace('__BUILD_STAMP__', stamp)
                .replace('__SIZE__', str(len(data))))
    open(OUTPUT, 'w').write(page)

    return data, stamp


def verify(data, stamp):
    """Decode what actually landed in the page and look inside it."""
    page = open(OUTPUT).read()
    m = re.search(r'var PKG = "([A-Za-z0-9+/=]+)"', page)
    if not m:
        sys.exit('no payload found in the generated page')

    back = base64.b64decode(m.group(1))
    if back != data:
        sys.exit('the page payload does not match the tarball')

    tar = tarfile.open(fileobj=io.BytesIO(back))
    names = tar.getnames()
    so = tar.extractfile(BINARY).read()

    if so[4] != 2:
        sys.exit('the embedded binary is not 64-bit (ELF class %d)' % so[4])
    if re.search(rb'VOICE_BUILD[A-Za-z0-9_]*', so).group().decode() != stamp:
        sys.exit('stamp mismatch between page and binary')
    for needed in REQUIRED:
        if needed not in names:
            sys.exit('%s missing from the embedded bundle' % needed)

    # The page is served BY the device, so the request must be relative.
    # A hard-coded address only ever worked over USB.
    if re.search(r"fetch\('https?://", page):
        sys.exit('the page targets an absolute address; use a relative one')

    print('install-voice.html: %s, %d bytes embedded, %d entries'
          % (stamp, len(data), len(names)))


if __name__ == '__main__':
    verify(*build())
