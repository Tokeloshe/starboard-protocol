#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the README animation (docs/media/starboard-in-action.svg) and the
GitHub Pages player (site/index.html).

Every number on screen comes from the two reference demos:

    ./build/starboard_crossing_demo     (Act I, the crossing)
    ports/c/murmuration                 (Act II, the flock)

If ports/c/murmuration is built, the hop-by-hop spread of the maneuver
notice is read live from its transcript; otherwise the transcript below
(captured from v0.6.0) is used. The output is a single self-contained SVG
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

T = 48.0                     # loop length, seconds
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
FLOCK = dict(digest="1E3AC8C816631BCF", nb="P5S01", nb_km="317 km",
             frag="1993-036BX", frag_miss="140 m", dv="0.315 m/s",
             notice="204 bytes", msgs="284")
P_PLANES, S_SLOTS = 6, 12
N_SAT = P_PLANES * S_SLOTS


def read_hops():
    exe = os.path.join(ROOT, "ports", "c", "murmuration")
    text = None
    if os.access(exe, os.X_OK):
        try:
            text = subprocess.run([exe], capture_output=True, text=True,
                                  timeout=60).stdout
            src = "live run of ports/c/murmuration"
        except Exception:
            text = None
    if not text:
        text, src = HOPS_FALLBACK, "embedded v0.6.0 transcript"
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
    print(f"flock hops from {src}: {max(rounds)} hops to 72/72", file=sys.stderr)
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


def anim(points, prop, cls=None, ease="linear"):
    """Register keyframes; return a class name. points: [(t, css_value)]."""
    _n[0] += 1
    name = cls or f"a{_n[0]}"
    pts = sorted(points, key=lambda p: p[0])
    if pts[0][0] > 0:
        pts.insert(0, (0.0, pts[0][1]))
    if pts[-1][0] < T:
        pts.append((T, pts[-1][1]))
    frames = []
    last = None
    for t, v in pts:
        pct = max(0.0, min(100.0, t / T * 100))
        key = f"{pct:.3f}".rstrip("0").rstrip(".")
        if key == last:
            if pct >= 100:
                continue
            pct += 0.001
            key = f"{pct:.3f}".rstrip("0").rstrip(".")
        last = key
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


