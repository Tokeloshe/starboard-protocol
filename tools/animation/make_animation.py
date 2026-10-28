#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the README animation (docs/media/starboard-in-action.svg) and the
GitHub Pages launch page (site/index.html, from tools/animation/page.html).

Every number on screen comes from the reference demos and the conformance
suite:

    ./build/starboard_crossing_demo     (Act I, the crossing)
    ports/c/murmuration                 (Act II, the 72-craft flock, and the
                                         8-craft formation re-plan in Act III)
    ./build/starboard_conformance       (Act III verdicts, SB-001..SB-005)

If ports/c/murmuration is built, the flock spread, the formation alerts and
the plan table are read live from its transcript; otherwise the transcript
below (captured from v0.6.0) is used. The output is a single self-contained SVG
driven by CSS keyframes only: no script, no external fonts or images, so
GitHub renders and plays it inline in a README.

    python3 tools/animation/make_animation.py
"""
import math
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_SVG = os.path.join(ROOT, "docs", "media", "starboard-in-action.svg")
OUT_HTML = os.path.join(ROOT, "site", "index.html")

T = 61.0                     # loop length, seconds
W, H = 1200, 675

# ----------------------------------------------------------------- data
# Act I, from ./build/starboard_crossing_demo
CROSS = dict(
    a="ACME-1", a_id="2026-001A", a_op="ACME Space",
    b="ORBX-9", b_id="2026-044B", b_op="OrbX",
    tca="900.000 s", miss="150.0 m", pc="1.043e-03",
    bits="0x3F5117B3E9975E2E", conj="0x65AA1F77F9321790",
    dv="1.012 m/s", cw_save="19%", miss_after="1280.0 m", r_safe="1024 m",
    ret1="1.307 m/s", ret2="0.370 m/s", total="2.689 m/s",
)

# Act II, from ports/c/murmuration (v0.6.0 transcript)
HOPS_FALLBACK = """
   hop  1  #*.........**...............................................*...........   5/72 know
   hop  2  ##*.......*##*.........**.......................*...........#*.........*  13/72 know
   hop  3  ###*.....*####*.......*##*.........**...........#*.........*##*.......*#  24/72 know
   hop  4  ####*...*######*.....*####*.......*##*.........*##*.......*####*.....*##  36/72 know
   hop  5  #####*.*########*...*######*.....*####*.......*####*.....*######*...*###  48/72 know
   hop  6  ######*##########*.*########*...*######*.....*######*...*########*.*####  59/72 know
   hop  7  ##################*##########*.*########*...*########*.*##########*#####  67/72 know
   hop  8  ##############################*##########*.*##########*#################  71/72 know
   hop  9  ##########################################*#############################  72/72 know
"""
CLUSTER_FALLBACK = """
 Act II - a tight formation: 8-craft cluster, 2 km apart
   shared CDM: fragment 2009-005CQ vs DC-4, miss 60 m
   hop  1  ..*#*...   3/8 know
   hop  2  .*###*..   5/8 know
   hop  3  *#####*.   7/8 know
   hop  4  #######*   8/8 know
     DC-1  closest   5844 m at burn+  624 s  clear
     DC-2  closest   3851 m at burn+  620 s  clear
     DC-3  closest   1861 m at burn+  604 s  clear
     DC-5  closest    569 m at burn+13885 s  <- FLOCK ALERT
     DC-6  closest    593 m at burn+ 9045 s  <- FLOCK ALERT
     DC-7  closest     61 m at burn+ 5429 s  <- FLOCK ALERT
     DC-8  closest   2001 m at burn+ 5144 s  clear
      2700 s    3 conflicts  1.00 m/s    1 conflict   8.02 m/s
      3600 s    3 conflicts  1.12 m/s    1 conflict   8.98 m/s
      4500 s    2 conflicts  1.93 m/s    1 conflict  15.42 m/s
      5400 s    1 conflict   6.62 m/s    3 conflicts 52.94 m/s
      6300 s    0 conflicts  5.33 m/s    2 conflicts 42.64 m/s
      7200 s    4 conflicts  1.75 m/s    1 conflict  13.98 m/s
   chosen: burn lead 6300 s, DC-4 alone
   (closest pair 1231 m, closest to the fragment 1184 m, 5.33 m/s total).
