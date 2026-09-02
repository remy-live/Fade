#!/usr/bin/env python3
"""Builds install.html from installer-template.html and the tarball.

The page carries the whole bundle base64-encoded, so it can install itself
when opened from the device's own File Manager. That makes it a build
artifact, not source: it is regenerated here rather than hand-edited, and
it is kept out of git along with the tarball (see .gitignore).

After writing it, the payload is decoded back and inspected. A page that
carries the wrong binary looks perfectly fine and installs the wrong
plugin, so the check is not optional.
"""
import base64
import io
import re
import sys
import tarfile

TARBALL = 'fade-aarch64.tar.gz'
TEMPLATE = 'installer-template.html'
OUTPUT = 'install.html'


def build():
    data = open(TARBALL, 'rb').read()
    payload = base64.b64encode(data).decode()

    so = tarfile.open(fileobj=io.BytesIO(data)).extractfile('fade.lv2/fade.so').read()
    stamp = re.search(rb'FADE_BUILD[A-Za-z0-9_]*', so)
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
    so = tar.extractfile('fade.lv2/fade.so').read()

    if so[4] != 2:
        sys.exit('the embedded binary is not 64-bit (ELF class %d)' % so[4])
    if re.search(rb'FADE_BUILD[A-Za-z0-9_]*', so).group().decode() != stamp:
        sys.exit('stamp mismatch between page and binary')
    for needed in ('fade.lv2/fade.ttl', 'fade.lv2/fade_stereo.ttl',
                   'fade.lv2/manifest.ttl', 'fade.lv2/modgui.ttl'):
        if needed not in names:
            sys.exit('%s missing from the embedded bundle' % needed)
    if sum(1 for n in names if n.endswith('.png')) != 4:
        sys.exit('expected four bank images in the embedded bundle')

    # The page is served BY the device, so the request must be relative.
    # A hard-coded address only ever worked over USB.
    if re.search(r"fetch\('https?://", page):
        sys.exit('the page targets an absolute address; use a relative one')

    print('install.html: %s, %d bytes embedded, %d entries'
          % (stamp, len(data), len(names)))


if __name__ == '__main__':
    verify(*build())