# =================================================================== build
def build():
    rounds = read_hops()
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

    # =========================================================== chrome
    # title card
    A(f'<g class="{fade_to([(0, 1), (3.6, 1), (4.5, 0), (47.1, 0), (T, 1)])}">')
    A(text(600, 318, "STARBOARD", 50, INK, family=SANS, weight=200, anchor="middle", ls=18))
    A(f'<line x1="520" y1="342" x2="680" y2="342" stroke="{AMBER}" stroke-opacity=".7"/>')
    A(text(600, 374, "the rules of the orbital road", 15, DIM, family=SANS, anchor="middle", ls=3))
    A(text(600, 420, "two satellites · no radio contact · one answer, bit for bit", 11.5, DIM,
           anchor="middle", ls=1.2))
    A('</g>')
    # end card
    A(f'<g class="{fade(44.9, 47.1, .6, 1, .5)}">')
    A(text(600, 330, "rules need bits that agree.", 22, INK, family=SANS, weight=300, anchor="middle", ls=2.5))
    A(text(600, 364, "github.com/Tokeloshe/starboard-protocol", 11.5, DIM, anchor="middle", ls=1.4))
    A('</g>')

    # header: wordmark, act, mission clock
    hdr = fade(4.4, 44.6, .8)
    A(f'<g class="{hdr}">')
    A(text(40, 44, "STARBOARD", 12, INK, family=SANS, weight=600, ls=5))
    A(f'<line x1="166" y1="34" x2="166" y2="48" stroke="{DIM}" stroke-opacity=".5"/>')
    A(text(180, 44, "ACT ONE   THE CROSSING", 10.5, DIM, ls=2.6, cls=fade(4.4, 30.2, .6)))
    A(text(180, 44, "ACT TWO   THE FLOCK", 10.5, DIM, ls=2.6, cls=fade(30.6, 44.6, .6)))
    clock = [(4.6, 17.4, "T−15:00"), (17.4, 19.6, "T−14:30"), (19.6, 21.3, "T−07:30"),
             (21.3, 22.8, "T−03:45"), (22.8, 25.5, "T+00:00  TCA"), (25.5, 27.8, "T+80:41"),
             (27.8, 30.2, "T+272:03"), (30.6, 44.6, "T−6h")]
    for t0, t1, s in clock:
        A(text(1160, 44, s, 12, INK, anchor="end", ls=1.4,
               cls=anim([(t0 - .01, 0), (t0, 1), (t1 - .01, 1), (t1, 0)], "opacity")))
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
<clipPath id="frame"><rect width="1200" height="675" rx="14"/></clipPath>
</defs>'''

    style = ("<style>" + "".join(CSS) +
             "@media (prefers-reduced-motion:reduce){*{animation-duration:" + f(T) + "s!important}}"
             "</style>")
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
           f'role="img" aria-labelledby="sbTitle sbDesc">'
           f'<title id="sbTitle">Starboard in action</title>'
           f'<desc id="sbDesc">Two satellites, ACME-1 and ORBX-9, receive the same conjunction '
           f'warning, each compute Pc {CROSS["bits"]} and the same verdict with no communication; '
           f'ACME-1 gives way with a {CROSS["dv"]} burn, passes {CROSS["miss_after"]} clear and returns '
           f'to its slot. Then a 72-satellite flock relays one maneuver notice hop by hop until all '
           f'ledgers are identical.</desc>'
           + defs + style + '<g clip-path="url(#frame)">' + "".join(out) + "</g></svg>")
    return svg


def write_html(svg):
    inline = svg.replace('width="1200" height="675" role="img"', 'role="img" id="film"')
    html = PAGE.replace("{{SVG}}", inline).replace("{{T}}", f(T))
    os.makedirs(os.path.dirname(OUT_HTML), exist_ok=True)
    with open(OUT_HTML, "w", encoding="utf-8") as fh:
        fh.write(html)


PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Starboard · The Rules of the Orbital Road</title>
<meta name="description" content="An open, deterministic collision-avoidance protocol for satellites. Watch two rival satellites agree on who gives way with zero communication, then a 72-satellite flock learn about the maneuver.">
<meta property="og:title" content="Starboard · The Rules of the Orbital Road">
<meta property="og:description" content="Two satellites, no radio contact, one answer, bit for bit.">
<style>
:root{--bg:#04060c;--ink:#E9EEF7;--dim:#7C8AA5;--line:#1a2338;--amber:#FFB547;--cyan:#62D6FF;--mint:#7CF2C2}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.6 ui-sans-serif,-apple-system,"Segoe UI",Helvetica,Arial,sans-serif;-webkit-font-smoothing:antialiased}
a{color:var(--cyan);text-decoration:none}a:hover{text-decoration:underline}
.wrap{max-width:1180px;margin:0 auto;padding:0 16px}
header{display:flex;align-items:center;justify-content:space-between;padding:22px 0}
.mark{letter-spacing:.42em;font-weight:600;font-size:13px}
nav a{color:var(--dim);font-size:13px;margin-left:22px;letter-spacing:.06em}
.stage{position:relative;border-radius:14px;overflow:hidden;box-shadow:0 30px 80px rgba(0,0,0,.6),0 0 0 1px var(--line)}
.stage svg{display:block;width:100%;height:auto}
.bar{display:flex;align-items:center;gap:14px;margin:14px 0 6px}
button{background:none;border:1px solid var(--line);color:var(--ink);border-radius:999px;height:36px;min-width:36px;padding:0 14px;font:inherit;font-size:13px;cursor:pointer;letter-spacing:.04em}
button:hover{border-color:var(--dim)}
button.on{border-color:var(--amber);color:var(--amber)}
input[type=range]{flex:1;accent-color:var(--amber);min-width:80px}
.time{font:12px ui-monospace,Menlo,Consolas,monospace;color:var(--dim);width:92px;text-align:right}
.chapters{display:flex;flex-wrap:wrap;gap:8px;margin:6px 0 40px}
.chapters button{color:var(--dim)}
h1{font-weight:300;font-size:clamp(28px,4vw,44px);letter-spacing:.02em;margin:56px 0 8px}
.lede{color:var(--dim);max-width:720px;font-size:17px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:18px;margin:36px 0}
.card{border:1px solid var(--line);border-radius:12px;padding:20px;background:#070b16}
.card h3{margin:0 0 6px;font-size:13px;letter-spacing:.22em;font-weight:600}
.card p{margin:0;color:var(--dim);font-size:14px}
pre{background:#070b16;border:1px solid var(--line);border-radius:12px;padding:18px;overflow:auto;font:12.5px/1.55 ui-monospace,Menlo,Consolas,monospace;color:#c8d3e6}
.k{color:var(--dim)}.a{color:var(--amber)}.c{color:var(--cyan)}.m{color:var(--mint)}
footer{color:var(--dim);font-size:13px;padding:48px 0 60px;border-top:1px solid var(--line);margin-top:60px}
@media (max-width:640px){nav{display:none}.time{width:70px}}
</style>
</head>
<body>
<div class="wrap">
<header><div class="mark">STARBOARD</div>
<nav><a href="https://github.com/Tokeloshe/starboard-protocol">GitHub</a><a href="https://github.com/Tokeloshe/starboard-protocol/blob/main/docs/DESIGN.md">Specification</a><a href="https://github.com/Tokeloshe/starboard-protocol/blob/main/docs/FLIGHT.md">Flight guide</a></nav></header>

<div class="stage">{{SVG}}</div>
<div class="bar">
<button id="play" aria-label="Pause">Pause</button>
<input id="scrub" type="range" min="0" max="{{T}}" step="0.01" value="0" aria-label="Scrub timeline">
<span class="time" id="time">0.0 s</span>
<button id="slow" aria-pressed="false">0.5×</button>
</div>
<div class="chapters">
<button data-t="4.6">Two craft, one CDM</button>
<button data-t="8.6">Each decides alone</button>
<button data-t="15.4">Bit-identical</button>
<button data-t="17.4">Give way</button>
<button data-t="22.9">Closest approach</button>
<button data-t="25.5">Back to slot</button>
<button data-t="30.6">The murmuration</button>
<button data-t="41.8">Ledgers agree</button>
</div>

<h1>The rules of the orbital road.</h1>
<p class="lede">Two spacecraft from different operators, with no radio contact, each work out alone which one should move out of the way, and both always get the same answer. Everything in the film above is taken from the reference demos in the repository.</p>

<div class="grid">
<div class="card"><h3 class="a">AGREE</h3><p>Both craft run the same five rules on the same ordered pair. Theorem 1, machine-checked with CBMC: they always reach the same verdict.</p></div>
<div class="card"><h3 class="c">SAME BITS</h3><p>Pc 0x3F5117B3E9975E2E on every compiler, 11 CPU architectures and bare-metal Cortex-M. A computer that would get the maths wrong refuses to fly.</p></div>
<div class="card"><h3 class="m">THE FLOCK</h3><p>A 204-byte maneuver notice ripples over inter-satellite links until all 72 craft hold the same ledger. The flock warns, it never decides.</p></div>
</div>

<pre><span class="k">$ ./build/starboard_crossing_demo</span>
   Pc bits  ACME 0x3F5117B3E9975E2E | ORBX 0x3F5117B3E9975E2E  <span class="m">IDENTICAL [ok]</span>
   ACME-1   verdict: <span class="a">I GIVE WAY</span>   (Rule 4 - starboard)
   ORBX-9   verdict: <span class="c">I STAND ON</span>   (Rule 4 - starboard)
   dv = 1.012 m/s   hill [radial -0.690, along -0.740, cross -0.000]
 T+000:00  TCA - closest approach 1280.0 m &gt;= R_SAFE 1024 m  <span class="m">[ok]</span>

<span class="k">$ ports/c/murmuration</span>
   hop  1  #*.........**...............................................*...........   5/72 know
   hop  9  ##########################################*#############################  72/72 know
   flock ledgers: <span class="m">ALL 71 IDENTICAL</span> (digest 1E3AC8C816631BCF on every satellite)</pre>

<footer>Specification CC BY 4.0 · reference code AGPL-3.0-or-later · <a href="https://github.com/Tokeloshe/starboard-protocol">github.com/Tokeloshe/starboard-protocol</a><br>Shadow-mode ready, not yet flight-proven. See <a href="https://github.com/Tokeloshe/starboard-protocol/blob/main/docs/OPEN-PROBLEMS.md">OPEN-PROBLEMS.md</a>.</footer>
</div>
<script>
(function(){
  var T={{T}}, film=document.getElementById('film');
  var play=document.getElementById('play'), scrub=document.getElementById('scrub'),
      time=document.getElementById('time'), slow=document.getElementById('slow');
  var anims=[], paused=false, rate=1, dragging=false;
  function grab(){ try{ anims=film.getAnimations({subtree:true}); }catch(e){ anims=document.getAnimations(); } }
  function now(){ return anims.length ? ((anims[0].currentTime||0)/1000)%T : 0; }
  function seek(t){ anims.forEach(function(a){ a.currentTime=t*1000; }); }
  function setPaused(p){ paused=p; anims.forEach(function(a){ a.playbackRate=p?0:rate; });
    play.textContent=p?'Play':'Pause'; play.setAttribute('aria-label',p?'Play':'Pause'); }
  function tick(){ if(!dragging){ var t=now(); scrub.value=t.toFixed(2); time.textContent=t.toFixed(1)+' s'; }
    requestAnimationFrame(tick); }
  grab();
  if(!anims.length){ document.querySelector('.bar').style.display='none'; document.querySelector('.chapters').style.display='none'; return; }
  play.onclick=function(){ setPaused(!paused); };
  slow.onclick=function(){ rate=rate===1?.5:1; if(!paused) anims.forEach(function(a){ a.playbackRate=rate; });
    slow.classList.toggle('on',rate!==1); slow.setAttribute('aria-pressed',rate!==1); };
  scrub.addEventListener('input',function(){ dragging=true; seek(+scrub.value); time.textContent=(+scrub.value).toFixed(1)+' s'; });
  scrub.addEventListener('change',function(){ dragging=false; });
  document.querySelectorAll('.chapters button').forEach(function(b){ b.onclick=function(){ seek(+b.dataset.t); }; });
  document.addEventListener('keydown',function(e){ if(e.target.tagName==='INPUT')return;
    if(e.code==='Space'){ e.preventDefault(); setPaused(!paused); }
    if(e.code==='ArrowRight'){ seek(Math.min(T-.01,now()+2)); }
    if(e.code==='ArrowLeft'){ seek(Math.max(0,now()-2)); } });
  tick();
})();
</script>
</body>
</html>
"""

if __name__ == "__main__":
    svg = build()
    os.makedirs(os.path.dirname(OUT_SVG), exist_ok=True)
    with open(OUT_SVG, "w", encoding="utf-8") as fh:
        fh.write(svg)
    write_html(svg)
    print(f"wrote {os.path.relpath(OUT_SVG, ROOT)} ({len(svg) / 1024:.0f} KB) and "
          f"{os.path.relpath(OUT_HTML, ROOT)}", file=sys.stderr)
