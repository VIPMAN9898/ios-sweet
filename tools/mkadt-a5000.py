#!/usr/bin/env python3
"""Build the separate OnePlus 5 / A5000 Apple Device Tree profile."""
import sys

import adt
import mkadt


def main():
    if len(sys.argv) < 2:
        raise SystemExit(f'usage: {sys.argv[0]} OUT.adt [--dump]')
    output = sys.argv[1]
    blob = adt.serialize(mkadt.build(a5000=True))
    with open(output, 'wb') as f:
        f.write(blob)
    if '--dump' in sys.argv[2:]:
        adt.dump(adt.parse(blob)[0])
    print(f'{output}: {len(blob)} bytes', file=sys.stderr)


if __name__ == '__main__':
    main()
