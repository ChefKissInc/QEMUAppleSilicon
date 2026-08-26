#!/usr/bin/env python3

import os
import sys

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} src dest", file=sys.stderr)
    sys.exit(1)

src, dest = sys.argv[1], sys.argv[2]

os.makedirs(os.path.dirname(dest), exist_ok=True)
if os.path.lexists(dest):
    os.unlink(dest)

os.symlink(src, dest)