"""

# Act III verdicts, from ./build/starboard_conformance (SB-C1 vectors)
VERDICTS = [
    ("SB-001", "R4", "starboard", "crossing"),
    ("SB-004", "R1", "capability", "derelict stands on"),
    ("SB-002", "R3", "overtaking", "chaser gives way"),
    ("SB-005", "R2", "tier", "crewed stands on"),
    ("SB-003", "R5", "fair coin", "head-on"),
]

FLOCK = dict(digest="1E3AC8C816631BCF", nb="P5S01", nb_km="317 km",
             frag="1993-036BX", frag_miss="140 m", dv="0.315 m/s",
             notice="204 bytes", msgs="284")
P_PLANES, S_SLOTS = 6, 12
N_SAT = P_PLANES * S_SLOTS


def read_transcript():
    exe = os.path.join(ROOT, "ports", "c", "murmuration")
    if os.access(exe, os.X_OK):
        try:
            text = subprocess.run([exe], capture_output=True, text=True,
                                  timeout=60).stdout
            if text:
                return text, "live run of ports/c/murmuration"
        except Exception:
            pass
    return HOPS_FALLBACK + CLUSTER_FALLBACK, "embedded v0.6.0 transcript"


def read_cluster(text):
    """Act II of murmuration: the 8-craft formation, alerts and plan table."""
    part = text[text.find("8-craft cluster"):] if "8-craft cluster" in text else text
    hops = [None] * 8
    for m in re.finditer(r"hop\s+(\d+)\s+([#*.]{8})\s+(\d+)/8", part):
        for i, ch in enumerate(m.group(2)):
            if ch == "*" and hops[i] is None:
                hops[i] = int(m.group(1))
        if int(m.group(3)) == 8:
            break
    origin = [i for i, h in enumerate(hops) if h is None]
    for i in origin:
        hops[i] = 0
    alerts = [(m.group(1), int(m.group(2)), m.group(3) is not None) for m in re.finditer(
        r"(DC-\d)\s+closest\s+(\d+) m at burn\+\s*\d+ s\s+(<- FLOCK ALERT)?", part)]
    rows = [(int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4)), m.group(5))
            for m in re.finditer(r"^\s+(\d+) s\s+(\d+) conflicts?\s+([\d.]+) m/s\s+"
                                 r"(\d+) conflicts?\s+([\d.]+) m/s", part, re.M)]
    chosen = re.search(r"chosen: burn lead (\d+) s", part)
    clo = re.search(r"closest pair (\d+) m, closest to the fragment (\d+) m, ([\d.]+) m/s", part)
    frag = re.search(r"fragment (\S+) vs (DC-\d), miss (\d+) m", part)
    if not (len(origin) == 1 and len(alerts) == 7 and len(rows) >= 4 and chosen and clo and frag):
        sys.exit("could not parse the 8-craft formation transcript")
    return dict(hops=hops, origin=origin[0], alerts=alerts, rows=rows,
                chosen=int(chosen.group(1)), pair=clo.group(1), frag_m=clo.group(2),
                dv=clo.group(3), frag=frag.group(1), mover=frag.group(2), miss=frag.group(3))


def read_hops(text):
    src = ""
    rounds = [None] * N_SAT
    rounds[0] = 0
    for m in re.finditer(r"hop\s+(\d+)\s+([#*.]{72})\s+(\d+)/72", text):
        h, bar = int(m.group(1)), m.group(2)
        for i, ch in enumerate(bar):
            if ch == "*" and rounds[i] is None:
                rounds[i] = h
        if int(m.group(3)) == N_SAT:
            break
    if any(r is None for r in rounds):
        sys.exit("could not parse the 72-satellite flock transcript")
    m = re.search(r"digest ([0-9A-F]{16})", text)
    if m:
        FLOCK["digest"] = m.group(1)
    print(f"flock hops: {max(rounds)} hops to 72/72", file=sys.stderr)
    return rounds


def neighbours(i):
    p, k = divmod(i, S_SLOTS)
    return [p * S_SLOTS + (k + 1) % S_SLOTS, p * S_SLOTS + (k - 1) % S_SLOTS,
            ((p + 1) % P_PLANES) * S_SLOTS + k, ((p - 1) % P_PLANES) * S_SLOTS + k]


# -------------------------------------------------------- css keyframes
CSS = []
_n = [0]


def f(x):
    s = f"{x:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


OFF = [0.0]                  # time shift applied to every keyframe (per act)


def anim(points, prop, cls=None, ease="linear"):
    """Register keyframes; return a class name. points: [(t, css_value)]."""
    _n[0] += 1
    name = cls or f"a{_n[0]}"
    pts = sorted(((t + OFF[0], v) for t, v in points), key=lambda p: p[0])
    pre = [p for p in pts if p[0] <= 0]
    pts = [p for p in pts if 0 < p[0] < T]
    pts.insert(0, (0.0, pre[-1][1] if pre else pts[0][1]))
    pts.append((T, pts[-1][1]))
    frames = []
    last = -1.0
    for t, v in pts:
        pct = max(t / T * 100, last + 0.001)
        if pct > 100:
            continue
        last = pct
        key = f"{pct:.3f}".rstrip("0").rstrip(".")
        frames.append(f"{key}%{{{prop}:{v}}}")
    CSS.append(f"@keyframes {name}{{{''.join(frames)}}}"
               f".{name}{{animation:{name} {f(T)}s {ease} infinite}}")
    return name


def fade(t_in, t_out, d=0.5, peak=1.0, d_out=None):
    d_out = d if d_out is None else d_out
    return anim([(t_in, 0), (t_in + d, peak), (t_out - d_out, peak), (t_out, 0)],
                "opacity")


def fade_to(points):
    return anim(points, "opacity")


def move(samples):
    return anim([(t, f"translate({f(x)}px,{f(y)}px)") for t, x, y in samples],
                "transform")


# ------------------------------------------------------------- helpers
def pchip(xs, ys):
    n = len(xs)
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0:
            m[i] = 0.0
        else:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])

    def ev(x):
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        i = max(j for j in range(n - 1) if xs[j] <= x)
        t = (x - xs[i]) / h[i]
        h00, h10 = 2 * t**3 - 3 * t**2 + 1, t**3 - 2 * t**2 + t
        h01, h11 = -2 * t**3 + 3 * t**2, t**3 - t**2
        return h00 * ys[i] + h10 * h[i] * m[i] + h01 * ys[i + 1] + h11 * h[i] * m[i + 1]
    return ev


def smooth(a, b, t):
    if t <= a:
        return 0.0
    if t >= b:
        return 1.0
    x = (t - a) / (b - a)
    return x * x * (3 - 2 * x)


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


MONO = "ui-monospace,'SF Mono',SFMono-Regular,Menlo,Consolas,'Liberation Mono',monospace"
SANS = "ui-sans-serif,-apple-system,'Segoe UI',Helvetica,Arial,sans-serif"

AMBER, CYAN, ROSE, MINT, INK, DIM = "#FFB547", "#62D6FF", "#FF5D73", "#7CF2C2", "#E9EEF7", "#7C8AA5"


def text(x, y, s, size=13, fill=INK, family=MONO, weight=400, anchor="start",
         ls=0, cls="", extra="", halo=False):
    c = f' class="{cls}"' if cls else ""
    if halo:
        extra += ' stroke="#060A14" stroke-width="4" stroke-linejoin="round" paint-order="stroke"'
    l = f' letter-spacing="{ls}"' if ls else ""
    return (f'<text x="{f(x)}" y="{f(y)}" font-family="{family}" font-size="{size}" '
            f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"{l}{c}{extra}>'
            f'{esc(s)}</text>')



# ============================================================ ACT III
A3_IN, A3_OUT = 42.6, 57.9          # absolute seconds in the loop
T_REF = A3_IN - 0.6                 # field positions are defined at this time


def build_act3(A, cl):
    """The mega-constellation: crossing trains of craft flowing in unison,
    conjunctions resolving at different moments with the real conformance
    verdicts, and the murmuration formation re-plan as the centrepiece."""
    rnd = random.Random(467)
    t_end = A3_OUT + 0.6

    def unit(deg):
        a = math.radians(deg)
        return math.cos(a), math.sin(a)

    act = fade(A3_IN, A3_OUT, .9)
    A(f'<g class="{act}">')

    # Earth far below: a few great curves of the ground track grid
    A('<g fill="none" stroke="#7FA9E6" stroke-opacity=".05" stroke-width="1">')
    for r in (1500, 1700, 1900, 2100, 2300):
        A(f'<circle cx="600" cy="{600 + r}" r="{r}"/>')
    A('</g>')

    # the field is dimmed while the formation takes the stage
    field_op = anim([(HERO0 - .2, 1), (HERO0 + .6, .45), (57.0, .45), (57.7, .8)], "opacity")
    A(f'<g class="{field_op}">')

    # ------------------------------------------------------------ streams
    LAY = {
        "far":  dict(r=.9, op=.4, v=20, sp=17, tr=8,  col="#8FB4F0"),
        "mid":  dict(r=1.35, op=.64, v=31, sp=23, tr=14, col="#C9DBFA"),
        "near": dict(r=2.0, op=.95, v=46, sp=33, tr=24, col="#FFFFFF"),
    }
    STREAMS = [
        ("far", -16, -150), ("far", 24, 70), ("far", -52, 210), ("far", 47, -190),
        ("far", -98, -330), ("far", 101, 300), ("far", 8, 260), ("far", -27, 120),
        ("far", 36, -20), ("far", -44, -280), ("far", 55, 330), ("far", -84, 60),
        ("mid", -31, 40), ("mid", 29, -95), ("mid", -63, -215), ("mid", 61, 175),
        ("mid", -7, 225), ("mid", 13, -255), ("mid", -22, -330), ("mid", 42, 300),
        ("near", -39, -60), ("near", 35, 115), ("near", -74, 265), ("near", 77, -250),
    ]
    sym = []
    dots = []                       # (stream index, x0, y0) at T_REF, group-local
    for si, (lay, ang, offn) in enumerate(STREAMS):
        L = LAY[lay]
        ux, uy = unit(ang)
        nx, ny = -uy, ux
        bx, by = 600 + nx * offn, 337 + ny * offn
        sym.append(f'<g id="st{si}" transform="rotate({ang})">'
                   f'<rect x="-{L["tr"]}" y="-{f(L["r"] * .55)}" width="{L["tr"]}" '
                   f'height="{f(L["r"] * 1.1)}" fill="url(#trl_{lay})"/>'
                   f'<circle r="{L["r"]}" fill="{L["col"]}"/></g>')
        travel = L["v"] * (t_end - T_REF)
        phase = rnd.uniform(0, L["sp"])
        k = -80
        while k < 80:
            k += 1
            if rnd.random() < .09:                  # trains have gaps
                continue
            s0 = phase + k * L["sp"] + rnd.uniform(-1.6, 1.6)
            x0, y0 = bx + ux * s0, by + uy * s0
            seen = False
            for q in range(7):
                d = travel * q / 6
                x, y = x0 + ux * d, y0 + uy * d
                if -40 < x < W + 40 and -40 < y < H + 40:
                    seen = True
                    break
            if seen:
                dots.append((si, x0, y0))
    STREAM_SYMS.extend(sym)

    # ------------------------------------------------- conjunction events
    EVENTS = [
        # verdict index, te, P, a(angle, speed), b(angle, speed, kind), side
        (0, 45.0, (905, 168), (-22, 48), (23, 48, "craft"), -1),
        (1, 46.1, (318, 292), (-28, 46), (205, 14, "derelict"), 1),
        (2, 47.2, (930, 478), (198, 80), (198, 44, "craft"), 1),
        (3, 48.2, (585, 150), (64, 46), (8, 26, "station"), -1),
        (4, 49.2, (712, 566), (4, 46), (184, 46, "craft"), 1),
    ]
    waves = []                      # (t0, x, y, R)

    def bump(t, te):
        return smooth(te - 1.25, te - .2, t) * (1 - smooth(te + .7, te + 1.9, t))

    actors = []
    for vi, te, P, (aa, va), (ab, vb, kind), side in EVENTS:
        uax, uay = unit(aa)
        ubx, uby = unit(ab)
        D = 12.0
        lat = 3.5 if kind == "craft" and abs(((aa - ab) % 360) - 180) < 5 else 0

        def pa(t, P=P, uax=uax, uay=uay, va=va, te=te, side=side):
            b_ = bump(t, te) * D * side
            return (P[0] + uax * va * (t - te) - uay * b_, P[1] + uay * va * (t - te) + uax * b_)

        def pb(t, P=P, ubx=ubx, uby=uby, vb=vb, te=te, lat=lat):
            return (P[0] + ubx * vb * (t - te) - uby * lat, P[1] + uby * vb * (t - te) + ubx * lat)

        actors.append((pa, aa, "giver", te, None))
        actors.append((pb, ab, kind, te, None))
        waves.append((te - 1.15, *pa(te - 1.15), 235.0))
        EVENT_LOG.append((te - 1.25, vi))
        EVENT_MARKS.append((te, P, vi, pa(te + .1), side, (uax, uay)))

    # ------------------------------------------------ flashes in the field
    C_WAVE = 170.0
    flashes = {}
    for di, (si, x0, y0) in enumerate(dots):
        lay, ang, _ = STREAMS[si]
        ux, uy = unit(ang)
        v = LAY[lay]["v"]
        for (tw, sx, sy, R) in waves:
            def g(t):
                x = x0 + ux * v * (t - T_REF)
                y = y0 + uy * v * (t - T_REF)
                return math.hypot(x - sx, y - sy) - C_WAVE * (t - tw)
            if g(tw) > R + 30:
                continue
            lo, hi = tw, tw + (R + 60) / C_WAVE
            if g(hi) > 0:
                continue
            for _ in range(28):
                mid = (lo + hi) / 2
                lo, hi = (mid, hi) if g(mid) > 0 else (lo, mid)
            d = C_WAVE * (hi - tw)
            if d <= R:
                flashes.setdefault(di, []).append((hi, .95 - .6 * d / R))

    # draw streams, one moving group per stream
    by_stream = {}
    for di, (si, x0, y0) in enumerate(dots):
        by_stream.setdefault(si, []).append((di, x0, y0))
    for si, items in by_stream.items():
        lay, ang, _ = STREAMS[si]
        ux, uy = unit(ang)
        v = LAY[lay]["v"]
        dx, dy = ux * v * (t_end - T_REF), uy * v * (t_end - T_REF)
        mv = anim([(T_REF, "translate(0px,0px)"), (t_end, f"translate({f(dx)}px,{f(dy)}px)")],
                  "transform")
        A(f'<g class="{mv}" opacity="{LAY[lay]["op"]}">')
        for di, x0, y0 in items:
            A(f'<use xlink:href="#st{si}" x="{f(x0)}" y="{f(y0)}"/>')
        for di, x0, y0 in items:
            if di in flashes:
                pts = []
                for ta, pk in sorted(flashes[di]):
                    pts += [(ta - .02, 0), (ta, f(pk)), (ta + .7, 0)]
                A(f'<circle cx="{f(x0)}" cy="{f(y0)}" r="7" fill="url(#flash)" '
                  f'class="{anim(pts, "opacity")}" opacity="0"/>')
        A('</g>')

    # wave rings
    for (tw, sx, sy, R) in waves:
        sc = anim([(tw - .01, "scale(.02)"), (tw, "scale(.02)"), (tw + R / C_WAVE, "scale(1)")], "transform")
        op = anim([(tw - .01, 0), (tw, .55), (tw + R / C_WAVE, 0)], "opacity")
        A(f'<circle cx="{f(sx)}" cy="{f(sy)}" r="{f(R)}" fill="none" stroke="{MINT}" stroke-width="1.2" '
          f'style="transform-box:fill-box;transform-origin:center" class="{sc} {op}" opacity="0"/>')

    # screening rings and verdict badges at each conjunction
    for te, P, vi, pa_te, side, (ux, uy) in EVENT_MARKS:
        sb, rule, rname, what = VERDICTS[vi]
        pul = []
        t = te - 2.3
        while t < te - .3:
            pul += [(t, "scale(.5)"), (t + .8, "scale(1.5)")]
            t += .8
        A(f'<g class="{fade(te - 2.4, te + .2, .3)}">'
          f'<circle cx="{f(P[0])}" cy="{f(P[1])}" r="15" fill="none" stroke="{ROSE}" stroke-width="1" '
          f'style="transform-box:fill-box;transform-origin:center" '
          f'class="{anim(pul, "transform")} {anim([(q[0], 0 if "1.5" in q[1] else .9) for q in pul], "opacity")}"/></g>')
        bx = P[0] + 18 if P[0] < 980 else P[0] - 150
        by = P[1] - 20 if P[1] > 120 else P[1] + 34
        A(f'<g class="{fade(te - 1.5, te + 2.3, .3)}">'
          f'<rect x="{f(bx)}" y="{f(by - 12)}" width="26" height="16" rx="8" fill="{AMBER}" fill-opacity=".18" stroke="{AMBER}" stroke-opacity=".8"/>'
          + text(bx + 13, by, rule, 10, AMBER, anchor="middle", weight=700, ls=.6)
          + text(bx + 33, by, rname, 10.5, INK, ls=.6, halo=True)
          + text(bx + 33, by + 14, f"{what} · {sb}", 9.5, DIM, ls=.4, halo=True)
          + '</g>')

    # actors: craft that take part in a conjunction
    for pfun, ang, kind, te, _ in actors:
        t0, t1 = T_REF, t_end
        smp = []
        t = t0
        while t <= t1 + 1e-6:
            dense = te - 1.6 < t < te + 2.2
            smp.append((t, *pfun(t)))
            t += .1 if dense else .5
        mv = move(smp)
        A(f'<g class="{mv}">')
        if kind == "derelict":
            spin = anim([(0, "rotate(0deg)"), (T, "rotate(1800deg)")], "transform")
            A(f'<g style="transform-box:fill-box;transform-origin:center" class="{spin}">'
              f'<path d="M-3.4,-1.6 L1.4,-3.6 L3.8,.6 L.4,3.4 L-2.8,2.2 Z" fill="#8C93A3"/></g>')
            A(text(-44, 16, "derelict", 9.5, DIM, ls=.6, halo=True,
                   cls=fade(te - 2.6, te + 1.8, .4)))
        elif kind == "station":
            A(f'<g transform="rotate({ang})"><rect x="-26" y="-1" width="26" height="2" fill="url(#trl_near)"/>'
              f'<rect x="-1.2" y="-8" width="2.4" height="16" fill="#E9EEF7" opacity=".8"/>'
              f'<rect x="-4" y="-2.4" width="8" height="4.8" rx="1" fill="#fff"/></g>')
        else:
            A(f'<use xlink:href="#act{0 if kind == "giver" else 1}" transform="rotate({ang})"/>')
        col = AMBER if kind == "giver" else CYAN
        A(f'<g class="{fade(te - 1.5, te + 2.3, .25)}" opacity="0">'
          f'<circle r="9" fill="url(#{"glowA" if kind == "giver" else "glowC"})"/>'
          f'<circle r="2.6" fill="{col}"/></g>')
        A('</g>')
    A('</g>')                        # field

    # ------------------------------------------------- the formation (hero)
    hero(A, cl)

    # ----------------------------------------------------------- HUD
    A(f'<g class="{fade(A3_IN + .3, A3_OUT, .6)}">')
    A('<rect x="40" y="88" width="420" height="72" rx="10" fill="url(#hudfade)"/>')
    A(text(64, 120, "THE MEGA-CONSTELLATION", 15, INK, family=SANS, weight=600, ls=4))
    A(text(64, 143, "crossing shells · every craft runs the same rules on board", 11, DIM, ls=.4))
    A('</g>')
    A(f'<g class="{fade(43.4, A3_OUT, .6)}">'
      f'<rect x="40" y="446" width="400" height="176" rx="10" fill="url(#hudfade)"/>'
      + text(64, 476, "VERDICTS, EACH PAIR ALONE", 9.5, DIM, ls=2) + '</g>')
    for row, (tl, vi) in enumerate(EVENT_LOG + [(HERO0 + 3.9, None)]):
        y = 500 + row * 21
        if vi is None:
            A(f'<g class="{fade(tl, A3_OUT, .35)}">'
              + text(64, y, "L4", 10.5, MINT, weight=700, ls=.6)
              + text(98, y, "flock", 10.5, INK, ls=.4)
              + text(196, y, f'formation re-plan, {cl["mover"]}', 10.5, DIM, ls=.3)
              + '</g>')
            continue
        sb, rule, rname, what = VERDICTS[vi]
        A(f'<g class="{fade(tl, A3_OUT, .35)}">'
          + text(64, y, rule, 10.5, AMBER, weight=700, ls=.6)
          + text(98, y, rname, 10.5, INK, ls=.4)
          + text(196, y, what, 10.5, DIM, ls=.3)
          + text(420, y, sb, 9.5, DIM, anchor="end", ls=.4)
          + '</g>')
    A(text(1160, 640, "every pair decides alone · 0 bytes exchanged to agree", 10.5, DIM,
           anchor="end", ls=.6, halo=True, cls=fade(44.0, A3_OUT, .6)))
    A('</g>')                        # act 3


HERO0 = 49.6                         # the formation enters


def hero(A, cl):
    """8 craft, 2 km apart, moving as one; one must give way to a fragment."""
    ang = -24.0
    ux, uy = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    nx, ny = -uy, ux
    SP = 34.0                        # px per 2 km
    v = 50.0
    S = (372.0, 478.0)               # centroid at HERO0
    t_end = A3_OUT + .6
    mover = int(cl["mover"].split("-")[1]) - 1
    slots = [(S[0] + ux * (3.5 - i) * SP, S[1] + uy * (3.5 - i) * SP) for i in range(8)]

    T_N1, HOP = HERO0 + 1.9, .34                    # first announcement
    T_AL = T_N1 + 4 * HOP + .25                     # alerts raised
    T_SC = T_AL + .9                                # plans screened
    T_N2 = T_SC + 1.35                              # re-announce, seq 2
    T_TCA = T_N2 + 1.5                              # fragment passes the slot

    dx, dy = ux * v * (t_end - HERO0), uy * v * (t_end - HERO0)
    grp = anim([(HERO0, "translate(0px,0px)"), (t_end, f"translate({f(dx)}px,{f(dy)}px)")], "transform")
    A(f'<g class="{fade(HERO0, A3_OUT, .7)}"><g class="{grp}">')

    # ring of laser links: neighbours along the string, and the wrap link
    for i in range(7):
        a, b = slots[i], slots[i + 1]
        A(f'<line x1="{f(a[0])}" y1="{f(a[1])}" x2="{f(b[0])}" y2="{f(b[1])}" stroke="#8FB7FF" stroke-opacity=".35" stroke-width="1"/>')
    a, b = slots[7], slots[0]
    mx, my = (a[0] + b[0]) / 2 + nx * 70, (a[1] + b[1]) / 2 + ny * 70
    A(f'<path d="M{f(a[0])},{f(a[1])} Q{f(mx)},{f(my)} {f(b[0])},{f(b[1])}" fill="none" stroke="#8FB7FF" '
      f'stroke-opacity=".22" stroke-dasharray="2 4"/>')

    # notice hops along the ring, twice (plan, then re-plan)
    hops = cl["hops"]
    parent = {}
    for i in range(8):
        if hops[i] > 0:
            parent[i] = min(j for j in ((i - 1) % 8, (i + 1) % 8) if hops[j] == hops[i] - 1)
    for i, j in parent.items():
        a, b = slots[j], slots[i]
        pts = []
        for t0 in (T_N1, T_N2):
            th = t0 + (hops[i] - 1) * HOP
            pts += [(th - .01, 0), (th, .95), (th + .55, 0)]
        A(f'<line x1="{f(a[0])}" y1="{f(a[1])}" x2="{f(b[0])}" y2="{f(b[1])}" stroke="{MINT}" '
          f'stroke-width="2.2" class="{anim(pts, "opacity")}" opacity="0"/>')

    # plan A (announced): drift back through the neighbours' slots
    s4 = slots[mover]
    xa = 3 * SP - 4
    pa = [(s4[0] - ux * x + nx * 5 * math.sin(math.pi * x / xa),
           s4[1] - uy * x + ny * 5 * math.sin(math.pi * x / xa)) for x in range(0, int(xa) + 1, 2)]
    A(f'<polyline points="{" ".join(f"{f(x)},{f(y)}" for x, y in pa)}" fill="none" stroke="{ROSE}" '
      f'stroke-width="1.3" stroke-dasharray="3 3" class="{fade(T_N1 - .3, T_SC + .6, .3)}" opacity="0"/>')
    # plan B (chosen): bow out beside the string and come home
    def offB(t):
        b = smooth(T_N2 + .1, T_N2 + 1.0, t) * (1 - smooth(T_TCA + .5, T_TCA + 1.5, t))
        dr = smooth(T_N2 + .1, T_N2 + 1.2, t) * (1 - smooth(T_TCA + .4, T_TCA + 1.6, t))
        return (-nx * 26 * b - ux * 12 * dr, -ny * 26 * b - uy * 12 * dr)
    pb = []
    for q in range(0, 41):
        t = T_N2 + (T_TCA + 1.6 - T_N2) * q / 40
        ox, oy = offB(t)
        pb.append((s4[0] + ox, s4[1] + oy))
    A(f'<polyline points="{" ".join(f"{f(x)},{f(y)}" for x, y in pb)}" fill="none" stroke="{AMBER}" '
      f'stroke-width="1.3" stroke-dasharray="3 3" class="{fade(T_SC + .5, T_TCA + 1.4, .3)}" opacity="0"/>')

    # members
    for i, (x, y) in enumerate(slots):
        if i == mover:
            continue
        A(f'<circle cx="{f(x)}" cy="{f(y)}" r="15" fill="url(#glowW)"/>'
          f'<circle cx="{f(x)}" cy="{f(y)}" r="3.8" fill="#fff"/>')
        if hops[i] > 0:
            pts = []
            for t0 in (T_N1, T_N2):
                th = t0 + (hops[i] - 1) * HOP + HOP * .6
                pts += [(th - .01, 0), (th, 1), (th + .6, 0)]
            A(f'<circle cx="{f(x)}" cy="{f(y)}" r="16" fill="url(#flash)" class="{anim(pts, "opacity")}" opacity="0"/>')
    # alerts on the neighbours plan A would sweep through
    for name, dist, alert in cl["alerts"]:
        if not alert:
            continue
        i = int(name.split("-")[1]) - 1
        x, y = slots[i]
        pul = anim([(T_AL - .01, 0), (T_AL, 1), (T_AL + .35, .35), (T_AL + .7, 1), (T_AL + 1.05, .35),
                    (T_SC + .9, .35), (T_SC + 1.3, 0)], "opacity")
        A(f'<g class="{pul}" opacity="0"><circle cx="{f(x)}" cy="{f(y)}" r="10" fill="none" stroke="{ROSE}" stroke-width="1.4"/></g>')
        A(text(x + nx * 20 - 12, y + ny * 20 + 6, f"{dist} m", 9.5, ROSE, ls=.4, halo=True,
               cls=fade(T_AL + .1, T_SC + 1.2, .3)))
    # the mover
    smp = []
    t = HERO0
    while t <= t_end:
        ox, oy = offB(t)
        smp.append((t, s4[0] + ox, s4[1] + oy))
        t += .1 if T_N2 - .2 < t < T_TCA + 1.8 else .6
    A(f'<g class="{move([(tt, x - s4[0], y - s4[1]) for tt, x, y in smp])}">'
      f'<circle cx="{f(s4[0])}" cy="{f(s4[1])}" r="18" fill="url(#glowA)"/>'
      f'<circle cx="{f(s4[0])}" cy="{f(s4[1])}" r="4.2" fill="#fff"/>'
      f'<circle cx="{f(s4[0])}" cy="{f(s4[1])}" r="4.2" fill="{AMBER}" opacity=".7"/></g>')
    A(text(s4[0] - nx * 24 - 14, s4[1] - ny * 24 + 2, f'{cl["mover"]}', 11, AMBER, weight=700, ls=1,
           halo=True, cls=fade(HERO0 + .4, T_SC + .2, .4)))
    # captions riding with the formation
    cx, cy = slots[0][0] - nx * 30 + ux * 14, slots[0][1] - ny * 30 + uy * 14
    def cap(t0, t1, l1, c1, l2=None):
        A(f'<g class="{fade(t0, t1, .3)}">' + text(cx, cy, l1, 11, c1, family=SANS, weight=600, ls=2, halo=True)
          + (text(cx, cy + 16, l2, 10, INK, ls=.4, halo=True) if l2 else "") + '</g>')
    cap(HERO0 + .3, T_N1 - .1, "8 CRAFT · 2 KM APART", INK, "one formation, one ledger")
    cap(T_N1 - .1, T_AL, f'{cl["mover"]} GIVES WAY', AMBER, "announces its plan to the ring")
    cap(T_AL, T_SC, "FLOCK ALERT ×3", ROSE, "same ledger on all 8: they agree on who")
    cap(T_SC, T_N2 + .3, "SCREEN EVERY PLAN", INK, "against the whole flock and the fragment")
    cap(T_N2 + .3, T_TCA + .1, "RE-ANNOUNCE · SEQ 2", MINT, "the update ripples through the ring")
    cap(T_TCA + .1, A3_OUT, "ALL CLEAR", MINT,
        f'closest pair {cl["pair"]} m · fragment {cl["frag_m"]} m')
    A('</g></g>')

    # the fragment, in the world frame, crossing the mover's old slot at TCA
    sx = s4[0] + ux * v * (T_TCA - HERO0)
    sy = s4[1] + uy * v * (T_TCA - HERO0)
    fa = ang + 104
    fx, fy = math.cos(math.radians(fa)), math.sin(math.radians(fa))
    vf = 62.0
    fr = move([(HERO0, sx + fx * vf * (HERO0 - T_TCA), sy + fy * vf * (HERO0 - T_TCA)),
               (t_end, sx + fx * vf * (t_end - T_TCA), sy + fy * vf * (t_end - T_TCA))])
    spin = anim([(0, "rotate(0deg)"), (T, "rotate(2160deg)")], "transform")
    A(f'<g class="{fade(HERO0 + .6, A3_OUT, .5)}"><g class="{fr}">'
      f'<circle r="8" fill="url(#glowR)"/>'
      f'<g style="transform-box:fill-box;transform-origin:center" class="{spin}">'
      f'<path d="M-3,-1.4 L1.2,-3.2 L3.2,.8 L-.2,3 L-2.6,1.6 Z" fill="{ROSE}"/></g>'
      + text(9, 16, f'fragment {cl["frag"]} · miss {cl["miss"]} m', 9.5, ROSE, ls=.4, halo=True,
             cls=fade(HERO0 + .8, T_SC, .4))
      + '</g></g>')

    # the plan table, straight from the transcript
    px, py = 800, 446
    A(f'<g class="{fade(T_SC - .1, T_TCA + .6, .35)}">')
    A(f'<rect x="{px}" y="{py}" width="350" height="{48 + 19 * len(cl["rows"])}" rx="10" fill="#070C18" '
      f'fill-opacity=".86" stroke="#2A3550"/>')
    A(text(px + 18, py + 24, "LEAD", 9, DIM, ls=1.6))
    A(text(px + 98, py + 24, f'{cl["mover"]} ALONE', 9, DIM, ls=1.6))
    A(text(px + 238, py + 24, "ALL 8 IN UNISON", 9, DIM, ls=1.6))
    for r, (lead, c1, dv1, c2, dv2) in enumerate(cl["rows"]):
        y = py + 46 + r * 19
        best = lead == cl["chosen"]
        t0 = T_SC + .05 + r * .1
        A(f'<g class="{fade(t0, T_TCA + .6, .2)}">')
        if best:
            A(f'<rect x="{px + 8}" y="{y - 13}" width="334" height="18" rx="5" fill="{AMBER}" '
              f'fill-opacity=".14" stroke="{AMBER}" stroke-opacity=".7" class="{fade(T_SC + .75, T_TCA + .6, .2)}"/>')
        A(text(px + 18, y, f"{lead} s", 10.5, INK if best else DIM, ls=.3))
        A(text(px + 98, y, f"{c1} · {dv1} m/s", 10.5,
               AMBER if best else DIM, ls=.2))
        A(text(px + 238, y, f"{c2} · {dv2} m/s", 10.5, DIM, ls=.2))
        A('</g>')
    A('</g>')


STREAM_SYMS, EVENT_LOG, EVENT_MARKS = [], [], []


# =================================================================== build
def build():
    text_, src = read_transcript()
    rounds = read_hops(text_)
    cluster = read_cluster(text_)
    print(f"transcript: {src}", file=sys.stderr)
    out = []
    A = out.append

    # ---------------------------------------------------------- backdrop
    A('<rect width="1200" height="675" fill="url(#bg)"/>')
    rnd = random.Random(1990)
    # twinkle loops: build repeating keyframes across the whole loop
    star_cls = []
    for ph in range(3):
        pts, t = [], ph * 1.6
        pts.append((0, .5))
        while t < T:
            pts += [(t, .25), (min(T, t + 2.4), .95)]
            t += 4.8
        star_cls.append(anim(pts, "opacity"))
    A('<g>')
    for i in range(170):
        x, y = rnd.uniform(0, W), rnd.uniform(0, H)
        r = rnd.choice([.45, .55, .65, .8, 1.0, 1.25])
        op = rnd.uniform(.25, .75)
        c = star_cls[i % 3] if i % 4 == 0 else ""
        cl = f' class="{c}"' if c else ""
        A(f'<circle cx="{f(x)}" cy="{f(y)}" r="{r}" fill="#CFE3FF" opacity="{f(op)}"{cl}/>')
    A('</g>')

    # ==================================================== ACT I: crossing
    OFF[0] = -1.8                    # acts I and II run 1.8 s earlier than authored
    C = (600.0, 250.0)
    DIAG = 1 / math.sqrt(2)
    LA, LB = 420.0, 245.0            # px per unit progress, ACME and ORBX

    # time-dilated approach: fast in, slow while each craft thinks, fast out
    u_of = pchip([4.4, 8.0, 17.0, 19.0, 23.0, 27.0, 29.0],
                 [-1.0, -.60, -.44, -.36, 0.0, .80, .99])

    def off_y(t):                    # ACME radial offset (down = toward Earth)
        return 44 * smooth(17.9, 22.6, t) * (1 - smooth(25.6, 28.4, t))

    def acme(t):
        u = u_of(t)
        return C[0] + u * LA, C[1] + off_y(t)

    def orbx(t):
        u = u_of(t)
        return C[0] + u * LB * DIAG, C[1] + u * LB * DIAG

    act1_in, act1_out = 4.2, 30.4
    act1 = fade(act1_in, act1_out, .8)
    A(f'<g class="{act1}">')

    # Earth limb
    A('<circle cx="600" cy="2330" r="1830" fill="url(#earth)" opacity=".75"/>')
    A('<circle cx="600" cy="2330" r="1830" fill="none" stroke="url(#limb)" stroke-width="1.6" opacity=".75"/>')
    A('<circle cx="600" cy="2330" r="1846" fill="none" stroke="#5FB4FF" stroke-width="22" opacity=".05" filter="url(#soft)"/>')

    # tracks
    trk_a = fade(4.6, act1_out, 1.0, .55)
    trk_b = fade(5.0, 26.4, 1.0, .55)
    A(f'<line x1="110" y1="250" x2="1090" y2="250" stroke="url(#trkA)" stroke-width="1" class="{trk_a}"/>')
    x0, y0 = C[0] - 250 * DIAG * 1.3, C[1] - 250 * DIAG * 1.3
    x1, y1 = C[0] + 300 * DIAG, C[1] + 300 * DIAG
    A(f'<line x1="{f(x0)}" y1="{f(y0)}" x2="{f(x1)}" y2="{f(y1)}" stroke="url(#trkB)" stroke-width="1" class="{trk_b}"/>')
    A(text(120, 276, f'{CROSS["a"]}  ·  equatorial, 552 km', 10.5, AMBER, ls=1.2,
           cls=fade(5.2, 16.5, .8, .7)))
    A(text(C[0] - 120 * DIAG - 12, C[1] - 120 * DIAG + 26, f'{CROSS["b"]}  ·  45° incl', 10.5, CYAN, ls=1.2, anchor="end",
           cls=fade(5.6, 16.5, .8, .7)))

    # ACME flown path after the burn (revealed in step with the craft)
    path_pts = [(t / 10, *acme(t / 10)) for t in range(178, 292, 2)]
    d = "M" + " L".join(f"{f(x)},{f(y)}" for _, x, y in path_pts)
    seg = [0.0]
    for i in range(1, len(path_pts)):
        seg.append(seg[-1] + math.dist(path_pts[i - 1][1:], path_pts[i][1:]))
    L = seg[-1]
    reveal = anim([(0, f(L))] + [(t, f(L - s)) for (t, _, _), s in zip(path_pts, seg)],
                  "stroke-dashoffset")
    A(f'<g class="{fade(17.8, act1_out, .3)}"><path d="{d}" fill="none" stroke="{AMBER}" '
      f'stroke-width="1.6" stroke-dasharray="{f(L)} {f(L)}" class="{reveal}" opacity=".85"/></g>')
    # original (abandoned) path, dashed
    A(f'<line x1="{f(acme(17.9)[0])}" y1="250" x2="1090" y2="250" stroke="{AMBER}" '
      f'stroke-width="1" stroke-dasharray="2 6" class="{fade(18.2, 28.6, .6, .5)}"/>')

    # danger ring at the predicted crossing
    pulse_pts = []
    t = 8.4
    while t < 18.4:
        pulse_pts += [(t, "scale(.55)"), (t + 1.6, "scale(1.6)")]
        t += 1.6
    A(f'<g class="{fade(8.2, 19.2, .5)}">'
      f'<circle cx="600" cy="250" r="20" fill="none" stroke="{ROSE}" stroke-width="1" '
      f'style="transform-box:fill-box;transform-origin:center" '
      f'class="{anim(pulse_pts, "transform")} {anim([(p[0], 0 if "1.6" in p[1] else .9) for p in pulse_pts], "opacity")}"/>'
      f'<circle cx="600" cy="250" r="3" fill="{ROSE}"/>'
      f'</g>')
    A(text(626, 226, f'predicted miss {CROSS["miss"]}', 11, ROSE, ls=.6, cls=fade(8.6, 18.6, .6)))
    A(text(626, 242, f'Pc {CROSS["pc"]}  ≥  1e-4', 11, ROSE, ls=.6, cls=fade(9.0, 18.6, .6, .75)))

    # CDM delivery, one message, two independent receivers
    cdm = fade(5.8, 9.2, .4)
    A(f'<g class="{cdm}">')
    A(text(600, 92, "CCSDS  CONJUNCTION  DATA  MESSAGE", 10.5, DIM, anchor="middle", ls=2.4))
    for who, col in ((acme, AMBER), (orbx, CYAN)):
        tx, ty = who(7.2)
        L2 = math.dist((600, 102), (tx, ty))
        dash = anim([(0, f(L2)), (6.0, f(L2)), (7.1, 0)], "stroke-dashoffset")
        A(f'<line x1="600" y1="102" x2="{f(tx)}" y2="{f(ty)}" stroke="{col}" stroke-width="1" '
          f'stroke-dasharray="{f(L2)} {f(L2)}" class="{dash}" opacity=".7"/>')
    A('</g>')

    # satellites
    def craft(col, ang, trail_len, sampler, t_in, t_out, name):
        smp = [(t / 10, *sampler(t / 10)) for t in range(int(t_in * 10), int(t_out * 10) + 1, 2)]
        mv = move(smp)
        vis = fade(t_in, t_out, .6)
        A(f'<g class="{vis}"><g class="{mv}"><g transform="rotate({ang})">')
        A(f'<line x1="{-trail_len}" y1="0" x2="-4" y2="0" stroke="url(#trail{name})" stroke-width="2.2" stroke-linecap="round"/>')
        A(f'<circle r="11" fill="{col}" opacity=".16" filter="url(#glow)"/>')
        A(f'<rect x="-1.2" y="-9" width="2.4" height="18" rx="1" fill="{col}" opacity=".55"/>')
        A(f'<rect x="-2.2" y="-12" width="4.4" height="6" rx=".8" fill="{col}" opacity=".9"/>')
        A(f'<rect x="-2.2" y="6" width="4.4" height="6" rx=".8" fill="{col}" opacity=".9"/>')
        A(f'<circle r="3.4" fill="#fff"/><circle r="3.4" fill="{col}" opacity=".55"/>')
        A('</g>')
        return smp, mv

    smpA, mvA = craft(AMBER, 0, 90, acme, 4.4, 29.6, "A")
    # ring pulses when the CDM lands, and the burns
    def ring_at(cls_move, col, t0, r=16, dur=1.1):
        sc = anim([(t0 - .01, "scale(.3)"), (t0, "scale(.3)"), (t0 + dur, "scale(1.8)")], "transform")
        op = anim([(t0 - .01, 0), (t0, .9), (t0 + dur, 0)], "opacity")
        A(f'<circle r="{r}" fill="none" stroke="{col}" stroke-width="1.2" '
          f'style="transform-box:fill-box;transform-origin:center" class="{sc} {op}"/>')

    ring_at(None, AMBER, 7.25)
    # burn flare (thrust opposes the along-track motion)
    def flare(t0, dur=0.9):
        A(f'<g class="{fade(t0, t0 + dur, .15, 1, .5)}">'
          f'<path d="M4,0 L28,-3.5 L34,0 L28,3.5 Z" fill="url(#flare)" filter="url(#glow)"/>'
          f'<path d="M4,0 L22,-1.6 L26,0 L22,1.6 Z" fill="#FFF3D6"/></g>')
    flare(17.6, 1.2)
    flare(25.6, .8)
    flare(28.1, .7)
    A('</g></g>')

    smpB, mvB = craft(CYAN, 45, 70, orbx, 4.4, 26.6, "B")
    ring_at(None, CYAN, 7.25)
    A('</g></g>')

    # TCA measurement
    yA = acme(23.0)[1]
    A(f'<g class="{fade(22.7, 25.6, .4)}">'
      f'<line x1="574" y1="250" x2="574" y2="{f(yA)}" stroke="{MINT}" stroke-width="1"/>'
      f'<line x1="568" y1="250" x2="580" y2="250" stroke="{MINT}" stroke-width="1"/>'
      f'<line x1="568" y1="{f(yA)}" x2="580" y2="{f(yA)}" stroke="{MINT}" stroke-width="1"/>'
      + text(560, (250 + yA) / 2 + 4, CROSS["miss_after"], 12, MINT, anchor="end", ls=.6)
      + '</g>')

    # HUD panels, identical timing on both craft
    def panel(x, who, wid, op, col, verdict, verdict_col):
        y = 446
        A(f'<g class="{fade_to([(7.6, 0), (8.4, 1), (16.9, 1), (17.6, .28), (act1_out - .8, .28), (act1_out, 0)])}">')
        A(f'<rect x="{x}" y="{y}" width="380" height="190" rx="10" fill="#0A1020" fill-opacity=".78" '
          f'stroke="{col}" stroke-opacity=".35"/>')
        A(f'<rect x="{x}" y="{y}" width="3" height="190" rx="1.5" fill="{col}" opacity=".9"/>')
        A(text(x + 20, y + 30, who, 14, col, weight=700, ls=1.6))
        A(text(x + 20 + len(who) * 10.6 + 14, y + 30, wid, 11, DIM, ls=.6))
        A(text(x + 360, y + 30, "ON BOARD · ALONE", 9.5, DIM, anchor="end", ls=1.6))
        rows = [("TCA", CROSS["tca"]), ("MISS", CROSS["miss"]), ("Pc", CROSS["pc"]), ("BITS", None),
                ("RULES", None), ("VERDICT", None)]
        for i, (k, v) in enumerate(rows):
            ty = y + 58 + i * 22
            t0 = 8.9 + i * 1.05
            A(text(x + 20, ty, k, 9.5, DIM, ls=1.8, cls=fade(t0, 40, .35)))
            if v:
                A(text(x + 104, ty, v, 13, INK, cls=fade(t0 + .15, 40, .35)))
        # bits: scramble then resolve
        ty, t0 = y + 58 + 3 * 22, 8.9 + 3 * 1.05
        r2 = random.Random(7)
        for j in range(3):
            junk = "0x" + "".join(r2.choice("0123456789ABCDEF") for _ in range(16))
            A(text(x + 104, ty, junk, 13, DIM, cls=fade(t0 + .1 + j * .22, t0 + .32 + j * .22, .05)))
        A(text(x + 104, ty, CROSS["bits"], 13, INK, weight=600, cls=fade(t0 + .78, 40, .2)))
        # rule chain
        ty, t0 = y + 58 + 4 * 22, 8.9 + 4 * 1.05
        for j in range(5):
            px = x + 104 + j * 46
            lit = j == 3
            on = t0 + .2 + j * .32
            if j < 4:
                A(f'<rect x="{px}" y="{ty - 12}" width="38" height="16" rx="8" fill="none" stroke="{DIM}" '
                  f'stroke-opacity=".5" class="{fade(t0, 40, .3)}"/>')
            else:
                A(f'<rect x="{px}" y="{ty - 12}" width="38" height="16" rx="8" fill="none" stroke="{DIM}" '
                  f'stroke-opacity=".25" stroke-dasharray="2 3" class="{fade(t0, 40, .3)}"/>')
            A(text(px + 19, ty, f"R{j + 1}", 10, DIM, anchor="middle", ls=.8, cls=fade(t0, 40, .3)))
            if j < 3:
                A(f'<rect x="{px}" y="{ty - 12}" width="38" height="16" rx="8" fill="{DIM}" '
                  f'class="{fade(on, on + .45, .1, .35, .3)}"/>')
            if lit:
                A(f'<g class="{fade(on, 40, .25)}"><rect x="{px}" y="{ty - 12}" width="38" height="16" rx="8" '
                  f'fill="{verdict_col}" opacity=".2" stroke="{verdict_col}"/>'
                  + text(px + 19, ty, "R4", 10, verdict_col, anchor="middle", weight=700, ls=.8) + '</g>')
        # verdict
        ty, t0 = y + 58 + 5 * 22, 8.9 + 5 * 1.05
        A(text(x + 104, ty, verdict, 13.5, verdict_col, weight=700, ls=1.4, cls=fade(t0 + .25, 40, .4)))
        A(text(x + 360, ty, "Rule 4 · starboard", 10.5, verdict_col, anchor="end", ls=.6,
               cls=fade(t0 + .6, 40, .4, .8)))
        A('</g>')

    panel(48, CROSS["a"], CROSS["a_id"], CROSS["a_op"], AMBER, "I GIVE WAY", AMBER)
    panel(772, CROSS["b"], CROSS["b_id"], CROSS["b_op"], CYAN, "I STAND ON", CYAN)

    # centre column: agreement, then the narrative of the maneuver
    A(f'<g class="{fade(15.4, 17.4, .5)}">')
    A(f'<line x1="430" y1="560" x2="472" y2="560" stroke="{AMBER}" stroke-opacity=".6"/>')
    A(f'<line x1="728" y1="560" x2="770" y2="560" stroke="{CYAN}" stroke-opacity=".6"/>')
    A(text(600, 528, "BIT-IDENTICAL", 17, INK, family=SANS, weight=600, anchor="middle", ls=4))
    A(text(600, 553, "same Pc · same verdict · same id", 11, DIM, anchor="middle", ls=.4))
    A(text(600, 572, f'conj {CROSS["conj"]}', 11, DIM, anchor="middle", ls=.4))
    A(text(600, 598, "0 bytes exchanged", 12, MINT, anchor="middle", ls=1.6))
    A('</g>')

    def caption(t0, t1, lines, col=INK):
        A(f'<g class="{fade(t0, t1, .45)}">')
        for i, (s, c, sz) in enumerate(lines):
            A(text(600, 512 + i * 24, s, sz, c or col, anchor="middle",
                   ls=(2.6 if i == 0 else .5), family=(SANS if i == 0 else MONO),
                   weight=(600 if i == 0 else 400)))
        A('</g>')

    caption(17.4, 21.2, [(f'{CROSS["a"]} GIVES WAY', AMBER, 14),
                         (f'min-Δv burn {CROSS["dv"]}', INK, 12.5),
                         (f'Clohessy-Wiltshire plan, {CROSS["cw_save"]} cheaper', DIM, 11)])
    caption(21.2, 22.9, [(f'{CROSS["b"]} HOLDS COURSE', CYAN, 14),
                         ("predictable by design", DIM, 11.5)])
    caption(22.9, 25.5, [("CLOSEST APPROACH", MINT, 14),
                         (f'{CROSS["miss_after"]}  ≥  R_SAFE {CROSS["r_safe"]}', INK, 12.5),
                         (f'was {CROSS["miss"]}', DIM, 11)])
    caption(25.5, 29.8, [("BACK TO ITS SLOT", AMBER, 14),
                         (f'return burns {CROSS["ret1"]} + {CROSS["ret2"]}', INK, 12.5),
                         ("offset 0.000 m · drift 0.000000 m/s", DIM, 11)])
    A('</g>')   # act 1

    # ===================================================== ACT II: flock
    act2_in, act2_out = 30.2, 44.6
    act2 = fade(act2_in, act2_out, 1.0)
    GC = (640.0, 322.0)
    RE, RS = 148.0, 214.0            # Earth and (stylised) shell radius, px
    inc = math.radians(53)

    def eci(p, k, du=0.0):
        raan = math.radians(p * 60.0)
        u = math.radians(k * 30.0 + p * 5.0) + du
        x, y = math.cos(u), math.sin(u)
        # rotate by inclination about x, then RAAN about z
        y, z = y * math.cos(inc), y * math.sin(inc)
        x, y = x * math.cos(raan) - y * math.sin(raan), x * math.sin(raan) + y * math.cos(raan)
        return x, y, z

    # choose a view that puts the origin satellite on the near side
    ox, oy, oz = eci(0, 0)
    az = -math.atan2(oy, ox) - math.radians(70)
    el = math.radians(18)

    def proj(v, r=RS):
        x, y, z = v
        x, y = x * math.cos(az) - y * math.sin(az), x * math.sin(az) + y * math.cos(az)
        # y is depth toward viewer after tilt
        y, z = y * math.cos(el) - z * math.sin(el), y * math.sin(el) + z * math.cos(el)
        return GC[0] + r * x, GC[1] - r * z, -y      # depth >0 = in front

    pos = [proj(eci(*divmod(i, S_SLOTS))) for i in range(N_SAT)]

    A(f'<g class="{act2}">')
    # back halves of rings
    rings_back, rings_front = [], []
    for p in range(P_PLANES):
        pts = [proj(eci(p, 0, math.radians(a))) for a in range(0, 361, 3)]
        cur, front = [], pts[0][2] > 0
        for q in pts:
            if (q[2] > 0) != front and cur:
                (rings_front if front else rings_back).append(cur + [q])
                cur, front = [], q[2] > 0
            cur.append(q)
        (rings_front if front else rings_back).append(cur)

    def poly(seg, stroke, op, w=1):
        return (f'<polyline points="{" ".join(f"{f(a)},{f(b)}" for a, b, _ in seg)}" fill="none" '
                f'stroke="{stroke}" stroke-opacity="{op}" stroke-width="{w}"/>')

    for sgm in rings_back:
        A(poly(sgm, "#8FB7FF", .10))

    links = set()
    for i in range(N_SAT):
        for j in neighbours(i):
            links.add((min(i, j), max(i, j)))

    def link_line(i, j, op):
        a, b = pos[i], pos[j]
        return (f'<line x1="{f(a[0])}" y1="{f(a[1])}" x2="{f(b[0])}" y2="{f(b[1])}" '
                f'stroke="#8FB7FF" stroke-opacity="{op}" stroke-width=".7"/>')

    for i, j in sorted(links):
        if pos[i][2] + pos[j][2] < 0:
            A(link_line(i, j, .07))

    HOP0, HOPDT = 32.6, .9
    t_of = lambda h: HOP0 + (h - 1) * HOPDT
    parent = [None] * N_SAT
    for i in range(1, N_SAT):
        parent[i] = min(j for j in neighbours(i) if rounds[j] == rounds[i] - 1)

    lit = {}
    for h in range(0, max(rounds) + 1):
        t0 = t_of(h) if h else HOP0 - .9
        lit[h] = anim([(t0 - .01, 0), (t0, 1), (act2_out, 1)], "opacity")

    def sat_dot(i, front):
        x, y, _ = pos[i]
        h = rounds[i]
        col = AMBER if i == 0 else MINT
        s = (f'<circle cx="{f(x)}" cy="{f(y)}" r="2.6" fill="#9FB3D6" opacity="{".85" if front else ".45"}"/>'
             f'<g class="{lit[h]}" opacity="0"><circle cx="{f(x)}" cy="{f(y)}" r="9" fill="{col}" opacity=".22" filter="url(#glow)"/>'
             f'<circle cx="{f(x)}" cy="{f(y)}" r="3.1" fill="#fff"/>'
             f'<circle cx="{f(x)}" cy="{f(y)}" r="3.1" fill="{col}" opacity=".6"/></g>')
        return s

    for i in range(N_SAT):
        if pos[i][2] < 0:
            A(f'<g opacity=".5">{sat_dot(i, False)}</g>')

    def tree_links(front_layer):
        for i in range(1, N_SAT):
            j, h = parent[i], rounds[i]
            a, b = pos[j], pos[i]
            front = a[2] + b[2] >= 0
            if front != front_layer:
                continue
            t1 = t_of(h)
            t0 = t1 - HOPDT * .85
            op = 1 if front else .5
            L3 = math.dist(a[:2], b[:2])
            dash = anim([(t0, f(L3)), (t1, 0)], "stroke-dashoffset")
            lo = anim([(t0 - .01, 0), (t0, .95 * op), (t1 + .4, .95 * op), (t1 + 1.6, .32 * op),
                       (act2_out, .32 * op)], "opacity")
            A(f'<line x1="{f(a[0])}" y1="{f(a[1])}" x2="{f(b[0])}" y2="{f(b[1])}" stroke="{MINT}" '
              f'stroke-width="1.3" stroke-dasharray="{f(L3)} {f(L3)}" class="{dash} {lo}" opacity="0"/>')
            mv = move([(t0, a[0], a[1]), (t1, b[0], b[1])])
            po = anim([(t0 - .01, 0), (t0, op), (t1 - .05, op), (t1 + .1, 0)], "opacity")
            A(f'<g class="{po}" opacity="0"><g class="{mv}"><circle r="2.2" fill="#fff"/>'
              f'<circle r="6" fill="{MINT}" opacity=".35" filter="url(#glow)"/></g></g>')

    def ripples(front_layer):
        for i in range(N_SAT):
            h = rounds[i]
            x, y, z = pos[i]
            if (z >= 0) != front_layer:
                continue
            t0 = t_of(h) if h else HOP0 - .9
            col = AMBER if i == 0 else MINT
            sc = anim([(t0 - .01, "scale(.2)"), (t0, "scale(.2)"), (t0 + 1.2, "scale(1.4)")], "transform")
            op = anim([(t0 - .01, 0), (t0, .9 if z >= 0 else .4), (t0 + 1.2, 0)], "opacity")
            A(f'<circle cx="{f(x)}" cy="{f(y)}" r="13" fill="none" stroke="{col}" stroke-width="1" '
              f'style="transform-box:fill-box;transform-origin:center" class="{sc} {op}" opacity="0"/>')

    tree_links(False)
    ripples(False)

    # Earth
    A(f'<circle cx="{f(GC[0])}" cy="{f(GC[1])}" r="{RE + 16}" fill="#3C8DFF" opacity=".06" filter="url(#soft)"/>')
    A(f'<circle cx="{f(GC[0])}" cy="{f(GC[1])}" r="{RE}" fill="url(#globe)"/>')
    # graticule
    A(f'<g fill="none" stroke="#9CC6FF" stroke-opacity=".08" stroke-width=".8">')
    for latd in (-60, -30, 0, 30, 60):
        lat = math.radians(latd)
        pts = []
        for a in range(0, 361, 4):
            lo = math.radians(a)
            v = (math.cos(lat) * math.cos(lo), math.cos(lat) * math.sin(lo), math.sin(lat))
            q = proj(v, RE)
            if q[2] > 0:
                pts.append(q)
            elif pts:
                A(poly(pts, "#9CC6FF", .08)); pts = []
        if pts:
            A(poly(pts, "#9CC6FF", .08))
    for lod in range(0, 180, 30):
        lo = math.radians(lod)
        pts = []
        for a in range(0, 361, 4):
            lat = math.radians(a)
            v = (math.cos(lat) * math.cos(lo), math.cos(lat) * math.sin(lo), math.sin(lat))
            q = proj(v, RE)
            if q[2] > 0:
                pts.append(q)
            elif pts:
                A(poly(pts, "#9CC6FF", .08)); pts = []
        if pts:
            A(poly(pts, "#9CC6FF", .08))
    A('</g>')
    A(f'<circle cx="{f(GC[0])}" cy="{f(GC[1])}" r="{RE}" fill="url(#globeShade)"/>')
    A(f'<circle cx="{f(GC[0])}" cy="{f(GC[1])}" r="{RE}" fill="none" stroke="url(#rim)" stroke-width="1.6"/>')

    for sgm in rings_front:
        A(poly(sgm, "#8FB7FF", .22))
    for i, j in sorted(links):
        if pos[i][2] + pos[j][2] >= 0:
            A(link_line(i, j, .13))

    tree_links(True)

    for i in range(N_SAT):
        if pos[i][2] >= 0:
            A(sat_dot(i, True))

    ripples(True)

    # every craft checks the mover's new path against itself
    chk = anim([(41.0, 0), (41.35, .9), (42.2, 0)], "opacity")
    A(f'<g class="{chk}" opacity="0">')
    for i in range(1, N_SAT):
        x, y, z = pos[i]
        A(f'<circle cx="{f(x)}" cy="{f(y)}" r="7" fill="none" stroke="#fff" stroke-opacity="{".7" if z >= 0 else ".25"}" stroke-width=".8"/>')
    A('</g>')

    # origin: the dodge
    x, y, _ = pos[0]
    A(f'<g class="{fade(30.9, act2_out, .5)}">')
    for k in range(3):
        t0 = 31.2 + k * .55
        sc = anim([(t0 - .01, "scale(.3)"), (t0, "scale(.3)"), (t0 + 1.4, "scale(2.2)")], "transform")
        op = anim([(t0 - .01, 0), (t0, .9), (t0 + 1.4, 0)], "opacity")
        A(f'<circle cx="{f(x)}" cy="{f(y)}" r="12" fill="none" stroke="{AMBER}" '
          f'style="transform-box:fill-box;transform-origin:center" class="{sc} {op}" opacity="0"/>')
    A(text(x + 16, y - 16, "P0S00", 11, AMBER, weight=700, ls=1.2, halo=True))
    A('</g>')
    # the dead fragment slipping past
    fr = move([(30.6, x - 70, y + 34), (33.6, x + 52, y - 26)])
    A(f'<g class="{fade(30.6, 33.8, .4, .9)}"><g class="{fr}">'
      f'<path d="M-2,-1.5 L1.5,-2.5 L2.6,1 L-.6,2.6 Z" fill="{ROSE}"/></g></g>')
    A(text(x - 78, y + 56, f'fragment {FLOCK["frag"]} · miss {FLOCK["frag_miss"]}', 10, ROSE, ls=.6, halo=True,
           cls=fade(30.8, 33.6, .4, .85)))

    # closest neighbour callout
    nb = int(FLOCK["nb"][1]) * S_SLOTS + int(FLOCK["nb"][3:])
    x2, y2, _ = pos[nb]
    A(f'<g class="{fade(42.2, act2_out, .5)}">'
      f'<circle cx="{f(x2)}" cy="{f(y2)}" r="7" fill="none" stroke="{INK}" stroke-opacity=".8"/>'
      + text(x2 + 12, y2 + 18, f'{FLOCK["nb"]} · {FLOCK["nb_km"]} · clear', 10.5, INK, ls=.6, halo=True)
      + '</g>')

    # left column narration
    LX = 64
    A(text(LX, 120, "THE MURMURATION", 15, INK, family=SANS, weight=600, ls=4,
           cls=fade(30.6, act2_out, .6)))
    A(text(LX, 142, "72 satellites · 6 planes × 12 · 550 km · 53°", 11, DIM, ls=.4,
           cls=fade(30.8, act2_out, .6)))
    A(text(LX, 194, "P0S00 GIVES WAY", 12, AMBER, family=SANS, weight=600, ls=2.4,
           cls=fade(31.0, act2_out, .5)))
    A(text(LX, 214, f'debris can’t be verified · burn {FLOCK["dv"]}', 11, DIM, ls=.3,
           cls=fade(31.2, act2_out, .5)))
    A(text(LX, 234, f'maneuver notice · {FLOCK["notice"]} · 4 laser links', 11, DIM, ls=.3,
           cls=fade(31.6, act2_out, .5)))

    counts = []
    for h in range(0, max(rounds) + 1):
        counts.append(sum(1 for r in rounds if r <= h))
    for h in range(0, max(rounds) + 1):
        t0 = t_of(h) if h else 31.9
        t1 = t_of(h + 1) if h < max(rounds) else act2_out
        cls = anim([(t0 - .01, 0), (t0, 1), (t1 - .01, 1), (t1, 0)] if h < max(rounds)
                   else [(t0 - .01, 0), (t0, 1), (t1 - .6, 1), (t1, 0)], "opacity")
        A(f'<g class="{cls}" opacity="0">')
        A(text(LX, 312, "HOP", 10, DIM, ls=2.4))
        A(text(LX, 352, f"{h}", 40, INK, family=SANS, weight=300))
        A(text(LX + 92, 312, "KNOW", 10, DIM, ls=2.4))
        A(text(LX + 92, 352, f"{counts[h]}", 40, MINT if h else AMBER, family=SANS, weight=300))
        A(text(LX + 92 + (26 if counts[h] < 10 else 48), 352, "/72", 15, DIM, family=SANS))
        A('</g>')

    # the terminal strip: 72 cells, exactly the transcript order
    sx, sy, cw = 64, 410, 3.6
    A(f'<g class="{fade(31.8, act2_out, .5)}">')
    A(text(sx, sy - 12, "FLOCK LEDGER, 72 CRAFT", 9.5, DIM, ls=2))
    for i in range(N_SAT):
        cx = sx + i * cw
        A(f'<rect x="{f(cx)}" y="{sy}" width="{f(cw - 1)}" height="12" rx=".6" fill="#2A3550"/>')
        A(f'<rect x="{f(cx)}" y="{sy}" width="{f(cw - 1)}" height="12" rx=".6" '
          f'fill="{AMBER if i == 0 else MINT}" class="{lit[rounds[i]]}" opacity="0"/>')
    A('</g>')

    # verdict of the flock
    A(f'<g class="{fade(42.0, act2_out, .6)}">')
    A(text(LX, 476, "ALL 71 LEDGERS IDENTICAL", 13, MINT, family=SANS, weight=600, ls=2.6))
    A(text(LX, 498, f'digest {FLOCK["digest"]} on every satellite', 11, INK, ls=.3))
    A(text(LX, 518, f'0 conflicts · nobody else moves', 11, DIM, ls=.3))
    A(text(LX, 538, f'{FLOCK["msgs"]} link messages · duplicates dropped', 11, DIM, ls=.3))
    A('</g>')
    A(text(LX, 600, "the flock warns, it never decides", 11, DIM, ls=1.2,
           cls=fade(43.0, act2_out, .6, .8)))
    A('</g>')  # act 2
    OFF[0] = 0.0

    build_act3(A, cluster)

    # =========================================================== chrome
    # title card
    A(f'<g class="{fade_to([(0, 1), (2.0, 1), (2.6, 0), (60.3, 0), (T, 1)])}">')
    A(text(600, 318, "STARBOARD", 50, INK, family=SANS, weight=200, anchor="middle", ls=18))
    A(f'<line x1="520" y1="342" x2="680" y2="342" stroke="{AMBER}" stroke-opacity=".7"/>')
    A(text(600, 374, "the rules of the orbital road", 15, DIM, family=SANS, anchor="middle", ls=3))
    A(text(600, 420, "two satellites · no radio contact · one answer, bit for bit", 11.5, DIM,
           anchor="middle", ls=1.2))
    A('</g>')
    # end card
    A(f'<g class="{fade(58.0, 60.3, .6, 1, .5)}">')
    A(text(600, 330, "rules need bits that agree.", 22, INK, family=SANS, weight=300, anchor="middle", ls=2.5))
    A(text(600, 364, "github.com/Tokeloshe/starboard-protocol", 11.5, DIM, anchor="middle", ls=1.4))
    A('</g>')

    # header: wordmark, act, mission clock
    hdr = fade(2.6, A3_OUT, .8)
    A(f'<g class="{hdr}">')
    A(text(40, 44, "STARBOARD", 12, INK, family=SANS, weight=600, ls=5))
    A(f'<line x1="166" y1="34" x2="166" y2="48" stroke="{DIM}" stroke-opacity=".5"/>')
    A(text(180, 44, "ACT ONE   THE CROSSING", 10.5, DIM, ls=2.6, cls=fade(2.6, 28.4, .6)))
    A(text(180, 44, "ACT TWO   THE FLOCK", 10.5, DIM, ls=2.6, cls=fade(28.8, 42.6, .6)))
    A(text(180, 44, "ACT THREE   THE MEGA-CONSTELLATION", 10.5, DIM, ls=2.6, cls=fade(43.0, A3_OUT, .6)))
    OFF[0] = -1.8
    clock = [(4.6, 17.4, "T−15:00"), (17.4, 19.6, "T−14:30"), (19.6, 21.3, "T−07:30"),
             (21.3, 22.8, "T−03:45"), (22.8, 25.5, "T+00:00  TCA"), (25.5, 27.8, "T+80:41"),
             (27.8, 30.2, "T+272:03"), (30.6, 44.6, "T−6h")]
    for t0, t1, s in clock:
        A(text(1160, 44, s, 12, INK, anchor="end", ls=1.4,
               cls=anim([(t0 - .01, 0), (t0, 1), (t1 - .01, 1), (t1, 0)], "opacity")))
    OFF[0] = 0.0
    A(text(1160, 44, "CONTINUOUS", 12, INK, anchor="end", ls=1.4, cls=fade(43.0, A3_OUT, .4)))
    A(text(1160 - 150, 44, "MISSION CLOCK", 9, DIM, anchor="end", ls=2))
    A('</g>')
    # progress hairline
    A(f'<rect x="-1200" y="673" width="1200" height="2" fill="{AMBER}" opacity=".55" '
      f'class="{anim([(0, "translateX(0px)"), (T, "translateX(1200px)")], "transform")}"/>')

    # ------------------------------------------------------------ defs
    defs = f'''<defs>
<radialGradient id="bg" cx="50%" cy="30%" r="80%"><stop offset="0" stop-color="#0B1426"/><stop offset=".6" stop-color="#060A14"/><stop offset="1" stop-color="#03050A"/></radialGradient>
<radialGradient id="earth" cx="50%" cy="0%" r="60%"><stop offset="0" stop-color="#123A6B"/><stop offset=".35" stop-color="#0A1D38"/><stop offset="1" stop-color="#050B16"/></radialGradient>
<linearGradient id="limb" x1="0" x2="1"><stop offset="0" stop-color="#5FB4FF" stop-opacity="0"/><stop offset=".5" stop-color="#9BD3FF" stop-opacity=".9"/><stop offset="1" stop-color="#5FB4FF" stop-opacity="0"/></linearGradient>
<radialGradient id="globe" cx="38%" cy="32%" r="80%"><stop offset="0" stop-color="#1A4C86"/><stop offset=".55" stop-color="#0C2443"/><stop offset="1" stop-color="#050C18"/></radialGradient>
<radialGradient id="globeShade" cx="30%" cy="25%" r="85%"><stop offset=".55" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".55"/></radialGradient>
<linearGradient id="rim" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9BD3FF" stop-opacity=".9"/><stop offset=".6" stop-color="#5FB4FF" stop-opacity=".15"/><stop offset="1" stop-color="#5FB4FF" stop-opacity="0"/></linearGradient>
<linearGradient id="trkA" x1="0" x2="1"><stop offset="0" stop-color="{AMBER}" stop-opacity="0"/><stop offset=".25" stop-color="{AMBER}"/><stop offset=".75" stop-color="{AMBER}"/><stop offset="1" stop-color="{AMBER}" stop-opacity="0"/></linearGradient>
<linearGradient id="trkB" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{CYAN}" stop-opacity="0"/><stop offset=".3" stop-color="{CYAN}"/><stop offset=".8" stop-color="{CYAN}"/><stop offset="1" stop-color="{CYAN}" stop-opacity="0"/></linearGradient>
<linearGradient id="trailA" x1="0" x2="1"><stop offset="0" stop-color="{AMBER}" stop-opacity="0"/><stop offset="1" stop-color="{AMBER}" stop-opacity=".8"/></linearGradient>
<linearGradient id="trailB" x1="0" x2="1"><stop offset="0" stop-color="{CYAN}" stop-opacity="0"/><stop offset="1" stop-color="{CYAN}" stop-opacity=".8"/></linearGradient>
<linearGradient id="flare" x1="0" x2="1"><stop offset="0" stop-color="#FFE7B0"/><stop offset="1" stop-color="{AMBER}" stop-opacity="0"/></linearGradient>
<filter id="glow" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="3"/></filter>
<filter id="soft" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="8"/></filter>
<linearGradient id="trl_far" x1="0" x2="1"><stop offset="0" stop-color="#8FB4F0" stop-opacity="0"/><stop offset="1" stop-color="#8FB4F0" stop-opacity=".55"/></linearGradient>
<linearGradient id="trl_mid" x1="0" x2="1"><stop offset="0" stop-color="#C9DBFA" stop-opacity="0"/><stop offset="1" stop-color="#C9DBFA" stop-opacity=".6"/></linearGradient>
<linearGradient id="trl_near" x1="0" x2="1"><stop offset="0" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#fff" stop-opacity=".65"/></linearGradient>
<radialGradient id="flash"><stop offset="0" stop-color="#E6FFF5"/><stop offset=".3" stop-color="{MINT}" stop-opacity=".8"/><stop offset="1" stop-color="{MINT}" stop-opacity="0"/></radialGradient>
<radialGradient id="glowA"><stop offset="0" stop-color="{AMBER}" stop-opacity=".7"/><stop offset="1" stop-color="{AMBER}" stop-opacity="0"/></radialGradient>
<radialGradient id="glowC"><stop offset="0" stop-color="{CYAN}" stop-opacity=".7"/><stop offset="1" stop-color="{CYAN}" stop-opacity="0"/></radialGradient>
<radialGradient id="glowR"><stop offset="0" stop-color="{ROSE}" stop-opacity=".6"/><stop offset="1" stop-color="{ROSE}" stop-opacity="0"/></radialGradient>
<radialGradient id="glowW"><stop offset="0" stop-color="#fff" stop-opacity=".45"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>
<linearGradient id="hudfade" x1="0" x2="1"><stop offset="0" stop-color="#04070F" stop-opacity=".88"/><stop offset=".75" stop-color="#04070F" stop-opacity=".7"/><stop offset="1" stop-color="#04070F" stop-opacity="0"/></linearGradient>
<g id="act0"><rect x="-26" y="-1.1" width="26" height="2.2" fill="url(#trl_near)"/><circle r="2.1" fill="#fff"/></g>
<g id="act1"><rect x="-26" y="-1.1" width="26" height="2.2" fill="url(#trl_near)"/><circle r="2.1" fill="#fff"/></g>
{"".join(STREAM_SYMS)}
<clipPath id="frame"><rect width="1200" height="675" rx="14"/></clipPath>
</defs>'''

    style = ("<style>" + "".join(CSS) +
             "@media (prefers-reduced-motion:reduce){*{animation-duration:" + f(T) + "s!important}}"
             "</style>")
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
           f'role="img" aria-labelledby="sbTitle sbDesc">'
           f'<title id="sbTitle">Starboard in action</title>'
           f'<desc id="sbDesc">Two satellites, ACME-1 and ORBX-9, receive the same conjunction '
           f'warning, each compute Pc {CROSS["bits"]} and the same verdict with no communication; '
           f'ACME-1 gives way with a {CROSS["dv"]} burn, passes {CROSS["miss_after"]} clear and returns '
           f'to its slot. Then a 72-satellite flock relays one maneuver notice hop by hop until all '
           f'ledgers are identical. Finally a mega-constellation of crossing satellite trains flows in '
           f'unison while conjunctions resolve across it, each pair reaching its verdict alone, and an '
           f'8-craft formation re-plans one member\u2019s dodge so nobody else is endangered.</desc>'
           + defs + style + '<g clip-path="url(#frame)">' + "".join(out) + "</g></svg>")
    return svg


def write_html(svg):
    inline = svg.replace('width="1200" height="675" role="img"', 'role="img" id="film"')
    with open(PAGE_TEMPLATE, encoding="utf-8") as fh:
        page = fh.read()
    html = page.replace("{{SVG}}", inline).replace("{{T}}", f(T))
    os.makedirs(os.path.dirname(OUT_HTML), exist_ok=True)
    with open(OUT_HTML, "w", encoding="utf-8") as fh:
        fh.write(html)


PAGE_TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "page.html")

if __name__ == "__main__":
    svg = build()
    os.makedirs(os.path.dirname(OUT_SVG), exist_ok=True)
    with open(OUT_SVG, "w", encoding="utf-8") as fh:
        fh.write(svg)
    write_html(svg)
    print(f"wrote {os.path.relpath(OUT_SVG, ROOT)} ({len(svg) / 1024:.0f} KB) and "
          f"{os.path.relpath(OUT_HTML, ROOT)}", file=sys.stderr)
