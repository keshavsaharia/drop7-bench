#!/usr/bin/env python3
"""How much of the chance node's joint distribution do N x M scenarios cover?

Reimplements src/core/native/engine.hpp:53 mix32 and
src/core/native/public-behavior.hpp:579 stratifiedUnit in Python, and measures
the fraction of joint atoms that receive non-zero weight at a chance node, for
the single-knob search (M = 1) and the factored search (M > 1).

Verified against the C++ by construction: the mapping is
  floor(stratifiedUnit(...) * 7) + 1  (public-behavior.hpp:601-606, :736-742).
"""
import statistics
import sys

MASK = 0xFFFFFFFF
REVEAL_DOMAIN = 0x5245564C
DISC_DOMAIN = 0x44495343
SAMPLE_MULT = 0x9E3779B9
DEPTH_MULT = 0x85EBCA6B


def mix32(v):
    v &= MASK
    v ^= v >> 16
    v = (v * 0x7FEB352D) & MASK
    v ^= v >> 15
    v = (v * 0x846CA68B) & MASK
    v ^= v >> 16
    return v


def stratified_unit(seed, sample, count, domain, event):
    event_seed = mix32(seed ^ domain ^ ((event + 1) * DEPTH_MULT & MASK))
    rotation = event_seed % count
    stratum = (sample + rotation) % count
    jitter = mix32(event_seed ^ ((sample + 1) * SAMPLE_MULT & MASK)) / 2**32
    return (stratum + jitter) / count


def disc_of(unit):
    return int(unit * 7.0) + 1


def scenarios(seed, n, m):
    """Yield (next_disc, reveal_event_values...) exactly as search.cpp draws them."""
    total = n * m
    out = []
    for d in range(n):
        for r in range(m):
            s = r * n + d
            reveals = tuple(
                disc_of(stratified_unit(seed, s, total, REVEAL_DOMAIN, e))
                for e in range(4)
            )
            nxt = disc_of(stratified_unit(seed, d, n, DISC_DOMAIN, 0))
            out.append((nxt, reveals))
    return out


def coverage(nodes, n, m):
    disc_atoms, rev1, rev2, rev3, disc_rev = [], [], [], [], []
    for seed in nodes:
        sc = scenarios(seed, n, m)
        disc_atoms.append(len({x[0] for x in sc}) / 7)
        rev1.append(len({x[1][0] for x in sc}) / 7)
        rev2.append(len({x[1][:2] for x in sc}) / 49)
        rev3.append(len({x[1][:3] for x in sc}) / 343)
        disc_rev.append(len({(x[0], x[1][0]) for x in sc}) / 49)
    return [statistics.mean(v) for v in (disc_atoms, rev1, rev2, rev3, disc_rev)]


if __name__ == "__main__":
    nodes = [mix32(0xD7075EED ^ (i * 2654435761)) for i in range(20000)]
    print("| N | M | scenarios | next disc | 1 reveal | 2 reveals joint | 3 reveals joint | (disc, reveal) joint |")
    print("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for n, m in [(5, 1), (7, 1), (5, 3), (7, 2), (7, 3), (7, 6), (7, 12)]:
        c = coverage(nodes, n, m)
        print(f"| {n} | {m} | {n*m} | {c[0]*100:.1f}% | {c[1]*100:.1f}% | {c[2]*100:.1f}% | {c[3]*100:.1f}% | {c[4]*100:.1f}% |")
