#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — worst-case stack depth from GCC call-graph info.
# Copyright (C) 2026 James H. All rights reserved.
#
# Reads the .ci files GCC writes with -fcallgraph-info=su (per-function
# static stack usage + call edges) and computes the deepest call chain from
# a root. Fails loudly on recursion (a cycle) or dynamic stack (alloca/VLA),
# both of which would make a static bound impossible — the flight code has
# neither, and this script is the proof.
#
#   python3 tools/stack_depth.py DIR [ROOT]
import glob
import os
import re
import sys


def parse(dirpath):
    frames, edges, dynamic = {}, {}, set()
    node_re = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"\s*label:\s*"([^"]*)"')
    edge_re = re.compile(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s*targetname:\s*"([^"]+)"')
    for f in glob.glob(os.path.join(dirpath, "*.ci")):
        text = open(f, encoding="utf-8", errors="replace").read()
        for m in node_re.finditer(text):
            title, label = m.group(1), m.group(2)
            s = re.search(r"(\d+) bytes \((static|dynamic[^)]*)\)", label)
            if s:
                frames[title] = max(frames.get(title, 0), int(s.group(1)))
                if s.group(2) != "static":
                    dynamic.add(title)
        for m in edge_re.finditer(text):
            edges.setdefault(m.group(1), set()).add(m.group(2))
    return frames, edges, dynamic


def depth(fn, frames, edges, stack, memo):
    if fn in stack:
        raise RuntimeError("recursion: " + " -> ".join(stack + [fn]))
    if fn in memo:
        return memo[fn]
    best, path = 0, []
    for callee in sorted(edges.get(fn, ())):
        d, p = depth(callee, frames, edges, stack + [fn], memo)
        if d > best:
            best, path = d, p
    memo[fn] = (frames.get(fn, 0) + best, [fn] + path)
    return memo[fn]


def main():
    d = sys.argv[1]
    root = sys.argv[2] if len(sys.argv) > 2 else "main"
    frames, edges, dynamic = parse(d)
    if dynamic:
        print("DYNAMIC stack (alloca/VLA) in:", ", ".join(sorted(dynamic)))
        return 1
    try:
        total, path = depth(root, frames, edges, [], {})
    except RuntimeError as e:
        print("FAIL:", e)
        return 1
    external = sorted({c for cs in edges.values() for c in cs if c not in frames})
    print(f"worst-case stack from {root}(): {total} bytes "
          f"(no recursion, no dynamic stack; {len(frames)} functions)")
    print("  deepest chain: " + " -> ".join(
        f"{p.split(':')[-1]}[{frames.get(p, 0)}]" for p in path))
    if external:
        print("  external leaf calls not counted (libc/libm, small leaves): "
              + ", ".join(x.split(':')[-1] for x in external[:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
