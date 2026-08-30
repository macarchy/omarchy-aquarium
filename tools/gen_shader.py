#!/usr/bin/env python3
"""Build step: specialise src/aquarium.frag for the embedded build.

Expands every entity loop marked with `//#unroll` into single-iteration
loops (which the GLSL compiler trivially unrolls) and replaces uniform seed
array reads with the literal values from seeds.c. With literal seeds and
unrolled loops, every uniform-only subexpression (entity positions, bounds,
floor heights under each rock and blade) migrates into the driver's
once-per-draw preamble instead of running per pixel, and the seed arrays
stop competing for the 512-entry uniform register file.

The un-specialised aquarium.frag stays valid GLSL: `--shader` users get the
rolled loops plus the uniform arrays uploaded by seeds.c, and render the
same scene a little slower.
"""

import math
import re
import struct
import sys


def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def fract(x):
    return f32(x - math.floor(x))


def hash11(n):
    return fract(f32(f32(math.sin(f32(f32(n) * f32(127.1)))) * f32(43758.5453123)))


def hash21y(n):
    return fract(f32(f32(math.sin(f32(f32(n) * f32(311.7)))) * f32(43758.5453123)))


def arg(i, a, b):
    return f32(f32(f32(i) * f32(a)) + f32(b))


def lit(v):
    s = repr(float(v))
    return s if ('.' in s or 'e' in s) else s + '.0'


# Must mirror seeds.c exactly.
SEEDS = {
    'uWeedSeed': [(hash11(arg(i, 7.13, 1.0)), hash21y(arg(i, 7.13, 1.0)),
                   hash11(arg(i, 11.31, 6.0)), 0.0) for i in range(20)],
    'uFishSeed': [(hash11(arg(i, 13.37, 5.0)), hash21y(arg(i, 13.37, 5.0)),
                   hash11(arg(i, 3.77, 11.0)), hash11(arg(i, 9.19, 23.0))) for i in range(16)],
    'uBubSeed': sorted([(hash11(arg(i, 31.7, 3.0)), hash21y(arg(i, 31.7, 3.0)),
                          0.0, 0.0) for i in range(14)]),
    'uSchoolSeed': [(hash11(arg(i, 21.9, 77.0)), hash21y(arg(i, 21.9, 77.0)), 0.0, 0.0)
                    for i in range(9)],
    'uRockSeed': [(hash11(arg(i, 17.7, 41.0)), hash21y(arg(i, 17.7, 41.0)),
                   hash11(arg(i, 3.3, 2.0)), hash11(arg(i, 5.9, 8.0)) if i < 4 else 0.0)
                  for i in range(5)],
    'uStarSeed': [(hash11(arg(i, 23.1, 9.0)), hash21y(arg(i, 23.1, 9.0)),
                   hash11(arg(i, 3.7, 4.0)), 0.0) for i in range(4)],
    'uJellySeed': [(hash11(arg(i, 27.3, 53.0)), hash21y(arg(i, 27.3, 53.0)),
                    hash11(arg(i, 4.9, 61.0)), 0.0) for i in range(5)],
    'uFishWarm': [hash11(arg(i, 5.41, 31.0)) for i in range(16)],
}

LOOP_RE = re.compile(
    r'^([ \t]*)//#unroll\n'
    r'\1for \(int (\w+) = (\d+); \2 < (\d+); \2\+\+\) \{\n'
    r'(.*?)'
    r'^\1\}\n',
    re.M | re.S)


def seed_sub(body, idx):
    def repl(m):
        name, expr, swz = m.group(1), m.group(2), m.group(3)
        k = eval(expr, {}, {'i': idx})
        vals = SEEDS[name]
        if name == 'uFishWarm':
            return lit(vals[k])
        v = vals[k]
        comp = {'x': 0, 'y': 1, 'z': 2, 'w': 3}
        if swz is None:
            return f'vec4({lit(v[0])}, {lit(v[1])}, {lit(v[2])}, {lit(v[3])})'
        if len(swz) == 1:
            return lit(v[comp[swz]])
        return f'vec{len(swz)}(' + ', '.join(lit(v[comp[c]]) for c in swz) + ')'
    return re.sub(r'\b(u\w+Seed|uFishWarm)\[([\w* +]+)\](?:\.([xyzw]{1,4}))?', repl, body)


def expand(m):
    indent, var, start, count, body = (m.group(1), m.group(2), int(m.group(3)),
                                       int(m.group(4)), m.group(5))
    out = []
    for k in range(start, count):
        b = seed_sub(body, k)
        out.append(f'{indent}for (int {var} = {k}; {var} < {k + 1}; {var}++) {{\n'
                   f'{b}{indent}}}\n')
    return ''.join(out)


def main():
    src = open(sys.argv[1]).read()
    out, n = LOOP_RE.subn(expand, src)
    marks = src.count('//#unroll')
    if n != marks:
        sys.exit(f'gen_shader: expanded {n} loops but found {marks} //#unroll marks')
    open(sys.argv[2], 'w').write(out)


if __name__ == '__main__':
    main()
