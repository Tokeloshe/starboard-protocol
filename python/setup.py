# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds the Starboard C library (the single-file dist/starboard.c) into the
# package as a plain shared library that starboard/_core.py loads with ctypes.
# A tiny Python-module stub lets setuptools compile it portably.
import os
import shutil
import sys

from setuptools import Extension, setup

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.join(HERE, "csrc")
DIST = os.path.join(HERE, "..", "dist")

# Building from the repository: take the current amalgamation. Building from
# an sdist: csrc/ already holds it.
if os.path.isfile(os.path.join(DIST, "starboard.c")):
    os.makedirs(CSRC, exist_ok=True)
    for name in ("starboard.c", "starboard.h"):
        shutil.copyfile(os.path.join(DIST, name), os.path.join(CSRC, name))

if sys.platform == "win32":
    # MSVC does not contract a*b+c into FMA under /fp:precise (the default).
    flags = ["/O2", "/fp:precise"]
else:
    # REQUIRED: keeps the arithmetic bit-identical to every other satellite.
    # The built-in self-test refuses a build that gets this wrong.
    flags = ["-std=c99", "-O2", "-ffp-contract=off"]

setup(
    ext_modules=[
        Extension(
            "starboard._starboard",
            sources=["csrc/starboard.c", "csrc/pystub.c"],
            include_dirs=["csrc"],
            extra_compile_args=flags,
        )
    ]
)
