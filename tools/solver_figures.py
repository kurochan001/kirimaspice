#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Draws the figures of docs/SOLVER_ALGORITHMS.md, except solver-rc.svg.

  tools/solver_figures.py                 # write docs/figures/solver-*.svg
  tools/solver_figures.py --skip-runs     # only the figures that need no run
  tools/solver_figures.py --out <dir>     # write somewhere else

Three kinds of figure:

  from formulas    solver-newton, solver-newton-convergence, solver-junction-limit,
                   solver-stability-regions, solver-damping
  from runs        solver-order and solver-ringing run the program
                   (--kirimaspice, build/kirimaspice by default);
                   solver-adaptive-steps runs the library through
                   tools/solver_guide_runs.cpp (--runs-tool,
                   build/tests/solver_guide_runs by default)
  diagrams         solver-flow, solver-stamps, solver-companion, solver-stencils

The numbers that the document quotes from the same data are printed on
standard output. Only the Python standard library is needed.

The series colours passed the checks of a palette validator on a white
surface (lightness, chroma, separation under colour-vision deficiency,
contrast): #1b64be, #a44919, #00856f, #7a4fd0, in this order.
"""

import argparse
import cmath
import math
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

TEXT = "#192d3b"    # primary text
MUTED = "#52616c"   # tick labels and notes
AXIS = "#233848"    # axis lines and diagram strokes
GRID = "#dde4e8"    # hairline grid
RULE = "#8c969d"    # zero lines, frames, leaders
ACCENT = "#126757"  # accent of the diagrams, as in solver-rc.svg
C_BE = "#1b64be"
C_TRAP = "#a44919"
C_TRB = "#00856f"
C_GEAR = "#7a4fd0"
C_REF = "#6b7780"   # reference curves and de-emphasised series
STABLE = "#cfe0f5"  # light step of the blue: a stable region

GAMMA = 2 - math.sqrt(2)


# ---------------------------------------------------------------------- SVG


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def f(v):
    """A coordinate with at most two decimals."""
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


def sub(base, index, size=13):
    """base with a subscript, written as solver-rc.svg writes it."""
    small = round(size * 0.72)
    return (f'{esc(base)}<tspan dy="4" font-size="{small}">{esc(index)}</tspan>'
            f'<tspan dy="-4" font-size="{size}">​</tspan>')


def linspace(a, b, n):
    return [a + (b - a) * i / (n - 1) for i in range(n)]


class Svg:
    def __init__(self, w, h, title, desc):
        self.o = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
            'role="img" aria-labelledby="title desc">',
            f'  <title id="title">{esc(title)}</title>',
            f'  <desc id="desc">{esc(desc)}</desc>',
            f'  <rect width="{w}" height="{h}" rx="8" fill="#fff"/>',
            f'  <g font-family="sans-serif" font-size="13" fill="{TEXT}">',
        ]

    def add(self, s):
        self.o.append("    " + s)

    def text(self, x, y, t, size=13, anchor="start", fill=None, weight=None, italic=False, rot=None,
             raw=False):
        a = f'x="{f(x)}" y="{f(y)}"'
        if size != 13:
            a += f' font-size="{size}"'
        if anchor != "start":
            a += f' text-anchor="{anchor}"'
        if fill:
            a += f' fill="{fill}"'
        if weight:
            a += f' font-weight="{weight}"'
        if italic:
            a += ' font-style="italic"'
        if rot is not None:
            a += f' transform="rotate({rot} {f(x)} {f(y)})"'
        self.add(f"<text {a}>{t if raw else esc(t)}</text>")

    def line(self, x1, y1, x2, y2, stroke=AXIS, width=1, dash=None, extra=""):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<line x1="{f(x1)}" y1="{f(y1)}" x2="{f(x2)}" y2="{f(y2)}" stroke="{stroke}" '
                 f'stroke-width="{f(width)}"{d}{extra}/>')

    def path(self, pts, stroke, width=2, dash=None, fill="none", close=False, extra=""):
        d = "M" + " L".join(f"{f(x)} {f(y)}" for x, y in pts) + (" Z" if close else "")
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{f(width)}" '
                 f'stroke-linejoin="round" stroke-linecap="round"{da}{extra}/>')

    def dot(self, x, y, color, r=4, hollow=False):
        if hollow:
            self.add(f'<circle cx="{f(x)}" cy="{f(y)}" r="{f(r)}" fill="#fff" stroke="{color}" stroke-width="2"/>')
        else:
            self.add(f'<circle cx="{f(x)}" cy="{f(y)}" r="{f(r)}" fill="{color}" stroke="#fff" stroke-width="2"/>')

    def rect(self, x, y, w, h, fill="none", stroke=None, width=1, rx=0, extra=""):
        s = f' stroke="{stroke}" stroke-width="{f(width)}"' if stroke else ""
        r = f' rx="{f(rx)}"' if rx else ""
        self.add(f'<rect x="{f(x)}" y="{f(y)}" width="{f(w)}" height="{f(h)}"{r} fill="{fill}"{s}{extra}/>')

    def legend(self, x, y, items, gap=22):
        """One row of line keys: items = [(label, colour, dash)]."""
        for label, color, dash in items:
            self.line(x, y - 4, x + 26, y - 4, stroke=color, width=2, dash=dash, extra=' stroke-linecap="round"')
            self.text(x + 32, y, label, size=12)
            x += 32 + 6.6 * len(label) + gap

    def save(self, path):
        Path(path).write_text("\n".join(self.o) + "\n  </g>\n</svg>\n", encoding="utf-8")


class Plot:
    """Axes in an Svg: linear or logarithmic scales, a hairline grid, the left and the bottom axis."""

    def __init__(self, svg, x, y, w, h, xlim, ylim, xlog=False, ylog=False, ytitle_dx=50):
        self.s, self.x, self.y, self.w, self.h = svg, x, y, w, h
        self.xlim, self.ylim, self.xlog, self.ylog, self.ytitle_dx = xlim, ylim, xlog, ylog, ytitle_dx

    @staticmethod
    def _t(v, lim, log):
        if log:
            return (math.log10(v) - math.log10(lim[0])) / (math.log10(lim[1]) - math.log10(lim[0]))
        return (v - lim[0]) / (lim[1] - lim[0])

    def X(self, v):
        return self.x + self.w * self._t(v, self.xlim, self.xlog)

    def Y(self, v):
        return self.y + self.h * (1 - self._t(v, self.ylim, self.ylog))

    def frame(self, xticks, yticks, xtitle=None, ytitle=None, xgrid=False, ygrid=True, zero=None):
        """xticks and yticks are lists of (value, label)."""
        s = self.s
        if ygrid:
            for v, _ in yticks:
                s.line(self.x, self.Y(v), self.x + self.w, self.Y(v), stroke=GRID)
        if xgrid:
            for v, _ in xticks:
                s.line(self.X(v), self.y, self.X(v), self.y + self.h, stroke=GRID)
        if zero is not None:
            s.line(self.x, self.Y(zero), self.x + self.w, self.Y(zero), stroke=RULE)
        s.line(self.x, self.y, self.x, self.y + self.h)
        s.line(self.x, self.y + self.h, self.x + self.w, self.y + self.h)
        for v, label in xticks:
            s.line(self.X(v), self.y + self.h, self.X(v), self.y + self.h + 4)
            s.text(self.X(v), self.y + self.h + 18, label, size=12, anchor="middle", fill=MUTED)
        for v, label in yticks:
            s.line(self.x - 4, self.Y(v), self.x, self.Y(v))
            s.text(self.x - 8, self.Y(v) + 4, label, size=12, anchor="end", fill=MUTED)
        if xtitle:
            s.text(self.x + self.w / 2, self.y + self.h + 38, xtitle, anchor="middle")
        if ytitle:
            s.text(self.x - self.ytitle_dx, self.y + self.h / 2, ytitle, anchor="middle", rot=-90)

    def curve(self, pts, color, dash=None, width=2):
        self.s.path([(self.X(a), self.Y(b)) for a, b in pts], color, width=width, dash=dash)

    def dots(self, pts, color, r=4, hollow=False):
        for a, b in pts:
            self.s.dot(self.X(a), self.Y(b), color, r=r, hollow=hollow)


def minus(s):
    return s.replace("-", "−")


# ---------------------------------------------------------------------- formulas of the document

# The RC-diode step of "Add a diode and solve with Newton's method"
G_R, G_C, V_S, V_N, I_S, N_VT = 1e-3, 1e-2, 1.0, 0.5, 1e-12, 0.026


def residual(v):
    return G_R * (v - V_S) + G_C * (v - V_N) + I_S * (math.exp(v / N_VT) - 1)


def residual_slope(v):
    return G_R + G_C + I_S / N_VT * math.exp(v / N_VT)


def residual_root():
    lo, hi = 0.4, 0.6
    for _ in range(200):
        mid = (lo + hi) / 2
        lo, hi = (lo, mid) if residual(mid) > 0 else (mid, hi)
    return (lo + hi) / 2


def limit_junction_step(vnew, vold, nvt, vcrit):
    """core::limit_junction_step of include/kirimaspice/core/device_utils.hpp."""
    if vnew <= vcrit or abs(vnew - vold) <= 2 * nvt:
        return vnew
    if vold > 0:
        arg = 1 + (vnew - vold) / nvt
        return vold + nvt * math.log(arg) if arg > 0 else vcrit
    return nvt * math.log(vnew / nvt) if vnew > nvt else vnew


def factor_be(z):
    return 1 / (1 - z)


def factor_trap(z):
    return (1 + z / 2) / (1 - z / 2)


def factor_trbdf2(z):
    """A trapezoidal stage over gamma h, then the BDF2 stage that the solver stamps."""
    g = GAMMA
    return ((1 + g * z / 2) / (1 - g * z / 2) - (1 - g) ** 2) / (g * (2 - g) * (1 - (1 - g) * z / (2 - g)))


BDF_ALPHA = {2: [1.5, -2, 0.5], 3: [11 / 6, -3, 1.5, -1 / 3]}


def bdf_locus(k, n):
    """Boundary locus of BDFk: z = sum over j of (1 - exp(-i theta))^j / j."""
    out = []
    for theta in linspace(0, 2 * math.pi, n):
        w = cmath.exp(-1j * theta)
        out.append(sum((1 - w) ** j / j for j in range(1, k + 1)))
    return out


def poly_roots(coeffs):
    """Roots of a polynomial by the Durand-Kerner iteration (highest power first)."""
    lead = coeffs[0]
    c = [x / lead for x in coeffs]
    n = len(c) - 1
    roots = [(0.4 + 0.9j) ** i for i in range(n)]
    for _ in range(200):
        new = []
        for i, r in enumerate(roots):
            value = sum(c[j] * r ** (n - j) for j in range(n + 1))
            denom = 1
            for j, other in enumerate(roots):
                if j != i:
                    denom *= r - other
            new.append(r - value / denom if denom != 0 else r)
        roots = new
    return roots


def bdf_stable(z, k):
    alpha = BDF_ALPHA[k]
    return max(abs(r) for r in poly_roots([alpha[0] - z] + alpha[1:])) <= 1 + 1e-9


# ---------------------------------------------------------------------- figures from formulas


def newton(out, facts):
    root = residual_root()
    u = 0.55
    v_next = u - residual(u) / residual_slope(u)
    facts.append(f"solver-newton: tangent at {u:.3f} V crosses zero at {v_next:.5f} V; root {root:.5f} V")
    s = Svg(640, 320, "Newton update for one RC-diode step",
            f"The tangent at 0.550 V crosses zero at {v_next:.5f} V. The residual curve crosses zero near "
            f"{root:.3f} V.")
    p = Plot(s, 70, 26, 540, 236, (0.498, 0.558), (-1.0, 2.4))
    p.frame([(v / 100, f"{v / 100:.2f}") for v in range(50, 56)],
            [(v / 2, minus(f"{v / 2:.1f}")) for v in range(-1, 5)], "v [V]", "F(v) [mA]", zero=0)
    xs = linspace(0.498, 0.558, 121)
    tangent = [(x, 1e3 * (residual(u) + residual_slope(u) * (x - u))) for x in (0.5145, 0.558)]
    s.line(p.X(u), p.Y(0), p.X(u), p.Y(1e3 * residual(u)), stroke=RULE, dash="2 3")
    p.curve(tangent, C_TRAP, dash="7 4")
    p.curve([(x, 1e3 * residual(x)) for x in xs], C_BE, width=2.5)
    s.dot(p.X(u), p.Y(1e3 * residual(u)), C_BE)
    s.add(f'<rect x="{f(p.X(v_next) - 4)}" y="{f(p.Y(0) - 4)}" width="8" height="8" fill="{C_TRAP}" '
          'stroke="#fff" stroke-width="2"/>')
    s.dot(p.X(root), p.Y(0), C_BE, hollow=True)
    s.text(p.X(u) - 10, p.Y(1e3 * residual(u)) - 8, "u", anchor="end", italic=True, size=14)
    s.text(p.X(v_next) + 10, p.Y(0) + 20, sub("v", "next", 14), italic=True, size=14, raw=True)
    s.text(p.X(root) - 2, p.Y(0) - 12, "v*", anchor="middle", italic=True, size=14)
    s.legend(p.x + 14, p.y + 16, [("F(v)", C_BE, None)])
    s.legend(p.x + 14, p.y + 36, [("F(u) + F′(u)(v − u)", C_TRAP, "7 4")])
    s.save(out / "solver-newton.svg")


def newton_convergence(out, facts):
    u = [0.55]
    for _ in range(6):
        u.append(u[-1] - residual(u[-1]) / residual_slope(u[-1]))
    root = residual_root()
    change = [abs(u[k + 1] - u[k]) for k in range(5)]
    tol = 1e-3 * root + 1e-6
    for k in range(6):
        facts.append(f"solver-newton-convergence: k={k} u={u[k]:.6f} V  F={residual(u[k]):.3e} A  "
                     f"g_D={I_S / N_VT * math.exp(u[k] / N_VT):.4e} S"
                     + (f"  change={change[k]:.3e} V" if k < 5 else ""))
    facts.append(f"solver-newton-convergence: tolerance RELTOL x |v| + VNTOL = {tol:.3e} V")
    s = Svg(640, 300, "Size of each Newton update for the RC-diode step",
            "Starting from 0.550 V the updates are 22.6 mV, 12.4 mV, 2.40 mV, 66.0 microvolts and "
            "46.9 nanovolts. The fourth update is the first below the tolerance of 0.51 mV.")
    p = Plot(s, 86, 30, 520, 200, (0.5, 5.5), (1e-8, 1e-1), ylog=True, ytitle_dx=66)
    yt = [(1e-8, "10 nV"), (1e-7, "100 nV"), (1e-6, "1 µV"), (1e-5, "10 µV"), (1e-4, "100 µV"),
          (1e-3, "1 mV"), (1e-2, "10 mV"), (1e-1, "100 mV")]
    p.frame([(k, str(k)) for k in range(1, 6)], yt, "Newton iteration", "Change of v")
    s.line(p.x, p.Y(tol), p.x + p.w, p.Y(tol), stroke=C_REF, width=1.5)
    s.text(p.x + 8, p.Y(tol) + 17, "Tolerance RELTOL × |v| + VNTOL = 0.51 mV", size=12, fill=MUTED)
    pts = [(k + 1, change[k]) for k in range(5)]
    p.curve(pts, C_BE)
    p.dots(pts, C_BE)
    for (k, v), label in zip(pts, ["22.6 mV", None, None, "66.0 µV", "46.9 nV"]):
        if label:
            s.text(p.X(k) + 10, p.Y(v) - 8, label, size=12)
    s.save(out / "solver-newton-convergence.svg")


def junction_limit(out, facts):
    nvt = N_VT
    vcrit = nvt * math.log(nvt / (math.sqrt(2) * I_S))

    def limit(vnew, vold):
        return limit_junction_step(vnew, vold, nvt, vcrit)

    facts.append(f"solver-junction-limit: V_crit={vcrit:.4f} V; from 0.65 V a proposal of 2 V is used at "
                 f"{limit(2.0, 0.65):.4f} V; from 0 V a proposal of 1 V is used at {limit(1.0, 0.0):.4f} V")
    s = Svg(640, 320, "Junction voltage limiting between Newton iterations",
            "Voltage at which the diode is evaluated against the voltage Newton proposes. Without limiting "
            "the two are equal. From a previous voltage of 0.65 V a proposal of 2 V is evaluated at 0.753 V. "
            "From 0 V a proposal above the critical voltage of 0.614 V is evaluated near 0.1 V.")
    p = Plot(s, 70, 44, 536, 210, (0, 2.0), (0, 1.0))
    p.frame([(v / 10, f"{v / 10:.1f}") for v in range(0, 21, 5)],
            [(v / 10, f"{v / 10:.1f}") for v in range(0, 11, 2)],
            "Junction voltage proposed by the Newton step [V]", "Voltage used [V]", xgrid=True)
    p.curve([(0, 0), (1.0, 1.0)], C_REF, dash="2 5")

    def span(a, b, n=90):
        return [a + (b - a) * i / n for i in range(n + 1)]

    # from 0.65 V: unchanged up to a step of 2 nV_T, logarithmic beyond (the rule jumps there)
    knee = 0.65 + 2 * nvt
    p.curve([(0, 0), (knee, knee)], C_BE)
    p.curve([(x, limit(x, 0.65)) for x in span(knee + 1e-9, 2.0)], C_BE)
    # from 0 V: unchanged up to V_crit, then near the knee of the exponential
    p.curve([(0, 0), (vcrit, vcrit)], C_TRAP, dash="7 4")
    p.curve([(x, limit(x, 0.0)) for x in span(vcrit + 1e-9, 2.0)], C_TRAP, dash="7 4")
    s.dot(p.X(vcrit), p.Y(vcrit), C_TRAP, hollow=True)
    s.line(p.X(vcrit), p.Y(0), p.X(vcrit), p.Y(vcrit), stroke=RULE, dash="2 3")
    s.text(p.X(vcrit) + 6, p.Y(0.5), sub("V", "crit") + " = 0.614 V", size=12, fill=MUTED, raw=True)
    s.legend(p.x, 26, [("no limiting", C_REF, "2 5"), ("previous voltage 0.65 V", C_BE, None),
                       ("previous voltage 0 V", C_TRAP, "7 4")])
    s.text(p.X(1.42), p.Y(limit(1.42, 0.65)) - 10,
           "0.65 V + n" + sub("V", "T") + " ln(1 + Δv / n" + sub("V", "T") + ")", size=12, raw=True, fill=TEXT)
    s.text(p.X(1.42), p.Y(limit(1.42, 0.0)) - 10, "n" + sub("V", "T") + " ln(v / n" + sub("V", "T") + ")",
           size=12, raw=True)
    s.save(out / "solver-junction-limit.svg")


def damping(out, facts):
    lowest = min((factor_trbdf2(-x).real, x) for x in (10 ** (k / 1000) for k in range(-2000, 3001)))
    facts.append(f"solver-damping: TR-BDF2 factor is zero at h/tau = {1 / (math.sqrt(2) - 1):.3f} and lowest, "
                 f"{lowest[0]:.4f}, at h/tau = {lowest[1]:.2f}; R(-100): BE {factor_be(-100):.6f}, "
                 f"TRAP {factor_trap(-100):.6f}, TR-BDF2 {factor_trbdf2(-100).real:.6f}")
    s = Svg(680, 320, "One-step amplification of an RC decay mode",
            "Backward Euler and TR-BDF2 approach zero at large h over tau. Trapezoidal integration crosses "
            "zero at h over tau equal to two and approaches minus one.")
    p = Plot(s, 70, 44, 580, 216, (0.01, 1000), (-1.1, 1.1), xlog=True)
    p.frame([(10.0 ** k, f"{10.0 ** k:g}") for k in range(-2, 4)],
            [(v / 2, minus(f"{v / 2:.1f}")) for v in range(-2, 3)], "h / τ", "R(−h / τ)", xgrid=True, zero=0)
    xs = [10 ** (k / 50) for k in range(-100, 151)]
    p.curve([(x, math.exp(-x)) for x in xs], C_REF, dash="2 5")
    p.curve([(x, factor_be(-x)) for x in xs], C_BE)
    p.curve([(x, factor_trap(-x)) for x in xs], C_TRAP, dash="7 4")
    p.curve([(x, factor_trbdf2(-x).real) for x in xs], C_TRB, dash="9 3 2 3")
    s.dot(p.X(2), p.Y(0), C_TRAP, hollow=True)
    s.legend(p.x + 84, 26, [("exp(−h/τ)", C_REF, "2 5"), ("BE", C_BE, None), ("TRAP", C_TRAP, "7 4"),
                            ("TR-BDF2", C_TRB, "9 3 2 3")])
    s.save(out / "solver-damping.svg")


def inside(poly, x, y):
    c = False
    for i in range(len(poly)):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % len(poly)]
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            c = not c
    return c


def stability_regions(out, facts):
    # TR-BDF2: the boundary |R(z)| = 1, traced around its double pole
    pole = 2 / GAMMA
    tr = []
    for a in linspace(0, 2 * math.pi, 241)[:-1]:
        lo, hi = 1e-6, 20.0
        d = complex(math.cos(a), math.sin(a))
        for _ in range(60):
            mid = (lo + hi) / 2
            lo, hi = (mid, hi) if abs(factor_trbdf2(pole + mid * d)) > 1 else (lo, mid)
        z = pole + lo * d
        tr.append((z.real, z.imag))
    b2 = [(z.real, z.imag) for z in bdf_locus(2, 361)]
    b3 = [(z.real, z.imag) for z in bdf_locus(3, 361)]

    def circle(cx):
        return [(cx + math.cos(a), math.sin(a)) for a in linspace(0, 2 * math.pi, 181)[:-1]]

    # Each drawn boundary must agree with the definition of stability at random points
    rng = random.Random(1)
    points = [(rng.uniform(-6, 14), rng.uniform(-8, 8)) for _ in range(1500)]

    def agreement(poly, stable, unstable_inside=True):
        hits = sum((stable(complex(x, y)) != inside(poly, x, y)) == unstable_inside for x, y in points)
        return hits / len(points)

    checks = {
        "forward Euler": agreement(circle(-1.0), lambda z: abs(1 + z) <= 1, unstable_inside=False),
        "BE": agreement(circle(1.0), lambda z: abs(factor_be(z)) <= 1),
        "TR-BDF2": agreement(tr, lambda z: abs(factor_trbdf2(z)) <= 1),
        "BDF2": agreement(b2, lambda z: bdf_stable(z, 2)),
        "BDF3": agreement(b3, lambda z: bdf_stable(z, 3)),
    }
    if min(checks.values()) < 0.995:
        raise SystemExit(f"solver_figures: a stability boundary disagrees with its definition: {checks}")
    fine = bdf_locus(3, 200001)
    left = [z for z in fine if z.real < 0 and abs(z) > 1e-3]
    alpha = min(math.degrees(math.atan2(abs(z.imag), -z.real)) for z in left)
    far = min(fine, key=lambda z: z.real)
    cross = [abs(z.imag) for z in fine if abs(z.real) < 2e-5 and abs(z.imag) > 1]
    facts.append(f"solver-stability-regions: BDF3 alpha = {alpha:.2f} deg; its boundary reaches Re z = "
                 f"{far.real:.4f} at Im z = {abs(far.imag):.3f} and returns to the axis at Im z = "
                 f"{sum(cross) / len(cross):.3f}; TR-BDF2 unstable up to Re z = {max(x for x, _ in tr):.2f}, "
                 f"Im z = {max(y for _, y in tr):.2f}")

    s = Svg(800, 420, "Regions of absolute stability in the plane of z = h times lambda",
            "Seven panels. Shaded: a decaying or growing mode does not grow in the computation. Forward Euler "
            "is stable only inside a unit disc at minus one. Backward Euler, BDF2 and TR-BDF2 are stable "
            "everywhere outside a closed region in the right half-plane. The trapezoidal rule is stable "
            "exactly in the left half-plane. The boundary of BDF3 crosses the imaginary axis into the left "
            "half-plane by at most 0.083.")
    pw, ph = 165, 132
    panels = [
        ("Forward Euler (explicit)", "disc", circle(-1.0)),
        ("Backward Euler", "hole", circle(1.0)),
        ("Trapezoidal", "half", None),
        ("TR-BDF2", "hole", tr),
        ("BDF2 (GEAR2)", "hole", b2),
        ("BDF3 (GEAR3)", "hole", b3),
        ("BDF3, near the imaginary axis", "zoom", b3),
    ]

    def origin(i):
        return 34 + (i % 4) * (pw + 28), 34 + (i // 4) * (ph + 62)

    s.add("<defs>")
    for i in range(len(panels)):
        px, py = origin(i)
        s.add(f'  <clipPath id="p{i}"><rect x="{px}" y="{py}" width="{pw}" height="{ph}"/></clipPath>')
    s.add("</defs>")
    for i, (title, kind, poly) in enumerate(panels):
        px, py = origin(i)
        xl, yl = ((-6.0, 14.0), (-8.0, 8.0)) if kind != "zoom" else ((-0.6, 0.65), (0.8, 1.8))

        def X(v, px=px, xl=xl):
            return px + pw * (v - xl[0]) / (xl[1] - xl[0])

        def Y(v, py=py, yl=yl):
            return py + ph * (1 - (v - yl[0]) / (yl[1] - yl[0]))

        s.text(px, py - 9, title, size=12, weight="bold")
        clip = f' clip-path="url(#p{i})"'
        if kind == "disc":
            s.rect(px, py, pw, ph, fill="#fff")
            s.path([(X(a), Y(b)) for a, b in poly], C_BE, width=1.5, fill=STABLE, close=True, extra=clip)
        elif kind == "half":
            s.rect(px, py, X(0) - px, ph, fill=STABLE)
            s.line(X(0), py, X(0), py + ph, stroke=C_BE, width=1.5)
        else:
            s.rect(px, py, pw, ph, fill=STABLE)
            s.path([(X(a), Y(b)) for a, b in poly], C_BE, width=1.5, fill="#fff", close=True, extra=clip)
        if yl[0] < 0 < yl[1]:
            s.line(px, Y(0), px + pw, Y(0), stroke=AXIS, width=0.8)
        if kind != "half":
            s.line(X(0), py, X(0), py + ph, stroke=AXIS, width=0.8)
        s.rect(px, py, pw, ph, stroke=RULE)
        xt = [-4, 0, 4, 8, 12] if kind != "zoom" else [-0.5, 0, 0.5]
        yt = [-8, -4, 0, 4, 8] if kind != "zoom" else [1.0, 1.5]
        for v in xt:
            s.line(X(v), py + ph, X(v), py + ph + 3, stroke=RULE)
            s.text(X(v), py + ph + 15, minus(f"{v:g}"), size=11, anchor="middle", fill=MUTED)
        for v in yt:
            s.line(px - 3, Y(v), px, Y(v), stroke=RULE)
            if i % 4 == 0 or kind == "zoom":
                s.text(px - 6, Y(v) + 4, minus(f"{v:g}"), size=11, anchor="end", fill=MUTED)
        if kind == "zoom":
            s.line(X(-1 / 12), Y(1.299), X(0.1), Y(1.299), stroke=RULE)
            s.text(X(0.12), Y(1.299) + 4, "−0.083", size=11)
        s.text(px + pw / 2, py + ph + 30, "Re z", size=11, anchor="middle", fill=MUTED)
    lx, ly = origin(7)
    s.rect(lx, ly + 6, 22, 14, fill=STABLE, stroke=C_BE, width=1.5)
    s.text(lx + 30, ly + 18, "stable: no mode grows", size=12)
    s.rect(lx, ly + 32, 22, 14, fill="#fff", stroke=RULE)
    s.text(lx + 30, ly + 44, "unstable", size=12)
    s.text(lx, ly + 72, "Horizontal: Re z,", size=12, fill=MUTED)
    s.text(lx, ly + 88, "vertical: Im z, z = hλ.", size=12, fill=MUTED)
    s.text(lx, ly + 110, "A decaying mode has", size=12, fill=MUTED)
    s.text(lx, ly + 126, "Re z < 0 (left half).", size=12, fill=MUTED)
    s.save(out / "solver-stability-regions.svg")


# ---------------------------------------------------------------------- figures from runs

RC_EXAMPLE = ("RC charging from an initial voltage\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1u\n"
              ".IC V(in)=1 V(out)=0\n.OPTIONS METHOD={method}\n.TRAN {step} {stop} UIC\n.END\n")
TWO_TIME_CONSTANTS = ("Two time constants\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1u\nRfast fast 0 1k\n"
                      "Cfast fast 0 1n\n.IC V(in)=1 V(out)=0 V(fast)=1\n.OPTIONS METHOD={method}\n"
                      ".TRAN 100u 1m UIC\n.END\n")


def observed_order(out, facts, session):
    stop = 1e-3
    exact = 1 - math.exp(-1)
    parts = [5, 10, 20, 40, 80, 160, 320]
    errors = {}
    for method in ("BE", "TRAP", "GEAR2", "GEAR3", "TR_BDF2"):
        errors[method] = []
        for n in parts:
            run = session.run_netlist(RC_EXAMPLE.format(method=method, step=f"{stop / n:.12g}", stop=f"{stop:.12g}"),
                                      ["--log-level", "error"])
            time, v = run.series("time"), run.series("v(out)")
            k = min(range(len(time)), key=lambda i: abs(time[i] - stop))
            if abs(time[k] - stop) > 1e-12:
                raise SystemExit(f"solver_figures: {method} at {n} steps has no point at 1 ms")
            errors[method].append(abs(v[k] - exact))
        facts.append(f"solver-order: {method:8s}" + " ".join(f"{e:.3e}" for e in errors[method])
                     + f"  ratio of the last two {errors[method][-2] / errors[method][-1]:.2f}")
    s = Svg(640, 340, "Error at 1 ms against the step length",
            "Log-log plot for the RC example. Halving the step halves the error of backward Euler and "
            "quarters the error of the trapezoidal rule and of BDF2.")
    p = Plot(s, 78, 44, 470, 230, (2.5, 250), (5e-7, 1e-1), xlog=True, ylog=True, ytitle_dx=58)
    xt = [(1e6 * stop / n, f"{1e6 * stop / n:g}") for n in reversed(parts)]
    yt = [(1e-6, "1 µV"), (1e-5, "10 µV"), (1e-4, "100 µV"), (1e-3, "1 mV"), (1e-2, "10 mV"), (1e-1, "100 mV")]
    p.frame(xt, yt, "Step length h [µs]", "Error of v(out) at 1 ms", xgrid=True)
    series = [("BE", "BE", C_BE, None), ("TRAP", "TRAP", C_TRAP, "7 4"), ("GEAR2", "GEAR2 (BDF2)", C_GEAR, "2 4")]
    for key, label, color, dash in series:
        pts = [(1e6 * stop / n, e) for n, e in zip(parts, errors[key])]
        p.curve(pts, color, dash=dash)
        p.dots(pts, color, r=3.5)
        s.text(p.X(pts[0][0]) + 10, p.Y(pts[0][1]) + 4, label, size=12)
    s.legend(p.x, 26, [(label, color, dash) for _, label, color, dash in series])
    # slope guides: the step times 4 against the error times 4 and times 16
    for x0, y0, order, label in ((5.0, 6e-3, 1, "slope 1"), (30.0, 1.4e-5, 2, "slope 2")):
        x1, y1 = x0 * 4, y0 * 4 ** order
        s.path([(p.X(x0), p.Y(y0)), (p.X(x1), p.Y(y0)), (p.X(x1), p.Y(y1)), (p.X(x0), p.Y(y0))], RULE,
               width=1, close=True)
        s.text(p.X(x1) + 6, p.Y(math.sqrt(y0 * y1)) + 4, label, size=12, fill=MUTED)
    s.save(out / "solver-order.svg")


def ringing(out, facts, session):
    data = {}
    for method in ("BE", "TRAP", "TR_BDF2"):
        run = session.run_netlist(TWO_TIME_CONSTANTS.format(method=method), ["--log-level", "error"])
        data[method] = list(zip(run.series("time"), run.series("v(fast)"), run.series("v(out)")))
        fast = [v for _, v, _ in data[method]]
        facts.append(f"solver-ringing: {method:8s} at 200 us v(fast)={fast[2]:.7e} V v(out)={data[method][2][2]:.7f} V;"
                     f" ratio of successive v(fast) {fast[3] / fast[2]:.6f}; at 1 ms {fast[-1]:.4e} V")
    s = Svg(640, 320, "Fast node of the two-time-constant example at a 100 µs step",
            "The exact voltage is zero on this scale after the first step. The trapezoidal rule alternates "
            "between about plus and minus 9 mV and loses 4 percent per step. Backward Euler and TR-BDF2 are "
            "below 0.5 mV from the second step on.")
    p = Plot(s, 70, 44, 500, 216, (50, 1050), (-12, 12))
    p.frame([(t, str(t)) for t in range(100, 1001, 100)],
            [(v, minus(f"{v:+d}".replace("+0", "0"))) for v in range(-12, 13, 4)],
            "Time [µs]", "v(fast) [mV]", zero=0)
    series = [("BE", "BE", C_BE, None), ("TRAP", "TRAP", C_TRAP, "7 4"), ("TR_BDF2", "TR-BDF2", C_TRB, "9 3 2 3")]
    for key, label, color, dash in series:
        pts = [(t * 1e6, v * 1e3) for t, v, _ in data[key] if t > 0]
        p.curve(pts, color, dash=dash)
        p.dots(pts, color, r=3.5)
    s.legend(p.x, 26, [(label, color, dash) for _, label, color, dash in series])
    last = data["TRAP"][-1]
    s.text(p.X(last[0] * 1e6) + 10, p.Y(last[1] * 1e3) + 4, "TRAP", size=12)
    s.text(p.X(last[0] * 1e6) + 10, p.Y(0) - 8, "BE,", size=12)
    s.text(p.X(last[0] * 1e6) + 10, p.Y(0) + 8, "TR-BDF2", size=12)
    s.text(p.X(100) + 10, p.Y(9.901) - 4, "first step, BE in all three: 9.90 mV", size=12, fill=MUTED)
    s.save(out / "solver-ringing.svg")


def adaptive_steps(out, facts, runs_tool):
    done = subprocess.run([str(runs_tool)], capture_output=True, text=True, check=True)
    t = []
    for line in done.stdout.splitlines():
        if line.startswith("run "):
            facts.append("solver-adaptive-steps: " + line)
        elif line.startswith("time "):
            t.append(float(line.split()[1]))
    h = [b - a for a, b in zip(t, t[1:])]
    ceiling = next(t[i] for i in range(len(h)) if h[i] >= 0.99e-4)
    facts.append("solver-adaptive-steps: first steps " + ", ".join(f"{x * 1e6:.4g}" for x in h[:5])
                 + f" us; ceiling reached at {ceiling * 1e3:.3f} ms; last step {h[-1] * 1e6:.4g} us")
    s = Svg(640, 320, "Step length chosen by the adaptive path on the RC example",
            "The adaptive run starts at 1 microsecond, grows by 10 to 20 percent per step and reaches its "
            "100 microsecond ceiling at 2.1 ms. It takes 156 steps where the fixed steps of 15 and 1 "
            "microseconds take 667 and 10,000.")
    p = Plot(s, 70, 30, 388, 230, (0, 10), (0.7, 150), ylog=True)
    p.frame([(v, str(v)) for v in range(0, 11, 2)], [(1, "1"), (10, "10"), (100, "100")],
            "Time [ms]", "Step length [µs]")
    for step, label in ((15, "Fixed 15 µs, 667 steps"), (1, "Fixed 1 µs, 10,000 steps")):
        s.line(p.x, p.Y(step), p.x + p.w, p.Y(step), stroke=C_REF, width=2)
        s.text(p.x + p.w + 8, p.Y(step) + 4, label, size=12)
    stair = []
    for i, step in enumerate(h):
        stair += [(t[i] * 1e3, step * 1e6), (t[i + 1] * 1e3, step * 1e6)]
    p.curve(stair, C_BE)
    s.text(p.x + p.w + 8, p.Y(100) + 4, f"Adaptive, {len(h)} steps", size=12)
    s.text(p.X(2.3), p.Y(100) + 18, "ceiling max_step = 100 µs", size=12, fill=MUTED)
    s.save(out / "solver-adaptive-steps.svg")


# ---------------------------------------------------------------------- diagrams


def marker(s, name, color):
    s.add(f'<defs><marker id="{name}" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" '
          f'orient="auto"><path d="M0 0 8 4 0 8Z" fill="{color}"/></marker></defs>')


def arrow(s, pts, color=AXIS, name="arrow", width=1.5):
    d = "M" + " L".join(f"{f(x)} {f(y)}" for x, y in pts)
    s.add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{f(width)}" stroke-linejoin="round" '
          f'marker-end="url(#{name})"/>')


def box(s, x, y, w, h, title, lines, fill="#f3f6f8", stroke=AXIS):
    s.rect(x, y, w, h, fill=fill, stroke=stroke, width=1.5, rx=6)
    s.text(x + w / 2, y + 21, title, anchor="middle", weight="bold")
    for i, line in enumerate(lines):
        s.text(x + w / 2, y + 38 + 15 * i, line, size=11.5, anchor="middle", fill=MUTED)


def flow(out):
    s = Svg(760, 352, "From a netlist to a waveform",
            "The parser turns the netlist into a circuit of devices. The solver repeats two nested loops: "
            "for each time point, devices stamp the matrix G and the vector b at a trial solution, KLU solves "
            "G x = b, and the solution is checked. A solution that has not converged becomes the next trial. "
            "A converged one is accepted, the devices store their history, and time advances. The accepted "
            "points go to the output files.")
    marker(s, "arrow", AXIS)
    marker(s, "arrow2", ACCENT)
    xs = [17, 167, 317, 467, 617]
    top = [("Netlist", [".cir text file"]), ("Parser", ["reads each line"]), ("Circuit", ["nodes and devices"]),
           ("Solver", ["DC, transient or AC"]), ("Output", ["raw, CSV, SSF files"])]
    for x, (title, lines) in zip(xs, top):
        box(s, x, 18, 126, 50, title, lines)
    for x in xs[:-1]:
        arrow(s, [(x + 126, 43), (x + 150, 43)])
    s.rect(17, 104, 726, 236, stroke=C_REF, width=1.5, rx=10, extra=' stroke-dasharray="5 4"')
    s.text(31, 126, "Time loop (transient): one pass per time point", weight="bold")
    s.rect(37, 142, 498, 148, stroke=ACCENT, width=1.5, rx=10)
    s.text(51, 164, "Newton loop: one pass per trial solution x", weight="bold", fill=ACCENT)
    s.line(530, 68, 530, 104, stroke=C_REF, width=1.5, dash="5 4")
    inner = [(53, "Stamp", ["each device adds its", "terms to G and b"]),
             (215, "Solve", ["KLU factors G and", "solves G x = b"]),
             (377, "Check", ["change and residual", "small, no limiting"])]
    for x, title, lines in inner:
        box(s, x, 178, 142, 62, title, lines, fill="#eef6f3", stroke=ACCENT)
    arrow(s, [(195, 209), (215, 209)])
    arrow(s, [(357, 209), (377, 209)])
    arrow(s, [(448, 240), (448, 264), (124, 264), (124, 241)], color=ACCENT, name="arrow2")
    s.text(286, 281, "not converged: the new x is the next trial", size=12, anchor="middle")
    box(s, 600, 178, 136, 62, "Accept", ["devices store this", "point's history"])
    arrow(s, [(519, 209), (600, 209)])
    s.text(568, 200, "converged", size=12, anchor="middle")
    arrow(s, [(668, 240), (668, 312), (27, 312), (27, 209), (53, 209)])
    s.text(343, 329, "next time point: t + h", size=12, anchor="middle")
    arrow(s, [(690, 178), (690, 69)])
    s.text(682, 148, "accepted points", size=12, anchor="end")
    s.save(out / "solver-flow.svg")


def stamps(out):
    s = Svg(660, 318, "Where R1, C1 and V1 write in the matrix and the right-hand side",
            "Rows in, out and V1; columns v_in, v_out and i_V. R1 writes plus and minus g in the four "
            "positions of rows in and out and columns v_in and v_out. C1 writes G_C on the diagonal of out "
            "and G_C times v_n on the right-hand side of out. V1 writes 1 at row in, column i_V, 1 at row "
            "V1, column v_in, and V_s on the right-hand side of row V1.")
    fills = {"R1": ("#dce8f8", C_BE), "C1": ("#f7e4d8", C_TRAP), "V1": ("#d8efe9", C_TRB)}
    cw, ch = 88, 60
    gx, gy = 128, 70
    cols = [gx, gx + cw, gx + 2 * cw]
    bx = gx + 3 * cw + 126
    xx = gx + 3 * cw + 30

    def chip(x, y, w, dev, raw):
        fill, stroke = fills[dev]
        s.rect(x, y, w, 21, fill=fill, stroke=stroke, width=1.2, rx=4)
        s.text(x + 7, y + 15, dev, size=11, fill=MUTED)
        s.text(x + w - 7, y + 15, raw, size=12.5, anchor="end", raw=True)

    def cell(col_x, row, chips):
        y = gy + row * ch
        if not chips:
            s.text(col_x + cw / 2, y + ch / 2 + 5, "0", anchor="middle", fill=RULE)
            return
        y0 = y + (ch - (21 * len(chips) + 5 * (len(chips) - 1))) / 2
        for i, (dev, raw) in enumerate(chips):
            chip(col_x + 6, y0 + i * 26, cw - 12, dev, raw)

    def bracket(x, w):
        y0, y1 = gy - 4, gy + 3 * ch + 4
        s.add(f'<path d="M{f(x + 8)} {f(y0)}H{f(x)}V{f(y1)}H{f(x + 8)} M{f(x + w - 8)} {f(y0)}H{f(x + w)}'
              f'V{f(y1)}H{f(x + w - 8)}" fill="none" stroke="{AXIS}" stroke-width="1.8" stroke-linejoin="round"/>')

    unknowns = [sub("v", "in"), sub("v", "out"), sub("i", "V")]
    for x, name in zip(cols, unknowns):
        s.text(x + cw / 2, gy - 16, name, anchor="middle", italic=True, raw=True)
    s.text(gx + 1.5 * cw, gy - 40, "Matrix G: one column per unknown", size=12, anchor="middle", fill=MUTED)
    s.text(xx + 30, gy - 16, "x", anchor="middle", italic=True)
    s.text(bx + cw / 2 + 12, gy - 16, "b", anchor="middle", italic=True)
    s.text(bx + cw / 2 + 12, gy - 40, "Right-hand side", size=12, anchor="middle", fill=MUTED)
    for r, (name, unit) in enumerate([("in", "KCL, in A"), ("out", "KCL, in A"), ("V1", "constraint, in V")]):
        y = gy + r * ch
        s.text(gx - 18, y + ch / 2 - 2, name, anchor="end", weight="bold")
        s.text(gx - 18, y + ch / 2 + 14, unit, size=11, anchor="end", fill=MUTED)
    for r in range(1, 3):
        s.line(gx + 6, gy + r * ch, gx + 3 * cw - 6, gy + r * ch, stroke=GRID)
    for c in range(1, 3):
        s.line(gx + c * cw, gy + 4, gx + c * cw, gy + 3 * ch - 4, stroke=GRID)
    g_c = sub("G", "C")
    cell(cols[0], 0, [("R1", "+g")])
    cell(cols[1], 0, [("R1", "−g")])
    cell(cols[2], 0, [("V1", "+1")])
    cell(cols[0], 1, [("R1", "−g")])
    cell(cols[1], 1, [("R1", "+g"), ("C1", "+" + g_c)])
    cell(cols[2], 1, [])
    cell(cols[0], 2, [("V1", "+1")])
    cell(cols[1], 2, [])
    cell(cols[2], 2, [])
    bracket(gx - 6, 3 * cw + 12)
    for r, name in enumerate(unknowns):
        s.text(xx + 30, gy + r * ch + ch / 2 + 5, name, anchor="middle", italic=True, raw=True)
    bracket(xx, 60)
    s.text(xx + 78, gy + 1.5 * ch + 5, "=", size=16, anchor="middle")
    s.text(bx + cw / 2 + 12, gy + ch / 2 + 5, "0", anchor="middle", fill=RULE)
    chip(bx + 4, gy + ch + 19, cw + 16, "C1", "+" + g_c + " " + sub("v", "n"))
    chip(bx + 4, gy + 2 * ch + 19, cw + 16, "V1", "+" + sub("V", "s"))
    bracket(bx - 4, cw + 32)
    lx, ly = 40, gy + 3 * ch + 36
    for dev, label in (("R1", "R1: g = 1/R"), ("C1", "C1: backward-Euler companion"),
                       ("V1", "V1: branch current and constraint")):
        fill, stroke = fills[dev]
        s.rect(lx, ly - 12, 18, 14, fill=fill, stroke=stroke, width=1.2, rx=3)
        s.text(lx + 25, ly, label, size=12)
        lx += 25 + 6.5 * len(label) + 22
    s.save(out / "solver-stamps.svg")


def companion(out):
    s = Svg(640, 226, "Companion model of a capacitor for one time step",
            "Left: a capacitor between nodes p and q with current i = C dv/dt. Right: for one step of "
            "length h it is replaced by a conductance G_eq in parallel with a current source I_eq, so that "
            "i = G_eq v + I_eq.")
    s.add(f'<defs><marker id="arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" '
          f'orient="auto"><path d="M0 0 8 4 0 8Z" fill="{ACCENT}"/></marker>'
          f'<marker id="arrowk" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" '
          f'orient="auto"><path d="M0 0 8 4 0 8Z" fill="{AXIS}"/></marker></defs>')
    st = f'fill="none" stroke="{AXIS}" stroke-width="2.5" stroke-linejoin="round" stroke-linecap="round"'
    s.add(f'<g {st}><path d="M120 48V101 M120 114V168 M95 101h50 M95 114h50"/></g>')
    s.add(f'<g fill="{AXIS}"><circle cx="120" cy="48" r="4"/><circle cx="120" cy="168" r="4"/></g>')
    s.text(120, 36, "p", anchor="middle", weight="bold", size=15)
    s.text(120, 192, "q", anchor="middle", weight="bold", size=15)
    s.text(156, 112, "C", size=15)
    s.add(f'<path d="M96 56v26" fill="none" stroke="{ACCENT}" stroke-width="2" marker-end="url(#arrow)"/>')
    s.text(78, 76, "i", size=15, italic=True, fill=ACCENT, anchor="middle")
    s.text(120, 214, "i = C dv/dt", anchor="middle", size=14)
    s.add(f'<path d="M222 108h112" fill="none" stroke="{AXIS}" stroke-width="2" marker-end="url(#arrowk)"/>')
    s.text(278, 96, "one step of length h", anchor="middle", size=12.5)
    s.text(278, 128, "integration formula", anchor="middle", size=12, fill=MUTED)
    s.add(f'<g {st}><path d="M420 48h130 M420 168h130 M420 48V82 M420 134V168 M550 48V80 M550 136V168"/>'
          f'<rect x="408" y="82" width="24" height="52"/><circle cx="550" cy="108" r="28"/></g>')
    s.add(f'<path d="M550 94v24" fill="none" stroke="{AXIS}" stroke-width="2" marker-end="url(#arrowk)"/>')
    s.add(f'<g {st}><path d="M485 30V48 M485 168V178"/></g>')
    s.add(f'<g fill="{AXIS}"><circle cx="485" cy="48" r="4"/><circle cx="485" cy="168" r="4"/></g>')
    s.text(485, 22, "p", anchor="middle", weight="bold", size=15)
    s.text(485, 196, "q", anchor="middle", weight="bold", size=15)
    s.text(396, 113, sub("G", "eq", 15), size=15, anchor="end", raw=True)
    s.text(590, 113, sub("I", "eq", 15), size=15, raw=True)
    s.add(f'<path d="M466 20v20" fill="none" stroke="{ACCENT}" stroke-width="2" marker-end="url(#arrow)"/>')
    s.text(452, 36, "i", size=15, italic=True, fill=ACCENT, anchor="middle")
    s.text(485, 216, "i = " + sub("G", "eq", 14) + " v + " + sub("I", "eq", 14), anchor="middle", size=14,
           raw=True)
    s.save(out / "solver-companion.svg")


def stencils(out):
    s = Svg(700, 318, "Stored points that each integration formula uses",
            "Backward Euler uses the voltage at t_n. The trapezoidal rule uses the voltage and the current "
            "at t_n. BDF2 uses the voltages at t_n and t_n-1, BDF3 also the voltage at t_n-2. TR-BDF2 first "
            "solves the point t_n + gamma h with the trapezoidal rule, then t_n+1 with a BDF2 formula over "
            "t_n and that intermediate point.")
    x0, dx = 318, 102
    tx = {-2: x0, -1: x0 + dx, 0: x0 + 2 * dx, "g": x0 + 2 * dx + GAMMA * dx, 1: x0 + 3 * dx}
    top, dy = 76, 34
    s.line(x0 - 30, top - 26, tx[1] + 40, top - 26, stroke=AXIS)
    s.add(f'<path d="M{f(tx[1] + 40)} {top - 30} l8 4 -8 4z" fill="{AXIS}"/>')
    s.text(tx[1] + 54, top - 22, "t", italic=True)
    labels = {-2: sub("t", "n−2"), -1: sub("t", "n−1"), 0: sub("t", "n"), "g": sub("t", "n") + " + γh",
              1: sub("t", "n+1")}
    for k, x in tx.items():
        s.line(x, top - 30, x, top - 22, stroke=AXIS)
        s.text(x, top - 38, labels[k], anchor="middle", size=12.5, raw=True, italic=True)
        s.line(x, top - 14, x, top + 5 * dy + 12, stroke=GRID)
    rows = [
        ("Backward Euler", "order 1", [(0, "v")], 1),
        ("Trapezoidal", "order 2", [(0, "vi")], 1),
        ("BDF2 (GEAR2)", "order 2", [(-1, "v"), (0, "v")], 1),
        ("BDF3 (GEAR3)", "order 3", [(-2, "v"), (-1, "v"), (0, "v")], 1),
        ("TR-BDF2, stage 1", "trapezoidal", [(0, "vi")], "g"),
        ("TR-BDF2, stage 2", "BDF2 formula", [(0, "v"), ("g", "v")], 1),
    ]
    for r, (name, note, used, target) in enumerate(rows):
        y = top + r * dy
        s.text(24, y + 4, name, weight="bold")
        s.text(178, y + 4, note, size=12, fill=MUTED)
        xs = [tx[k] for k, _ in used] + [tx[target]]
        s.line(min(xs), y, max(xs), y, stroke=AXIS, width=1.5)
        for k, kind in used:
            if kind == "vi":
                s.add(f'<circle cx="{f(tx[k])}" cy="{f(y)}" r="8.5" fill="#fff" stroke="{AXIS}" stroke-width="1.5"/>')
            s.add(f'<circle cx="{f(tx[k])}" cy="{f(y)}" r="5" fill="{AXIS}"/>')
        s.add(f'<circle cx="{f(tx[target])}" cy="{f(y)}" r="6" fill="#fff" stroke="{ACCENT}" stroke-width="2.5"/>')
    ly = top + 6 * dy + 14
    s.add(f'<circle cx="32" cy="{ly - 4}" r="5" fill="{AXIS}"/>')
    s.text(46, ly, "stored voltage", size=12)
    s.add(f'<circle cx="170" cy="{ly - 4}" r="8.5" fill="#fff" stroke="{AXIS}" stroke-width="1.5"/>'
          f'<circle cx="170" cy="{ly - 4}" r="5" fill="{AXIS}"/>')
    s.text(186, ly, "stored voltage and current", size=12)
    s.add(f'<circle cx="378" cy="{ly - 4}" r="6" fill="#fff" stroke="{ACCENT}" stroke-width="2.5"/>')
    s.text(392, ly, "point that the formula solves", size=12)
    s.save(out / "solver-stencils.svg")


# ---------------------------------------------------------------------- main


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=ROOT / "docs" / "figures", help="directory to write to")
    ap.add_argument("--kirimaspice", type=Path, default=ROOT / "build" / "kirimaspice",
                    help="the program, for solver-order and solver-ringing")
    ap.add_argument("--runs-tool", type=Path, default=ROOT / "build" / "tests" / "solver_guide_runs",
                    help="the build of tools/solver_guide_runs.cpp, for solver-adaptive-steps")
    ap.add_argument("--skip-runs", action="store_true", help="leave out the figures that need a run")
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    facts = []

    newton(out, facts)
    newton_convergence(out, facts)
    junction_limit(out, facts)
    damping(out, facts)
    stability_regions(out, facts)
    flow(out)
    stamps(out)
    companion(out)
    stencils(out)
    written = 9
    if not args.skip_runs:
        for path, option in ((args.kirimaspice, "--kirimaspice"), (args.runs_tool, "--runs-tool")):
            if not path.exists():
                print(f"solver_figures: {path} not found; build it, give {option}, or pass --skip-runs",
                      file=sys.stderr)
                return 1
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from kirimaspice_pyapi import KirimaspiceSession

        session = KirimaspiceSession(args.kirimaspice)
        observed_order(out, facts, session)
        ringing(out, facts, session)
        adaptive_steps(out, facts, args.runs_tool)
        written += 3
    print("\n".join(facts))
    print(f"solver_figures: wrote {written} figures to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
