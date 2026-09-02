# -*- coding: utf-8 -*-

"""
nop backend.
"""

__author__     = "Visual Ehrmanntraut <chefkiss.dev>"
__copyright__  = "Copyright 2026, Visual Ehrmanntraut <chefkiss.dev>"
__license__    = "GNU Affero General Public License version 3"

from tracetool import out


PUBLIC = True


def generate_h(event, group):
    for argname in event.args.names():
        out('    (void)%(argname)s;', argname=argname)

